#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#ifndef DK_ROOT_CMAKE_PATH
#error "DK_ROOT_CMAKE_PATH must name the root CMakeLists.txt"
#endif

#ifndef DK_PACKAGE_PROJECT_CONFIG_PATH
#error "DK_PACKAGE_PROJECT_CONFIG_PATH must name PackageProjectConfig.cmake"
#endif

int main() {
    std::ifstream input{DK_ROOT_CMAKE_PATH};
    const std::string source{
        std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    if (!input || input.bad()) {
        std::cerr << "could not read the root CMakeLists.txt\n";
        return 1;
    }

    if (source.find(
            R"(".*[\\\\/][Ww][Ii][Nn][Dd][Oo][Ww][Ss][\\\\/][Ss][Yy][Ss][Tt][Ee][Mm]32[\\\\/].*")") ==
        std::string::npos) {
        std::cerr << "System32 dependencies must be excluded with either path separator\n";
        return 1;
    }
    if (source.find(
            R"(".*[\\\\/][Ww][Ii][Nn][Dd][Oo][Ww][Ss][\\\\/][Ss][Yy][Ss][Ww][Oo][Ww]64[\\\\/].*")") ==
        std::string::npos) {
        std::cerr << "SysWOW64 dependencies must be excluded with either path separator\n";
        return 1;
    }
    if (source.find(R"("[Aa][Pp][Ii]-[Mm][Ss]-.*")") == std::string::npos) {
        std::cerr << "all api-ms Windows API-set dependencies must be excluded case-insensitively\n";
        return 1;
    }
    if (source.find(R"("[Ee][Xx][Tt]-[Mm][Ss]-.*")") == std::string::npos) {
        std::cerr << "all ext-ms Windows API-set dependencies must be excluded case-insensitively\n";
        return 1;
    }
    if (source.find("set(CMAKE_INSTALL_UCRT_LIBRARIES TRUE)") != std::string::npos) {
        std::cerr << "Windows UCRT DLL collection must not be forced\n";
        return 1;
    }

    if (source.find(R"(set(CPACK_INNOSETUP_ARCHITECTURE "x64"))") ==
        std::string::npos) {
        std::cerr << "the package must configure the x64 Inno Setup generator\n";
        return 1;
    }
    if (source.find(
            R"(set(CPACK_INNOSETUP_INSTALL_ROOT "{localappdata}/Programs"))") ==
        std::string::npos) {
        std::cerr << "the installer must default below the per-user local app data directory\n";
        return 1;
    }
    if (source.find(
            R"(set(CPACK_INNOSETUP_SETUP_PrivilegesRequired "lowest"))") ==
        std::string::npos) {
        std::cerr << "the installer must not require elevation\n";
        return 1;
    }

    std::ifstream package_config_input{DK_PACKAGE_PROJECT_CONFIG_PATH};
    const std::string package_config_source{
        std::istreambuf_iterator<char>{package_config_input},
        std::istreambuf_iterator<char>{}};
    if (!package_config_input || package_config_input.bad()) {
        std::cerr << "could not read PackageProjectConfig.cmake\n";
        return 1;
    }
    if (package_config_source.find(
            R"("dota-keyboard-${CPACK_PACKAGE_VERSION}-windows-x64")") ==
        std::string::npos) {
        std::cerr << "the ZIP package name must use the CPack package version\n";
        return 1;
    }
    if (package_config_source.find(
            R"("DotaKeyboardSetup-${CPACK_PACKAGE_VERSION}-windows-x64")") ==
        std::string::npos) {
        std::cerr << "the installer name must use the CPack package version\n";
        return 1;
    }
    return 0;
}
