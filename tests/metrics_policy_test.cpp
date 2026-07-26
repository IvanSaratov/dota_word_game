#include "dk/metrics.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using namespace std::chrono_literals;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "metrics_policy_test failure: " << message << '\n';
        std::exit(1);
    }
}

}  // namespace

int main() {
    const dk::LatencySummary summary{
        .capture = {2, 1.25, 1.00, 1.50},
        .detect = {2, 2.25, 2.00, 2.50},
        .ocr = {2, 3.25, 3.00, 3.50},
        .total = {2, 6.75, 6.00, 7.50},
    };
    const auto rendered = dk::format_latency_summary(summary);
    require(rendered.find('\n') == std::string::npos,
            "latency summary must be one line");
    require(
        rendered ==
            "Latency summary "
            "capture[n=2 mean=1.25ms median=1.00ms p95=1.50ms] "
            "detect[n=2 mean=2.25ms median=2.00ms p95=2.50ms] "
            "ocr[n=2 mean=3.25ms median=3.00ms p95=3.50ms] "
            "total[n=2 mean=6.75ms median=6.00ms p95=7.50ms]",
        "latency summary must have the stable compact format");

    const auto start = std::chrono::steady_clock::time_point{};
    dk::MetricsSchedule schedule{start};
    require(!schedule.take_if_due(start + 60s, false),
            "inactive processing must never emit");
    require(!schedule.take_if_due(start + 59s, true),
            "summary must not emit before 60 seconds");
    require(schedule.take_if_due(start + 60s, true),
            "summary must emit at 60 seconds");
    require(!schedule.take_if_due(start + 119s, true),
            "next summary must not emit before 120 seconds");
    require(schedule.take_if_due(start + 120s, true),
            "next summary must emit at 120 seconds");

    schedule.reset(start + 200s);
    require(!schedule.take_if_due(start + 259s, true),
            "reset must start a fresh 60-second period");
    require(schedule.take_if_due(start + 260s, true),
            "reset schedule must emit after its fresh period");

    for (const auto invalid_period : {0s, -1s}) {
        bool rejected = false;
        try {
            const dk::MetricsSchedule invalid_schedule{
                start, invalid_period};
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(rejected, "nonpositive metrics period must be rejected");
    }
}
