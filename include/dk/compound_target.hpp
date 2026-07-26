#pragma once

#include <map>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "dk/target_tracker.hpp"

namespace dk {

struct CompoundTarget {
    std::vector<TrackId> line_ids;
    std::string normalized_text;
    Box bounds;
    bool ambiguous;
    bool observed_this_frame;
    bool sent_owned;
};

class CompoundTargetAssembler {
public:
    std::vector<CompoundTarget> update(
        std::span<const LineTrackSnapshot> lines);

private:
    struct PairState {
        float relative_x{};
        float relative_y{};
        int stable_frames{};
        int clean_frames{};
        bool ambiguous{};
        bool grouped{};
    };

    std::map<std::pair<TrackId, TrackId>, PairState> pairs_;
};

}  // namespace dk
