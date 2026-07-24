# Task 7 report: focus-safe input and global hotkeys

## Delivered

- Added `InputSink`, `SendStatus`, and portable `validate_send_text`.
- Added `Win32InputSink`, which verifies that its bound window is foreground, emits only
  `A`-`Z` key down/up events, batches zero-delay input into one `SendInput` call, and
  uses a high-resolution waitable timer between delayed keys.
- Added diagnostics for rejected input, lost focus, partial/blocked `SendInput` calls,
  including `GetLastError` and the UIPI/elevated-target caveat.
- Delayed input rechecks the bound foreground window immediately before every key pair,
  so a focus change stops further injection with `not_foreground`.
- Added message-only-window global hotkey registration for F7/F8-style configured
  virtual keys, `WM_QUIT` handling, and destructor cleanup.
- Added a minimal Notepad smoke executable that reports one event per F7/F8 press and
  injects `TEST` only when the bound Notepad window is foreground.

## TDD evidence

1. `tests/input_sink_test.cpp` was created before `include/dk/input_sink.hpp`.
2. A standalone compilation of an include of `dk/input_sink.hpp` failed as expected
   because the header did not yet exist.
3. After the helper implementation, a standalone C++20 assertion program passed all
   five specified cases: `HYPERSTONE`, empty, space, apostrophe, and lowercase input.

## Verification

- `g++ -std=c++20 -Wall -Wextra -Wpedantic -Iinclude ...` passed the portable
  validation assertions and syntax-only inclusion of all public headers.
- `git diff --check` passed.
- `win32_input_sink_source_test` verifies the delayed per-letter loop contains that
  foreground guard before its `SendInput` call.

## Environment caveat

This macOS host has neither CMake/vcpkg dependencies nor a Windows SDK, so Catch2 CMake
tests, the Win32 translation-unit build, and the interactive Notepad smoke check could
not be run here. An attempted `_WIN32` syntax check against a temporary local SDK stub
was not usable because forcing `_WIN32` makes the host libc++ expect Windows CRT locale
macros. The Win32 code uses the native API signatures/constants and needs CI or a Windows
developer machine for final build and smoke verification.
