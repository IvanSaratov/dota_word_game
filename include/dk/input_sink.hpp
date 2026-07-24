#pragma once

#include <cstddef>
#include <functional>
#include <string_view>

namespace dk {

enum class SendStatus { sent, not_foreground, invalid_text, blocked, partial };
using CancellationPredicate = std::function<bool()>;

class InputSink {
public:
    virtual ~InputSink() = default;
    virtual SendStatus send_letters(std::string_view letters) = 0;
};

[[nodiscard]] constexpr bool validate_send_text(std::string_view text) {
    if (text.empty()) {
        return false;
    }

    for (const char character : text) {
        if (character < 'A' || character > 'Z') {
            return false;
        }
    }
    return true;
}

[[nodiscard]] constexpr SendStatus interrupted_send_status(
    std::size_t sent_pairs, SendStatus before_send_status) noexcept {
    return sent_pairs == 0 ? before_send_status : SendStatus::partial;
}

}  // namespace dk
