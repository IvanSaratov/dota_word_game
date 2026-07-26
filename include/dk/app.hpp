#pragma once

#include <optional>

#include "dk/candidate_detector.hpp"
#include "dk/config.hpp"
#include "dk/delay.hpp"
#include "dk/frame_source.hpp"
#include "dk/input_sink.hpp"
#include "dk/metrics.hpp"
#include "dk/ocr_recognizer.hpp"
#include "dk/target_tracker.hpp"

namespace dk {

class App {
public:
    App(const AppConfig& config, FrameSource& frames, CandidateDetector& detector,
        LineRecognizer& recognizer, InputSink& input,
        CancellationPredicate cancellation = {},
        DelayFunction delay = interruptible_delay);
    bool process_one_frame();
    [[nodiscard]] const std::optional<TextCandidate>& last_result() const noexcept;
    [[nodiscard]] const LatencyMetrics& metrics() const noexcept;

private:
    AppConfig config_;
    FrameSource& frames_;
    CandidateDetector& detector_;
    LineRecognizer& recognizer_;
    InputSink& input_;
    CancellationPredicate cancellation_;
    DelayFunction delay_;
    TargetTracker tracker_;
    LatencyMetrics metrics_;
    std::optional<TextCandidate> last_result_;
};

}  // namespace dk
