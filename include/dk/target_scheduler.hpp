#pragma once

#include <optional>
#include <span>

#include "dk/compound_target.hpp"

namespace dk {

[[nodiscard]] std::optional<CompoundTarget> select_lowest_ready(
    std::span<const CompoundTarget> targets);

}  // namespace dk
