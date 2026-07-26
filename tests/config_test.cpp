#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>

#include "dk/config.hpp"

namespace {

template <typename Mutation>
void check_rejected(Mutation mutation) {
    auto config = dk::AppConfig::defaults();
    mutation(config);
    CHECK_THROWS_AS(dk::serialize_config(config), std::invalid_argument);
}

}  // namespace

TEST_CASE("default configuration is safe") {
    const auto config = dk::AppConfig::defaults();
    CHECK_FALSE(config.live_input);
    CHECK(config.log_level == dk::LogLevel::info);
    CHECK(config.min_ocr_confidence == Catch::Approx(0.80F));
    CHECK(config.tracker.confirm_frames == 2);
    CHECK(config.tracker.unlock_missing_frames == 15);
    CHECK(config.hotkeys.calibrate == 0x76);  // F7
    CHECK(config.hotkeys.toggle == 0x77);     // F8
}

TEST_CASE("log level has a strict JSON contract") {
    CHECK(dk::parse_config(R"({})").log_level == dk::LogLevel::info);
    CHECK(dk::parse_config(R"({"log_level":"info"})").log_level ==
          dk::LogLevel::info);

    const auto debug = dk::parse_config(R"({"log_level":"debug"})");
    CHECK(debug.log_level == dk::LogLevel::debug);
    CHECK(dk::parse_config(dk::serialize_config(debug)).log_level ==
          dk::LogLevel::debug);
    CHECK(dk::serialize_config(debug).find(R"("log_level": "debug")") !=
          std::string::npos);

    for (const auto* invalid : {
             R"({"log_level":"INFO"})",
             R"({"log_level":"trace"})",
             R"({"log_level":null})",
             R"({"log_level":1})",
             R"({"log_level":true})",
         }) {
        CHECK_THROWS_WITH(
            dk::parse_config(invalid),
            Catch::Matchers::ContainsSubstring("log_level"));
    }
}

TEST_CASE("region round-trips in client-relative coordinates") {
    auto config = dk::AppConfig::defaults();
    config.window_title = L"Dota 2";
    config.region = dk::Box{100, 120, 900, 700};
    config.region_configured = true;

    const auto json = dk::serialize_config(config);
    const auto restored = dk::parse_config(json);

    CHECK(restored.window_title == L"Dota 2");
    CHECK(restored.region == dk::Box{100, 120, 900, 700});
    CHECK(restored.region_configured);
}

TEST_CASE("window title round-trips through UTF-8 JSON") {
    auto config = dk::AppConfig::defaults();
    config.window_title = L"\u0414\u043e\u0442\u0430 2 \U0001F3AE";

    const auto json = dk::serialize_config(config);
    const auto restored = dk::parse_config(json);

    CHECK(restored.window_title == config.window_title);
}

TEST_CASE("post-send delay has a validated JSON contract") {
    CHECK(dk::AppConfig::defaults().post_send_delay_ms == 100);

    const auto parsed = dk::parse_config(R"({"post_send_delay_ms":250})");
    CHECK(parsed.post_send_delay_ms == 250);
    CHECK(dk::parse_config(R"({"post_send_delay_ms":0})").post_send_delay_ms ==
          0);
    CHECK(dk::parse_config(R"({"post_send_delay_ms":5000})")
              .post_send_delay_ms == 5000);
    CHECK(dk::parse_config(R"({})").post_send_delay_ms == 100);
    CHECK(dk::parse_config(dk::serialize_config(parsed)).post_send_delay_ms == 250);

    CHECK_THROWS_WITH(
        dk::parse_config(R"({"post_send_delay_ms":-1})"),
        Catch::Matchers::ContainsSubstring("post_send_delay_ms"));
    CHECK_THROWS_WITH(
        dk::parse_config(R"({"post_send_delay_ms":5001})"),
        Catch::Matchers::ContainsSubstring("post_send_delay_ms"));
    CHECK_THROWS_WITH(
        dk::parse_config(R"({"post_send_delay_ms":4294967296})"),
        Catch::Matchers::ContainsSubstring("post_send_delay_ms"));
    CHECK_THROWS_WITH(
        dk::parse_config(R"({"post_send_delay_ms":-4294967296})"),
        Catch::Matchers::ContainsSubstring("post_send_delay_ms"));
    CHECK_THROWS_WITH(
        dk::parse_config(R"({"post_send_delay_ms":"100"})"),
        Catch::Matchers::ContainsSubstring("post_send_delay_ms"));
}

TEST_CASE("configuration validation rejects unsafe values") {
    check_rejected([](auto& config) {
        config.region_configured = true;
        config.region = dk::Box{0, 0, 0, 100};
    });
    check_rejected([](auto& config) { config.min_ocr_confidence = -0.01F; });
    check_rejected([](auto& config) { config.min_ocr_confidence = 1.01F; });
    check_rejected([](auto& config) {
        config.min_ocr_confidence = std::numeric_limits<float>::quiet_NaN();
    });
    check_rejected([](auto& config) { config.inter_key_delay_us = -1; });
    check_rejected([](auto& config) { config.hotkeys.toggle = config.hotkeys.calibrate; });
}

TEST_CASE("configuration validates tracker invariants") {
    check_rejected([](auto& config) { config.tracker.confirm_frames = 1; });
    check_rejected([](auto& config) { config.tracker.unlock_missing_frames = 0; });
    check_rejected([](auto& config) { config.tracker.max_center_distance_px = 0.0F; });
    check_rejected([](auto& config) {
        config.tracker.max_center_distance_px =
            std::numeric_limits<float>::quiet_NaN();
    });
    check_rejected([](auto& config) {
        config.tracker.max_center_distance_px =
            std::numeric_limits<float>::infinity();
    });
}

TEST_CASE("configuration validates detector invariants") {
    check_rejected([](auto& config) { config.detector.luminance_threshold = -1; });
    check_rejected([](auto& config) { config.detector.luminance_threshold = 256; });
    check_rejected([](auto& config) { config.detector.min_char_height_ratio = 0.0F; });
    check_rejected([](auto& config) {
        config.detector.min_char_height_ratio =
            std::numeric_limits<float>::quiet_NaN();
    });
    check_rejected([](auto& config) {
        config.detector.max_char_height_ratio =
            config.detector.min_char_height_ratio - 0.001F;
    });
    check_rejected([](auto& config) { config.detector.max_char_height_ratio = 1.001F; });
    check_rejected([](auto& config) { config.detector.baseline_tolerance_ratio = 0.0F; });
    check_rejected([](auto& config) {
        config.detector.baseline_tolerance_ratio =
            std::numeric_limits<float>::infinity();
    });
    check_rejected([](auto& config) { config.detector.max_gap_ratio = 0.0F; });
    check_rejected([](auto& config) {
        config.detector.max_gap_ratio = std::numeric_limits<float>::quiet_NaN();
    });
    check_rejected([](auto& config) { config.detector.min_components_per_line = 0; });
    check_rejected([](auto& config) { config.detector.crop_padding_px = -1; });
}

TEST_CASE("configuration validates normalized ignored rectangles") {
    check_rejected([](auto& config) {
        config.detector.ignored_regions.emplace_back(-0.01F, 0.0F, 0.5F, 0.5F);
    });
    check_rejected([](auto& config) {
        config.detector.ignored_regions.emplace_back(0.0F, 0.0F, 0.0F, 0.5F);
    });
    check_rejected([](auto& config) {
        config.detector.ignored_regions.emplace_back(0.75F, 0.0F, 0.5F, 0.5F);
    });
    check_rejected([](auto& config) {
        config.detector.ignored_regions.emplace_back(0.0F, 0.75F, 0.5F, 0.5F);
    });
    check_rejected([](auto& config) {
        config.detector.ignored_regions.emplace_back(
            std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.5F, 0.5F);
    });
}

TEST_CASE("configuration rejects invalid values loaded from JSON") {
    CHECK_THROWS_AS(
        dk::parse_config(R"({"region_configured":true,"region":{"x":0,"y":0,"width":100,"height":0}})"),
        std::invalid_argument);
    CHECK_THROWS_AS(dk::parse_config(R"({"min_ocr_confidence":1.1})"), std::invalid_argument);
    CHECK_THROWS_AS(dk::parse_config(R"({"inter_key_delay_us":-1})"), std::invalid_argument);
    CHECK_THROWS_AS(
        dk::parse_config(R"({"hotkeys":{"calibrate":118,"toggle":118}})"),
        std::invalid_argument);
    CHECK_THROWS_AS(
        dk::parse_config(R"({"tracker":{"confirm_frames":1}})"),
        std::invalid_argument);
    CHECK_THROWS_AS(
        dk::parse_config(R"({"detector":{"luminance_threshold":256}})"),
        std::invalid_argument);
    CHECK_THROWS_AS(
        dk::parse_config(
            R"({"detector":{"ignored_regions":[{"x":0.8,"y":0.1,"width":0.3,"height":0.2}]}})"),
        std::invalid_argument);
}

TEST_CASE("configuration atomically replaces an existing destination") {
    const auto directory = std::filesystem::temp_directory_path();
    const auto path = directory / "dota_keyboard_config_test.json";
    const auto temporary = std::filesystem::path{path.string() + ".tmp"};
    std::filesystem::remove(path);
    std::filesystem::remove(temporary);

    auto config = dk::AppConfig::defaults();
    config.window_title = L"\u0414\u043e\u0442\u0430 2";
    config.region = dk::Box{10, 20, 300, 400};
    config.region_configured = true;
    dk::save_config(path, dk::AppConfig::defaults());
    dk::save_config(path, config);

    const auto restored = dk::load_config(path);
    CHECK(restored.window_title == config.window_title);
    CHECK(restored.region == config.region);
    CHECK(restored.region_configured);
    CHECK_FALSE(std::filesystem::exists(temporary));

    std::filesystem::remove(path);
}
