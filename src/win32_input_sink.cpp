#include "dk/win32_input_sink.hpp"

#ifndef _WIN32
#error "win32_input_sink.cpp is only available on Windows"
#endif

#include <limits>
#include <sstream>
#include <vector>

namespace dk {
namespace {

INPUT key_event(char letter, bool key_up) {
    INPUT event{};
    event.type = INPUT_KEYBOARD;
    event.ki.wVk = static_cast<WORD>(letter);
    event.ki.dwFlags = key_up ? KEYEVENTF_KEYUP : 0;
    return event;
}

std::string send_input_diagnostic(UINT accepted, std::size_t requested, DWORD error) {
    std::ostringstream stream;
    stream << "SendInput accepted " << accepted << " of " << requested
           << " keyboard events (GetLastError=" << error
           << "). Input injection may be blocked by User Interface Privilege Isolation "
              "(UIPI) when the target runs elevated.";
    return stream.str();
}

std::string win32_diagnostic(const char* operation, DWORD error) {
    std::ostringstream stream;
    stream << operation << " failed (GetLastError=" << error << ").";
    return stream.str();
}

bool wait_for_delay(HANDLE timer, int delay_us, std::string& diagnostic) {
    LARGE_INTEGER due_time{};
    due_time.QuadPart = -static_cast<LONGLONG>(delay_us) * 10;
    if (!SetWaitableTimer(timer, &due_time, 0, nullptr, nullptr, FALSE)) {
        diagnostic = win32_diagnostic("SetWaitableTimer", GetLastError());
        return false;
    }
    if (WaitForSingleObject(timer, INFINITE) != WAIT_OBJECT_0) {
        diagnostic = win32_diagnostic("WaitForSingleObject", GetLastError());
        return false;
    }
    return true;
}

}  // namespace

Win32InputSink::Win32InputSink(HWND target, int inter_key_delay_us)
    : target_(target), inter_key_delay_us_(inter_key_delay_us) {}

const std::string& Win32InputSink::last_diagnostic() const noexcept {
    return diagnostic_;
}

SendStatus Win32InputSink::send_letters(std::string_view letters) {
    diagnostic_.clear();

    if (GetForegroundWindow() != target_) {
        diagnostic_ = "The bound target window is not foreground; no input was sent.";
        return SendStatus::not_foreground;
    }
    if (!validate_send_text(letters)) {
        diagnostic_ = "Refusing input that is not a nonempty sequence of A-Z letters.";
        return SendStatus::invalid_text;
    }
    if (letters.size() > static_cast<std::size_t>((std::numeric_limits<UINT>::max)() / 2)) {
        diagnostic_ = "Refusing text that exceeds the SendInput event limit.";
        return SendStatus::invalid_text;
    }

    if (inter_key_delay_us_ <= 0) {
        std::vector<INPUT> events;
        events.reserve(letters.size() * 2);
        for (const char letter : letters) {
            events.push_back(key_event(letter, false));
            events.push_back(key_event(letter, true));
        }

        const auto requested = static_cast<UINT>(events.size());
        SetLastError(ERROR_SUCCESS);
        const UINT accepted = SendInput(requested, events.data(), static_cast<int>(sizeof(INPUT)));
        if (accepted == requested) {
            return SendStatus::sent;
        }

        diagnostic_ = send_input_diagnostic(accepted, requested, GetLastError());
        return accepted == 0 ? SendStatus::blocked : SendStatus::partial;
    }

    HANDLE timer = CreateWaitableTimerExW(
        nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (timer == nullptr) {
        timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    }
    if (timer == nullptr) {
        diagnostic_ = win32_diagnostic("CreateWaitableTimer", GetLastError());
        return SendStatus::blocked;
    }

    const auto close_timer = [&timer] { CloseHandle(timer); };
    for (std::size_t index = 0; index < letters.size(); ++index) {
        INPUT events[] = {key_event(letters[index], false), key_event(letters[index], true)};
        if (GetForegroundWindow() != target_) {
            diagnostic_ = "The bound target window is no longer foreground; no further input was sent.";
            close_timer();
            return SendStatus::not_foreground;
        }
        SetLastError(ERROR_SUCCESS);
        const UINT accepted = SendInput(2, events, static_cast<int>(sizeof(INPUT)));
        if (accepted != 2) {
            diagnostic_ = send_input_diagnostic(accepted, 2, GetLastError());
            close_timer();
            return accepted == 0 ? SendStatus::blocked : SendStatus::partial;
        }
        if (index + 1 < letters.size() && !wait_for_delay(timer, inter_key_delay_us_, diagnostic_)) {
            close_timer();
            return SendStatus::blocked;
        }
    }
    close_timer();
    return SendStatus::sent;
}

}  // namespace dk
