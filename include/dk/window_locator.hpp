#pragma once

#include <cstdint>
#include <optional>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
using HWND = void*;
#endif

#include "dk/types.hpp"

namespace dk {

struct WindowBinding {
    HWND handle{};
    std::wstring title;
    std::uint32_t process_id{};
    Box client_bounds;
};

class WindowLocator {
public:
    [[nodiscard]] static std::optional<WindowBinding> foreground();
    [[nodiscard]] static std::optional<Box> client_screen_bounds(HWND window);
};

}  // namespace dk
