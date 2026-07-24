#include "dk/window_locator.hpp"

#ifndef _WIN32
#error "window_locator_win32.cpp is only available on Windows"
#endif

#include <vector>

namespace dk {

std::optional<Box> WindowLocator::client_screen_bounds(HWND window) {
    if (!window || !IsWindow(window) || window == GetDesktopWindow() ||
        window == GetShellWindow() || IsIconic(window)) {
        return std::nullopt;
    }

    RECT client{};
    if (!GetClientRect(window, &client)) {
        return std::nullopt;
    }
    POINT top_left{client.left, client.top};
    POINT bottom_right{client.right, client.bottom};
    if (!ClientToScreen(window, &top_left) || !ClientToScreen(window, &bottom_right)) {
        return std::nullopt;
    }

    const auto width = static_cast<int>(bottom_right.x - top_left.x);
    const auto height = static_cast<int>(bottom_right.y - top_left.y);
    if (width <= 0 || height <= 0) {
        return std::nullopt;
    }
    return Box{
        static_cast<int>(top_left.x),
        static_cast<int>(top_left.y),
        width,
        height,
    };
}

std::optional<WindowBinding> WindowLocator::foreground() {
    const auto window = GetForegroundWindow();
    const auto bounds = client_screen_bounds(window);
    if (!bounds) {
        return std::nullopt;
    }

    DWORD process_id{};
    if (GetWindowThreadProcessId(window, &process_id) == 0 || process_id == 0) {
        return std::nullopt;
    }

    const auto title_length = GetWindowTextLengthW(window);
    std::vector<wchar_t> title(static_cast<std::size_t>(title_length) + 1);
    SetLastError(ERROR_SUCCESS);
    const auto copied = GetWindowTextW(window, title.data(), static_cast<int>(title.size()));
    if (copied == 0 && GetLastError() != ERROR_SUCCESS) {
        return std::nullopt;
    }

    return WindowBinding{
        window,
        std::wstring{title.data(), static_cast<std::size_t>(copied)},
        static_cast<std::uint32_t>(process_id),
        *bounds,
    };
}

}  // namespace dk

#ifdef DK_WINDOW_LOCATOR_SMOKE

#include <chrono>
#include <iostream>
#include <string_view>
#include <thread>

#include "dk/region_selector.hpp"

int wmain(int argument_count, wchar_t** arguments) {
    const bool calibrate =
        argument_count > 1 && std::wstring_view{arguments[1]} == L"--calibrate";
    std::wcout << L"Switch to the target window within three seconds.\n";
    std::this_thread::sleep_for(std::chrono::seconds{3});

    const auto window = dk::WindowLocator::foreground();
    if (!window) {
        std::wcerr << L"No usable foreground window.\n";
        return 1;
    }
    const auto& bounds = window->client_bounds;
    std::wcout << L"Title: " << window->title << L"\nProcess: " << window->process_id
               << L"\nClient screen bounds: " << bounds.x << L"," << bounds.y << L" "
               << bounds.width << L"x" << bounds.height << L"\n";

    if (calibrate) {
        const auto region = dk::RegionSelector::select(window->handle, bounds);
        if (!region) {
            std::wcout << L"Calibration cancelled.\n";
            return 2;
        }
        std::wcout << L"Client-relative selection: " << region->x << L"," << region->y
                   << L" " << region->width << L"x" << region->height << L"\n";
    }
    return 0;
}

#endif
