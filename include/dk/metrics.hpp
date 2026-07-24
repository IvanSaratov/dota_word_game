#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <deque>

namespace dk {

enum class LatencyStage { capture, detect, ocr, total };

struct StageSummary {
    std::size_t count{};
    double mean_ms{};
    double median_ms{};
    double p95_ms{};
};

struct LatencySummary {
    StageSummary capture;
    StageSummary detect;
    StageSummary ocr;
    StageSummary total;
};

class LatencyMetrics {
public:
    static constexpr std::size_t capacity = 512;

    void record(LatencyStage stage, std::chrono::steady_clock::duration elapsed);
    [[nodiscard]] LatencySummary summary() const;

private:
    [[nodiscard]] static std::size_t index(LatencyStage stage) noexcept;

    std::array<std::deque<double>, 4> samples_;
};

}  // namespace dk
