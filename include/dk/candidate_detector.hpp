#pragma once

#include <vector>

#include <opencv2/core.hpp>

#include "dk/types.hpp"

namespace dk {
struct DetectorConfig {
    int luminance_threshold{145};
    float min_char_height_ratio{0.025F};
    float max_char_height_ratio{0.075F};
    float baseline_tolerance_ratio{0.30F};
    float max_gap_ratio{1.50F};
    int min_components_per_line{3};
    int crop_padding_px{6};
    std::vector<cv::Rect2f> ignored_regions;
};

class CandidateDetector {
public:
    explicit CandidateDetector(DetectorConfig config = {});
    [[nodiscard]] virtual std::vector<Box> detect(const cv::Mat& frame) const;
    virtual ~CandidateDetector() = default;

private:
    DetectorConfig config_;
};
}  // namespace dk
