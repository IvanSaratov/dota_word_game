#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "dk/candidate_detector.hpp"
#include "dk/target_tracker.hpp"
#include "dk/types.hpp"

namespace dk {

struct HotkeyConfig {
    unsigned calibrate{0x76};
    unsigned toggle{0x77};
};

struct AppConfig {
    std::wstring window_title;
    Box region;
    bool region_configured{false};
    bool live_input{false};
    float min_ocr_confidence{0.80F};
    int inter_key_delay_us{0};
    int post_send_delay_ms{100};
    DetectorConfig detector;
    TrackerConfig tracker;
    HotkeyConfig hotkeys;

    [[nodiscard]] static AppConfig defaults();
};

[[nodiscard]] std::string serialize_config(const AppConfig& config);
[[nodiscard]] AppConfig parse_config(std::string_view json);
[[nodiscard]] AppConfig load_config(const std::filesystem::path& path);
void save_config(const std::filesystem::path& path, const AppConfig& config);

}  // namespace dk
