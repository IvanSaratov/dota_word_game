#include "dk/text_normalizer.hpp"

namespace dk {
std::string normalize_for_input(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (const unsigned char ch : text) {
        if (ch >= 'a' && ch <= 'z') result.push_back(static_cast<char>(ch - 'a' + 'A'));
        else if (ch >= 'A' && ch <= 'Z') result.push_back(static_cast<char>(ch));
    }
    return result;
}
}
