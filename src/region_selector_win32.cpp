#include "dk/region_selector.hpp"

#ifndef _WIN32
#error "region_selector_win32.cpp is only available on Windows"
#endif

#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <system_error>

#include <windowsx.h>

namespace dk {
namespace {

constexpr wchar_t overlay_class_name[] = L"DotaKeyboard.PrivateRegionSelectorOverlay";
constexpr UINT_PTR owner_check_timer = 1;
constexpr UINT owner_check_interval_ms = 50;
constexpr int minimum_selection_size = 100;

struct SelectionState {
    HWND owner{};
    POINT start{};
    POINT current{};
    bool dragging{};
    bool finished{};
    std::optional<Box> result;
};

Box normalized_selection(const POINT start, const POINT end) {
    const auto start_x = static_cast<int>(start.x);
    const auto start_y = static_cast<int>(start.y);
    const auto end_x = static_cast<int>(end.x);
    const auto end_y = static_cast<int>(end.y);
    const auto left = std::min(start_x, end_x);
    const auto top = std::min(start_y, end_y);
    return Box{
        left,
        top,
        std::max(start_x, end_x) - left,
        std::max(start_y, end_y) - top,
    };
}

POINT clamped_point(HWND window, LPARAM value) {
    RECT client{};
    GetClientRect(window, &client);
    const auto x = std::clamp(
        GET_X_LPARAM(value),
        static_cast<int>(client.left),
        static_cast<int>(client.right));
    const auto y = std::clamp(
        GET_Y_LPARAM(value),
        static_cast<int>(client.top),
        static_cast<int>(client.bottom));
    return POINT{
        static_cast<LONG>(x),
        static_cast<LONG>(y),
    };
}

void cancel(HWND window, SelectionState& state) {
    state.result.reset();
    state.finished = true;
    if (GetCapture() == window) {
        ReleaseCapture();
    }
    DestroyWindow(window);
}

LRESULT CALLBACK overlay_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* state =
        reinterpret_cast<SelectionState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        state = static_cast<SelectionState*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }

    switch (message) {
        case WM_TIMER:
            if (state && wparam == owner_check_timer && !state->finished) {
                const auto escape_down =
                    (static_cast<unsigned short>(GetAsyncKeyState(VK_ESCAPE)) & 0x8000U) != 0;
                const auto foreground = GetForegroundWindow();
                if (!IsWindow(state->owner) || escape_down ||
                    (foreground != window && foreground != state->owner)) {
                    cancel(window, *state);
                }
            }
            return 0;

        case WM_KEYDOWN:
            if (state && wparam == VK_ESCAPE && !state->finished) {
                cancel(window, *state);
            }
            return 0;

        case WM_LBUTTONDOWN:
            if (state && !state->finished) {
                state->start = clamped_point(window, lparam);
                state->current = state->start;
                state->dragging = true;
                SetCapture(window);
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;

        case WM_MOUSEMOVE:
            if (state && state->dragging && !state->finished) {
                state->current = clamped_point(window, lparam);
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;

        case WM_LBUTTONUP:
            if (state && state->dragging && !state->finished) {
                state->current = clamped_point(window, lparam);
                state->dragging = false;
                const auto selection = normalized_selection(state->start, state->current);
                if (selection.width >= minimum_selection_size &&
                    selection.height >= minimum_selection_size) {
                    state->result = selection;
                    state->finished = true;
                    if (GetCapture() == window) {
                        ReleaseCapture();
                    }
                    DestroyWindow(window);
                } else {
                    if (GetCapture() == window) {
                        ReleaseCapture();
                    }
                    InvalidateRect(window, nullptr, FALSE);
                }
            }
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT paint{};
            const auto device = BeginPaint(window, &paint);
            RECT client{};
            GetClientRect(window, &client);
            FillRect(device, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            if (state && state->dragging) {
                const auto selection = normalized_selection(state->start, state->current);
                RECT outline{
                    selection.x,
                    selection.y,
                    selection.right(),
                    selection.bottom(),
                };
                const auto pen = CreatePen(PS_SOLID, 3, RGB(0, 255, 160));
                const auto old_pen = SelectObject(device, pen);
                const auto old_brush = SelectObject(device, GetStockObject(HOLLOW_BRUSH));
                Rectangle(device, outline.left, outline.top, outline.right, outline.bottom);
                SelectObject(device, old_brush);
                SelectObject(device, old_pen);
                DeleteObject(pen);
            }
            EndPaint(window, &paint);
            return 0;
        }

        case WM_DESTROY:
            if (state && !state->finished) {
                state->result.reset();
                state->finished = true;
            }
            return 0;

        case WM_NCDESTROY:
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            return 0;

        default:
            return DefWindowProcW(window, message, wparam, lparam);
    }
}

ATOM register_overlay_class() {
    static std::once_flag once;
    static ATOM atom{};
    std::call_once(once, [] {
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = overlay_window_proc;
        window_class.hInstance = GetModuleHandleW(nullptr);
        window_class.hCursor = LoadCursorW(nullptr, IDC_CROSS);
        window_class.lpszClassName = overlay_class_name;
        atom = RegisterClassExW(&window_class);
        if (!atom) {
            throw std::system_error(
                static_cast<int>(GetLastError()), std::system_category(), "register overlay class");
        }
    });
    return atom;
}

}  // namespace

std::optional<Box> RegionSelector::select(HWND owner, const Box& client_screen_bounds) {
    if (!owner || !IsWindow(owner)) {
        throw std::invalid_argument("region selector owner is not a valid window");
    }
    if (client_screen_bounds.width <= 0 || client_screen_bounds.height <= 0) {
        throw std::invalid_argument("region selector bounds must have positive size");
    }

    register_overlay_class();
    SelectionState state{};
    state.owner = owner;
    const auto overlay = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        overlay_class_name,
        L"",
        WS_POPUP,
        client_screen_bounds.x,
        client_screen_bounds.y,
        client_screen_bounds.width,
        client_screen_bounds.height,
        owner,
        nullptr,
        GetModuleHandleW(nullptr),
        &state);
    if (!overlay) {
        throw std::system_error(
            static_cast<int>(GetLastError()), std::system_category(), "create calibration overlay");
    }

    if (!SetLayeredWindowAttributes(overlay, 0, 160, LWA_ALPHA)) {
        const auto error = GetLastError();
        DestroyWindow(overlay);
        throw std::system_error(
            static_cast<int>(error), std::system_category(), "make calibration overlay translucent");
    }
    if (!SetTimer(overlay, owner_check_timer, owner_check_interval_ms, nullptr)) {
        const auto error = GetLastError();
        DestroyWindow(overlay);
        throw std::system_error(
            static_cast<int>(error), std::system_category(), "start calibration owner timer");
    }
    ShowWindow(overlay, SW_SHOW);
    UpdateWindow(overlay);
    SetForegroundWindow(overlay);
    SetFocus(overlay);

    bool repost_quit{};
    int quit_code{};
    MSG message{};
    while (!state.finished) {
        const auto result = GetMessageW(&message, nullptr, 0, 0);
        if (result == -1) {
            const auto error = GetLastError();
            if (IsWindow(overlay)) {
                DestroyWindow(overlay);
            }
            throw std::system_error(
                static_cast<int>(error), std::system_category(), "read calibration message");
        }
        if (result == 0) {
            repost_quit = true;
            quit_code = static_cast<int>(message.wParam);
            state.result.reset();
            state.finished = true;
            break;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (IsWindow(overlay)) {
        DestroyWindow(overlay);
    }
    if (IsWindow(owner) && !IsIconic(owner)) {
        SetForegroundWindow(owner);
    }
    if (repost_quit) {
        PostQuitMessage(quit_code);
    }
    return state.result;
}

}  // namespace dk
