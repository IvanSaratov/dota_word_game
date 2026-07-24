#include <chrono>
#include <iostream>

#include "dk/hotkeys.hpp"
#include "dk/win32_input_sink.hpp"

namespace {

const char* status_name(dk::SendStatus status) {
    switch (status) {
        case dk::SendStatus::sent: return "sent";
        case dk::SendStatus::not_foreground: return "not_foreground";
        case dk::SendStatus::invalid_text: return "invalid_text";
        case dk::SendStatus::blocked: return "blocked";
        case dk::SendStatus::partial: return "partial";
    }
    return "unknown";
}

}  // namespace

int wmain() {
    const HWND notepad = FindWindowW(L"Notepad", nullptr);
    if (notepad == nullptr) {
        std::cerr << "Open Notepad before starting this smoke check.\n";
        return 1;
    }

    dk::Win32InputSink input{notepad, 0};
    dk::Hotkeys hotkeys{VK_F7, VK_F8};
    std::cout << "Press F7 to print calibrate, F8 to send TEST to foreground Notepad.\n";
    for (;;) {
        switch (hotkeys.poll(std::chrono::milliseconds{250})) {
            case dk::HotkeyEvent::none:
                break;
            case dk::HotkeyEvent::calibrate:
                std::cout << "calibrate\n";
                break;
            case dk::HotkeyEvent::toggle: {
                const auto status = input.send_letters("TEST");
                std::cout << "toggle: " << status_name(status) << '\n';
                if (status != dk::SendStatus::sent) {
                    std::cout << input.last_diagnostic() << '\n';
                }
                break;
            }
            case dk::HotkeyEvent::quit:
                return 0;
        }
    }
}
