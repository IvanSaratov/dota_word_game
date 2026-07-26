#pragma once

#include <chrono>
#include <functional>

#include "dk/input_sink.hpp"

namespace dk {

using DelayFunction = std::function<bool(
    std::chrono::milliseconds, const CancellationPredicate&)>;

struct DelayRuntime {
    std::function<std::chrono::steady_clock::time_point()> now;
    std::function<void(std::chrono::steady_clock::duration)> sleep_for;
};

[[nodiscard]] bool interruptible_delay(
    std::chrono::milliseconds duration,
    const CancellationPredicate& cancellation);

[[nodiscard]] bool interruptible_delay(
    std::chrono::milliseconds duration,
    const CancellationPredicate& cancellation,
    const DelayRuntime& runtime);

}  // namespace dk
