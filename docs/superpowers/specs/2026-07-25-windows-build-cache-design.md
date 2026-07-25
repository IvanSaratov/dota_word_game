# Windows build cache and local build design

## Goal

Make repeated Windows GitHub Actions builds reuse completed vcpkg packages, while
documenting an equivalent manual Windows 11 x64 build as a fallback.

## Current problem

`VCPKG_DEFAULT_BINARY_CACHE` points to
`${{ github.workspace }}\.vcpkg-binary-cache`, but `actions/cache` restores and
saves `${{ runner.temp }}\vcpkg-binary-cache`. The directory populated by vcpkg
therefore is not persisted. The combined configure/build/test/package step also
delays cache persistence until the complete job succeeds.

## Considered approaches

1. Replace GitHub Actions with manual builds. This avoids hosted-runner wait
   time, but the first local build still compiles OpenCV and ONNX Runtime and
   requires the user to maintain a matching Visual Studio/vcpkg environment.
2. Keep the existing combined job and only correct the path. This is a small
   change, but a later compile or test failure prevents a newly created cache
   from being saved.
3. Keep GitHub Actions, split dependency configuration from application
   build/test/package, and explicitly restore/save the real vcpkg binary cache.
   Also document the manual build. This provides reproducible downloadable
   artifacts and a local fallback, so it is the selected approach.

## Workflow design

- Restore `${{ github.workspace }}\.vcpkg-binary-cache` with
  `actions/cache/restore`.
- Use a cache key derived from the Windows runner image and `vcpkg.json`.
- Keep the pinned vcpkg baseline fetch, checkout, validation, and bootstrap.
- Run `cmake --preset windows-release` as its own dependency/configuration step.
  Manifest-mode vcpkg installs dependencies during this command.
- Immediately save the binary cache with `actions/cache/save` when the restore
  was not an exact hit. The save step uses `always()` so successfully built
  packages can survive a later configuration failure.
- Run build, CTest, install, CPack, package smoke test, and artifact upload after
  the cache-save boundary.
- Use the same cache key for restore and save. A changed `vcpkg.json` produces a
  new cache; `restore-keys` may supply older packages and vcpkg fills only the
  missing entries.

## Manual Windows build documentation

`README.ru.md` will describe:

- Windows 11 x64, Visual Studio 2022 with Desktop development with C++, Git,
  CMake 3.28 or newer, PowerShell 7, and a full vcpkg clone.
- Fetching and checking out the exact `builtin-baseline` from `vcpkg.json`, then
  bootstrapping vcpkg and setting `VCPKG_ROOT`.
- Fetching the pinned OCR assets.
- Configure, Release build, CTest, install, and CPack commands matching CI.
- The expected ZIP location and the fact that preserving the vcpkg binary cache
  and build directory makes subsequent local builds faster.

## Rollout

The current GitHub Actions run must continue untouched. The workflow and README
commit will contain `[skip ci]`, which skips only the push-triggered run for that
commit. After the current run ends, the updated workflow can be started with
`workflow_dispatch` and its cache behavior verified on a following run.

## Verification

- Parse `CMakePresets.json` and validate the workflow text with `actionlint` when
  available.
- Assert that the restore/save paths exactly equal
  `VCPKG_DEFAULT_BINARY_CACHE`.
- Assert that the configure command precedes the explicit cache-save step and
  build/test/package follows it.
- Check README commands against the committed presets and scripts.
- Run `git diff --check` and the existing portable test suite.
- Confirm that pushing the documentation/workflow commit does not create a new
  push-triggered run while the current run remains active.
