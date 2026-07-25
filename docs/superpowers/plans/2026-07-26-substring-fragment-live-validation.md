# Substring Fragment Suppression and Live Validation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> `superpowers:subagent-driven-development` (recommended) or
> `superpowers:executing-plans` to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Suppress a large OCR crop when its normalized text is a strict
substring of a live sent word, then verify the corrected build with native
Windows CI and a real-input game run.

**Architecture:** Extend only the sent-track ownership stage in
`TargetTracker`: exact text remains first, spatially related strict substrings
become the second authoritative rule, and the existing 65%-area geometric rule
remains the fallback for non-substring OCR errors. Reuse the existing
`live_input` configuration field for the final integration test while
preserving the packaged safe default.

**Tech Stack:** C++20, Catch2 3, CMake/CTest, GitHub Actions, GitHub CLI,
Windows 11 x64.

## Global Constraints

- `ALLPICK` and `ALL` are excluded only from evaluating the supplied second
  dry-run; no product blocklist or general OCR exception is added.
- A strict substring is contiguous, non-empty, and shorter than the canonical
  normalized text.
- Substring ownership requires the candidate center to lie inside the retained
  full-size bounds expanded by `max_center_distance_px`.
- Exact sent-text ownership remains first and refreshes full-size bounds.
- Strict-substring and 65%-area fragments never replace canonical text or
  retained full-size bounds.
- The 65% geometric fragment ceiling remains exactly unchanged.
- Default `unlock_missing_frames` remains exactly 15.
- Default `confirm_frames` remains exactly 2.
- Packaged `config.json` keeps `"live_input": false`.
- The existing `live_input` field remains the only live/dry mode variable.
- No public tag or merge to `main` occurs before native Windows CI and user
  live-input acceptance.

**Design:** `docs/superpowers/specs/2026-07-25-runtime-dedupe-portable-artifact-design.md`

---

### Task 1: Suppress large strict-substring fragments

**Files:**

- Modify: `src/target_tracker.cpp`
- Modify: `tests/target_tracker_test.cpp`

**Interfaces:**

- Consumes: existing `TextCandidate`, `Box`, `TargetTracker::update`, and
  `TargetTracker::mark_sent`.
- Produces: unchanged public tracker API.
- Invariant: ordinary one-to-one association receives only candidates not
  already owned by an exact, substring, or geometric sent-track rule.

- [ ] **Step 1: Add a failing log-derived regression**

Add this case to `tests/target_tracker_test.cpp`:

```cpp
TEST_CASE("sent word consumes large strict substring crop") {
    dk::TargetTracker tracker;
    const auto full =
        boxed_word("ABADDON", {751, 420, 286, 52});
    const auto crop =
        boxed_word("ADDON", {818, 413, 216, 51});

    CHECK_FALSE(update_tracker(tracker, {full}));
    auto ready = update_tracker(tracker, {full});
    REQUIRE(ready);
    tracker.mark_sent(*ready);

    CHECK_FALSE(update_tracker(tracker, {crop}));
    CHECK_FALSE(update_tracker(tracker, {crop}));
}
```

The crop area is `216 * 51 = 11016`, or 74.1% of the retained
`286 * 52 = 14872` area. It therefore proves the new substring rule rather
than the existing 65% rule.

- [ ] **Step 2: Add a non-substring preservation regression**

Add this case:

```cpp
TEST_CASE("overlapping non-substring above area ceiling stays eligible") {
    dk::TargetTracker tracker;
    const auto sent_word =
        boxed_word("ABADDON", {751, 420, 286, 52});
    const auto distinct =
        boxed_word("MEDUSA", {818, 413, 216, 51});

    CHECK_FALSE(update_tracker(tracker, {sent_word}));
    auto ready = update_tracker(tracker, {sent_word});
    REQUIRE(ready);
    tracker.mark_sent(*ready);

    CHECK_FALSE(update_tracker(tracker, {distinct}));
    const auto selected = update_tracker(tracker, {distinct});
    REQUIRE(selected);
    CHECK(selected->normalized_text == "MEDUSA");
}
```

This candidate is spatially overlapping but is neither a strict substring nor
small enough for the 65% rule.

- [ ] **Step 3: Run the focused regression and verify RED**

On a configured Windows tree:

```powershell
cmake --build --preset windows-release --target target_tracker_test
ctest --test-dir build/windows-release -C Release --output-on-failure `
  -R "^target_tracker_test$"
```

Expected before production changes: the strict-substring case fails because
the second `ADDON` becomes eligible; the `MEDUSA` preservation case passes.

When CMake/Catch2 is unavailable on the macOS controller, add both exact
scenarios to `/private/tmp/dk_task1_tracker_regression.cpp`, compile it against
the unchanged production source, and require the `ADDON` assertion to fail:

```bash
clang++ -std=c++20 -Wall -Wextra -Wpedantic -Werror \
  -Iinclude src/target_tracker.cpp \
  /private/tmp/dk_task1_tracker_regression.cpp \
  -o /tmp/dota-substring-red
/tmp/dota-substring-red
```

- [ ] **Step 4: Implement strict-substring geometry**

Add a private helper beside the existing geometry helpers in
`src/target_tracker.cpp`:

```cpp
bool is_strict_substring(
    const TextCandidate& candidate, const TextCandidate& sent) noexcept {
    const auto& candidate_text = candidate.normalized_text;
    const auto& sent_text = sent.normalized_text;
    return !candidate_text.empty() &&
           candidate_text.size() < sent_text.size() &&
           sent_text.find(candidate_text) != std::string::npos;
}

bool is_substring_fragment(
    const TextCandidate& candidate, const TextCandidate& sent,
    float expansion) noexcept {
    return is_strict_substring(candidate, sent) &&
           center_inside_expanded(candidate.bounds, sent.bounds, expansion);
}
```

Preserve the existing exact ownership pass. Insert a new sent ownership pass
after exact ownership and before the existing `is_fragment` area pass. For
every unmatched candidate:

1. consider only live sent tracks satisfying `is_substring_fragment`;
2. select the closest qualifying sent track using `center_distance`;
3. call `consume_for_sent_track` with `refresh_full_bounds=false`.

Do not merge the substring and area rules into one predicate: their order is
part of the approved design, and the strict-substring rule must win before the
geometric fallback when multiple live sent tracks overlap.

- [ ] **Step 5: Run focused and sanitizer-backed verification**

Run the Windows focused test from Step 3. On the controller, compile and run
the complete direct tracker harness normally and with sanitizers:

```bash
clang++ -std=c++20 -Wall -Wextra -Wpedantic -Werror \
  -Iinclude src/target_tracker.cpp \
  /private/tmp/dk_task1_tracker_regression.cpp \
  -o /tmp/dota-substring-green
/tmp/dota-substring-green

clang++ -std=c++20 -Wall -Wextra -Wpedantic -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Iinclude src/target_tracker.cpp \
  /private/tmp/dk_task1_tracker_regression.cpp \
  -o /tmp/dota-substring-green-san
/tmp/dota-substring-green-san

git diff --check
```

Expected: `ADDON` remains suppressed, `MEDUSA` becomes eligible after two
frames, every earlier exact/fragment/expiry/competing-track regression passes,
sanitizers are quiet, and the diff check is clean.

- [ ] **Step 6: Commit the behavior**

```bash
git add src/target_tracker.cpp tests/target_tracker_test.cpp
git commit -m "fix: suppress substring word fragments"
```

---

### Task 2: Review and native Windows verification

**Files:**

- Verify only: Task 1 diff and all previously packaged files.

**Interfaces:**

- Consumes: committed Task 1 behavior.
- Produces: reviewed feature HEAD and successful Windows package artifacts.

- [ ] **Step 1: Request independent task review**

Review the Task 1 base-to-head diff for:

- strict substring semantics and expanded-center geometry;
- exact → substring → 65%-area → ordinary ownership order;
- canonical text/bounds preservation;
- non-substring candidate eligibility;
- no change to 15-frame expiry, 65% ceiling, confirmation latency, or public
  tracker API;
- meaningful RED/GREEN evidence.

Expected: no Critical or Important findings.

- [ ] **Step 2: Run all controller-side checks**

```bash
git diff --check 77ea8b9..HEAD
jq empty CMakePresets.json

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

Expected: every command exits 0 with no warnings or whitespace errors.

- [ ] **Step 3: Push the reviewed feature HEAD**

```bash
git push origin feature/runtime-hardening-release
```

Expected: the `Windows package` workflow starts for the substring-fix HEAD.

- [ ] **Step 4: Verify Windows CI**

```bash
RUN_ID=$(gh run list --repo IvanSaratov/dota_word_game \
  --branch feature/runtime-hardening-release \
  --workflow "Windows package" --limit 1 \
  --json databaseId --jq '.[0].databaseId')
gh run watch "$RUN_ID" --repo IvanSaratov/dota_word_game --exit-status
```

Expected:

- all CTest cases, including both new tracker cases, pass;
- the vcpkg exact cache is restored and configure/install completes without a
  dependency rebuild;
- ZIP validation and installer install/check/uninstall pass;
- portable, installer, and CTest-log artifacts upload successfully.

- [ ] **Step 5: Verify artifact structure and safe default**

```bash
RUN_ID=$(gh run list --repo IvanSaratov/dota_word_game \
  --branch feature/runtime-hardening-release \
  --workflow "Windows package" --limit 1 \
  --json databaseId --jq '.[0].databaseId')
ARTIFACT_DIR=$(mktemp -d /tmp/dota-substring-portable.XXXXXX)
gh run download "$RUN_ID" --repo IvanSaratov/dota_word_game \
  --name dota-keyboard-windows-x64-portable \
  --dir "$ARTIFACT_DIR"
find "$ARTIFACT_DIR" -type f -name '*.zip' -print
jq -e '.live_input == false and
       .tracker.confirm_frames == 2 and
       .tracker.unlock_missing_frames == 15' \
  "$ARTIFACT_DIR/config.json"
```

Expected: `find` prints nothing and `jq` exits 0.

---

### Task 3: Validate real keyboard input

**Files:**

- User-local only: downloaded artifact `config.json`
- User output: `dota_live_logs.log`
- No repository default changes.

**Interfaces:**

- Consumes: Task 2 portable or installed artifact.
- Produces: user confirmation that the game accepts input and a live-mode log
  with exactly one successful dispatch per target.

- [ ] **Step 1: Enable the existing variable locally**

In the downloaded/extracted copy only, change:

```json
"live_input": false
```

to:

```json
"live_input": true
```

Do not edit `config/default.json` in the repository.

- [ ] **Step 2: Start with visible live-mode confirmation**

Launch from Git Bash while preserving console output:

```bash
./dota_keyboard.exe 2>&1 | tee dota_live_logs.log
```

Before pressing F8, require the banner:

```text
LIVE INPUT ENABLED
```

After F7 calibration and F8, require:

```text
RUNNING (LIVE INPUT)
```

- [ ] **Step 3: Exercise real game input**

Keep the game focused, process several target words, and stop immediately with
F8 if unrelated HUD/notification text is inside the calibrated region.

For each target, require both:

- the game visibly accepts the word and awards/removes the target;
- the console contains exactly one line beginning with `Input sent for ` and
  ending with that target's normalized text.

The previously observed `ALL PICK` notification is not a product exception.
Exclude notification/HUD areas from calibration where practical.

- [ ] **Step 4: Inspect the live log**

```bash
rg '^Input ' dota_live_logs.log
rg 'Schema error: Trying to register schema' dota_live_logs.log
```

Acceptance criteria:

- no `Input sent for ADDON` after `Input sent for ABADDON`;
- no other cropped substring is sent;
- no target is sent twice;
- no Cyrillic or mixed-script OCR is sent;
- no schema-registration flood appears;
- the user confirms that actual keystrokes reached and were accepted by the
  game.

- [ ] **Step 5: Continue the release sequence only after acceptance**

After the user supplies `dota_live_logs.log` and confirms the game response,
use `superpowers:finishing-a-development-branch` to merge the accepted feature
branch into `main`, verify the `main` Windows run, create tag `v0.1.0`, and
verify the public installer plus SHA-256 release assets.
