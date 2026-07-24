#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#ifndef DK_MAIN_WIN32_SOURCE_PATH
#error "DK_MAIN_WIN32_SOURCE_PATH must name main_win32.cpp"
#endif

namespace {

bool require_text(
    const std::string& source, const std::string& expected, const char* requirement) {
    if (source.find(expected) != std::string::npos) {
        return true;
    }
    std::cerr << "missing main control-flow guard: " << requirement << '\n';
    return false;
}

}  // namespace

int main() {
    std::ifstream input{DK_MAIN_WIN32_SOURCE_PATH};
    const std::string source{
        std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    if (!input || input.bad()) {
        std::cerr << "could not read main_win32.cpp\n";
        return 1;
    }

    bool valid = true;
    valid &= require_text(
        source,
        "if (config.region_configured && !config.window_title.empty())",
        "only a non-empty saved title may auto-bind");
    valid &= source.find("config.window_title.empty() ||") == std::string::npos;
    if (!valid) {
        std::cerr << "empty saved title must remain unbound\n";
    }
    valid &= require_text(
        source,
        "if (target && !IsWindow(target))",
        "only a previously bound HWND can be classified as destroyed");
    valid &= require_text(
        source,
        "if (!target) {\n                continue;\n            }",
        "an unbound startup must stay in the hotkey loop");

    const auto summary = source.find("if (pipeline && now >= next_metrics)");
    const auto unbound_continue = source.find("if (!target) {", summary);
    const auto stopped_continue = source.find("if (!running) {", summary);
    if (summary == std::string::npos || unbound_continue == std::string::npos ||
        stopped_continue == std::string::npos || summary > unbound_continue ||
        summary > stopped_continue) {
        std::cerr << "periodic metrics must run before stopped/unbound continues\n";
        valid = false;
    }
    return valid ? 0 : 1;
}
