#pragma once

#include <string_view>

namespace dk {

enum class SendStatus { sent, not_foreground, invalid_text, blocked, partial };

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

}  // namespace dk
