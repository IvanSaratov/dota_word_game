#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#ifndef DK_APP_SOURCE_PATH
#error "DK_APP_SOURCE_PATH must name app.cpp"
#endif

#ifndef DK_MAIN_WIN32_SOURCE_PATH
#error "DK_MAIN_WIN32_SOURCE_PATH must name main_win32.cpp"
#endif

namespace {

std::string read_all(const char* path) {
    std::ifstream input{path};
    const std::string source{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{},
    };
    if (!input || input.bad()) {
        std::cerr << "could not read source file " << path << '\n';
        std::exit(1);
    }
    return source;
}

int count(const std::string& source, const std::string& text) {
    int matches{};
    std::size_t offset{};
    while ((offset = source.find(text, offset)) != std::string::npos) {
        ++matches;
        offset += text.size();
    }
    return matches;
}

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "log_policy_test failure: " << message << '\n';
        std::exit(1);
    }
}

}  // namespace

int main() {
    const auto app = read_all(DK_APP_SOURCE_PATH);
    require(app.find("std::clog") == std::string::npos,
            "App must not write directly to clog");
    require(app.find("std::cout") == std::string::npos,
            "App must not write directly to cout");
    require(app.find("std::cerr") == std::string::npos,
            "App must not write directly to cerr");

    const auto main = read_all(DK_MAIN_WIN32_SOURCE_PATH);
    require(main.find("std::cout <<") == std::string::npos,
            "runtime main output must use Logger instead of cout insertion");
    require(count(main, "std::cerr <<") <= 1,
            "only bootstrap failure may use direct cerr insertion");
    require(count(main, "\"RUNNING (\"") == 1,
            "RUNNING transition must have one logging site");
    require(count(main, "\"STOPPED (\"") == 1,
            "STOPPED transition must have one logging site");
}
