#include "dk/metrics.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace dk {
namespace {

StageSummary summarize(const std::deque<double>& samples) {
    if (samples.empty()) {
        return {};
    }

    std::vector<double> sorted(samples.begin(), samples.end());
    std::ranges::sort(sorted);
    const auto count = sorted.size();
    const double mean =
        std::accumulate(sorted.begin(), sorted.end(), 0.0) / static_cast<double>(count);
    const double median = count % 2 == 0
                              ? (sorted[count / 2 - 1] + sorted[count / 2]) / 2.0
                              : sorted[count / 2];
    const auto p95_index = static_cast<std::size_t>(
        std::floor(0.95 * static_cast<double>(count - 1)));
    return {count, mean, median, sorted[p95_index]};
}

}  // namespace

std::size_t LatencyMetrics::index(LatencyStage stage) noexcept {
    return static_cast<std::size_t>(stage);
}

void LatencyMetrics::record(
    LatencyStage stage, std::chrono::steady_clock::duration elapsed) {
    auto& samples = samples_[index(stage)];
    if (samples.size() == capacity) {
        samples.pop_front();
    }
    samples.push_back(std::chrono::duration<double, std::milli>(elapsed).count());
}

LatencySummary LatencyMetrics::summary() const {
    return {
        summarize(samples_[index(LatencyStage::capture)]),
        summarize(samples_[index(LatencyStage::detect)]),
        summarize(samples_[index(LatencyStage::ocr)]),
        summarize(samples_[index(LatencyStage::total)]),
    };
}

std::string format_latency_summary(const LatencySummary& summary) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(2) << "Latency summary ";
    const auto append = [&output](
                            const char* name,
                            const StageSummary& stage,
                            const bool trailing_space) {
        output << name << "[n=" << stage.count << " mean=" << stage.mean_ms
               << "ms median=" << stage.median_ms << "ms p95=" << stage.p95_ms
               << "ms]";
        if (trailing_space) {
            output << ' ';
        }
    };
    append("capture", summary.capture, true);
    append("detect", summary.detect, true);
    append("ocr", summary.ocr, true);
    append("total", summary.total, false);
    return output.str();
}

MetricsSchedule::MetricsSchedule(
    const std::chrono::steady_clock::time_point now,
    const std::chrono::seconds period)
    : period_(period),
      next_(now + period_) {
    if (period_ <= std::chrono::seconds::zero()) {
        throw std::invalid_argument("metrics period must be positive");
    }
}

bool MetricsSchedule::take_if_due(
    const std::chrono::steady_clock::time_point now,
    const bool processing_active) {
    if (!processing_active || now < next_) {
        return false;
    }
    do {
        next_ += period_;
    } while (next_ <= now);
    return true;
}

void MetricsSchedule::reset(
    const std::chrono::steady_clock::time_point now) {
    next_ = now + period_;
}

}  // namespace dk
