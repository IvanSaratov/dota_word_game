#pragma once
#include <string>

namespace dk {
struct Box {
    int x{};
    int y{};
    int width{};
    int height{};
    [[nodiscard]] int right() const noexcept { return x + width; }
    [[nodiscard]] int bottom() const noexcept { return y + height; }
    [[nodiscard]] float center_x() const noexcept { return x + width * 0.5F; }
    [[nodiscard]] float center_y() const noexcept { return y + height * 0.5F; }
    bool operator==(const Box&) const = default;
};

struct TextCandidate {
    std::string raw_text;
    std::string normalized_text;
    float confidence{};
    Box bounds;
};
}  // namespace dk
