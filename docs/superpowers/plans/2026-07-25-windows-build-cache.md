# Windows Build Cache and Manual Build Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Persist the vcpkg packages actually produced by Windows CI and document an equivalent manual Windows 11 x64 build.

**Architecture:** GitHub Actions restores the workspace vcpkg binary cache, configures CMake so manifest dependencies are installed, and explicitly saves the cache before compiling and testing the application. The Russian README mirrors the pinned vcpkg and CMake commands used by CI.

**Tech Stack:** GitHub Actions, PowerShell 7, CMake Presets, vcpkg manifest mode, Visual Studio 2022, Markdown.

## Global Constraints

- The active GitHub Actions run `30148252834` must not be canceled.
- The implementation push must not start a second push-triggered run; the HEAD commit message must contain `[skip ci]`.
- The target remains Windows 11 x64 with Visual Studio 2022.
- The pinned `builtin-baseline` in `vcpkg.json` remains the dependency source of truth.
- The existing OCR asset hashes, package smoke test, and artifact names remain unchanged.

---

### Task 1: Persist the real vcpkg binary cache

**Files:**
- Modify: `.github/workflows/windows.yml`

**Interfaces:**
- Consumes: `VCPKG_DEFAULT_BINARY_CACHE`, `vcpkg.json`, CMake configure preset `windows-release`.
- Produces: cache key `windows-2022-vcpkg-${{ hashFiles('vcpkg.json') }}` and a configured `build/windows-release` tree for the later build step.

- [ ] **Step 1: Record the failing cache-path assertion**

Run:

```bash
python3 - <<'PY'
from pathlib import Path
text = Path(".github/workflows/windows.yml").read_text()
assert "path: ${{ github.workspace }}\\.vcpkg-binary-cache" in text
PY
```

Expected: FAIL because the workflow currently caches `${{ runner.temp }}\vcpkg-binary-cache`.

- [ ] **Step 2: Replace the combined cache action with explicit restore**

Use:

```yaml
      - name: Restore vcpkg binary cache
        id: vcpkg-cache
        uses: actions/cache/restore@v5
        with:
          path: ${{ github.workspace }}\.vcpkg-binary-cache
          key: windows-2022-vcpkg-${{ hashFiles('vcpkg.json') }}
          restore-keys: |
            windows-2022-vcpkg-
```

- [ ] **Step 3: Split dependency configuration from application build**

Insert after OCR asset download:

```yaml
      - name: Configure project and install dependencies
        shell: pwsh
        run: |
          $ErrorActionPreference = 'Stop'
          cmake --preset windows-release
          if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

      - name: Save vcpkg binary cache
        if: ${{ always() && steps.vcpkg-cache.outputs.cache-hit != 'true' }}
        uses: actions/cache/save@v5
        with:
          path: ${{ github.workspace }}\.vcpkg-binary-cache
          key: windows-2022-vcpkg-${{ hashFiles('vcpkg.json') }}
```

Remove `cmake --preset windows-release` from the later step and rename it
`Build, test, install, and package`.

- [ ] **Step 4: Verify workflow structure**

Run:

```bash
python3 - <<'PY'
from pathlib import Path

text = Path(".github/workflows/windows.yml").read_text()
cache_path = r"${{ github.workspace }}\.vcpkg-binary-cache"
cache_key = r"windows-2022-vcpkg-${{ hashFiles('vcpkg.json') }}"
assert text.count(f"path: {cache_path}") == 2
assert text.count(f"key: {cache_key}") == 2
assert text.index("Configure project and install dependencies") < text.index("Save vcpkg binary cache")
assert text.index("Save vcpkg binary cache") < text.index("Build, test, install, and package")
PY
```

Expected: PASS.

- [ ] **Step 5: Run workflow static validation**

Run:

```bash
git diff --check
```

If `actionlint` is installed, also run:

```bash
actionlint .github/workflows/windows.yml
```

Expected: exit 0.

### Task 2: Document the manual Windows 11 x64 build

**Files:**
- Modify: `README.ru.md`

**Interfaces:**
- Consumes: `scripts/fetch-models.ps1`, `CMakePresets.json`, `vcpkg.json`.
- Produces: copy-paste PowerShell commands yielding `build/windows-release/dota-keyboard-*-windows-x64.zip`.

- [ ] **Step 1: Add prerequisites**

Add a `## Самостоятельная сборка на Windows 11 x64` section before
`## Неполадки`, listing Visual Studio 2022 Desktop development with C++, Git,
CMake 3.28+, PowerShell 7, and a full vcpkg clone.

- [ ] **Step 2: Add pinned vcpkg setup commands**

Document:

```powershell
$VcpkgRoot = 'C:\src\vcpkg'
$Baseline = (Get-Content .\vcpkg.json -Raw | ConvertFrom-Json).'builtin-baseline'
git -C $VcpkgRoot fetch --no-tags origin $Baseline
git -C $VcpkgRoot checkout --detach $Baseline
& "$VcpkgRoot\bootstrap-vcpkg.bat" -disableMetrics
$env:VCPKG_ROOT = $VcpkgRoot
```

- [ ] **Step 3: Add build, test, and package commands**

Document:

```powershell
pwsh -NoProfile -File .\scripts\fetch-models.ps1
cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release --output-on-failure
cmake --install .\build\windows-release --config Release --prefix .\dist
cpack --preset windows-release
```

Explain that the first build can be long, subsequent builds reuse vcpkg and
build outputs, and the ZIP appears under `build\windows-release`.

- [ ] **Step 4: Verify documented commands**

Run:

```bash
rg -n 'builtin-baseline|fetch-models.ps1|cmake --preset windows-release|ctest --preset windows-release|cpack --preset windows-release' README.ru.md
```

Expected: all five command groups are present.

### Task 3: Verify and deliver without a parallel CI run

**Files:**
- Modify: `.github/workflows/windows.yml`
- Modify: `README.ru.md`
- Add: `docs/superpowers/plans/2026-07-25-windows-build-cache.md`

**Interfaces:**
- Consumes: existing portable/source test commands and active Actions run state.
- Produces: a pushed implementation commit whose push workflow is skipped.

- [ ] **Step 1: Run the existing portable/source test suite**

Run:

```bash
test_bin_dir=$(mktemp -d /private/tmp/dota-word-tests.XXXXXX)
clang++ -std=c++20 -Wall -Wextra -Werror -Iinclude tests/hotkey_state_portable_test.cpp -o "$test_bin_dir/hotkey"
clang++ -std=c++20 -Wall -Wextra -Werror -Iinclude tests/input_sink_portable_test.cpp -o "$test_bin_dir/input"
clang++ -std=c++20 -Wall -Wextra -Werror -DDK_APP_SOURCE_PATH=\"$(pwd)/src/app.cpp\" tests/app_source_test.cpp -o "$test_bin_dir/app"
clang++ -std=c++20 -Wall -Wextra -Werror -DDK_CONFIG_SOURCE_PATH=\"$(pwd)/src/config.cpp\" tests/config_source_test.cpp -o "$test_bin_dir/config"
clang++ -std=c++20 -Wall -Wextra -Werror -DDK_DXGI_CAPTURE_SOURCE_PATH=\"$(pwd)/src/dxgi_capture_win32.cpp\" tests/dxgi_capture_source_test.cpp -o "$test_bin_dir/dxgi"
clang++ -std=c++20 -Wall -Wextra -Werror -DDK_MAIN_WIN32_SOURCE_PATH=\"$(pwd)/src/main_win32.cpp\" tests/main_win32_source_test.cpp -o "$test_bin_dir/main"
clang++ -std=c++20 -Wall -Wextra -Werror -DDK_WIN32_INPUT_SINK_SOURCE_PATH=\"$(pwd)/src/win32_input_sink.cpp\" tests/win32_input_sink_source_test.cpp -o "$test_bin_dir/win32_input"
for test_exe in "$test_bin_dir"/*; do "$test_exe"; done
```

Expected: 7/7 pass.

- [ ] **Step 2: Verify repository state**

Run:

```bash
git diff --check
git status --short
```

Expected: only the planned workflow, README, and plan changes.

- [ ] **Step 3: Commit with the skip instruction**

Run:

```bash
git add .github/workflows/windows.yml README.ru.md docs/superpowers/plans/2026-07-25-windows-build-cache.md
git commit -m "ci: persist Windows dependency cache [skip ci]"
```

- [ ] **Step 4: Push and confirm no second push run**

Push `feature/windows-word-input`, then list workflow runs for the branch.

Expected: active run `30148252834` continues and no new push-triggered run
exists for the implementation commit.

- [ ] **Step 5: Defer native cache verification**

After run `30148252834` ends, trigger `workflow_dispatch` on the updated branch.
After that run completes successfully, trigger it once more and compare timings
to confirm a vcpkg cache hit. Do not claim the speedup before these runs provide
evidence.
