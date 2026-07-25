#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#ifndef DK_WINDOWS_WORKFLOW_PATH
#error "DK_WINDOWS_WORKFLOW_PATH must name windows.yml"
#endif

#ifndef DK_RELEASE_WORKFLOW_PATH
#error "DK_RELEASE_WORKFLOW_PATH must name release.yml"
#endif

namespace {

std::string read_file(const char* path) {
    std::ifstream input{path};
    const std::string source{
        std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    if (!input || input.bad()) {
        std::cerr << "could not read " << path << '\n';
        return {};
    }
    return source;
}

std::size_t count_text(const std::string& source, const std::string& text) {
    std::size_t count = 0;
    for (auto position = source.find(text); position != std::string::npos;
         position = source.find(text, position + text.size())) {
        ++count;
    }
    return count;
}

bool require_text(
    const std::string& source, const std::string& text, const char* requirement) {
    if (source.find(text) != std::string::npos) {
        return true;
    }
    std::cerr << "missing Windows workflow contract: " << requirement << '\n';
    return false;
}

bool require_smoke_contracts(const std::string& source, const char* workflow_name) {
    bool valid = true;
    if (count_text(source, "live_input -ne $false") < 2) {
        std::cerr << workflow_name
                  << " must validate live_input=false in ZIP and installed configs\n";
        valid = false;
    }
    valid &= require_text(
        source,
        "Microsoft\\Windows\\Start Menu\\Programs",
        "installed Start Menu shortcut lookup");
    valid &= require_text(
        source,
        "CreateShortcut($_.FullName).TargetPath",
        "Start Menu shortcut target validation");
    valid &= require_text(
        source,
        "HKCU:\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
        "per-user uninstall registration lookup");
    valid &= require_text(
        source,
        "Test-Path -LiteralPath $uninstallRegistryPath",
        "uninstall registration removal validation");
    valid &= require_text(
        source,
        "$remainingPayload",
        "complete installed payload removal validation");
    valid &= require_text(
        source,
        "$remainingShortcuts",
        "Start Menu shortcut removal validation");
    valid &= require_text(
        source,
        "\"/DIR=`\"$installRoot`\"\"",
        "embedded-quoted Inno Setup install directory");
    return valid;
}

bool require_vcpkg_cache_contracts(
    const std::string& source, const char* workflow_name) {
    bool valid = true;
    const std::string cache_key =
        "windows-2022-vcpkg-v2-${{ hashFiles('vcpkg.json', "
        "'CMakePresets.json', 'cmake/triplets/**') }}";
    if (count_text(source, cache_key) != 2) {
        std::cerr << workflow_name
                  << " must use the v2 dependency-config-aware key for restore and save\n";
        valid = false;
    }
    valid &= require_text(
        source,
        "if: ${{ success() && steps.vcpkg-cache.outputs.cache-hit != 'true' }}",
        "success-only vcpkg cache save");
    if (source.find(
            "always() && steps.vcpkg-cache.outputs.cache-hit != 'true'") !=
        std::string::npos) {
        std::cerr << workflow_name
                  << " must not save a partial vcpkg cache after cancellation or failure\n";
        valid = false;
    }
    return valid;
}

}  // namespace

int main() {
    const auto windows_source = read_file(DK_WINDOWS_WORKFLOW_PATH);
    const auto release_source = read_file(DK_RELEASE_WORKFLOW_PATH);
    if (windows_source.empty() || release_source.empty()) {
        return 1;
    }

    bool valid = true;
    valid &= require_smoke_contracts(windows_source, "windows.yml");
    valid &= require_smoke_contracts(release_source, "release.yml");
    valid &= require_vcpkg_cache_contracts(windows_source, "windows.yml");
    valid &= require_vcpkg_cache_contracts(release_source, "release.yml");
    valid &= require_text(
        release_source, "--generate-notes", "generated GitHub release notes");
    valid &= require_text(
        release_source, "--notes $releaseNotes", "explicit GitHub release notes");
    valid &= require_text(
        release_source, "unsigned", "unsigned-installer release warning");
    valid &= require_text(
        release_source, "SmartScreen", "Microsoft SmartScreen release warning");
    valid &= require_text(
        release_source, ".sha256", "published installer checksum guidance");
    return valid ? 0 : 1;
}
