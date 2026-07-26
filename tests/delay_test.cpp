#include <chrono>

#include <catch2/catch_test_macros.hpp>

#include "dk/delay.hpp"

using namespace std::chrono_literals;

TEST_CASE("zero and uncancelled delays complete") {
    CHECK(dk::interruptible_delay(0ms, {}));
    CHECK(dk::interruptible_delay(1ms, {}));
}

TEST_CASE("a pre-cancelled delay returns promptly") {
    const auto start = std::chrono::steady_clock::now();

    CHECK_FALSE(dk::interruptible_delay(250ms, [] { return true; }));

    const auto elapsed = std::chrono::steady_clock::now() - start;
    CHECK(elapsed < 20ms);
}

TEST_CASE("a delay stops when cancellation becomes true") {
    int cancellation_checks = 0;
    const dk::CancellationPredicate cancellation = [&cancellation_checks] {
        ++cancellation_checks;
        return cancellation_checks >= 2;
    };

    CHECK_FALSE(dk::interruptible_delay(250ms, cancellation));
    CHECK(cancellation_checks >= 2);
}
