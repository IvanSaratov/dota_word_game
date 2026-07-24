#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace dk {

struct CtcResult {
    std::string text;
    float confidence{};
};

[[nodiscard]] CtcResult decode_ctc(
    std::span<const int64_t> class_ids,
    std::span<const float> class_scores,
    std::span<const std::string> dictionary,
    int64_t blank_id = 0);

}  // namespace dk
