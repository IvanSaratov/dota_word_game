# Task 8 report: pipeline orchestration, metrics, and application loop

## Delivered

- Added a portable `App` boundary that captures one frame, detects/crops candidates
  without copying, recognizes and normalizes OCR text, rejects empty/low-confidence
  results, tracks selected-region coordinates, and chooses the confirmed lower target.
- Dry run logs and locks confirmed targets without touching `InputSink`. Live input
  locks only `SendStatus::sent`; `blocked` and `partial` return false immediately so
  the application loop stops.
- Added bounded latency metrics for capture, detection, OCR, and total processing.
  Each stage retains the latest 512 samples and reports count, mean, median, and p95.
- Added the Win32 executable loop with config fallback, prominent dry/live status,
  foreground-window calibration, client-relative saved regions, screen-coordinate
  reconstruction, F7 capture/config rebuild, and F8 stopped/running toggling that
  never mutates dry/live mode.
- Added five-second and shutdown metric output plus clean stops for destroyed windows,
  invalid regions, console closure, blocked input, and partial input.
- Added fake-driven Catch2 tests and CMake wiring for `app_test` and `dota_keyboard`.
- Hardened startup recovery so only a non-empty exact saved title may auto-bind.
  Missing/mismatched titles remain safely stopped in the hotkey loop for F7
  calibration; a null target is never misreported as a destroyed window.
- Periodic metrics are checked before unbound/stopped loop continues, so an existing
  stopped pipeline still prints its five-second summary.

## TDD evidence

1. `tests/app_test.cpp` was written before `include/dk/app.hpp`; the first clang
   compilation failed specifically because `dk/app.hpp` did not exist.
2. A dependency-free clang harness then exercised the real `app.cpp`, `metrics.cpp`,
   `target_tracker.cpp`, and `text_normalizer.cpp` with fake frame/detector/OCR/input
   adapters and a minimal OpenCV ABI stub.
3. The harness passed 4/4 grouped scenarios: live lower-target input, dry run,
   rejection plus blocked/partial stop, and bounded metric summaries.
4. Review regressions first failed because `last_result_` was sticky and the main loop
   conflated an unbound target with a destroyed HWND. After the minimal fixes, the
   portable harness and `main_win32_source_test` both passed.

## Verification

- Portable harness: `portable app harness: 4/4 passed`.
- `tests/app_test.cpp` passed clang C++20 syntax checking with temporary Catch2/OpenCV
  headers.
- `src/metrics.cpp` compiled with `-Wall -Wextra -Wpedantic`.
- `git diff --check` passed.
- The live dedupe test processes four identical frames for one target and observes
  exactly one sink call. The dry-run test observes a result on confirmation and no
  result on the following locked frame.
- `main_win32_source_test` passed its startup binding, null-target recovery, and
  stopped-state metric ordering checks.

## Interface decision

`App::run` was not added. The task's exact application boundary exposes
`process_one_frame`, `last_result`, and `metrics`; hotkey polling, window lifetime,
calibration, and periodic output belong to `main_win32.cpp`. A minimal `run()` would
either duplicate that policy or be an uninterruptible loop, so the exact interface
section governs and `process_one_frame` remains the coherent orchestration boundary.

## Environment caveat

This macOS host has no CMake executable, vcpkg dependencies, or Windows SDK, so the
Windows preset/CTest suite and native `main_win32.cpp` build could not run locally.
Forcing `_WIN32` against a temporary Windows header stub is not usable with host libc++,
which then expects Windows CRT locale macros. CI or a Windows developer machine must
run the prescribed Windows build, Catch2 suite, and interactive F7/F8 smoke check.
