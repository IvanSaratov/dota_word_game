# Runtime Hardening and Tagged Releases Implementation Plan

> **For Codex:** Execute this plan with `superpowers:executing-plans` or
> `superpowers:subagent-driven-development`. Use
> `superpowers:test-driven-development` for every behavior change and
> `superpowers:verification-before-completion` before each completion claim.

**Goal:** Make moving OCR targets fire once, suppress one-letter false
positives and ONNX schema spam, produce clean Windows packages plus a per-user
installer, and publish installer releases only from valid version tags on
`main`.

**Architecture:** Replace the tracker's separate frame history and stationary
locks with persistent spatial tracks that carry confirmation, missing-frame,
and sent state while their bounds move. Keep OCR filtering in `App`, package
the same installed tree through ZIP and CPack Inno Setup, and isolate public
release publication in a tag-triggered job after a read-only build job has
validated tag ancestry and package contents.

**Tech Stack:** C++20, Catch2, CMake/CPack 3.28+, ONNX Runtime, PowerShell,
GitHub Actions `windows-2022`, Inno Setup 6.

---

## Task 1: Persist sent state while targets move

**Files:**

- Modify: `include/dk/target_tracker.hpp`
- Modify: `src/target_tracker.cpp`
- Modify: `tests/target_tracker_test.cpp`

### Step 1: Add failing movement, partial-OCR, and disappearance tests

Add these cases to `tests/target_tracker_test.cpp`:

```cpp
TEST_CASE("sent target remains locked while moving farther than one match radius") {
    dk::TargetTracker tracker;
    CHECK_FALSE(update_tracker(tracker, {word("BANE", 100)}));
    auto ready = update_tracker(tracker, {word("BANE", 170)});
    REQUIRE(ready);
    tracker.mark_sent(*ready);

    CHECK_FALSE(update_tracker(tracker, {word("BANE", 240)}));
    CHECK_FALSE(update_tracker(tracker, {word("BANE", 310)}));
}

TEST_CASE("temporary partial OCR does not unlock a sent moving target") {
    dk::TargetTracker tracker;
    CHECK_FALSE(update_tracker(tracker, {word("BANE", 100)}));
    auto ready = update_tracker(tracker, {word("BANE", 150)});
    REQUIRE(ready);
    tracker.mark_sent(*ready);

    CHECK_FALSE(update_tracker(tracker, {word("ANE", 200)}));
    CHECK_FALSE(update_tracker(tracker, {word("BANE", 250)}));
    CHECK_FALSE(update_tracker(tracker, {word("BANE", 300)}));
}

TEST_CASE("same word can be selected again after the old target disappears") {
    dk::TargetTracker tracker;
    CHECK_FALSE(update_tracker(tracker, {word("BANE", 100)}));
    auto ready = update_tracker(tracker, {word("BANE", 150)});
    REQUIRE(ready);
    tracker.mark_sent(*ready);

    CHECK_FALSE(update_tracker(tracker, {}));
    CHECK_FALSE(update_tracker(tracker, {}));
    CHECK_FALSE(update_tracker(tracker, {word("BANE", 100)}));
    CHECK(update_tracker(tracker, {word("BANE", 150)}));
}

```

Keep the existing two-target test to protect lower-target priority. The
implementation must still associate tracks one-to-one: a single observation
may extend only one prior track.

### Step 2: Run the focused test and confirm RED

On Windows:

```powershell
cmake --build --preset windows-debug --target target_tracker_test
ctest --preset windows-debug -R "target_tracker_test" --output-on-failure
```

Expected: the movement and partial-OCR cases fail because the old lock is tied
to its original text and position.

### Step 3: Replace stationary locks with persistent tracks

In `include/dk/target_tracker.hpp`, make `Track` the only stored state:

```cpp
struct Track {
    TextCandidate value;
    int seen_frames{1};
    int missing_frames{};
    bool sent{};
};

TrackerConfig config_;
std::vector<Track> tracks_;
```

Remove `Lock`, `previous_`, and `locks_`.

In `src/target_tracker.cpp`, implement deterministic one-to-one spatial
association:

```cpp
float center_distance(const TextCandidate& left, const TextCandidate& right) {
    return std::hypot(left.bounds.center_x() - right.bounds.center_x(),
                      left.bounds.center_y() - right.bounds.center_y());
}

bool compatible_size(const TextCandidate& left, const TextCandidate& right) {
    const auto ratio_in_range = [](int left_size, int right_size) {
        const auto smaller = static_cast<float>(std::min(left_size, right_size));
        const auto larger = static_cast<float>(std::max(left_size, right_size));
        return larger > 0.0F && smaller / larger >= 0.5F;
    };
    return ratio_in_range(left.bounds.width, right.bounds.width) &&
           ratio_in_range(left.bounds.height, right.bounds.height);
}
```

Build all compatible `(distance, track index, candidate index)` pairs at or
below `max_center_distance_px`, sort by distance and then indices, and greedily
accept a pair only when neither side has been used.

For an accepted pair:

- Always replace the track bounds with the current candidate bounds.
- Reset `missing_frames` to zero.
- If `sent` is true, preserve the stable text and confirmation state; the
  current OCR text is only evidence that the same spatial object is present.
- If `sent` is false and normalized text is unchanged, replace the full value
  and increment `seen_frames`.
- If `sent` is false and normalized text changed, replace the full value and
  reset `seen_frames` to one.

For unmatched state:

- Increment `missing_frames` on unmatched tracks.
- Erase tracks whose `missing_frames >= unlock_missing_frames`.
- Create a fresh track for every unmatched candidate.
- Select the unsent, non-missing confirmed track with greatest
  `bounds.bottom()`.

Implement `mark_sent` by finding the current unsent track with equal normalized
text and the smallest center distance within `max_center_distance_px`, then
setting `sent = true`. Do not append a new lock.

### Step 4: Run focused tests and confirm GREEN

```powershell
cmake --build --preset windows-debug --target target_tracker_test
ctest --preset windows-debug -R "target_tracker_test" --output-on-failure
```

Expected: all tracker tests pass, including the existing lower-target
selection test.

### Step 5: Commit

```bash
git add include/dk/target_tracker.hpp src/target_tracker.cpp tests/target_tracker_test.cpp
git commit -m "fix: track moving words after input"
```

## Task 2: Reject one-letter OCR results but allow two letters

**Files:**

- Modify: `src/app.cpp`
- Modify: `tests/app_test.cpp`

### Step 1: Add a failing application-level test

Add a test with two boxes per frame. The first produces `"C"` twice and the
second produces `"IO"` twice:

```cpp
TEST_CASE("app rejects one letter and accepts two letters") {
    auto config = dk::AppConfig::defaults();
    config.live_input = true;
    FakeFrameSource frames{2};
    FakeDetector detector{{{10, 60, 100, 30}, {20, 20, 120, 30}}};
    FakeRecognizer recognizer{{
        {"C", .99F}, {"IO", .99F},
        {"C", .99F}, {"IO", .99F},
    }};
    FakeInputSink input;
    dk::App app(config, frames, detector, recognizer, input);

    CHECK(app.process_one_frame());
    CHECK(app.process_one_frame());

    REQUIRE(input.sent.size() == 1);
    CHECK(input.sent.front() == "IO");
}
```

### Step 2: Run focused test and confirm RED

```powershell
cmake --build --preset windows-debug --target app_test
ctest --preset windows-debug -R "app_test" --output-on-failure
```

Expected: `"C"` is accepted and chosen/sent.

### Step 3: Implement the minimum after normalization

In `src/app.cpp`, define:

```cpp
constexpr std::size_t kMinimumNormalizedLength = 2;
```

Change the filter to:

```cpp
if (normalized.size() < kMinimumNormalizedLength ||
    recognized.confidence < config_.min_ocr_confidence) {
    continue;
}
```

This deliberately counts the normalized input, so punctuation and spaces do
not make a one-letter result valid.

### Step 4: Run tests and commit

```powershell
cmake --build --preset windows-debug --target app_test
ctest --preset windows-debug -R "app_test" --output-on-failure
git add src/app.cpp tests/app_test.cpp
git commit -m "fix: ignore one-letter OCR candidates"
```

## Task 3: Silence ONNX schema registration noise

**Files:**

- Create: `tests/ocr_logging_source_test.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `src/ocr_recognizer.cpp`

### Step 1: Add a source regression test

Create `tests/ocr_logging_source_test.cpp` using the same source-reading
pattern as the other portable source tests. Assert that the file contains:

```cpp
ORT_LOGGING_LEVEL_FATAL
```

and does not contain:

```cpp
ORT_LOGGING_LEVEL_WARNING
```

Register it in `tests/CMakeLists.txt`:

```cmake
add_executable(ocr_logging_source_test ocr_logging_source_test.cpp)
target_compile_definitions(ocr_logging_source_test PRIVATE
  DK_OCR_RECOGNIZER_SOURCE_PATH="${PROJECT_SOURCE_DIR}/src/ocr_recognizer.cpp")
add_test(NAME ocr_logging_source_test COMMAND ocr_logging_source_test)
```

### Step 2: Run and confirm RED

```powershell
cmake --build --preset windows-debug --target ocr_logging_source_test
ctest --preset windows-debug -R "ocr_logging_source_test" --output-on-failure
```

### Step 3: Raise only the ONNX Runtime logger threshold

Keep the existing function-local static singleton in
`src/ocr_recognizer.cpp`, changing only:

```cpp
static Ort::Env environment{ORT_LOGGING_LEVEL_FATAL, "dota_keyboard_ocr"};
```

Do not catch or downgrade session/model exceptions.

### Step 4: Run tests and commit

```powershell
cmake --build --preset windows-debug --target ocr_logging_source_test ocr_recognizer_test
ctest --preset windows-debug -R "ocr_(logging_source|recognizer)_test" --output-on-failure
git add src/ocr_recognizer.cpp tests/ocr_logging_source_test.cpp tests/CMakeLists.txt
git commit -m "fix: suppress ONNX schema registration noise"
```

## Task 4: Exclude Windows system DLLs from the installed tree

**Files:**

- Modify: `CMakeLists.txt`
- Modify: `tests/packaging_cmake_source_test.cpp`
- Modify: `.github/workflows/windows.yml`

### Step 1: Strengthen the packaging source test

Update `tests/packaging_cmake_source_test.cpp` to require:

- no `set(CMAKE_INSTALL_UCRT_LIBRARIES TRUE)`;
- `api-ms` and `ext-ms` pre-exclusion;
- path-separator-independent `System32` and `SysWOW64` post-exclusion.

The test must fail against the current forward-slash-only expression.

### Step 2: Run and confirm RED

```powershell
cmake --build --preset windows-debug --target packaging_cmake_source_test
ctest --preset windows-debug -R "packaging_cmake_source_test" --output-on-failure
```

### Step 3: Fix CPack dependency filtering

In `CMakeLists.txt`:

- Keep `InstallRequiredSystemLibraries` so the redistributable MSVC DLLs remain
  available.
- Remove `CMAKE_INSTALL_UCRT_LIBRARIES TRUE`; Windows 11 supplies UCRT.
- Use case-insensitive pre-exclusions for API-set forwarders.
- Use both slash types for Windows system paths:

```cmake
PRE_EXCLUDE_REGEXES
  "[Aa][Pp][Ii]-[Mm][Ss]-.*"
  "[Ee][Xx][Tt]-[Mm][Ss]-.*"
POST_EXCLUDE_REGEXES
  ".*[\\\\/][Ww][Ii][Nn][Dd][Oo][Ww][Ss][\\\\/][Ss][Yy][Ss][Tt][Ee][Mm]32[\\\\/].*"
  ".*[\\\\/][Ww][Ii][Nn][Dd][Oo][Ww][Ss][\\\\/][Ss][Yy][Ss][Ww][Oo][Ww]64[\\\\/].*"
```

### Step 4: Make CI reject forbidden DLLs explicitly

In `.github/workflows/windows.yml`, after extraction, collect every DLL
basename in lowercase and fail on:

```powershell
$forbiddenDllNames = @(
  'kernel32.dll', 'user32.dll', 'gdi32.dll', 'advapi32.dll',
  'd3d11.dll', 'dxgi.dll', 'setupapi.dll', 'dbghelp.dll', 'ucrtbase.dll'
)
```

Also fail on names matching `api-ms-*.dll` or `ext-ms-*.dll`.

Retain the positive checks for ONNX Runtime, OpenCV, `vcruntime140*`, and
`msvcp140*`.

### Step 5: Run tests and commit

```powershell
cmake --build --preset windows-debug --target packaging_cmake_source_test
ctest --preset windows-debug -R "packaging_cmake_source_test" --output-on-failure
git add CMakeLists.txt tests/packaging_cmake_source_test.cpp .github/workflows/windows.yml
git commit -m "fix: keep Windows system DLLs out of packages"
```

## Task 5: Generate and smoke-test a per-user installer

**Files:**

- Create: `cmake/PackageProjectConfig.cmake`
- Modify: `CMakeLists.txt`
- Modify: `CMakePresets.json`
- Modify: `tests/packaging_cmake_source_test.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `.github/workflows/windows.yml`
- Modify: `README.ru.md`

### Step 1: Add failing assertions for installer policy

Extend `tests/packaging_cmake_source_test.cpp` to require the `INNOSETUP`
generator configuration, `{localappdata}/Programs`, and:

```cmake
set(CPACK_INNOSETUP_SETUP_PrivilegesRequired "lowest")
```

Add a source-path definition for
`cmake/PackageProjectConfig.cmake` and assert that it assigns:

- `dota-keyboard-${CPACK_PACKAGE_VERSION}-windows-x64` for ZIP;
- `DotaKeyboardSetup-${CPACK_PACKAGE_VERSION}-windows-x64` for INNOSETUP.

### Step 2: Run and confirm RED

```powershell
cmake --build --preset windows-debug --target packaging_cmake_source_test
ctest --preset windows-debug -R "packaging_cmake_source_test" --output-on-failure
```

### Step 3: Configure CPack Inno Setup

In `CMakeLists.txt`, add before `include(CPack)`:

```cmake
set(CPACK_PACKAGE_NAME "Dota Keyboard")
set(CPACK_PACKAGE_VENDOR "IvanSaratov")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "DotaKeyboard")
set(CPACK_PACKAGE_EXECUTABLES "dota_keyboard" "Dota Keyboard")
set(CPACK_PROJECT_CONFIG_FILE
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PackageProjectConfig.cmake")
set(CPACK_INNOSETUP_ARCHITECTURE "x64")
set(CPACK_INNOSETUP_INSTALL_ROOT "{localappdata}/Programs")
set(CPACK_INNOSETUP_SETUP_PrivilegesRequired "lowest")
set(CPACK_INNOSETUP_USE_MODERN_WIZARD ON)
set(CPACK_INNOSETUP_LANGUAGES "english;russian")
set(CPACK_INNOSETUP_IGNORE_LICENSE_PAGE ON)
set(CPACK_INNOSETUP_RUN_EXECUTABLES "dota_keyboard")
```

Create `cmake/PackageProjectConfig.cmake`:

```cmake
if(CPACK_GENERATOR STREQUAL "INNOSETUP")
  set(CPACK_PACKAGE_FILE_NAME
    "DotaKeyboardSetup-${CPACK_PACKAGE_VERSION}-windows-x64")
elseif(CPACK_GENERATOR STREQUAL "ZIP")
  set(CPACK_PACKAGE_FILE_NAME
    "dota-keyboard-${CPACK_PACKAGE_VERSION}-windows-x64")
endif()
```

In `CMakePresets.json`, retain `windows-release` for ZIP and add:

```json
{
  "name": "windows-installer",
  "configurePreset": "windows-release",
  "configurations": ["Release"],
  "generators": ["INNOSETUP"]
}
```

### Step 4: Build and validate both package types in CI

In the build step, run:

```powershell
cpack --preset windows-release
cpack --preset windows-installer
```

Add an installer validation step that:

1. Requires exactly one
   `DotaKeyboardSetup-*-windows-x64.exe`.
2. Installs silently into a unique directory below `$env:RUNNER_TEMP`:

```powershell
$process = Start-Process -FilePath $installer.FullName -Wait -PassThru `
  -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART',
                "/DIR=$installRoot"
if ($process.ExitCode -ne 0) { throw "Installer exited with $($process.ExitCode)." }
```

3. Applies the same required/forbidden file checks to `$installRoot`.
4. Captures `dota_keyboard.exe --check-install` output and rejects a match for
   `Schema error: Trying to register schema`.
5. Runs `unins000.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART` and confirms
   that the installed executable is gone.

Also change the ZIP smoke invocation to capture combined output and reject the
same schema-error flood.

Upload two separate Actions artifacts:

- `dota-keyboard-windows-x64-portable`
- `dota-keyboard-windows-x64-installer`

### Step 5: Document install and unsigned warning

In `README.ru.md`, make the installer the recommended path. Document:

- no administrator rights are required;
- the default location is below `%LOCALAPPDATA%\Programs`;
- Windows SmartScreen may warn because the executable is not code-signed;
- the portable ZIP remains available in Actions for diagnostics;
- `live_input=false` remains the initial safe mode.

### Step 6: Commit

```bash
git add CMakeLists.txt CMakePresets.json cmake/PackageProjectConfig.cmake \
  tests/packaging_cmake_source_test.cpp tests/CMakeLists.txt \
  .github/workflows/windows.yml README.ru.md
git commit -m "feat: package per-user Windows installer"
```

## Task 6: Enforce tag and `main` release policy

**Files:**

- Create: `scripts/validate-release.ps1`
- Create: `scripts/test-release-policy.ps1`
- Create: `.github/workflows/release.yml`
- Modify: `README.ru.md`

### Step 1: Write executable policy tests first

Create `scripts/test-release-policy.ps1`. It must:

- create a temporary Git repository;
- write a minimal `CMakeLists.txt` with `project(... VERSION 0.1.0)`;
- create a `main` commit and point `refs/remotes/origin/main` to it;
- invoke `validate-release.ps1` in child PowerShell processes;
- expect success for tag `v0.1.0` on the main commit;
- expect failure for `v0.1`, `release-0.1.0`, `v0.1.1`, and a feature commit
  not reachable from `origin/main`;
- remove the temporary repository in `finally`.

Run:

```powershell
pwsh -NoProfile -File scripts/test-release-policy.ps1
```

Expected: RED because the validator does not exist.

### Step 2: Implement release validation

Create `scripts/validate-release.ps1` with mandatory parameters `Tag`,
`Commit`, and `MainRef`. It must:

1. Require `^v([0-9]+)\.([0-9]+)\.([0-9]+)$`.
2. Read the version from the root
   `project(dota_keyboard VERSION X.Y.Z ...)` declaration.
3. Require tag version equality with the CMake version.
4. Resolve `Commit` and `MainRef` through `git rev-parse --verify`.
5. Run `git merge-base --is-ancestor <commit> <main-ref>` and fail unless exit
   code is zero.
6. Exit nonzero with a precise error for every rejected condition.

Run the policy test again and require GREEN.

### Step 3: Create a least-privilege release workflow

Create `.github/workflows/release.yml`:

```yaml
name: Windows release

on:
  push:
    tags:
      - 'v*'
```

Use two jobs:

- `build-release`: `windows-2022`, `permissions: contents: read`. Checkout
  with `fetch-depth: 0`, fetch `origin/main`, run the policy tests and validator,
  then reuse the normal CI configure/build/test/package/smoke steps. Produce
  the installer, portable ZIP, and an installer checksum file made with
  `Get-FileHash -Algorithm SHA256`.
- `publish-release`: needs `build-release`, `permissions: contents: write`.
  Download only the installer and checksum artifact and publish immediately:

```powershell
$installer = Get-ChildItem .\release-assets\DotaKeyboardSetup-*-windows-x64.exe -File
$checksum = Get-ChildItem .\release-assets\DotaKeyboardSetup-*-windows-x64.exe.sha256 -File
if (@($installer).Count -ne 1 -or @($checksum).Count -ne 1) {
  throw 'Expected exactly one installer and one checksum.'
}
gh release create $env:GITHUB_REF_NAME `
  $installer.FullName $checksum.FullName `
  --verify-tag --generate-notes --title $env:GITHUB_REF_NAME
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
```

Do not pass `--draft` or `--prerelease`. The portable ZIP stays an Actions
artifact and is not a public Release asset.

### Step 4: Add release instructions

In `README.ru.md`, document the maintainer flow:

```bash
git switch main
git pull --ff-only
git tag v0.1.0
git push origin v0.1.0
```

State that the tag must exactly match `project(... VERSION ...)` and be placed
on a commit already present in `origin/main`.

### Step 5: Verify and commit

```powershell
pwsh -NoProfile -File scripts/test-release-policy.ps1
```

```bash
git add scripts/validate-release.ps1 scripts/test-release-policy.ps1 \
  .github/workflows/release.yml README.ru.md
git commit -m "ci: publish validated tagged Windows releases"
```

## Task 7: Verify the complete branch on Windows CI

**Files:**

- Modify only if failures reveal a defect in files already listed above.

### Step 1: Run static repository checks locally

```bash
git diff --check main...HEAD
rg -n "TBD|TODO|FIXME" \
  include src tests scripts cmake .github/workflows README.ru.md CMakeLists.txt CMakePresets.json
git status --short
```

Expected: no whitespace errors or unfinished placeholders. The screenshot and
`dota_logs.log` remain untracked and are not staged.

### Step 2: Push the feature branch and inspect CI

```bash
git push -u origin feature/runtime-hardening-release
run_id=$(gh run list --branch feature/runtime-hardening-release \
  --workflow windows.yml --limit 1 --json databaseId --jq '.[0].databaseId')
gh run watch "$run_id" --exit-status
```

If a check fails, use `superpowers:systematic-debugging`: inspect the failing
step and CTest log, add or adjust a reproducing test, then make the smallest
fix and rerun.

### Step 3: Confirm package evidence

Require all of the following from the successful run:

- all CTest cases pass;
- portable ZIP validation passes;
- installer silent install, `--check-install`, schema-noise check, and
  uninstall pass;
- installer and portable ZIP artifacts both exist;
- the installed tree has no forbidden Windows OS DLLs.

Do not create or push a version tag during branch verification.

### Step 4: Request code review and fix only verified findings

Use `superpowers:requesting-code-review` against `main...HEAD`. Resolve concrete
correctness or requirement gaps, rerun the relevant focused tests, and repeat
the complete Windows workflow after any packaging/runtime change.

### Step 5: Final branch verification

```bash
git log --oneline main..HEAD
git diff --check main...HEAD
git status --short
gh run view "$run_id" --json conclusion,url
```

Expected: clean tracked worktree, only the user's screenshot/log untracked,
and workflow conclusion `success`.

## Task 8: User acceptance, merge, and first release

This task requires explicit user confirmation after they test the installer.

### Step 1: Have the user test the installer in dry mode

The user downloads the installer Actions artifact, selects the smallest useful
F7 region, and runs with `live_input=false`. Acceptance criteria:

- a moving visible word produces one `[DRY] would type`;
- a partial OCR fluctuation does not repeat that line;
- a later new copy of the same word can produce a new line after disappearance;
- no one-letter result is proposed;
- two-letter words remain eligible;
- startup has no schema-registration flood.

### Step 2: Merge only after acceptance

Use `superpowers:finishing-a-development-branch`. Merge the reviewed branch
into `main`, push `main`, and wait for the normal Windows workflow to succeed.

### Step 3: Create the first public release only after separate confirmation

Confirm that `CMakeLists.txt` still says version `0.1.0`, then:

```bash
git tag v0.1.0
git push origin v0.1.0
```

Watch `.github/workflows/release.yml` to success and confirm that the published
GitHub Release contains exactly the installer EXE and its SHA256 file.
