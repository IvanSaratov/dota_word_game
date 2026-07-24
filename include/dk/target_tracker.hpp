#pragma once

#include <optional>
#include <span>
#include <vector>

#include "dk/types.hpp"

namespace dk {

struct TrackerConfig {
    int confirm_frames{2};
    float max_center_distance_px{90.0F};
    int unlock_missing_frames{2};
};

class TargetTracker {
public:
    explicit TargetTracker(TrackerConfig config = {});
    [[nodiscard]] std::optional<TextCandidate> update(std::span<const TextCandidate> candidates);
    void mark_sent(const TextCandidate& candidate);

private:
    struct Track {
        TextCandidate value;
        int seen_frames{1};
    };
    struct Lock {
        TextCandidate value;
        int missing_frames{};
    };

    TrackerConfig config_;
    std::vector<Track> previous_;
    std::vector<Lock> locks_;
};

}  // namespace dk
