#include "dk/ctc_decoder.hpp"

#include <cstddef>
#include <stdexcept>

namespace dk {

CtcResult decode_ctc(
    const std::span<const int64_t> class_ids,
    const std::span<const float> class_scores,
    const std::span<const std::string> dictionary,
    const int64_t blank_id) {
    if (class_ids.size() != class_scores.size()) {
        throw std::invalid_argument("CTC class id and score counts differ");
    }

    CtcResult result;
    double score_sum = 0.0;
    std::size_t emitted_count = 0;
    int64_t previous_id = 0;
    bool has_previous = false;

    for (std::size_t index = 0; index < class_ids.size(); ++index) {
        const int64_t class_id = class_ids[index];
        const bool repeated = has_previous && class_id == previous_id;
        previous_id = class_id;
        has_previous = true;

        if (class_id == blank_id) {
            continue;
        }
        if (class_id < 0 || static_cast<std::uint64_t>(class_id) >= dictionary.size()) {
            throw std::out_of_range("CTC class id is outside the dictionary");
        }
        if (repeated) {
            continue;
        }

        result.text += dictionary[static_cast<std::size_t>(class_id)];
        score_sum += class_scores[index];
        ++emitted_count;
    }

    if (emitted_count != 0) {
        result.confidence = static_cast<float>(score_sum / static_cast<double>(emitted_count));
    }
    return result;
}

}  // namespace dk
