#include "dk/compound_target.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>

namespace dk {
namespace {

using PairKey = std::pair<TrackId, TrackId>;

PairKey pair_key(TrackId left, TrackId right) {
    return std::minmax(left, right);
}

bool expanded_boxes_connect(
    const LineTrackSnapshot& left,
    const LineTrackSnapshot& right) {
    const auto median_height =
        (static_cast<float>(left.value.bounds.height) +
         static_cast<float>(right.value.bounds.height)) *
        0.5F;
    const auto expansion = median_height * 1.5F;
    const auto& left_box = left.value.bounds;
    const auto& right_box = right.value.bounds;
    return static_cast<float>(left_box.x) - expansion <=
               static_cast<float>(right_box.right()) + expansion &&
           static_cast<float>(right_box.x) - expansion <=
               static_cast<float>(left_box.right()) + expansion &&
           static_cast<float>(left_box.y) - expansion <=
               static_cast<float>(right_box.bottom()) + expansion &&
           static_cast<float>(right_box.y) - expansion <=
               static_cast<float>(left_box.bottom()) + expansion;
}

float motion_tolerance(
    const LineTrackSnapshot& left,
    const LineTrackSnapshot& right) {
    const auto median_height =
        (static_cast<float>(left.value.bounds.height) +
         static_cast<float>(right.value.bounds.height)) *
        0.5F;
    return std::max(6.0F, median_height * 0.25F);
}

Box united_bounds(const Box& left, const Box& right) {
    const auto x = std::min(left.x, right.x);
    const auto y = std::min(left.y, right.y);
    const auto right_edge = std::max(left.right(), right.right());
    const auto bottom_edge = std::max(left.bottom(), right.bottom());
    return {x, y, right_edge - x, bottom_edge - y};
}

bool reading_order(
    const LineTrackSnapshot* left,
    const LineTrackSnapshot* right) {
    if (left->value.bounds.y != right->value.bounds.y) {
        return left->value.bounds.y < right->value.bounds.y;
    }
    if (left->value.bounds.x != right->value.bounds.x) {
        return left->value.bounds.x < right->value.bounds.x;
    }
    return left->id < right->id;
}

}  // namespace

std::vector<CompoundTarget> CompoundTargetAssembler::update(
    std::span<const LineTrackSnapshot> lines) {
    std::erase_if(pairs_, [&](const auto& entry) {
        const auto has_id = [&](TrackId id) {
            return std::ranges::find(lines, id, &LineTrackSnapshot::id) !=
                   lines.end();
        };
        return !has_id(entry.first.first) || !has_id(entry.first.second);
    });

    for (std::size_t left_index = 0; left_index < lines.size(); ++left_index) {
        for (std::size_t right_index = left_index + 1;
             right_index < lines.size(); ++right_index) {
            const auto& left = lines[left_index];
            const auto& right = lines[right_index];
            const auto key = pair_key(left.id, right.id);
            auto existing = pairs_.find(key);

            if (!left.observed_this_frame || !right.observed_this_frame) {
                if (existing != pairs_.end() && !existing->second.grouped) {
                    pairs_.erase(existing);
                }
                continue;
            }
            if (!expanded_boxes_connect(left, right)) {
                if (existing != pairs_.end()) {
                    pairs_.erase(existing);
                }
                continue;
            }

            const auto* first = left.id == key.first ? &left : &right;
            const auto* second = left.id == key.first ? &right : &left;
            const auto relative_x =
                second->value.bounds.center_x() -
                first->value.bounds.center_x();
            const auto relative_y =
                second->value.bounds.center_y() -
                first->value.bounds.center_y();

            if (existing == pairs_.end()) {
                pairs_.emplace(key, PairState{
                    .relative_x = relative_x,
                    .relative_y = relative_y,
                    .stable_frames = 1,
                    .provisional = true,
                });
                continue;
            }

            auto& state = existing->second;
            const auto tolerance = motion_tolerance(left, right);
            const auto stable =
                std::abs(relative_x - state.relative_x) <= tolerance &&
                std::abs(relative_y - state.relative_y) <= tolerance;
            state.relative_x = relative_x;
            state.relative_y = relative_y;
            if (stable) {
                ++state.stable_frames;
                if (!state.grouped && state.stable_frames >= 2) {
                    state.provisional = true;
                }
                if (state.ambiguous) {
                    ++state.clean_frames;
                    if (state.clean_frames >= 2) {
                        state.ambiguous = false;
                    }
                }
            } else {
                if (state.grouped) {
                    state.ambiguous = true;
                } else {
                    state.provisional = false;
                }
                state.stable_frames = 1;
                state.clean_frames = 0;
            }
            if (state.stable_frames >= 3) {
                state.grouped = true;
                state.provisional = false;
            }
        }
    }

    std::vector<bool> retained(lines.size());
    for (std::size_t index = 0; index < lines.size(); ++index) {
        retained[index] = lines[index].confirmed;
    }
    for (const auto& [key, state] : pairs_) {
        if (!state.grouped) {
            continue;
        }
        for (std::size_t index = 0; index < lines.size(); ++index) {
            if (lines[index].id == key.first ||
                lines[index].id == key.second) {
                retained[index] = true;
            }
        }
    }

    std::vector<std::size_t> parents(lines.size());
    std::iota(parents.begin(), parents.end(), 0);
    const auto root = [&](std::size_t index) {
        while (parents[index] != index) {
            index = parents[index];
        }
        return index;
    };
    const auto index_for_id = [&](TrackId id) {
        return static_cast<std::size_t>(
            std::ranges::find(lines, id, &LineTrackSnapshot::id) -
            lines.begin());
    };
    for (const auto& [key, state] : pairs_) {
        if (!state.grouped) {
            continue;
        }
        const auto left_root = root(index_for_id(key.first));
        const auto right_root = root(index_for_id(key.second));
        if (left_root != right_root) {
            parents[right_root] = left_root;
        }
    }

    std::vector<CompoundTarget> targets;
    for (std::size_t component = 0; component < lines.size(); ++component) {
        if (!retained[component] || root(component) != component) {
            continue;
        }

        std::vector<const LineTrackSnapshot*> members;
        for (std::size_t index = 0; index < lines.size(); ++index) {
            if (retained[index] && root(index) == component) {
                members.push_back(&lines[index]);
            }
        }
        std::ranges::sort(members, reading_order);

        CompoundTarget target{
            .bounds = members.front()->value.bounds,
            .observed_this_frame = true,
        };
        for (const auto* member : members) {
            target.line_ids.push_back(member->id);
            target.normalized_text += member->value.normalized_text;
            target.bounds = united_bounds(target.bounds, member->value.bounds);
            target.observed_this_frame =
                target.observed_this_frame && member->observed_this_frame;
            target.sent_owned = target.sent_owned || member->sent;
        }
        for (const auto& [key, state] : pairs_) {
            const auto contains = [&](TrackId id) {
                return std::ranges::find(
                           target.line_ids, id) != target.line_ids.end();
            };
            target.ambiguous =
                target.ambiguous ||
                (state.grouped && state.ambiguous &&
                 contains(key.first) && contains(key.second)) ||
                (!state.grouped && state.provisional &&
                 (contains(key.first) || contains(key.second)));
        }
        targets.push_back(std::move(target));
    }
    return targets;
}

}  // namespace dk
