#include <chrono>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "dk/delay.hpp"

using namespace std::chrono_literals;

TEST_CASE("zero and uncancelled delays complete") {
    auto now = std::chrono::steady_clock::time_point{};
    std::vector<std::chrono::steady_clock::duration> sleeps;
    const dk::DelayRuntime runtime{
        [&now] { return now; },
        [&now, &sleeps](auto duration) {
            sleeps.push_back(duration);
            now += duration;
        },
    };

    CHECK(dk::interruptible_delay(0ms, {}, runtime));
    CHECK(sleeps.empty());
    CHECK(dk::interruptible_delay(11ms, {}, runtime));
    CHECK((sleeps == std::vector<std::chrono::steady_clock::duration>{
                          5ms, 5ms, 1ms}));
}

TEST_CASE("a pre-cancelled delay returns promptly") {
    auto now = std::chrono::steady_clock::time_point{};
    int sleep_calls = 0;
    const dk::DelayRuntime runtime{
        [&now] { return now; },
        [&sleep_calls](auto) { ++sleep_calls; },
    };

    CHECK_FALSE(
        dk::interruptible_delay(250ms, [] { return true; }, runtime));
    CHECK(sleep_calls == 0);
}

TEST_CASE("a delay stops when cancellation becomes true") {
    auto now = std::chrono::steady_clock::time_point{};
    std::vector<std::chrono::steady_clock::duration> sleeps;
    const dk::DelayRuntime runtime{
        [&now] { return now; },
        [&now, &sleeps](auto duration) {
            sleeps.push_back(duration);
            now += duration;
        },
    };
    int cancellation_checks = 0;
    const dk::CancellationPredicate cancellation = [&cancellation_checks] {
        ++cancellation_checks;
        return cancellation_checks >= 2;
    };

    CHECK_FALSE(dk::interruptible_delay(250ms, cancellation, runtime));
    CHECK(cancellation_checks == 2);
    CHECK((sleeps ==
           std::vector<std::chrono::steady_clock::duration>{5ms}));
}
