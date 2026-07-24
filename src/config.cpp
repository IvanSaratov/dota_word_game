#include "dk/config.hpp"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace dk {
namespace {

using Json = nlohmann::json;

void append_utf8(std::string& result, std::uint32_t code_point) {
    if (code_point <= 0x7F) {
        result.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7FF) {
        result.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
        result.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else if (code_point <= 0xFFFF) {
        result.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
        result.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        result.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else {
        result.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
        result.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
        result.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        result.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    }
}

std::string to_utf8(std::wstring_view text) {
    std::string result;
    result.reserve(text.size());

    for (std::size_t index = 0; index < text.size(); ++index) {
        std::uint32_t code_point = static_cast<std::uint32_t>(text[index]);
        if constexpr (sizeof(wchar_t) == 2) {
            if (code_point >= 0xD800 && code_point <= 0xDBFF) {
                if (++index >= text.size()) {
                    throw std::invalid_argument("window title contains an incomplete UTF-16 pair");
                }
                const auto low = static_cast<std::uint32_t>(text[index]);
                if (low < 0xDC00 || low > 0xDFFF) {
                    throw std::invalid_argument("window title contains an invalid UTF-16 pair");
                }
                code_point = 0x10000 + ((code_point - 0xD800) << 10) + (low - 0xDC00);
            } else if (code_point >= 0xDC00 && code_point <= 0xDFFF) {
                throw std::invalid_argument("window title contains an unpaired UTF-16 surrogate");
            }
        }
        if (code_point > 0x10FFFF || (code_point >= 0xD800 && code_point <= 0xDFFF)) {
            throw std::invalid_argument("window title contains an invalid Unicode code point");
        }
        append_utf8(result, code_point);
    }
    return result;
}

std::uint32_t continuation(std::string_view text, std::size_t& index) {
    if (index >= text.size()) {
        throw std::invalid_argument("window title contains truncated UTF-8");
    }
    const auto byte = static_cast<unsigned char>(text[index++]);
    if ((byte & 0xC0) != 0x80) {
        throw std::invalid_argument("window title contains invalid UTF-8");
    }
    return byte & 0x3F;
}

std::wstring from_utf8(std::string_view text) {
    std::wstring result;
    result.reserve(text.size());

    for (std::size_t index = 0; index < text.size();) {
        const auto lead = static_cast<unsigned char>(text[index++]);
        std::uint32_t code_point{};
        std::uint32_t minimum{};
        if (lead <= 0x7F) {
            code_point = lead;
        } else if ((lead & 0xE0) == 0xC0) {
            code_point = lead & 0x1F;
            minimum = 0x80;
            code_point = (code_point << 6) | continuation(text, index);
        } else if ((lead & 0xF0) == 0xE0) {
            code_point = lead & 0x0F;
            minimum = 0x800;
            code_point = (code_point << 6) | continuation(text, index);
            code_point = (code_point << 6) | continuation(text, index);
        } else if ((lead & 0xF8) == 0xF0) {
            code_point = lead & 0x07;
            minimum = 0x10000;
            code_point = (code_point << 6) | continuation(text, index);
            code_point = (code_point << 6) | continuation(text, index);
            code_point = (code_point << 6) | continuation(text, index);
        } else {
            throw std::invalid_argument("window title contains invalid UTF-8");
        }

        if (code_point < minimum || code_point > 0x10FFFF ||
            (code_point >= 0xD800 && code_point <= 0xDFFF)) {
            throw std::invalid_argument("window title contains invalid UTF-8");
        }

        if constexpr (sizeof(wchar_t) == 2) {
            if (code_point <= 0xFFFF) {
                result.push_back(static_cast<wchar_t>(code_point));
            } else {
                code_point -= 0x10000;
                result.push_back(static_cast<wchar_t>(0xD800 + (code_point >> 10)));
                result.push_back(static_cast<wchar_t>(0xDC00 + (code_point & 0x3FF)));
            }
        } else {
            result.push_back(static_cast<wchar_t>(code_point));
        }
    }
    return result;
}

void validate(const AppConfig& config) {
    if (config.region_configured && (config.region.width <= 0 || config.region.height <= 0)) {
        throw std::invalid_argument("configured region must have positive width and height");
    }
    if (!std::isfinite(config.min_ocr_confidence) ||
        config.min_ocr_confidence < 0.0F || config.min_ocr_confidence > 1.0F) {
        throw std::invalid_argument("minimum OCR confidence must be between zero and one");
    }
    if (config.inter_key_delay_us < 0) {
        throw std::invalid_argument("inter-key delay cannot be negative");
    }
    if (config.hotkeys.calibrate == config.hotkeys.toggle) {
        throw std::invalid_argument("calibrate and toggle hotkeys must differ");
    }
}

Json box_to_json(const Box& box) {
    return {{"x", box.x}, {"y", box.y}, {"width", box.width}, {"height", box.height}};
}

Box box_from_json(const Json& json, Box fallback = {}) {
    return Box{
        json.value("x", fallback.x),
        json.value("y", fallback.y),
        json.value("width", fallback.width),
        json.value("height", fallback.height),
    };
}

Json detector_to_json(const DetectorConfig& detector) {
    Json ignored_regions = Json::array();
    for (const auto& region : detector.ignored_regions) {
        ignored_regions.push_back(
            {{"x", region.x}, {"y", region.y}, {"width", region.width}, {"height", region.height}});
    }
    return {
        {"luminance_threshold", detector.luminance_threshold},
        {"min_char_height_ratio", detector.min_char_height_ratio},
        {"max_char_height_ratio", detector.max_char_height_ratio},
        {"baseline_tolerance_ratio", detector.baseline_tolerance_ratio},
        {"max_gap_ratio", detector.max_gap_ratio},
        {"min_components_per_line", detector.min_components_per_line},
        {"crop_padding_px", detector.crop_padding_px},
        {"ignored_regions", std::move(ignored_regions)},
    };
}

void read_detector(const Json& json, DetectorConfig& detector) {
    detector.luminance_threshold =
        json.value("luminance_threshold", detector.luminance_threshold);
    detector.min_char_height_ratio =
        json.value("min_char_height_ratio", detector.min_char_height_ratio);
    detector.max_char_height_ratio =
        json.value("max_char_height_ratio", detector.max_char_height_ratio);
    detector.baseline_tolerance_ratio =
        json.value("baseline_tolerance_ratio", detector.baseline_tolerance_ratio);
    detector.max_gap_ratio = json.value("max_gap_ratio", detector.max_gap_ratio);
    detector.min_components_per_line =
        json.value("min_components_per_line", detector.min_components_per_line);
    detector.crop_padding_px = json.value("crop_padding_px", detector.crop_padding_px);
    if (const auto iterator = json.find("ignored_regions"); iterator != json.end()) {
        detector.ignored_regions.clear();
        for (const auto& region : *iterator) {
            detector.ignored_regions.emplace_back(
                region.at("x").get<float>(),
                region.at("y").get<float>(),
                region.at("width").get<float>(),
                region.at("height").get<float>());
        }
    }
}

void atomic_replace(const std::filesystem::path& temporary, const std::filesystem::path& target) {
#ifdef _WIN32
    if (!MoveFileExW(
            temporary.c_str(),
            target.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        throw std::system_error(
            static_cast<int>(GetLastError()), std::system_category(), "replace configuration");
    }
#else
    std::error_code error;
    std::filesystem::rename(temporary, target, error);
    if (error) {
        throw std::filesystem::filesystem_error(
            "replace configuration", temporary, target, error);
    }
#endif
}

}  // namespace

AppConfig AppConfig::defaults() {
    return {};
}

std::string serialize_config(const AppConfig& config) {
    validate(config);

    Json json{
        {"window_title", to_utf8(config.window_title)},
        {"region", box_to_json(config.region)},
        {"region_configured", config.region_configured},
        {"live_input", config.live_input},
        {"min_ocr_confidence", config.min_ocr_confidence},
        {"inter_key_delay_us", config.inter_key_delay_us},
        {"detector", detector_to_json(config.detector)},
        {"tracker",
         {
             {"confirm_frames", config.tracker.confirm_frames},
             {"max_center_distance_px", config.tracker.max_center_distance_px},
             {"unlock_missing_frames", config.tracker.unlock_missing_frames},
         }},
        {"hotkeys",
         {
             {"calibrate", config.hotkeys.calibrate},
             {"toggle", config.hotkeys.toggle},
         }},
    };
    return json.dump(2) + '\n';
}

AppConfig parse_config(std::string_view text) {
    const auto json = Json::parse(text);
    auto config = AppConfig::defaults();

    config.window_title = from_utf8(json.value("window_title", std::string{}));
    if (const auto iterator = json.find("region"); iterator != json.end()) {
        config.region = box_from_json(*iterator, config.region);
    }
    config.region_configured = json.value("region_configured", config.region_configured);
    config.live_input = json.value("live_input", config.live_input);
    config.min_ocr_confidence =
        json.value("min_ocr_confidence", config.min_ocr_confidence);
    config.inter_key_delay_us = json.value("inter_key_delay_us", config.inter_key_delay_us);
    if (const auto iterator = json.find("detector"); iterator != json.end()) {
        read_detector(*iterator, config.detector);
    }
    if (const auto iterator = json.find("tracker"); iterator != json.end()) {
        config.tracker.confirm_frames =
            iterator->value("confirm_frames", config.tracker.confirm_frames);
        config.tracker.max_center_distance_px =
            iterator->value("max_center_distance_px", config.tracker.max_center_distance_px);
        config.tracker.unlock_missing_frames =
            iterator->value("unlock_missing_frames", config.tracker.unlock_missing_frames);
    }
    if (const auto iterator = json.find("hotkeys"); iterator != json.end()) {
        config.hotkeys.calibrate =
            iterator->value("calibrate", config.hotkeys.calibrate);
        config.hotkeys.toggle = iterator->value("toggle", config.hotkeys.toggle);
    }

    validate(config);
    return config;
}

AppConfig load_config(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("unable to open configuration: " + path.string());
    }
    const std::string contents{
        std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    if (input.bad()) {
        throw std::runtime_error("unable to read configuration: " + path.string());
    }
    return parse_config(contents);
}

void save_config(const std::filesystem::path& path, const AppConfig& config) {
    auto temporary = path;
    temporary += ".tmp";
    const auto serialized = serialize_config(config);

    try {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            throw std::runtime_error(
                "unable to open temporary configuration: " + temporary.string());
        }
        output.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
        output.flush();
        if (!output) {
            throw std::runtime_error(
                "unable to write temporary configuration: " + temporary.string());
        }
        output.close();
        atomic_replace(temporary, path);
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}

}  // namespace dk
