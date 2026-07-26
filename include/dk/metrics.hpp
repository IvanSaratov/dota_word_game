#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <deque>
#include <string>

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

[[nodiscard]] std::string format_latency_summary(
    const LatencySummary& summary);

class MetricsSchedule {
public:
    explicit MetricsSchedule(
        std::chrono::steady_clock::time_point now,
        std::chrono::seconds period = std::chrono::seconds{60});

    bool take_if_due(
        std::chrono::steady_clock::time_point now,
        bool processing_active);
    void reset(std::chrono::steady_clock::time_point now);

private:
    std::chrono::seconds period_;
    std::chrono::steady_clock::time_point next_;
};

}  // namespace dk
