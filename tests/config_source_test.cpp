#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#ifndef DK_CONFIG_SOURCE_PATH
#error "DK_CONFIG_SOURCE_PATH must name config.cpp"
#endif

namespace {

bool require_text(
    const std::string& source, const std::string& expected, const char* requirement) {
    if (source.find(expected) != std::string::npos) {
        return true;
    }
    std::cerr << "missing configuration validation: " << requirement << '\n';
    return false;
}

}  // namespace

int main() {
    std::ifstream input{DK_CONFIG_SOURCE_PATH};
    const std::string source{
        std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    if (!input || input.bad()) {
        std::cerr << "could not read config.cpp\n";
        return 1;
    }

    bool valid = true;
    valid &= require_text(source, "confirm_frames < 2", "two-frame minimum");
    valid &= require_text(source, "unlock_missing_frames < 1", "positive unlock count");
    valid &= require_text(
        source,
        "!finite_positive(config.tracker.max_center_distance_px)",
        "finite positive tracker distance");
    valid &= require_text(
        source,
        "luminance_threshold < 0 ||",
        "luminance lower bound");
    valid &= require_text(
        source,
        "luminance_threshold > 255",
        "luminance upper bound");
    valid &= require_text(
        source,
        "min_char_height_ratio > detector.max_char_height_ratio",
        "ordered character-height ratios");
    valid &= require_text(
        source,
        "detector.max_char_height_ratio > 1.0F",
        "normalized maximum character height");
    valid &= require_text(
        source,
        "!finite_positive(detector.baseline_tolerance_ratio)",
        "finite positive baseline tolerance");
    valid &= require_text(
        source,
        "!finite_positive(detector.max_gap_ratio)",
        "finite positive maximum gap");
    valid &= require_text(
        source,
        "detector.min_components_per_line < 1",
        "positive component count");
    valid &= require_text(
        source,
        "detector.crop_padding_px < 0",
        "nonnegative crop padding");
    valid &= require_text(
        source,
        "validate_ignored_region(region)",
        "finite positive normalized ignored rectangles");
    return valid ? 0 : 1;
}
