#include <fstream>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <string>

#ifndef DK_ROOT_CMAKE_PATH
#error "DK_ROOT_CMAKE_PATH must name the root CMakeLists.txt"
#endif

#ifndef DK_PACKAGE_PROJECT_CONFIG_PATH
#error "DK_PACKAGE_PROJECT_CONFIG_PATH must name PackageProjectConfig.cmake"
#endif

#ifndef DK_CMAKE_PRESETS_PATH
#error "DK_CMAKE_PRESETS_PATH must name CMakePresets.json"
#endif

#ifndef DK_VCPKG_TRIPLET_PATH
#error "DK_VCPKG_TRIPLET_PATH must name the project vcpkg triplet"
#endif

#ifndef DK_INNO_EXTRA_SCRIPT_PATH
#error "DK_INNO_EXTRA_SCRIPT_PATH must name the Inno Setup extra script"
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
    if (source.find(R"(set(CPACK_PROJECT_CONFIG_FILE)") == std::string::npos ||
        source.find(
            R"("${CMAKE_CURRENT_SOURCE_DIR}/cmake/PackageProjectConfig.cmake")") ==
            std::string::npos) {
        std::cerr << "CPack must load PackageProjectConfig.cmake for each generator\n";
        return 1;
    }
    if (source.find("set(CPACK_INNOSETUP_EXTRA_SCRIPTS") ==
            std::string::npos ||
        source.find(
            R"("${CMAKE_CURRENT_SOURCE_DIR}/cmake/InnoRuntimeCleanup.iss")") ==
            std::string::npos) {
        std::cerr
            << "the installer must include runtime log cleanup instructions\n";
        return 1;
    }

    std::ifstream inno_script_input{DK_INNO_EXTRA_SCRIPT_PATH};
    const std::string inno_script_source{
        std::istreambuf_iterator<char>{inno_script_input},
        std::istreambuf_iterator<char>{}};
    if (!inno_script_input || inno_script_input.bad()) {
        std::cerr << "could not read the Inno Setup extra script\n";
        return 1;
    }
    if (inno_script_source.find("[UninstallDelete]") == std::string::npos ||
        inno_script_source.find(
            R"(Type: files; Name: "{app}\dota-keyboard.log")") ==
            std::string::npos) {
        std::cerr
            << "uninstall must remove the runtime-created dota-keyboard.log\n";
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

    std::ifstream presets_input{DK_CMAKE_PRESETS_PATH};
    const std::string presets_source{
        std::istreambuf_iterator<char>{presets_input},
        std::istreambuf_iterator<char>{}};
    if (!presets_input || presets_input.bad()) {
        std::cerr << "could not read CMakePresets.json\n";
        return 1;
    }
    const auto package_presets = presets_source.find(R"("packagePresets")");
    const auto zip_preset =
        presets_source.find(R"("name": "windows-release")", package_presets);
    const auto installer_preset =
        presets_source.find(R"("name": "windows-installer")", zip_preset);
    const auto zip_generator = presets_source.find(R"("generators": ["ZIP"])", zip_preset);
    const auto installer_generator =
        presets_source.find(R"("generators": ["INNOSETUP"])", installer_preset);
    if (package_presets == std::string::npos || zip_preset == std::string::npos ||
        installer_preset == std::string::npos || zip_generator == std::string::npos ||
        installer_generator == std::string::npos ||
        zip_generator > installer_preset) {
        std::cerr << "package presets must provide ZIP and INNOSETUP generators\n";
        return 1;
    }

    if (presets_source.find(
            R"("VCPKG_TARGET_TRIPLET": "x64-windows-dota")") ==
            std::string::npos ||
        presets_source.find(
            R"("VCPKG_OVERLAY_TRIPLETS": "${sourceDir}/cmake/triplets")") ==
            std::string::npos) {
        std::cerr << "Windows presets must use the project overlay triplet\n";
        return 1;
    }

    const auto static_debug_preset = presets_source.find(
        R"("name": "windows-static-debug")");
    const auto static_release_preset = presets_source.find(
        R"("name": "windows-static-release")");
    if (static_debug_preset == std::string::npos ||
        static_release_preset == std::string::npos ||
        presets_source.find(
            R"("VCPKG_TARGET_TRIPLET": "x64-windows-dota-static")") ==
            std::string::npos ||
        presets_source.find(
            R"("CMAKE_MSVC_RUNTIME_LIBRARY": "MultiThreaded$<$<CONFIG:Debug>:Debug>")") ==
            std::string::npos ||
        presets_source.find(R"("binaryDir": "${sourceDir}/build/windows-static-debug")") ==
            std::string::npos ||
        presets_source.find(R"("binaryDir": "${sourceDir}/build/windows-static-release")") ==
            std::string::npos) {
        std::cerr << "static Windows presets must select the static triplet and CRT\n";
        return 1;
    }

    std::ifstream triplet_input{DK_VCPKG_TRIPLET_PATH};
    const std::string triplet_source{
        std::istreambuf_iterator<char>{triplet_input},
        std::istreambuf_iterator<char>{}};
    if (!triplet_input || triplet_input.bad()) {
        std::cerr << "could not read the project vcpkg triplet\n";
        return 1;
    }
    if (triplet_source.find("set(VCPKG_TARGET_ARCHITECTURE x64)") ==
            std::string::npos ||
        triplet_source.find("set(VCPKG_CRT_LINKAGE dynamic)") ==
            std::string::npos ||
        triplet_source.find("set(VCPKG_LIBRARY_LINKAGE dynamic)") ==
            std::string::npos ||
        triplet_source.find("set(VCPKG_PROVIDED_FORTRAN ON)") ==
            std::string::npos) {
        std::cerr
            << "the overlay triplet must preserve all pinned x64-windows base settings\n";
        return 1;
    }
    const auto onnx_condition =
        triplet_source.find(R"(if("${PORT}" STREQUAL "onnx"))");
    const std::string disable_registration_statement =
        R"(list(APPEND VCPKG_CMAKE_CONFIGURE_OPTIONS -DONNX_DISABLE_STATIC_REGISTRATION=ON))";
    const auto disable_registration =
        triplet_source.find(disable_registration_statement);
    const auto condition_end = triplet_source.find("endif()", disable_registration);
    if (onnx_condition == std::string::npos ||
        disable_registration == std::string::npos ||
        condition_end == std::string::npos ||
        !(onnx_condition < disable_registration &&
          disable_registration < condition_end) ||
        triplet_source.find(
            "-DONNX_DISABLE_STATIC_REGISTRATION=ON",
            disable_registration + disable_registration_statement.size()) !=
            std::string::npos) {
        std::cerr
            << "the overlay triplet must disable static registration only for the onnx port\n";
        return 1;
    }

    const auto static_triplet_path =
        std::filesystem::path{DK_VCPKG_TRIPLET_PATH}.parent_path() /
        "x64-windows-dota-static.cmake";
    std::ifstream static_triplet_input{static_triplet_path};
    const std::string static_triplet_source{
        std::istreambuf_iterator<char>{static_triplet_input},
        std::istreambuf_iterator<char>{}};
    if (!static_triplet_input || static_triplet_input.bad() ||
        static_triplet_source.find("set(VCPKG_TARGET_ARCHITECTURE x64)") ==
            std::string::npos ||
        static_triplet_source.find("set(VCPKG_CRT_LINKAGE static)") ==
            std::string::npos ||
        static_triplet_source.find("set(VCPKG_LIBRARY_LINKAGE static)") ==
            std::string::npos ||
        static_triplet_source.find("set(VCPKG_PROVIDED_FORTRAN ON)") ==
            std::string::npos ||
        static_triplet_source.find(R"(if("${PORT}" STREQUAL "onnx"))") ==
            std::string::npos ||
        static_triplet_source.find(
            "list(APPEND VCPKG_CMAKE_CONFIGURE_OPTIONS "
            "-DONNX_DISABLE_STATIC_REGISTRATION=ON)") == std::string::npos) {
        std::cerr << "static overlay triplet must keep the required static ONNX settings\n";
        return 1;
    }

    const auto verifier_path =
        std::filesystem::path{DK_CMAKE_PRESETS_PATH}.parent_path() /
        "scripts/verify_single_exe.ps1";
    std::ifstream verifier_input{verifier_path};
    const std::string verifier_source{
        std::istreambuf_iterator<char>{verifier_input},
        std::istreambuf_iterator<char>{}};
    if (!verifier_input || verifier_input.bad() ||
        verifier_source.find("dumpbin /dependents") == std::string::npos ||
        verifier_source.find("opencv") == std::string::npos ||
        verifier_source.find("onnxruntime") == std::string::npos ||
        verifier_source.find("vcruntime") == std::string::npos ||
        verifier_source.find("msvcp") == std::string::npos ||
        verifier_source.find("concrt") == std::string::npos ||
        verifier_source.find("ucrtbase") == std::string::npos ||
        verifier_source.find("$env:WINDIR") == std::string::npos ||
        verifier_source.find("System32") == std::string::npos ||
        verifier_source.find("exit 1") == std::string::npos ||
        verifier_source.find("rejected") == std::string::npos) {
        std::cerr << "single-EXE verifier must reject bundled runtimes and resolve system DLLs\n";
        return 1;
    }
    return 0;
}
