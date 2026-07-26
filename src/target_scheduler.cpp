#include "dk/target_scheduler.hpp"

namespace dk {
namespace {

bool ready(const CompoundTarget& target) {
    return !target.ambiguous && target.observed_this_frame &&
           !target.normalized_text.empty() && !target.sent_owned;
}

bool lower_priority_before(
    const CompoundTarget& candidate,
    const CompoundTarget& selected) {
    if (candidate.bounds.bottom() != selected.bounds.bottom()) {
        return candidate.bounds.bottom() > selected.bounds.bottom();
    }
    if (candidate.bounds.center_y() != selected.bounds.center_y()) {
        return candidate.bounds.center_y() > selected.bounds.center_y();
    }
    return candidate.line_ids < selected.line_ids;
}

}  // namespace

std::optional<CompoundTarget> select_lowest_ready(
    std::span<const CompoundTarget> targets) {
    const CompoundTarget* selected = nullptr;
    for (const auto& target : targets) {
        if (ready(target) &&
            (selected == nullptr ||
             lower_priority_before(target, *selected))) {
            selected = &target;
        }
    }
    if (selected == nullptr) {
        return std::nullopt;
    }
    return *selected;
}

}  // namespace dk
