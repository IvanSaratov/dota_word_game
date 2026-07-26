#pragma once

#include <chrono>
#include <functional>

#include "dk/input_sink.hpp"

namespace dk {

using DelayFunction = std::function<bool(
    std::chrono::milliseconds, const CancellationPredicate&)>;

[[nodiscard]] bool interruptible_delay(
    std::chrono::milliseconds duration,
    const CancellationPredicate& cancellation);

}  // namespace dk
