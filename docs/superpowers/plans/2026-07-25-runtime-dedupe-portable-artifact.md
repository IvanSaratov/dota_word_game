# Runtime Deduplication and Portable Artifact Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> `superpowers:subagent-driven-development` (recommended) or
> `superpowers:executing-plans` to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prevent duplicate and cropped word dispatches observed in the Windows
dry-run, ignore Cyrillic OCR output, remove the ZIP-inside-ZIP portable Actions
download, and carry the accepted build through the first public release.

**Design:** `docs/superpowers/specs/2026-07-25-runtime-dedupe-portable-artifact-design.md`

**Architecture:** Extend `TargetTracker` with sent-track-specific association:
canonical exact-text reacquisition, bounded missing-frame retention, and
geometric suppression of substantially smaller fragments. Keep Cyrillic
rejection in the normalizer so invalid text cannot enter tracking. Continue to
validate the CPack ZIP, but upload its extracted contents as the portable
Actions artifact.

**Tech Stack:** C++20, Catch2 3, CMake/CPack, PowerShell 7, GitHub Actions,
vcpkg, Inno Setup 6, GitHub CLI.

## Global Constraints

- Windows 11 x64 remains the supported runtime target.
- `confirm_frames` remains 2; valid words must not gain another confirmation
  frame of latency.
- Default `unlock_missing_frames` becomes exactly 15.
- Fragment suppression uses an area ceiling of exactly 65% and expands the
  retained full-word bounds by `max_center_distance_px`.
- Cyrillic U+0400–U+04FF causes the complete OCR result to normalize to empty.
- ASCII punctuation, whitespace, and digits remain removable; two-letter ASCII
  English words remain valid.
- `live_input` remains `false` in every packaged default configuration.
- Portable output remains multi-file; only the redundant inner ZIP is removed
  from Actions downloads.
- No public tag is created before native Windows CI and user dry-run acceptance.

---

### Task 1: Retain sent words and suppress detector fragments

**Files:**

- Modify: `include/dk/target_tracker.hpp`
- Modify: `src/target_tracker.cpp`
- Modify: `config/default.json`
- Modify: `tests/target_tracker_test.cpp`
- Modify: `tests/config_test.cpp`

**Interfaces:**

- Consumes: `TextCandidate`, `Box`, and the existing
  `TargetTracker::update`/`mark_sent` API.
- Produces: unchanged public tracker API; `TrackerConfig` defaults to
  `.unlock_missing_frames = 15`.
- Invariant: a sent track's `value.normalized_text` and full-size bounds remain
  canonical when a fragment is consumed.

- [ ] **Step 1: Add log-derived failing tracker tests**

Add a box-aware helper next to the existing `word` helper:

```cpp
static TextCandidate boxed_word(
    std::string text, Box bounds, float confidence = 0.99F) {
    return {text, text, confidence, bounds};
}
```

Add Catch2 cases that encode the observed failures:

```cpp
TEST_CASE("sent exact text reacquires after a short gap and position jump") {
    dk::TargetTracker tracker;
    CHECK_FALSE(update_tracker(
        tracker, {boxed_word("MEDUSA", {213, 357, 229, 51})}));
    auto ready = update_tracker(
        tracker, {boxed_word("MEDUSA", {215, 356, 229, 51})});
    REQUIRE(ready);
    tracker.mark_sent(*ready);

    for (int frame = 0; frame < 14; ++frame) {
        CHECK_FALSE(update_tracker(tracker, {}));
    }
    CHECK_FALSE(update_tracker(
        tracker, {boxed_word("MEDUSA", {455, 695, 229, 51})}));
    CHECK_FALSE(update_tracker(
        tracker, {boxed_word("MEDUSA", {462, 716, 229, 52})}));
}

TEST_CASE("sent word consumes nearby stable cropped fragments") {
    struct Scenario {
        const char* full;
        Box full_bounds;
        const char* fragment;
        Box fragment_bounds;
    };
    const Scenario scenarios[] = {
        {"HYPERSTONE", {380, 583, 341, 52}, "HYPE", {398, 658, 134, 51}},
        {"BLADEMAIL", {393, 581, 315, 66}, "EMA", {542, 579, 89, 49}},
        {"BLOODTHORN", {715, 0, 380, 34}, "RN", {1015, 0, 78, 33}},
        {"BLOODTHORN", {715, 0, 380, 34}, "DIH", {857, 0, 109, 33}},
    };

    for (const auto& scenario : scenarios) {
        dk::TargetTracker tracker;
        CHECK_FALSE(update_tracker(
            tracker, {boxed_word(scenario.full, scenario.full_bounds)}));
        auto ready = update_tracker(
            tracker, {boxed_word(scenario.full, scenario.full_bounds)});
        REQUIRE(ready);
        tracker.mark_sent(*ready);

        CHECK_FALSE(update_tracker(
            tracker,
            {boxed_word(scenario.fragment, scenario.fragment_bounds)}));
        CHECK_FALSE(update_tracker(
            tracker,
            {boxed_word(scenario.fragment, scenario.fragment_bounds)}));
    }
}

TEST_CASE("different full-size word near a sent track stays eligible") {
    dk::TargetTracker tracker;
    CHECK_FALSE(update_tracker(
        tracker, {boxed_word("HYPERSTONE", {380, 583, 341, 52})}));
    auto sent = update_tracker(
        tracker, {boxed_word("HYPERSTONE", {382, 585, 341, 52})});
    REQUIRE(sent);
    tracker.mark_sent(*sent);

    CHECK_FALSE(update_tracker(
        tracker, {boxed_word("BLADEMAIL", {390, 590, 315, 52})}));
    const auto distinct = update_tracker(
        tracker, {boxed_word("BLADEMAIL", {394, 594, 315, 52})});
    REQUIRE(distinct);
    CHECK(distinct->normalized_text == "BLADEMAIL");
}

TEST_CASE("same word unlocks after fifteen completely missing frames") {
    dk::TargetTracker tracker;
    CHECK_FALSE(update_tracker(tracker, {word("BANE", 100)}));
    auto ready = update_tracker(tracker, {word("BANE", 150)});
    REQUIRE(ready);
    tracker.mark_sent(*ready);

    for (int frame = 0; frame < 15; ++frame) {
        CHECK_FALSE(update_tracker(tracker, {}));
    }
    CHECK_FALSE(update_tracker(tracker, {word("BANE", 100)}));
    CHECK(update_tracker(tracker, {word("BANE", 150)}));
}
```

Update the old two-frame disappearance test so it either uses an explicit
`.unlock_missing_frames = 2` configuration or is replaced by the exact
15-frame default test above. Extend `config_test.cpp` to require:

```cpp
CHECK(dk::AppConfig::defaults().tracker.unlock_missing_frames == 15);
```

- [ ] **Step 2: Run the focused tests and verify RED**

On a configured Windows tree:

```powershell
cmake --build --preset windows-release --target target_tracker_test config_test
ctest --test-dir build/windows-release -C Release --output-on-failure `
  -R "target_tracker_test|config_test"
```

Expected: failures for the 15-frame default, position-jump duplicate, fragment
scenarios, and distinct nearby full-size word.

- [ ] **Step 3: Implement sent-track-specific matching**

Change the default in `include/dk/target_tracker.hpp`:

```cpp
int unlock_missing_frames{15};
```

Add geometry helpers in `src/target_tracker.cpp`:

```cpp
#include <cstdint>

std::int64_t area(const Box& box) noexcept {
    return static_cast<std::int64_t>(box.width) * box.height;
}

bool center_inside_expanded(
    const Box& candidate, const Box& retained, float expansion) noexcept {
    return candidate.center_x() >= retained.x - expansion &&
           candidate.center_x() <= retained.right() + expansion &&
           candidate.center_y() >= retained.y - expansion &&
           candidate.center_y() <= retained.bottom() + expansion;
}

bool is_fragment(
    const TextCandidate& candidate, const TextCandidate& sent,
    float expansion) noexcept {
    const auto candidate_area = area(candidate.bounds);
    const auto sent_area = area(sent.bounds);
    return candidate_area > 0 && sent_area > 0 &&
           candidate_area * 100 <= sent_area * 65 &&
           center_inside_expanded(candidate.bounds, sent.bounds, expansion);
}
```

Refactor `TargetTracker::update` in this order:

1. Build normal distance/size matches, but exclude a sent track when candidate
   normalized text differs from its canonical normalized text.
2. Apply the existing greedy one-to-one normal matches.
3. For every still-unmatched candidate, locate the nearest live sent track with
   identical canonical normalized text. Consume the candidate even beyond
   `max_center_distance_px`; when that sent track was unmatched, set
   `missing_frames = 0`, update only its full candidate bounds, and mark the
   track matched.
4. For every still-unmatched candidate, locate a live sent track satisfying
   `is_fragment`. Mark the candidate consumed. When the sent track was
   unmatched, set `missing_frames = 0` and mark it matched, but do not replace
   canonical text or full-size bounds.
5. Increment missing counts, erase expired tracks, create new tracks, and
   select the lowest confirmed unsent target as before.

Use a small helper/lambda for steps 3–4 so candidate consumption and sent-track
refresh cannot diverge:

```cpp
const auto consume_for_sent_track =
    [&](std::size_t track_index, std::size_t candidate_index,
        bool refresh_full_bounds) {
        auto& track = tracks_[track_index];
        matched_candidates[candidate_index] = true;
        if (!matched_tracks[track_index]) {
            matched_tracks[track_index] = true;
            track.missing_frames = 0;
        }
        if (refresh_full_bounds) {
            track.value.bounds = candidates[candidate_index].bounds;
        }
    };
```

Change `config/default.json` to:

```json
"unlock_missing_frames": 15
```

- [ ] **Step 4: Run tracker/config tests and verify GREEN**

Run the focused command from Step 2.

Expected: every focused test passes, including existing moving-two-target,
consecutive-confirmation, and configured two-frame expiration coverage.

- [ ] **Step 5: Commit tracker behavior**

```bash
git add include/dk/target_tracker.hpp src/target_tracker.cpp \
  config/default.json tests/target_tracker_test.cpp tests/config_test.cpp
git commit -m "fix: suppress duplicate moving word fragments"
```

---

### Task 2: Reject Cyrillic and mixed OCR text

**Files:**

- Modify: `src/text_normalizer.cpp`
- Modify: `tests/text_normalizer_test.cpp`
- Modify: `tests/app_test.cpp`

**Interfaces:**

- Consumes: UTF-8 `std::string_view`.
- Produces: unchanged `normalize_for_input(std::string_view)` signature.
- Rule: any encoded code point U+0400–U+04FF returns an empty normalized
  string before ASCII extraction.

- [ ] **Step 1: Add failing Cyrillic tests**

Extend `tests/text_normalizer_test.cpp`:

```cpp
TEST_CASE("normalization rejects Cyrillic and mixed-script OCR") {
    CHECK(dk::normalize_for_input("МЕЧ").empty());
    CHECK(dk::normalize_for_input("BLADEМЕЧ").empty());
    CHECK(dk::normalize_for_input("МЕЧBLADE").empty());
    CHECK(dk::normalize_for_input("IO") == "IO");
}
```

Add an app-level case using the existing fake recognizer/input fixtures:

```cpp
TEST_CASE("app never dispatches mixed Cyrillic OCR") {
    auto config = dk::AppConfig::defaults();
    config.live_input = true;
    FakeFrameSource frames{2};
    FakeDetector detector{{{30, 200, 240, 48}}};
    FakeRecognizer recognizer{{
        {"BLADEМЕЧ", .99F},
        {"BLADEМЕЧ", .99F},
    }};
    FakeInputSink input;
    dk::App app(config, frames, detector, recognizer, input);

    CHECK(app.process_one_frame());
    CHECK_FALSE(app.last_result());
    CHECK(app.process_one_frame());
    CHECK_FALSE(app.last_result());
    CHECK(input.sent.empty());
}
```

- [ ] **Step 2: Run normalizer/app tests and verify RED**

```powershell
cmake --build --preset windows-release --target text_normalizer_test app_test
ctest --test-dir build/windows-release -C Release --output-on-failure `
  -R "text_normalizer_test|app_test"
```

Expected: mixed `BLADEМЕЧ` currently normalizes to `BLADE`, so both new
regressions fail.

- [ ] **Step 3: Implement common-Cyrillic UTF-8 detection**

Add a private helper in `src/text_normalizer.cpp`:

```cpp
bool contains_common_cyrillic(std::string_view text) noexcept {
    for (std::size_t index = 0; index + 1 < text.size(); ++index) {
        const auto first = static_cast<unsigned char>(text[index]);
        const auto second = static_cast<unsigned char>(text[index + 1]);
        if (first >= 0xD0 && first <= 0xD3 &&
            second >= 0x80 && second <= 0xBF) {
            return true;
        }
    }
    return false;
}
```

Call it before extracting ASCII:

```cpp
std::string normalize_for_input(std::string_view text) {
    if (contains_common_cyrillic(text)) {
        return {};
    }
    // Existing uppercase ASCII extraction remains unchanged.
}
```

- [ ] **Step 4: Run normalizer/app tests and verify GREEN**

Run the focused command from Step 2.

Expected: Cyrillic and mixed text are ignored; existing punctuation,
whitespace, one-letter rejection, and two-letter acceptance tests still pass.

- [ ] **Step 5: Commit Cyrillic rejection**

```bash
git add src/text_normalizer.cpp tests/text_normalizer_test.cpp tests/app_test.cpp
git commit -m "fix: ignore Cyrillic OCR exceptions"
```

---

### Task 3: Remove the nested portable ZIP and document distributions

**Files:**

- Modify: `.github/workflows/windows.yml`
- Modify: `.github/workflows/release.yml`
- Modify: `tests/windows_workflow_source_test.cpp`
- Modify: `README.ru.md`

**Interfaces:**

- Consumes: the validated CPack ZIP created by the existing package step.
- Produces: Actions artifact `dota-keyboard-windows-x64-portable` whose payload
  root contains application files and no `.zip` file.
- Preserves: installer artifact, installer lifecycle smoke test, public
  installer/checksum release assets, and ZIP validation.

- [ ] **Step 1: Add failing workflow source contracts**

Add a helper to `tests/windows_workflow_source_test.cpp`:

```cpp
bool require_flat_portable_artifact(
    const std::string& source, const char* workflow_name) {
    bool valid = true;
    valid &= require_text(
        source,
        "$extractRoot = Join-Path $env:GITHUB_WORKSPACE "
        "'build/windows-release/validated-portable'",
        "stable validated portable extraction directory");
    valid &= require_text(
        source,
        "path: build/windows-release/validated-portable",
        "flat validated portable artifact payload");
    if (source.find(
            "path: build/windows-release/dota-keyboard-*-windows-x64.zip") !=
        std::string::npos) {
        std::cerr << workflow_name
                  << " must not upload the CPack ZIP inside an Actions ZIP\n";
        valid = false;
    }
    return valid;
}
```

Call the helper for both workflows. Retain the existing smoke, cache, release
warning, and installer assertions.

- [ ] **Step 2: Run the workflow source test and verify RED**

Compile it portably:

```bash
c++ -std=c++20 -Wall -Wextra -Wpedantic -Werror \
  tests/windows_workflow_source_test.cpp \
  '-DDK_WINDOWS_WORKFLOW_PATH=".github/workflows/windows.yml"' \
  '-DDK_RELEASE_WORKFLOW_PATH=".github/workflows/release.yml"' \
  -o /tmp/dota-windows-workflow-source-test
/tmp/dota-windows-workflow-source-test
```

Expected: failure reporting the missing stable extraction directory and the
inner CPack ZIP upload.

- [ ] **Step 3: Make the validated extraction directory artifact-stable**

In the `Validate packaged ZIP` step of both workflows, replace the temporary
runner path with:

```powershell
$extractRoot = Join-Path $env:GITHUB_WORKSPACE `
  'build/windows-release/validated-portable'
if (Test-Path -LiteralPath $extractRoot) {
  Remove-Item -LiteralPath $extractRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $extractRoot | Out-Null
Expand-Archive -LiteralPath $zip[0].FullName -DestinationPath $extractRoot
```

Rename `Upload validated portable ZIP` to
`Upload validated portable files` and change its path in both workflows:

```yaml
- name: Upload validated portable files
  uses: actions/upload-artifact@v4
  with:
    name: dota-keyboard-windows-x64-portable
    if-no-files-found: error
    path: build/windows-release/validated-portable
```

Do not remove `cpack --preset windows-release`; the CPack ZIP remains the object
that is tested before extraction.

- [ ] **Step 4: Update user documentation**

Replace the portable explanation in `README.ru.md` with wording that states:

```markdown
В GitHub Actions доступны два варианта:

- `dota-keyboard-windows-x64-installer` — рекомендуемый установщик; GitHub
  скачивает внешний ZIP, внутри которого находится один установочный EXE;
- `dota-keyboard-windows-x64-portable` — переносимые файлы без установки;
  внешний ZIP GitHub уже содержит EXE, DLL, модели и конфиг, дополнительного
  вложенного ZIP больше нет.

На странице tagged GitHub Release установщик и контрольная сумма скачиваются
напрямую, без ZIP-обёртки Actions.
```

Keep the warning that portable files must stay together.

- [ ] **Step 5: Run source tests and verify GREEN**

Run the portable workflow source test command from Step 2, then:

```bash
git diff --check
```

Expected: source test exit 0 and no whitespace errors.

- [ ] **Step 6: Commit artifact and documentation changes**

```bash
git add .github/workflows/windows.yml .github/workflows/release.yml \
  tests/windows_workflow_source_test.cpp README.ru.md
git commit -m "ci: flatten portable Actions artifact"
```

---

### Task 4: Review and native Windows release-candidate verification

**Files:**

- Verify only: all files changed by Tasks 1–3
- No release tag in this task

**Interfaces:**

- Consumes: commits from Tasks 1–3.
- Produces: reviewed feature HEAD, successful Windows run, installer and flat
  portable artifacts ready for user dry-run.

- [ ] **Step 1: Run all available local verification**

```bash
git diff --check ecfe8ac..HEAD
jq empty CMakePresets.json
```

Compile and run the two host-portable policy tests with strict warnings:

```bash
c++ -std=c++20 -Wall -Wextra -Wpedantic -Werror \
  tests/windows_workflow_source_test.cpp \
  '-DDK_WINDOWS_WORKFLOW_PATH=".github/workflows/windows.yml"' \
  '-DDK_RELEASE_WORKFLOW_PATH=".github/workflows/release.yml"' \
  -o /tmp/dota-windows-workflow-source-test
/tmp/dota-windows-workflow-source-test

c++ -std=c++20 -Wall -Wextra -Wpedantic -Werror \
  tests/packaging_cmake_source_test.cpp \
  '-DDK_ROOT_CMAKE_PATH="CMakeLists.txt"' \
  '-DDK_PACKAGE_PROJECT_CONFIG_PATH="cmake/PackageProjectConfig.cmake"' \
  '-DDK_CMAKE_PRESETS_PATH="CMakePresets.json"' \
  '-DDK_VCPKG_TRIPLET_PATH="cmake/triplets/x64-windows-dota.cmake"' \
  -o /tmp/dota-packaging-source-test
/tmp/dota-packaging-source-test
```

Expected: both executables exit 0 with `-Werror`. Full tracker, normalizer, app,
OCR, packaging, and installer execution remains the native Windows CI gate.

- [ ] **Step 2: Request independent review**

Review `b29f601..HEAD` for:

- exact/fragment suppression correctness and one-to-one candidate behavior;
- same-word re-eligibility after 15 fully missing frames;
- distinct full-size candidate preservation;
- UTF-8 Cyrillic detection without breaking ASCII punctuation;
- identical Windows/release portable extraction and upload logic;
- no regression to installer, release ancestry, warning, or safe defaults.

Expected: no Critical or Important findings.

- [ ] **Step 3: Push the reviewed feature branch**

```bash
git push origin feature/runtime-hardening-release
```

Expected: GitHub starts the `Windows package` workflow for the reviewed HEAD.

- [ ] **Step 4: Verify the native Windows run**

```bash
gh run list --repo IvanSaratov/dota_word_game \
  --branch feature/runtime-hardening-release \
  --workflow "Windows package" --limit 1
RUN_ID=$(gh run list --repo IvanSaratov/dota_word_game \
  --branch feature/runtime-hardening-release \
  --workflow "Windows package" --limit 1 \
  --json databaseId --jq '.[0].databaseId')
gh run watch "$RUN_ID" --exit-status
```

Expected:

- all CTest cases pass;
- packaged ZIP check succeeds without schema-registration output;
- installer install/check/uninstall lifecycle succeeds;
- portable and installer artifacts upload successfully.

- [ ] **Step 5: Verify portable artifact structure**

Download the artifact to a fresh temporary directory:

```bash
RUN_ID=$(gh run list --repo IvanSaratov/dota_word_game \
  --branch feature/runtime-hardening-release \
  --workflow "Windows package" --limit 1 \
  --json databaseId --jq '.[0].databaseId')
ARTIFACT_DIR=$(mktemp -d -t dota-portable-artifact.XXXXXX)
gh run download "$RUN_ID" \
  --repo IvanSaratov/dota_word_game \
  --name dota-keyboard-windows-x64-portable \
  --dir "$ARTIFACT_DIR"
find "$ARTIFACT_DIR" -type f -name '*.zip' -print
find "$ARTIFACT_DIR" -maxdepth 4 -type f -print
```

Expected: `find` prints nothing. The directory directly contains
`dota_keyboard.exe`, required DLLs, `assets/models`, `config.json`, and
`README.ru.md`.

- [ ] **Step 6: Hand off the second dry-run**

Ask the user to keep `live_input=false`, calibrate with F7, run with F8, and
save the console log. Acceptance criteria:

- every visible intended English word has exactly one `[DRY] would type`;
- no sent word repeats after a short OCR gap;
- no cropped substring such as `HYPE`, `EMA`, `RN`, or `DIH` is proposed;
- Cyrillic/mixed output is never proposed;
- no ONNX schema-registration flood appears.

---

### Task 5: Merge accepted code and publish v0.1.0

**Files:**

- No source changes expected.
- Git refs changed only after Task 4 user acceptance.

**Interfaces:**

- Consumes: accepted feature HEAD and successful feature Windows run.
- Produces: updated `main`, successful `main` Windows run, tag `v0.1.0`, and a
  public GitHub Release containing installer EXE plus SHA-256 checksum.

- [ ] **Step 1: Finish the development branch**

Use `superpowers:finishing-a-development-branch`. Merge the reviewed feature
branch into local `main` without discarding user files in the original
checkout.

- [ ] **Step 2: Push main and verify its Windows run**

```bash
git push origin main
MAIN_RUN_ID=$(gh run list --repo IvanSaratov/dota_word_game \
  --branch main --workflow "Windows package" --limit 1 \
  --json databaseId --jq '.[0].databaseId')
gh run watch "$MAIN_RUN_ID" --exit-status
```

Expected: the run passes and reuses the complete `v2` dependency cache.

- [ ] **Step 3: Create and push the first release tag**

Confirm `v0.1.0` does not already exist, then tag the accepted `main` commit:

```bash
git tag --list v0.1.0
git tag -a v0.1.0 -m "Dota Keyboard v0.1.0"
git push origin v0.1.0
```

Expected: the first command prints nothing; pushing the tag starts
`Windows release`.

- [ ] **Step 4: Verify the release workflow and assets**

```bash
gh run list --repo IvanSaratov/dota_word_game \
  --workflow "Windows release" --limit 1
RELEASE_RUN_ID=$(gh run list --repo IvanSaratov/dota_word_game \
  --workflow "Windows release" --limit 1 \
  --json databaseId --jq '.[0].databaseId')
gh run watch "$RELEASE_RUN_ID" --exit-status
gh release view v0.1.0 --repo IvanSaratov/dota_word_game
```

Expected: release policy confirms the tag is on `main`; build, tests, ZIP,
installer lifecycle, and checksum succeed; the public release exposes exactly
one `DotaKeyboardSetup-0.1.0-windows-x64.exe` and its `.sha256` file.
