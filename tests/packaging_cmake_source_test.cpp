#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#ifndef DK_ROOT_CMAKE_PATH
#error "DK_ROOT_CMAKE_PATH must name the root CMakeLists.txt"
#endif

int main() {
    std::ifstream input{DK_ROOT_CMAKE_PATH};
    const std::string source{
        std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    if (!input || input.bad()) {
        std::cerr << "could not read the root CMakeLists.txt\n";
        return 1;
    }

    if (source.find("\"ext-ms-.*\"") == std::string::npos) {
        std::cerr << "all ext-ms Windows API-set dependencies must be excluded\n";
        return 1;
    }
    if (source.find("\"ext-ms-win-.*\"") != std::string::npos) {
        std::cerr << "the ext-ms exclusion must not be limited to ext-ms-win\n";
        return 1;
    }
    return 0;
}
