#include "dk/delay.hpp"

#include <algorithm>
#include <thread>

namespace dk {

bool interruptible_delay(
    std::chrono::milliseconds duration,
    const CancellationPredicate& cancellation) {
    if (duration <= std::chrono::milliseconds::zero()) {
        return true;
    }

    constexpr auto poll_interval =
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::milliseconds{5});
    const auto deadline = std::chrono::steady_clock::now() + duration;
    auto now = std::chrono::steady_clock::now();
    while (now < deadline) {
        if (cancellation && cancellation()) {
            return false;
        }

        std::this_thread::sleep_for(std::min(deadline - now, poll_interval));
        now = std::chrono::steady_clock::now();
    }
    return true;
}

}  // namespace dk
