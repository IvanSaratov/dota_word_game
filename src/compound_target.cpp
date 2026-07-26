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

bool expanded_boxes_connect(const Box& left, const Box& right) {
    const auto median_height =
        (static_cast<float>(left.height) +
         static_cast<float>(right.height)) *
        0.5F;
    const auto expansion = median_height * 1.5F;
    return static_cast<float>(left.x) - expansion <=
               static_cast<float>(right.right()) + expansion &&
           static_cast<float>(right.x) - expansion <=
               static_cast<float>(left.right()) + expansion &&
           static_cast<float>(left.y) - expansion <=
               static_cast<float>(right.bottom()) + expansion &&
           static_cast<float>(right.y) - expansion <=
               static_cast<float>(left.bottom()) + expansion;
}

bool expanded_boxes_connect(
    const LineTrackSnapshot& left,
    const LineTrackSnapshot& right) {
    return expanded_boxes_connect(left.value.bounds, right.value.bounds);
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
    std::erase_if(text_by_id_, [&](const auto& entry) {
        return std::ranges::find(lines, entry.first, &LineTrackSnapshot::id) ==
               lines.end();
    });
    const auto id_is_present = [&](TrackId id) {
        return std::ranges::find(lines, id, &LineTrackSnapshot::id) !=
               lines.end();
    };
    for (auto& [key, episode] : ambiguity_episodes_) {
        if (!id_is_present(key.first) || !id_is_present(key.second)) {
            episode.active = false;
        }
        for (auto replacement = episode.replacement_texts.begin();
             replacement != episode.replacement_texts.end();) {
            const auto line = std::ranges::find(
                lines, replacement->first, &LineTrackSnapshot::id);
            if (line == lines.end()) {
                replacement =
                    episode.replacement_texts.erase(replacement);
            } else if (
                line->observed_this_frame &&
                line->value.normalized_text != replacement->second) {
                if (episode.active) {
                    replacement->second = line->value.normalized_text;
                    ++replacement;
                } else {
                    replacement =
                        episode.replacement_texts.erase(replacement);
                }
            } else {
                ++replacement;
            }
        }
    }
    std::erase_if(ambiguity_episodes_, [](const auto& entry) {
        return !entry.second.active &&
               entry.second.replacement_texts.empty();
    });
    std::erase_if(sent_compounds_, [&](auto& sent) {
        std::erase_if(sent.line_ids, [&](TrackId id) {
            return !id_is_present(id);
        });
        std::erase_if(sent.released_aliases, [&](const auto& alias) {
            return !id_is_present(alias.first);
        });
        for (const auto& line : lines) {
            if (line.observed_this_frame && !line.sent &&
                sent.line_ids.contains(line.id) &&
                line.value.normalized_text != sent.normalized_text) {
                sent.line_ids.erase(line.id);
                sent.released_aliases.insert_or_assign(
                    line.id, line.value.normalized_text);
            }
        }
        if (sent.line_ids.empty()) {
            return true;
        }

        bool has_observed_member = false;
        Box current_bounds{};
        for (const auto& line : lines) {
            if (!line.observed_this_frame ||
                !sent.line_ids.contains(line.id)) {
                continue;
            }
            current_bounds = has_observed_member
                ? united_bounds(current_bounds, line.value.bounds)
                : line.value.bounds;
            has_observed_member = true;
        }
        if (has_observed_member) {
            sent.bounds = current_bounds;
        }
        for (const auto& line : lines) {
            if (line.observed_this_frame &&
                line.value.normalized_text == sent.normalized_text &&
                expanded_boxes_connect(line.value.bounds, sent.bounds)) {
                sent.line_ids.insert(line.id);
                sent.released_aliases.erase(line.id);
            }
        }
        return false;
    });

    std::vector<TrackId> text_changes;
    for (const auto& line : lines) {
        if (!line.observed_this_frame) {
            continue;
        }
        const auto [text, inserted] =
            text_by_id_.try_emplace(line.id, line.value.normalized_text);
        if (!inserted && text->second != line.value.normalized_text) {
            text->second = line.value.normalized_text;
            text_changes.push_back(line.id);
        }
    }

    std::erase_if(pairs_, [&](const auto& entry) {
        return !id_is_present(entry.first.first) ||
               !id_is_present(entry.first.second);
    });

    for (const auto& entry : pairs_) {
        const auto& key = entry.first;
        const auto left =
            std::ranges::find(lines, key.first, &LineTrackSnapshot::id);
        const auto right =
            std::ranges::find(lines, key.second, &LineTrackSnapshot::id);
        if (left->observed_this_frame && right->observed_this_frame) {
            continue;
        }
        auto& episode = ambiguity_episodes_[key];
        episode.active = true;
        episode.clean_frames = 0;
    }

    const auto sent_owned_id = [&](TrackId id) {
        return std::ranges::any_of(
            sent_compounds_, [&](const auto& sent) {
                return sent.line_ids.contains(id);
            });
    };
    const auto released_sent_alias = [&](TrackId id) {
        return std::ranges::any_of(
            sent_compounds_, [&](const auto& sent) {
                return sent.released_aliases.contains(id);
            });
    };
    for (auto& [key, episode] : ambiguity_episodes_) {
        if (!episode.active) {
            continue;
        }
        const auto left =
            std::ranges::find(lines, key.first, &LineTrackSnapshot::id);
        const auto right =
            std::ranges::find(lines, key.second, &LineTrackSnapshot::id);
        if (left == lines.end() || right == lines.end()) {
            episode.active = false;
            continue;
        }
        if (left->observed_this_frame && right->observed_this_frame) {
            ++episode.clean_frames;
            if (episode.clean_frames >= 2) {
                episode.active = false;
            }
            continue;
        }
        episode.clean_frames = 0;
        for (const auto& line : lines) {
            if (!line.observed_this_frame ||
                line.id == key.first || line.id == key.second ||
                sent_owned_id(line.id) ||
                released_sent_alias(line.id)) {
                continue;
            }
            const auto overlaps_missing_member =
                (!left->observed_this_frame &&
                 expanded_boxes_connect(line, *left)) ||
                (!right->observed_this_frame &&
                 expanded_boxes_connect(line, *right));
            if (!overlaps_missing_member) {
                continue;
            }
            episode.replacement_texts.insert_or_assign(
                line.id, line.value.normalized_text);
        }
    }

    for (std::size_t left_index = 0; left_index < lines.size(); ++left_index) {
        for (std::size_t right_index = left_index + 1;
             right_index < lines.size(); ++right_index) {
            const auto& left = lines[left_index];
            const auto& right = lines[right_index];
            const auto key = pair_key(left.id, right.id);
            auto existing = pairs_.find(key);

            if (!left.observed_this_frame || !right.observed_this_frame) {
                if (existing != pairs_.end()) {
                    auto& state = existing->second;
                    state.ambiguous = true;
                    state.clean_frames = 0;
                    state.disconnected_frames = 0;
                    state.provisional = true;
                    ++state.missing_frames;
                    if (!state.grouped && state.missing_frames >= 2) {
                        pairs_.erase(existing);
                    }
                }
                continue;
            }
            if (!expanded_boxes_connect(left, right)) {
                if (existing != pairs_.end()) {
                    auto& state = existing->second;
                    state.missing_frames = 0;
                    if (state.grouped) {
                        state.ambiguous = true;
                        state.clean_frames = 0;
                        ++state.disconnected_frames;
                        if (state.disconnected_frames >= 2) {
                            pairs_.erase(existing);
                        }
                    } else {
                        pairs_.erase(existing);
                    }
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
            state.missing_frames = 0;
            state.disconnected_frames = 0;
            const auto text_changed =
                std::ranges::find(text_changes, left.id) != text_changes.end() ||
                std::ranges::find(text_changes, right.id) != text_changes.end();
            if (text_changed) {
                state.ambiguous = true;
                state.clean_frames = 0;
            }
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
                if (state.ambiguous && !text_changed) {
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

    for (const auto& [key, state] : pairs_) {
        if (!state.grouped || state.ambiguous) {
            continue;
        }
        const auto left =
            std::ranges::find(lines, key.first, &LineTrackSnapshot::id);
        const auto right =
            std::ranges::find(lines, key.second, &LineTrackSnapshot::id);
        if (left->observed_this_frame && right->observed_this_frame) {
            for (auto& [cause, episode] : ambiguity_episodes_) {
                if (cause == key) {
                    episode.active = false;
                }
                if (episode.replacement_texts.contains(key.first) &&
                    episode.replacement_texts.contains(key.second)) {
                    episode.replacement_texts.erase(key.first);
                    episode.replacement_texts.erase(key.second);
                    episode.active = false;
                }
            }
        }
    }
    std::erase_if(ambiguity_episodes_, [](const auto& entry) {
        return !entry.second.active &&
               entry.second.replacement_texts.empty();
    });

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
            target.sent_owned =
                target.sent_owned || member->sent ||
                std::ranges::any_of(
                    sent_compounds_, [&](const auto& sent) {
                        return sent.line_ids.contains(member->id);
                    });
            target.ambiguous =
                target.ambiguous ||
                std::ranges::any_of(
                    ambiguity_episodes_, [&](const auto& episode) {
                        return episode.second.replacement_texts.contains(
                            member->id);
                    });
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

void CompoundTargetAssembler::mark_sent(const CompoundTarget& target) {
    sent_compounds_.push_back({
        .line_ids = std::set<TrackId>(
            target.line_ids.begin(), target.line_ids.end()),
        .normalized_text = target.normalized_text,
        .bounds = target.bounds,
    });
}

}  // namespace dk
