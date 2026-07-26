#pragma once

#include <map>
#include <set>
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
    void mark_sent(const CompoundTarget& target);

private:
    struct PairState {
        float relative_x{};
        float relative_y{};
        int stable_frames{};
        int clean_frames{};
        int missing_frames{};
        int disconnected_frames{};
        bool ambiguous{};
        bool grouped{};
        bool provisional{};
    };

    struct SentCompound {
        std::set<TrackId> line_ids;
        std::string normalized_text;
        Box bounds;
    };

    struct AmbiguityEpisode {
        std::map<TrackId, std::string> replacement_texts;
        int clean_frames{};
        bool active{true};
    };

    std::map<std::pair<TrackId, TrackId>, PairState> pairs_;
    std::map<TrackId, std::string> text_by_id_;
    std::map<std::pair<TrackId, TrackId>, AmbiguityEpisode>
        ambiguity_episodes_;
    std::vector<SentCompound> sent_compounds_;
};

}  // namespace dk
