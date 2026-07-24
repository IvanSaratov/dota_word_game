#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#ifndef DK_DXGI_CAPTURE_SOURCE_PATH
#error "DK_DXGI_CAPTURE_SOURCE_PATH must name dxgi_capture_win32.cpp"
#endif

namespace {

bool require_source_text(
    const std::string& source, const std::string& expected, const char* requirement) {
    if (source.find(expected) != std::string::npos) {
        return true;
    }
    std::cerr << "missing DXGI capture source guard: " << requirement << '\n';
    return false;
}

}  // namespace

int main() {
    std::ifstream input{DK_DXGI_CAPTURE_SOURCE_PATH};
    if (!input) {
        std::cerr << "could not open DXGI capture source\n";
        return 1;
    }
    const std::string source{
        std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    if (input.bad()) {
        std::cerr << "could not read DXGI capture source\n";
        return 1;
    }

    bool valid = true;
    valid &= require_source_text(
        source,
        "selected_desc.Rotation != DXGI_MODE_ROTATION_IDENTITY",
        "reject non-identity output rotation");
    valid &= require_source_text(
        source,
        "rotated monitor outputs are unsupported",
        "describe unsupported rotated outputs");

    const auto access_lost = source.find(
        "if (acquire_result == DXGI_ERROR_ACCESS_LOST)");
    const auto next_return = source.find("return std::nullopt;", access_lost);
    const auto full_initialize = source.find("initialize();", access_lost);
    if (access_lost == std::string::npos || next_return == std::string::npos ||
        full_initialize == std::string::npos || full_initialize > next_return) {
        std::cerr << "DXGI_ERROR_ACCESS_LOST does not fully reinitialize before returning\n";
        valid = false;
    }
    valid &= require_source_text(
        source,
        "acquired_resource.Reset();\n            initialize();",
        "drop an acquired resource before ACCESS_LOST reinitialization");
    valid &= require_source_text(
        source,
        "void initialize() {\n        reset_capture_state();",
        "release prior capture state before initialization");

    const auto reset_begin = source.find("void reset_capture_state() noexcept");
    const auto reset_end = source.find("void recreate_duplication()", reset_begin);
    if (reset_begin == std::string::npos || reset_end == std::string::npos) {
        std::cerr << "missing full DXGI capture state reset\n";
        valid = false;
    } else {
        const auto reset = source.substr(reset_begin, reset_end - reset_begin);
        for (const auto* state : {
                 "duplication_.Reset();",
                 "staging_texture_.Reset();",
                 "context_.Reset();",
                 "device_.Reset();",
                 "output1_.Reset();",
                 "selected_output_.Reset();",
                 "selected_adapter_.Reset();",
                 "source_box_ = {};",
                 "frame_buffer_.release();",
             }) {
            if (reset.find(state) == std::string::npos) {
                std::cerr << "DXGI capture reset omits: " << state << '\n';
                valid = false;
            }
        }
    }

    return valid ? 0 : 1;
}
