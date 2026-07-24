#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#ifndef DK_WIN32_INPUT_SINK_SOURCE_PATH
#error "DK_WIN32_INPUT_SINK_SOURCE_PATH must name win32_input_sink.cpp"
#endif

int main() {
    std::ifstream input{DK_WIN32_INPUT_SINK_SOURCE_PATH};
    if (!input) {
        std::cerr << "could not open Win32 input sink source\n";
        return 1;
    }
    const std::string source{
        std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    if (input.bad()) {
        std::cerr << "could not read Win32 input sink source\n";
        return 1;
    }

    const auto delayed_loop = source.find("for (std::size_t index = 0; index < letters.size(); ++index)");
    const auto foreground_guard = source.find("if (GetForegroundWindow() != target_)", delayed_loop);
    const auto delayed_send = source.find("SendInput(2, events", delayed_loop);
    if (delayed_loop == std::string::npos || foreground_guard == std::string::npos ||
        delayed_send == std::string::npos || foreground_guard > delayed_send) {
        std::cerr << "delayed SendInput must recheck foreground inside the per-letter loop\n";
        return 1;
    }
    return 0;
}
