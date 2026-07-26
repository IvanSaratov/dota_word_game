#include "dk/delay.hpp"

#include <algorithm>
#include <thread>

namespace dk {

bool interruptible_delay(
    std::chrono::milliseconds duration,
    const CancellationPredicate& cancellation) {
    static const DelayRuntime runtime{
        [] { return std::chrono::steady_clock::now(); },
        [](auto sleep_duration) {
            std::this_thread::sleep_for(sleep_duration);
        },
    };
    return interruptible_delay(duration, cancellation, runtime);
}

bool interruptible_delay(
    std::chrono::milliseconds duration,
    const CancellationPredicate& cancellation,
    const DelayRuntime& runtime) {
    if (duration <= std::chrono::milliseconds::zero()) {
        return true;
    }

    constexpr auto poll_interval =
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::milliseconds{5});
    auto now = runtime.now();
    const auto deadline = now + duration;
    while (now < deadline) {
        if (cancellation && cancellation()) {
            return false;
        }

        runtime.sleep_for(std::min(deadline - now, poll_interval));
        now = runtime.now();
    }
    return true;
}

}  // namespace dk
