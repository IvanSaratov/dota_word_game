# Final review fix report

## Status

The requested final-review safety fixes are implemented on top of `caab58c`.
No benchmark was run and no fixture or acceptance result was invented.

## Fixes

- Restored default construction of `FrameReleaseGuard`.
- Stored the exact screen-space capture region in `Pipeline`; the main loop now
  recomputes the client-relative screen region before every frame and rebuilds
  capture before OCR/input when the window moved. Invalid geometry pauses
  processing and requires an F8 retry or F7 recalibration.
- Moved `Hotkeys` and its Win32 message loop to a dedicated `std::jthread`.
  Atomic processing/calibration/quit state remains responsive while capture,
  OCR, or delayed input blocks. Startup registration failures and later thread
  failures propagate to the main thread.
- Added one shared cancellation predicate to `App` and `Win32InputSink`.
  `App` checks it immediately before dispatch; the Windows sink checks again at
  the final zero-delay `SendInput` boundary and before every delayed pair.
- Delayed input tracks complete accepted pairs. Cancellation, focus loss,
  timer failure, or a zero-event `SendInput` failure returns `partial` after a
  prefix; before a prefix it returns `blocked` or `not_foreground`. A partially
  accepted key pair is also `partial`.
- Added all requested tracker and detector configuration invariants, including
  finite positive values and normalized ignored-rectangle bounds.
- Catches exceptions only around per-frame processing, discards the failed
  pipeline without sending, and rebuilds on the next attempt. Five consecutive
  frame failures pause processing while leaving F7/F8 available. Configuration,
  OCR construction, hotkey registration, and pipeline initialization errors
  remain fatal.

## RED evidence

The new focused tests were run before production changes:

- `dxgi_capture_source_test`: failed for missing
  `FrameReleaseGuard() = default`.
- `win32_input_sink_source_test`: failed for missing final-boundary
  cancellation/focus checks and prefix-aware failure statuses.
- `main_win32_source_test`: failed for missing dedicated hotkey ownership,
  shared cancellation, capture-region identity/rebuild, and bounded frame
  recovery.
- `app_source_test`: failed for missing pre-dispatch cancellation.
- `config_source_test`: failed all requested tracker/detector validation
  predicates.
- `input_sink_portable_test`: did not compile because
  `interrupted_send_status` did not exist.

Each failure was the expected regression, not a harness/setup failure.

## GREEN evidence

The same six dependency-free tests compile with
`clang++ -std=c++20 -Wall -Wextra -Wpedantic -Werror` and run successfully.
The Catch2 suites were also extended with behavioral cases for cancellation
during OCR, delayed-prefix status, and every invalid configuration class.

## Windows-only caveats

This macOS host has no CMake/CTest, Windows SDK, MSVC, vcpkg OpenCV,
ONNX Runtime, or interactive Windows desktop. Native compilation and the full
Catch2 suite therefore remain CI/Windows checks. Windows verification should
also exercise:

- F8 during a long OCR call and during an inter-key timer;
- F7/F8 ordering while the calibration selector is active;
- moving the game window across monitor boundaries;
- partial/blocked `SendInput` behavior under UIPI;
- repeated DXGI/OCR frame exceptions and recovery.

The calibration overlay is not permanent: `RegionSelector::select` destroys it
on completion, cancellation, error, or quit. While active, allowing either the
overlay or its owner game window to be foreground is intentional so the
topmost clickable selector does not cancel merely because Windows returns
focus to the owner during activation; any third-party foreground window still
cancels selection.
