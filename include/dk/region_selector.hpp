#pragma once

#include <optional>

#ifdef _WIN32
#include <Windows.h>
#else
using HWND = void*;
#endif

#include "dk/types.hpp"

namespace dk {

class RegionSelector {
public:
    [[nodiscard]] static std::optional<Box> select(
        HWND owner, const Box& client_screen_bounds);
};

}  // namespace dk
