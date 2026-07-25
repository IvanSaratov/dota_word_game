#include "dk/target_tracker.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace dk {
namespace {

float center_distance(const TextCandidate& left, const TextCandidate& right) {
    return std::hypot(left.bounds.center_x() - right.bounds.center_x(),
                      left.bounds.center_y() - right.bounds.center_y());
}

bool compatible_size(const TextCandidate& left, const TextCandidate& right) {
    const auto ratio_in_range = [](int left_size, int right_size) {
        const auto smaller = static_cast<float>(std::min(left_size, right_size));
        const auto larger = static_cast<float>(std::max(left_size, right_size));
        return larger > 0.0F && smaller / larger >= 0.5F;
    };
    return ratio_in_range(left.bounds.width, right.bounds.width) &&
           ratio_in_range(left.bounds.height, right.bounds.height);
}

std::int64_t area(const Box& box) noexcept {
    return static_cast<std::int64_t>(box.width) * box.height;
}

bool center_inside_expanded(
    const Box& candidate, const Box& retained, float expansion) noexcept {
    return candidate.center_x() >= retained.x - expansion &&
           candidate.center_x() <= retained.right() + expansion &&
           candidate.center_y() >= retained.y - expansion &&
           candidate.center_y() <= retained.bottom() + expansion;
}

bool is_fragment(
    const TextCandidate& candidate, const TextCandidate& sent,
    float expansion) noexcept {
    const auto candidate_area = area(candidate.bounds);
    const auto sent_area = area(sent.bounds);
    return candidate_area > 0 && sent_area > 0 &&
           candidate_area * 100 <= sent_area * 65 &&
           center_inside_expanded(candidate.bounds, sent.bounds, expansion);
}

}  // namespace

TargetTracker::TargetTracker(TrackerConfig config) : config_(config) {}

std::optional<TextCandidate> TargetTracker::update(std::span<const TextCandidate> candidates) {
    struct Match {
        float distance;
        std::size_t track_index;
        std::size_t candidate_index;
    };

    std::vector<bool> matched_tracks(tracks_.size());
    std::vector<bool> matched_candidates(candidates.size());
    const auto consume_for_sent_track =
        [&](std::size_t track_index, std::size_t candidate_index,
            bool refresh_full_bounds) {
            auto& track = tracks_[track_index];
            matched_candidates[candidate_index] = true;
            if (!matched_tracks[track_index]) {
                matched_tracks[track_index] = true;
                track.missing_frames = 0;
            }
            if (refresh_full_bounds) {
                track.value.bounds = candidates[candidate_index].bounds;
            }
        };

    for (std::size_t candidate_index = 0; candidate_index < candidates.size();
         ++candidate_index) {
        std::optional<std::size_t> closest_track;
        auto closest_distance = 0.0F;
        for (std::size_t track_index = 0; track_index < tracks_.size(); ++track_index) {
            const auto& track = tracks_[track_index];
            if (!track.sent ||
                track.value.normalized_text != candidates[candidate_index].normalized_text) {
                continue;
            }
            const auto distance = center_distance(track.value, candidates[candidate_index]);
            if (!closest_track || distance < closest_distance) {
                closest_track = track_index;
                closest_distance = distance;
            }
        }
        if (closest_track) {
            consume_for_sent_track(*closest_track, candidate_index, true);
        }
    }

    for (std::size_t candidate_index = 0; candidate_index < candidates.size();
         ++candidate_index) {
        if (matched_candidates[candidate_index]) {
            continue;
        }

        std::optional<std::size_t> closest_track;
        auto closest_distance = 0.0F;
        for (std::size_t track_index = 0; track_index < tracks_.size(); ++track_index) {
            const auto& track = tracks_[track_index];
            if (!track.sent || !is_fragment(
                                   candidates[candidate_index], track.value,
                                   config_.max_center_distance_px)) {
                continue;
            }
            const auto distance = center_distance(track.value, candidates[candidate_index]);
            if (!closest_track || distance < closest_distance) {
                closest_track = track_index;
                closest_distance = distance;
            }
        }
        if (closest_track) {
            consume_for_sent_track(*closest_track, candidate_index, false);
        }
    }

    std::vector<Match> matches;
    for (std::size_t track_index = 0; track_index < tracks_.size(); ++track_index) {
        if (matched_tracks[track_index]) {
            continue;
        }
        for (std::size_t candidate_index = 0; candidate_index < candidates.size();
             ++candidate_index) {
            if (matched_candidates[candidate_index]) {
                continue;
            }
            if (tracks_[track_index].sent &&
                tracks_[track_index].value.normalized_text !=
                    candidates[candidate_index].normalized_text) {
                continue;
            }
            const auto distance = center_distance(
                tracks_[track_index].value, candidates[candidate_index]);
            if (distance <= config_.max_center_distance_px &&
                compatible_size(tracks_[track_index].value, candidates[candidate_index])) {
                matches.push_back({distance, track_index, candidate_index});
            }
        }
    }
    std::ranges::sort(matches, [](const Match& left, const Match& right) {
        if (left.distance != right.distance) {
            return left.distance < right.distance;
        }
        if (left.track_index != right.track_index) {
            return left.track_index < right.track_index;
        }
        return left.candidate_index < right.candidate_index;
    });

    for (const auto& match : matches) {
        if (matched_tracks[match.track_index] || matched_candidates[match.candidate_index]) {
            continue;
        }
        matched_tracks[match.track_index] = true;
        matched_candidates[match.candidate_index] = true;

        auto& track = tracks_[match.track_index];
        const auto& candidate = candidates[match.candidate_index];
        track.missing_frames = 0;
        if (track.sent) {
            track.value.bounds = candidate.bounds;
        } else if (track.value.normalized_text == candidate.normalized_text) {
            track.value = candidate;
            ++track.seen_frames;
        } else {
            track.value = candidate;
            track.seen_frames = 1;
        }
    }

    for (std::size_t track_index = 0; track_index < tracks_.size(); ++track_index) {
        if (!matched_tracks[track_index]) {
            ++tracks_[track_index].missing_frames;
            if (!tracks_[track_index].sent) {
                tracks_[track_index].seen_frames = 0;
            }
        }
    }
    std::erase_if(tracks_, [&](const Track& track) {
        return track.missing_frames >= config_.unlock_missing_frames;
    });

    for (std::size_t candidate_index = 0; candidate_index < candidates.size();
         ++candidate_index) {
        if (!matched_candidates[candidate_index]) {
            tracks_.push_back({candidates[candidate_index]});
        }
    }

    std::optional<TextCandidate> result;
    for (const auto& track : tracks_) {
        if (track.sent || track.missing_frames != 0 ||
            track.seen_frames < config_.confirm_frames ||
            (result && track.value.bounds.bottom() <= result->bounds.bottom())) {
            continue;
        }
        result = track.value;
    }
    return result;
}

void TargetTracker::mark_sent(const TextCandidate& candidate) {
    Track* closest = nullptr;
    auto closest_distance = config_.max_center_distance_px;
    for (auto& track : tracks_) {
        if (track.sent || track.missing_frames != 0 ||
            track.value.normalized_text != candidate.normalized_text) {
            continue;
        }
        const auto distance = center_distance(track.value, candidate);
        if (distance <= config_.max_center_distance_px &&
            (closest == nullptr || distance < closest_distance)) {
            closest = &track;
            closest_distance = distance;
        }
    }
    if (closest != nullptr) {
        closest->sent = true;
    }
}

}  // namespace dk
