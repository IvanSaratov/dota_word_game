#include "dk/app.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

#include "dk/target_scheduler.hpp"
#include "dk/text_normalizer.hpp"

namespace dk {
namespace {

using Clock = std::chrono::steady_clock;

constexpr std::size_t kMinimumNormalizedLength = 2;

const char* status_name(SendStatus status) noexcept {
    switch (status) {
        case SendStatus::sent: return "sent";
        case SendStatus::not_foreground: return "not_foreground";
        case SendStatus::invalid_text: return "invalid_text";
        case SendStatus::cancelled: return "cancelled";
        case SendStatus::blocked: return "blocked";
        case SendStatus::partial: return "partial";
    }
    return "unknown";
}

Box clip_to_frame(const Box& box, const cv::Mat& frame) noexcept {
    const int left = std::clamp(box.x, 0, frame.cols);
    const int top = std::clamp(box.y, 0, frame.rows);
    const int right = std::clamp(box.right(), 0, frame.cols);
    const int bottom = std::clamp(box.bottom(), 0, frame.rows);
    return {left, top, std::max(0, right - left), std::max(0, bottom - top)};
}

double milliseconds(Clock::duration elapsed) {
    return std::chrono::duration<double, std::milli>(elapsed).count();
}

std::optional<TextCandidate> revalidate(
    const CompoundTarget& target,
    std::span<const LineTrackSnapshot> lines) {
    if (target.line_ids.empty()) {
        return std::nullopt;
    }

    TextCandidate combined{
        .confidence = std::numeric_limits<float>::max(),
        .bounds = target.bounds,
    };
    for (const auto id : target.line_ids) {
        const auto line =
            std::ranges::find(lines, id, &LineTrackSnapshot::id);
        if (line == lines.end() || !line->confirmed ||
            !line->observed_this_frame || line->sent) {
            return std::nullopt;
        }
        if (!combined.raw_text.empty()) {
            combined.raw_text += '\n';
        }
        combined.raw_text += line->value.raw_text;
        combined.normalized_text += line->value.normalized_text;
        combined.confidence =
            std::min(combined.confidence, line->value.confidence);
    }
    if (combined.normalized_text != target.normalized_text) {
        return std::nullopt;
    }
    return combined;
}

}  // namespace

App::App(const AppConfig& config, FrameSource& frames, CandidateDetector& detector,
         LineRecognizer& recognizer, InputSink& input,
         CancellationPredicate cancellation, DelayFunction delay)
    : config_(config),
      frames_(frames),
      detector_(detector),
      recognizer_(recognizer),
      input_(input),
      cancellation_(std::move(cancellation)),
      delay_(std::move(delay)),
      tracker_(config.tracker) {}

bool App::process_one_frame() {
    last_result_.reset();
    const auto total_start = Clock::now();
    const auto capture_start = Clock::now();
    auto frame = frames_.next_frame();
    const auto capture_end = Clock::now();
    if (!frame) {
        return true;
    }

    const auto detect_start = Clock::now();
    const auto boxes = detector_.detect(frame->bgra);
    const auto detect_end = Clock::now();

    std::vector<TextCandidate> candidates;
    candidates.reserve(boxes.size());
    const auto ocr_start = Clock::now();
    for (const auto& box : boxes) {
        const Box selected_region_box = clip_to_frame(box, frame->bgra);
        if (selected_region_box.width == 0 || selected_region_box.height == 0) {
            continue;
        }

        const cv::Mat crop = frame->bgra(cv::Rect{
            selected_region_box.x,
            selected_region_box.y,
            selected_region_box.width,
            selected_region_box.height,
        });
        const auto recognized = recognizer_.recognize(crop);
        auto normalized = normalize_for_input(recognized.text);
        std::clog << "OCR raw=\"" << recognized.text << "\" normalized=\""
                  << normalized << "\" confidence=" << std::fixed
                  << std::setprecision(3) << recognized.confidence << " box=("
                  << selected_region_box.x << ',' << selected_region_box.y << ','
                  << selected_region_box.width << ',' << selected_region_box.height
                  << ")\n";
        if (normalized.size() < kMinimumNormalizedLength ||
            recognized.confidence < config_.min_ocr_confidence) {
            continue;
        }
        candidates.push_back({
            recognized.text,
            std::move(normalized),
            recognized.confidence,
            selected_region_box,
        });
    }
    const auto ocr_end = Clock::now();

    const auto tracker_frame = tracker_.update_lines(candidates);
    const auto targets = assembler_.update(tracker_frame.lines);
    const auto selected = select_lowest_ready(targets);
    bool keep_running = true;
    if (selected) {
        const auto combined = revalidate(*selected, tracker_frame.lines);
        if (combined) {
            last_result_ = combined;
            if (cancellation_ && cancellation_()) {
                std::clog << "Input cancelled before dispatch for "
                          << combined->normalized_text << '\n';
            } else if (!config_.live_input) {
                std::clog << "[DRY] would type " << combined->normalized_text
                          << '\n';
                assembler_.mark_sent(*selected);
                tracker_.mark_sent(selected->line_ids);
                delay_(
                    std::chrono::milliseconds{config_.post_send_delay_ms},
                    cancellation_);
            } else {
                const auto status =
                    input_.send_letters(combined->normalized_text);
                std::clog << "Input " << status_name(status) << " for "
                          << combined->normalized_text << '\n';
                if (status == SendStatus::sent) {
                    assembler_.mark_sent(*selected);
                    tracker_.mark_sent(selected->line_ids);
                    delay_(
                        std::chrono::milliseconds{config_.post_send_delay_ms},
                        cancellation_);
                } else if (status == SendStatus::cancelled) {
                    std::clog
                        << "Input cancellation is nonfatal; processing state "
                           "will be consumed by the main loop.\n";
                } else if (status == SendStatus::blocked ||
                           status == SendStatus::partial) {
                    keep_running = false;
                }
            }
        }
    }

    const auto total_end = Clock::now();
    metrics_.record(LatencyStage::capture, capture_end - capture_start);
    metrics_.record(LatencyStage::detect, detect_end - detect_start);
    metrics_.record(LatencyStage::ocr, ocr_end - ocr_start);
    metrics_.record(LatencyStage::total, total_end - total_start);
    std::clog << "Timing capture=" << milliseconds(capture_end - capture_start)
              << "ms detect=" << milliseconds(detect_end - detect_start)
              << "ms ocr=" << milliseconds(ocr_end - ocr_start)
              << "ms total=" << milliseconds(total_end - total_start) << "ms\n";
    return keep_running;
}

const std::optional<TextCandidate>& App::last_result() const noexcept {
    return last_result_;
}

const LatencyMetrics& App::metrics() const noexcept {
    return metrics_;
}

}  // namespace dk
