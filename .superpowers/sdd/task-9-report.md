# Task 9 report: Windows CI, ZIP packaging, and operator guide

## Delivered

- Added `.github/workflows/windows.yml`: a least-privilege (`contents: read`)
  `windows-2022` workflow that restores a vcpkg binary-artifact cache, downloads
  the pinned and hash-checked OCR assets, then configures, builds, tests,
  installs, packages, and uploads the Release ZIP plus the CTest log.
- The workflow uses the existing multi-config `windows-release` presets. It
  correctly passes `--config Release` to `cmake --install`; its package artifact
  path is `build/windows-release/dota-keyboard-*-windows-x64.zip`.
- Added Windows-only CMake install and CPack rules. The ZIP has the EXE, runtime
  DLL closure resolved from the vcpkg `bin` directory, OCR model and dictionary
  under `assets/models`, `config/default.json` renamed to `config.json`, and the
  Russian guide. The ZIP generator has no directory-install rule, source-package
  configuration, test fixture, vcpkg tree, or Python input.
- Added `README.ru.md` with exact F7 calibration and F8 dry-run/live-mode
  instructions, plus the requested diagnostics.
- Confirmed that `config/default.json` already keeps `live_input` false; no
  behavioral config change was necessary.

## Acceptance and benchmark status

The README contains the requested real-game matrix, but every row is explicitly
**not run**. It also records the 300-confirmed-target mean/p95 benchmark and the
40 ms p95 total target as pending. No fictional game acceptance or performance
result was recorded.

## Verification performed

- Ruby YAML structure check confirmed `windows-2022`, `contents: read`, pinned
  fetch, all prescribed CMake/CTest/CPack commands, vcpkg cache, and artifact
  upload.
- Ruby JSON check confirmed the `windows-release` ZIP package preset and
  `live_input: false`.
- Static CMake inspection confirmed the required installed files and rejected
  directory/source/test/vcpkg/Python packaging inputs.
- README presence check confirmed F7/F8, dry/live input, all key troubleshooting
  terms, pending matrix, 300-target count, and 40 ms criterion.
- `git diff --check` passed.

## Environment caveat

This macOS host has no `cmake`, `pwsh`, `actionlint`, Windows SDK, or vcpkg
installation. Therefore a Windows configure/build/CTest/package run and ZIP
contents inspection could not be executed locally. The new GitHub workflow is
the required Windows verification path; before release, inspect its uploaded ZIP
to confirm the resolved DLL closure and execute the manual game matrix and
benchmark described in the README.
