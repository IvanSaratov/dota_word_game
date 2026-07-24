#pragma once

#include <memory>
#include <optional>

#include "dk/frame_source.hpp"
#include "dk/types.hpp"

namespace dk {

class DxgiCapture final : public FrameSource {
public:
    explicit DxgiCapture(Box screen_region);
    ~DxgiCapture() override;

    DxgiCapture(const DxgiCapture&) = delete;
    DxgiCapture& operator=(const DxgiCapture&) = delete;

    [[nodiscard]] std::optional<CapturedFrame> next_frame() override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace dk
