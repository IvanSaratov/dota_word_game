#pragma once

#include <chrono>
#include <optional>

#include <opencv2/core.hpp>

namespace dk {

struct CapturedFrame {
    cv::Mat bgra;
    std::chrono::steady_clock::time_point captured_at;
};

class FrameSource {
public:
    virtual ~FrameSource() = default;
    [[nodiscard]] virtual std::optional<CapturedFrame> next_frame() = 0;
};

}  // namespace dk
