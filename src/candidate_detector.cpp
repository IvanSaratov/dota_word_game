#include "dk/candidate_detector.hpp"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <vector>

#include <opencv2/imgproc.hpp>

namespace dk {
namespace {
struct Component {
    Box bounds;

    [[nodiscard]] float center_y() const noexcept {
        return bounds.center_y();
    }
};

[[nodiscard]] float median_height(const std::vector<Component>& components) {
    std::vector<int> heights;
    heights.reserve(components.size());
    for (const auto& component : components) {
        heights.push_back(component.bounds.height);
    }
    const auto middle = heights.begin() + static_cast<std::ptrdiff_t>(heights.size() / 2);
    std::nth_element(heights.begin(), middle, heights.end());
    return static_cast<float>(*middle);
}

[[nodiscard]] float median_center_y(const std::vector<Component>& components) {
    std::vector<float> centers;
    centers.reserve(components.size());
    for (const auto& component : components) {
        centers.push_back(component.center_y());
    }
    const auto middle = centers.begin() + static_cast<std::ptrdiff_t>(centers.size() / 2);
    std::nth_element(centers.begin(), middle, centers.end());
    return *middle;
}

void mask_ignored_regions(cv::Mat& gray,
                          const std::vector<cv::Rect2f>& ignored_regions) {
    for (const auto& normalized : ignored_regions) {
        const int left = std::clamp(
            static_cast<int>(std::floor(normalized.x * gray.cols)), 0, gray.cols);
        const int top = std::clamp(
            static_cast<int>(std::floor(normalized.y * gray.rows)), 0, gray.rows);
        const int right = std::clamp(
            static_cast<int>(std::ceil((normalized.x + normalized.width) * gray.cols)),
            0, gray.cols);
        const int bottom = std::clamp(
            static_cast<int>(std::ceil((normalized.y + normalized.height) * gray.rows)),
            0, gray.rows);

        if (right > left && bottom > top) {
            gray(cv::Rect{left, top, right - left, bottom - top}).setTo(0);
        }
    }
}

[[nodiscard]] Box merge_with_padding(const std::vector<Component>& components,
                                     int padding, int frame_width,
                                     int frame_height) {
    int left = frame_width;
    int top = frame_height;
    int right = 0;
    int bottom = 0;
    for (const auto& component : components) {
        left = std::min(left, component.bounds.x);
        top = std::min(top, component.bounds.y);
        right = std::max(right, component.bounds.right());
        bottom = std::max(bottom, component.bounds.bottom());
    }

    left = std::max(0, left - padding);
    top = std::max(0, top - padding);
    right = std::min(frame_width, right + padding);
    bottom = std::min(frame_height, bottom + padding);
    return Box{left, top, right - left, bottom - top};
}
}  // namespace

CandidateDetector::CandidateDetector(DetectorConfig config)
    : config_(std::move(config)) {}

std::vector<Box> CandidateDetector::detect(const cv::Mat& frame) const {
    if (frame.empty()) {
        return {};
    }
    if (frame.depth() != CV_8U) {
        throw std::invalid_argument("CandidateDetector expects an 8-bit frame");
    }

    cv::Mat gray;
    switch (frame.channels()) {
        case 1:
            gray = frame;
            break;
        case 3:
            cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
            break;
        case 4:
            cv::cvtColor(frame, gray, cv::COLOR_BGRA2GRAY);
            break;
        default:
            throw std::invalid_argument(
                "CandidateDetector expects a grayscale, BGR, or BGRA frame");
    }

    if (frame.channels() == 1 && !config_.ignored_regions.empty()) {
        gray = gray.clone();
    }
    mask_ignored_regions(gray, config_.ignored_regions);

    cv::Mat mask;
    cv::threshold(gray, mask, config_.luminance_threshold, 255, cv::THRESH_BINARY);

    cv::Mat labels;
    cv::Mat stats;
    cv::Mat centroids;
    const int component_count =
        cv::connectedComponentsWithStats(mask, labels, stats, centroids);

    const float min_height = config_.min_char_height_ratio * frame.rows;
    const float max_height = config_.max_char_height_ratio * frame.rows;
    std::vector<Component> components;
    components.reserve(static_cast<std::size_t>(component_count - 1));
    for (int label = 1; label < component_count; ++label) {
        const int width = stats.at<int>(label, cv::CC_STAT_WIDTH);
        const int height = stats.at<int>(label, cv::CC_STAT_HEIGHT);
        const int area = stats.at<int>(label, cv::CC_STAT_AREA);
        if (height < min_height || height > max_height ||
            static_cast<float>(width) > 1.4F * height ||
            static_cast<float>(area) < 0.06F * width * height) {
            continue;
        }

        components.push_back(
            Component{Box{stats.at<int>(label, cv::CC_STAT_LEFT),
                          stats.at<int>(label, cv::CC_STAT_TOP), width, height}});
    }

    std::ranges::sort(components, [](const Component& lhs, const Component& rhs) {
        if (lhs.center_y() != rhs.center_y()) {
            return lhs.center_y() < rhs.center_y();
        }
        return lhs.bounds.x < rhs.bounds.x;
    });

    std::vector<std::vector<Component>> lines;
    for (const auto& component : components) {
        auto line = std::ranges::find_if(
            lines | std::views::reverse, [&component, this](const auto& candidate) {
                const float height = median_height(candidate);
                return std::abs(component.center_y() - median_center_y(candidate)) <=
                       config_.baseline_tolerance_ratio * height;
            });
        if (line == lines.rend()) {
            lines.push_back({component});
        } else {
            line->push_back(component);
        }
    }

    std::vector<Box> boxes;
    for (auto& line : lines) {
        std::ranges::sort(line, [](const Component& lhs, const Component& rhs) {
            return lhs.bounds.x < rhs.bounds.x;
        });

        const float height = median_height(line);
        std::vector<Component> group;
        for (const auto& component : line) {
            if (!group.empty()) {
                const int gap = component.bounds.x - group.back().bounds.right();
                if (gap > config_.max_gap_ratio * height) {
                    if (static_cast<int>(group.size()) >=
                        config_.min_components_per_line) {
                        boxes.push_back(merge_with_padding(
                            group, config_.crop_padding_px, frame.cols, frame.rows));
                    }
                    group.clear();
                }
            }
            group.push_back(component);
        }
        if (static_cast<int>(group.size()) >= config_.min_components_per_line) {
            boxes.push_back(merge_with_padding(
                group, config_.crop_padding_px, frame.cols, frame.rows));
        }
    }

    std::ranges::sort(boxes, [](const Box& lhs, const Box& rhs) {
        if (lhs.bottom() != rhs.bottom()) {
            return lhs.bottom() > rhs.bottom();
        }
        return lhs.x < rhs.x;
    });
    return boxes;
}
}  // namespace dk
