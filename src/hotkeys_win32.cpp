#include "dk/hotkeys.hpp"

#ifndef _WIN32
#error "hotkeys_win32.cpp is only available on Windows"
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <limits>
#include <sstream>
#include <stdexcept>

namespace dk {
namespace {

constexpr int calibrate_hotkey_id = 1;
constexpr int toggle_hotkey_id = 2;
constexpr wchar_t hotkey_window_class[] = L"DotaKeyboardHotkeyWindow";

void ensure_window_class() {
    static const bool registered = [] {
        WNDCLASSW window_class{};
        window_class.lpfnWndProc = DefWindowProcW;
        window_class.hInstance = GetModuleHandleW(nullptr);
        window_class.lpszClassName = hotkey_window_class;
        const ATOM atom = RegisterClassW(&window_class);
        return atom != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }();
    if (!registered) {
        std::ostringstream stream;
        stream << "Could not register the hotkey message window class (GetLastError="
               << GetLastError() << ").";
        throw std::runtime_error(stream.str());
    }
}

std::runtime_error hotkey_registration_error(const char* name, unsigned virtual_key, DWORD error) {
    std::ostringstream stream;
    stream << "Could not register " << name << " hotkey (virtual key " << virtual_key
           << ", GetLastError=" << error
           << "). The key may already be registered by another application.";
    return std::runtime_error(stream.str());
}

DWORD timeout_milliseconds(std::chrono::milliseconds timeout) {
    if (timeout.count() <= 0) {
        return 0;
    }
    constexpr auto maximum = static_cast<long long>(std::numeric_limits<DWORD>::max() - 1);
    return static_cast<DWORD>(timeout.count() > maximum ? maximum : timeout.count());
}

}  // namespace

class Hotkeys::Impl {
public:
    Impl(unsigned calibrate_vk, unsigned toggle_vk)
        : calibrate_vk_(calibrate_vk), toggle_vk_(toggle_vk) {
        if (calibrate_vk_ == toggle_vk_) {
            throw std::runtime_error("Calibrate and toggle hotkeys must use different virtual keys.");
        }
        ensure_window_class();
        window_ = CreateWindowExW(0, hotkey_window_class, L"", 0, 0, 0, 0, 0,
                                  HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (window_ == nullptr) {
            std::ostringstream stream;
            stream << "Could not create the hotkey message window (GetLastError=" << GetLastError()
                   << ").";
            throw std::runtime_error(stream.str());
        }

        if (!RegisterHotKey(window_, calibrate_hotkey_id, MOD_NOREPEAT, calibrate_vk_)) {
            const DWORD error = GetLastError();
            DestroyWindow(window_);
            window_ = nullptr;
            throw hotkey_registration_error("calibrate", calibrate_vk_, error);
        }
        if (!RegisterHotKey(window_, toggle_hotkey_id, MOD_NOREPEAT, toggle_vk_)) {
            const DWORD error = GetLastError();
            UnregisterHotKey(window_, calibrate_hotkey_id);
            DestroyWindow(window_);
            window_ = nullptr;
            throw hotkey_registration_error("toggle", toggle_vk_, error);
        }
    }

    ~Impl() {
        if (window_ != nullptr) {
            UnregisterHotKey(window_, calibrate_hotkey_id);
            UnregisterHotKey(window_, toggle_hotkey_id);
            DestroyWindow(window_);
        }
    }

    [[nodiscard]] HotkeyEvent poll(std::chrono::milliseconds timeout) {
        const DWORD result = MsgWaitForMultipleObjectsEx(
            0, nullptr, timeout_milliseconds(timeout), QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (result == WAIT_TIMEOUT) {
            return HotkeyEvent::none;
        }
        if (result == WAIT_FAILED) {
            std::ostringstream stream;
            stream << "Waiting for hotkey messages failed (GetLastError=" << GetLastError() << ").";
            throw std::runtime_error(stream.str());
        }

        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                return HotkeyEvent::quit;
            }
            if (message.message == WM_HOTKEY && message.hwnd == window_) {
                if (message.wParam == calibrate_hotkey_id) {
                    return HotkeyEvent::calibrate;
                }
                if (message.wParam == toggle_hotkey_id) {
                    return HotkeyEvent::toggle;
                }
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return HotkeyEvent::none;
    }

private:
    HWND window_{};
    unsigned calibrate_vk_{};
    unsigned toggle_vk_{};
};

Hotkeys::Hotkeys(unsigned calibrate_vk, unsigned toggle_vk)
    : impl_(std::make_unique<Impl>(calibrate_vk, toggle_vk)) {}

Hotkeys::~Hotkeys() = default;

HotkeyEvent Hotkeys::poll(std::chrono::milliseconds timeout) {
    return impl_->poll(timeout);
}

}  // namespace dk
