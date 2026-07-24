#pragma once

#include <chrono>
#include <memory>

namespace dk {

enum class HotkeyEvent { none, calibrate, toggle, quit };

class Hotkeys {
public:
    Hotkeys(unsigned calibrate_vk, unsigned toggle_vk);
    ~Hotkeys();

    Hotkeys(const Hotkeys&) = delete;
    Hotkeys& operator=(const Hotkeys&) = delete;
    Hotkeys(Hotkeys&&) = delete;
    Hotkeys& operator=(Hotkeys&&) = delete;

    [[nodiscard]] HotkeyEvent poll(std::chrono::milliseconds timeout);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace dk
