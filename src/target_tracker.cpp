#include "dk/target_tracker.hpp"

#include <algorithm>
#include <cmath>

namespace dk {
namespace {

bool matches(const TextCandidate& left, const TextCandidate& right, float maximum_distance) {
    if (left.normalized_text != right.normalized_text) {
        return false;
    }

    return std::hypot(left.bounds.center_x() - right.bounds.center_x(),
                      left.bounds.center_y() - right.bounds.center_y()) <= maximum_distance;
}

}  // namespace

TargetTracker::TargetTracker(TrackerConfig config) : config_(config) {}

std::optional<TextCandidate> TargetTracker::update(std::span<const TextCandidate> candidates) {
    for (auto& lock : locks_) {
        const bool present = std::any_of(candidates.begin(), candidates.end(), [&](const auto& candidate) {
            return matches(lock.value, candidate, config_.max_center_distance_px);
        });
        lock.missing_frames = present ? 0 : lock.missing_frames + 1;
    }
    std::erase_if(locks_, [&](const Lock& lock) {
        return lock.missing_frames >= config_.unlock_missing_frames;
    });

    std::vector<Track> current;
    current.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        const auto previous = std::find_if(previous_.begin(), previous_.end(), [&](const Track& track) {
            return matches(track.value, candidate, config_.max_center_distance_px);
        });
        current.push_back({candidate, previous == previous_.end() ? 1 : previous->seen_frames + 1});
    }
    previous_ = current;

    std::optional<TextCandidate> result;
    for (const auto& track : current) {
        const bool locked = std::any_of(locks_.begin(), locks_.end(), [&](const Lock& lock) {
            return matches(lock.value, track.value, config_.max_center_distance_px);
        });
        if (locked || track.seen_frames < config_.confirm_frames ||
            (result && track.value.bounds.bottom() <= result->bounds.bottom())) {
            continue;
        }
        result = track.value;
    }
    return result;
}

void TargetTracker::mark_sent(const TextCandidate& candidate) {
    locks_.push_back({candidate, 0});
}

}  // namespace dk
