#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "dk/types.hpp"

namespace dk {

struct TrackerConfig {
    int confirm_frames{2};
    float max_center_distance_px{90.0F};
    int unlock_missing_frames{15};
};

using TrackId = std::uint64_t;

struct LineTrackSnapshot {
    TrackId id;
    TextCandidate value;
    int seen_frames;
    int missing_frames;
    bool confirmed;
    bool sent;
    bool observed_this_frame;
};

struct TrackerFrame {
    std::vector<LineTrackSnapshot> lines;
};

class TargetTracker {
public:
    explicit TargetTracker(TrackerConfig config = {});
    [[nodiscard]] TrackerFrame update_lines(
        std::span<const TextCandidate> candidates);
    [[nodiscard]] std::optional<TextCandidate> update(std::span<const TextCandidate> candidates);
    void mark_sent(std::span<const TrackId> ids);
    void mark_sent(const TextCandidate& candidate);

private:
    struct Track {
        TrackId id;
        TextCandidate value;
        int seen_frames{1};
        int missing_frames{};
        bool sent{};
    };

    TrackerConfig config_;
    std::vector<Track> tracks_;
    TrackId next_id_{1};
};

}  // namespace dk
