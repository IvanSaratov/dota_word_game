#pragma once
#include <string>
#include <string_view>

namespace dk {
[[nodiscard]] std::string normalize_for_input(std::string_view text);
}
