#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#ifndef DK_REGION_SELECTOR_SOURCE_PATH
#error "DK_REGION_SELECTOR_SOURCE_PATH must name region_selector_win32.cpp"
#endif

int main() {
    std::ifstream input{DK_REGION_SELECTOR_SOURCE_PATH};
    const std::string source{
        std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    if (!input || input.bad()) {
        std::cerr << "could not read region_selector_win32.cpp\n";
        return 1;
    }

    const auto register_class = source.find("ATOM register_overlay_class()");
    const auto unicode_cursor = source.find(
        "LoadCursorW(nullptr, MAKEINTRESOURCEW(32515))", register_class);
    const auto ansi_cursor =
        source.find("LoadCursorW(nullptr, IDC_CROSS)", register_class);
    if (register_class == std::string::npos ||
        unicode_cursor == std::string::npos ||
        ansi_cursor != std::string::npos) {
        std::cerr
            << "wide overlay class registration must use a wide cursor resource\n";
        return 1;
    }
    return 0;
}
