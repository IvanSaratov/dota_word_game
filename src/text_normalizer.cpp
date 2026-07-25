#include "dk/text_normalizer.hpp"

namespace {

bool contains_common_cyrillic(std::string_view text) noexcept {
    for (std::size_t index = 0; index + 1 < text.size(); ++index) {
        const auto first = static_cast<unsigned char>(text[index]);
        const auto second = static_cast<unsigned char>(text[index + 1]);
        if (first >= 0xD0 && first <= 0xD3 &&
            second >= 0x80 && second <= 0xBF) {
            return true;
        }
    }
    return false;
}

}  // namespace

namespace dk {
std::string normalize_for_input(std::string_view text) {
    if (contains_common_cyrillic(text)) {
        return {};
    }

    std::string result;
    result.reserve(text.size());
    for (const unsigned char ch : text) {
        if (ch >= 'a' && ch <= 'z') result.push_back(static_cast<char>(ch - 'a' + 'A'));
        else if (ch >= 'A' && ch <= 'Z') result.push_back(static_cast<char>(ch));
    }
    return result;
}
}
