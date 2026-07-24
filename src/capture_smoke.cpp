#include "dk/dxgi_capture.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

int parse_integer(const char* text, const char* name) {
    const std::string_view value{text};
    int parsed{};
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
        throw std::invalid_argument(std::string{"invalid "} + name + ": " + text);
    }
    return parsed;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 6) {
        std::cerr << "usage: capture_smoke x y width height frames\n";
        return 2;
    }

    try {
        const dk::Box region{
            parse_integer(argv[1], "x"),
            parse_integer(argv[2], "y"),
            parse_integer(argv[3], "width"),
            parse_integer(argv[4], "height"),
        };
        const auto frame_count = parse_integer(argv[5], "frames");
        if (frame_count <= 0) {
            throw std::invalid_argument("frames must be positive");
        }

        dk::DxgiCapture capture{region};
        std::vector<double> elapsed_milliseconds;
        elapsed_milliseconds.reserve(static_cast<std::size_t>(frame_count));

        while (elapsed_milliseconds.size() < static_cast<std::size_t>(frame_count)) {
            const auto started_at = std::chrono::steady_clock::now();
            auto frame = capture.next_frame();
            const auto finished_at = std::chrono::steady_clock::now();
            if (!frame) {
                continue;
            }
            if (frame->bgra.empty() || frame->bgra.type() != CV_8UC4 ||
                frame->bgra.cols != region.width || frame->bgra.rows != region.height) {
                throw std::runtime_error("captured frame is not a nonempty BGRA region");
            }

            elapsed_milliseconds.push_back(
                std::chrono::duration<double, std::milli>(finished_at - started_at).count());
        }

        const auto mean =
            std::accumulate(
                elapsed_milliseconds.begin(), elapsed_milliseconds.end(), 0.0) /
            static_cast<double>(elapsed_milliseconds.size());
        std::sort(elapsed_milliseconds.begin(), elapsed_milliseconds.end());
        const auto p95_index =
            (elapsed_milliseconds.size() * 95U + 99U) / 100U - 1U;

        std::cout << "frames=" << elapsed_milliseconds.size() << " mean_ms="
                  << std::fixed << std::setprecision(3) << mean << " p95_ms="
                  << elapsed_milliseconds[p95_index] << " size=" << region.width << 'x'
                  << region.height << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "capture_smoke: " << error.what() << '\n';
        return 1;
    }
}
