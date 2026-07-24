#pragma once

#include <string>

#include "dk/input_sink.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
using HWND = void*;
#endif

namespace dk {

class Win32InputSink final : public InputSink {
public:
    Win32InputSink(HWND target, int inter_key_delay_us);

    SendStatus send_letters(std::string_view letters) override;
    [[nodiscard]] const std::string& last_diagnostic() const noexcept;

private:
    HWND target_{};
    int inter_key_delay_us_{};
    std::string diagnostic_;
};

}  // namespace dk
