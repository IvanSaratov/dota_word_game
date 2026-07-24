#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

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
    CHECK(config.min_ocr_confidence == Catch::Approx(0.80F));
    CHECK(config.tracker.confirm_frames == 2);
    CHECK(config.hotkeys.calibrate == 0x76);  // F7
    CHECK(config.hotkeys.toggle == 0x77);     // F8
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

TEST_CASE("configuration rejects invalid values loaded from JSON") {
    CHECK_THROWS_AS(
        dk::parse_config(R"({"region_configured":true,"region":{"x":0,"y":0,"width":100,"height":0}})"),
        std::invalid_argument);
    CHECK_THROWS_AS(dk::parse_config(R"({"min_ocr_confidence":1.1})"), std::invalid_argument);
    CHECK_THROWS_AS(dk::parse_config(R"({"inter_key_delay_us":-1})"), std::invalid_argument);
    CHECK_THROWS_AS(
        dk::parse_config(R"({"hotkeys":{"calibrate":118,"toggle":118}})"),
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
