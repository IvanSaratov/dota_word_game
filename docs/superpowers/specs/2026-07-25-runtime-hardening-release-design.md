# Runtime Hardening and Tagged Installer Releases

## Purpose

Harden the Windows utility using evidence from the first real-game dry run,
reduce the portable package to application-owned files, and publish a
directly downloadable Windows installer only from version tags that belong to
the `main` branch.

The dry-run log established four concrete facts:

- Real targets such as `RIKI`, `BANE`, `PLATEMAIL`, and `TANGO` are recognized
  with high confidence.
- Runtime performance is already acceptable: 1,072 measured frames had a
  9.656 ms mean and 22.260 ms p95 total time, with four frames above 40 ms.
- A target can be selected repeatedly as it moves farther than 90 pixels from
  its original locked position, and a temporary partial OCR result can be
  selected as a new target.
- A bright non-text object was repeatedly recognized as the one-letter result
  `C`, while startup emitted 1,222 duplicate ONNX schema messages across two
  launches.

This release must remain in dry-run mode by default. A public version tag is
created only after a new real-game dry run confirms the hardened behavior.

## Scope

The work contains four coordinated deliverables:

1. Spatially persistent target locking and a two-letter minimum.
2. Clean portable packaging without Windows operating-system DLLs.
3. A per-user Inno Setup installer and tag-gated GitHub Release workflow.
4. Quiet ONNX startup with a regression check for schema-message flooding.

Hard-coded HUD masks are not part of this change. The user should calibrate
the smallest region that still covers every possible target trajectory. The
existing normalized `ignored_regions` configuration remains available, but
the program must not mask the lower-right character area because valid words
can cross it.

## Target Tracking

### Track identity

`TargetTracker` will keep persistent spatial tracks instead of rebuilding a
text-keyed track list on every frame. Each track stores:

- the most recent bounding box;
- the most recently stable normalized text;
- the number of consecutive frames containing that stable text;
- whether the track has already been sent;
- the number of consecutive frames in which the object was missing.

Candidates and existing tracks are matched one-to-one by spatial proximity
and compatible box dimensions. Text equality is not required for spatial
identity. With at most two real targets, deterministic nearest-distance
matching is sufficient; each candidate and each existing track may
participate in at most one match.

A matched track always updates its bounding box and resets its missing-frame
count. Therefore, a sent lock follows the word as it falls instead of staying
at the position where it was first selected.

### Text confirmation

For an unsent track, equal normalized text on consecutive matched frames
increments the confirmation count. A different text replaces the provisional
text and resets confirmation to one. The track becomes eligible only after
`confirm_frames` equal results.

For a sent track, later OCR text is ignored for send eligibility. A sequence
such as `BANE`, `ANE`, `BANE` remains one sent spatial object and cannot
produce another input event.

### Disappearance and repeated words

An unmatched track increments its missing-frame count but remains available
for reassociation. It is erased after `unlock_missing_frames` consecutive
missing frames. Once erased, a new target with the same normalized text may
create a new track and be sent normally. This supports legitimate consecutive
occurrences of the same word without relying on a global text cooldown.

### Candidate filtering and selection

After normalization, candidates containing fewer than two ASCII letters are
discarded before tracking. One-letter false positives such as `C` cannot form
a track, while valid two-letter words remain supported.

Confidence filtering remains at the configured threshold. Among multiple
confirmed unsent tracks, the track with the largest bottom Y coordinate is
selected, preserving lower-target priority.

## Portable Package

The portable staging directory will continue to use CMake runtime dependency
resolution for transitive vcpkg libraries. It will contain:

- `dota_keyboard.exe`;
- the ONNX model and dictionary;
- `config.json` and `README.ru.md`;
- ONNX Runtime, OpenCV, protobuf, Abseil, RE2, zlib, and any other resolved
  vcpkg runtime DLL required by those libraries;
- the required Visual C++ runtime DLLs.

The package will not contain Windows operating-system components:

- `kernel32.dll`, `user32.dll`, `gdi32.dll`, `advapi32.dll`;
- `d3d11.dll`, `dxgi.dll`, `setupapi.dll`, `dbghelp.dll`;
- `ucrtbase.dll`;
- any `api-ms-*` or `ext-ms-*` API-set forwarder;
- any dependency resolved from `Windows\System32` or `Windows\SysWOW64`.

`CMAKE_INSTALL_UCRT_LIBRARIES` will not be enabled because Windows 10 and 11
already provide the Universal CRT. CMake path exclusions must handle both
forward and backslash separators. The CI package validator will independently
reject forbidden basenames case-insensitively, so a future CMake or runner
path-format change cannot silently reintroduce system DLLs.

The portable ZIP remains an Actions artifact for diagnostics and manual
fallback. It is not the primary public download.

## Inno Setup Installer

CPack will additionally generate
`DotaKeyboardSetup-<version>-windows-x64.exe` using the `INNOSETUP` generator
on the pinned `windows-2022` runner. The generator requires Inno Setup 6 or
newer; the runner provides it without an additional download. The installer
will:

- install per-user without requiring elevation;
- use `PrivilegesRequired=lowest`;
- place the payload below `%LOCALAPPDATA%\Programs`;
- create a Start Menu shortcut;
- register an uninstaller in Windows Apps & Features;
- preserve the safe default `live_input=false`;
- allow the user to launch the application after installation.

CI will run the installer silently into a unique temporary directory, verify
the installed file allowlist, execute
`dota_keyboard.exe --check-install`, and run the uninstaller silently. The
test fails if installation, startup validation, or removal fails.

The installer is not code-signed in this scope. The Release notes must state
that Windows SmartScreen may warn about an unsigned first release. Code
signing can be added later without changing the packaging architecture.

## Tagged Release Workflow

Normal pushes and pull requests continue to run the existing Windows build,
test, portable package, and artifact workflow. They never create a GitHub
Release.

A separate release workflow responds to tags matching the broad GitHub
trigger `v*`, then validates all of the following before granting publication
steps:

1. The tag has the exact semantic version form `vMAJOR.MINOR.PATCH`, with
   numeric components only.
2. The version equals `project(dota_keyboard VERSION ...)` in
   `CMakeLists.txt`.
3. The tagged commit is an ancestor of `origin/main`.
4. The full Windows build and test suite passes.
5. The portable package validation passes.
6. The silent installer round-trip passes.

A tag pointing only to a feature branch fails before publication. A valid tag
on any commit in `main` history is allowed, which supports rebuilding an older
released commit without requiring it to remain the branch tip.

The workflow uses the repository `GITHUB_TOKEN` with `contents: write` only
for the release job. After all validation succeeds, it immediately creates a
public GitHub Release with generated notes and uploads:

- `DotaKeyboardSetup-<version>-windows-x64.exe`;
- `DotaKeyboardSetup-<version>-windows-x64.exe.sha256`.

The installer and portable ZIP are also uploaded as Actions artifacts for
diagnostics. GitHub Actions artifact downloads are ZIP-wrapped by the
platform, while the GitHub Release installer is a direct `.exe` download.

## ONNX Startup Logging

The process will continue to create exactly one static `Ort::Env`. Its logging
threshold will be raised from `ORT_LOGGING_LEVEL_WARNING` to
`ORT_LOGGING_LEVEL_FATAL`. Model-load and session-construction failures remain
C++ exceptions handled by application startup and are not suppressed.

The packaged `--check-install` test will capture combined stdout and stderr.
It must exit successfully and contain no `Schema error: Trying to register
schema` text. This makes quiet startup an enforced behavior rather than an
assumption. If the ONNX Runtime build still writes the schema flood outside
the configured logger, the release remains blocked until the runtime package
configuration is corrected.

Per-frame OCR and timing lines remain available during dry-run validation.
Removing diagnostic frame output is outside this change.

## Tests

### Target tracker tests

Automated sequences will prove:

- one `BANE` moving more than 90 pixels over many adjacent frames is selected
  only once;
- `BANE`, `ANE`, `BANE` at adjacent positions remains one locked object;
- a single missing frame does not unlock a sent object;
- disappearance for `unlock_missing_frames` permits a later new `BANE`;
- two simultaneous tracks retain one-to-one identity and lower-target
  priority;
- a one-letter normalized candidate is rejected before tracking;
- a valid two-letter candidate can still be confirmed and selected.

### Package and installer tests

CI will prove:

- every required application file and vcpkg runtime dependency is installed;
- no forbidden Windows system DLL is present;
- no source, test, build-tree, or Python file is present;
- the portable executable passes `--check-install`;
- the installer completes a silent install, startup check, and uninstall;
- startup output contains no ONNX schema flood.

### Release-policy tests

The tag-validation logic will be implemented in a separately testable script.
Tests will cover:

- a valid `v0.1.0` tag and matching project version;
- malformed tags;
- a tag version different from the project version;
- a commit outside `main`;
- a valid tagged commit in `main` history.

No real GitHub Release is created by tests. Publication occurs only after an
actual validated tag push.

## Acceptance and Rollout

After CI passes on the feature branch, the user downloads the new portable
artifact and repeats dry-run validation with a smaller calibrated region.
Acceptance requires:

- no repeated `[DRY] would type` event for one continuously visible target;
- no partial-word event from the same target;
- no one-letter `C` event;
- correct handling of a real two-letter word;
- no ONNX schema flood at startup;
- p95 total latency remaining below 40 ms.

Only after this real-game acceptance is the first version tag created on
`main`. The tag then produces and immediately publishes the installer Release.
