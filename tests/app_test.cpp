#include <chrono>
#include <atomic>
#include <cstddef>
#include <deque>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dk/app.hpp"

namespace {

using namespace std::chrono_literals;

bool complete_delay(
    std::chrono::milliseconds,
    const dk::CancellationPredicate&) {
    return true;
}

class FakeFrameSource final : public dk::FrameSource {
public:
    explicit FakeFrameSource(int count) : remaining_(count) {}

    std::optional<dk::CapturedFrame> next_frame() override {
        if (remaining_-- <= 0) {
            return std::nullopt;
        }
        ++returned_frames;
        return dk::CapturedFrame{
            cv::Mat(720, 1000, CV_8UC4, cv::Scalar{}),
            std::chrono::steady_clock::now(),
        };
    }

    int returned_frames{};

private:
    int remaining_;
};

class FakeDetector final : public dk::CandidateDetector {
public:
    explicit FakeDetector(std::vector<dk::Box> boxes) : boxes_(std::move(boxes)) {}

    std::vector<dk::Box> detect(const cv::Mat&) const override {
        return boxes_;
    }

private:
    std::vector<dk::Box> boxes_;
};

class SequencedDetector final : public dk::CandidateDetector {
public:
    explicit SequencedDetector(std::deque<std::vector<dk::Box>> frames)
        : frames_(std::move(frames)) {}

    std::vector<dk::Box> detect(const cv::Mat&) const override {
        REQUIRE_FALSE(frames_.empty());
        auto boxes = frames_.front();
        frames_.pop_front();
        return boxes;
    }

private:
    mutable std::deque<std::vector<dk::Box>> frames_;
};

class FakeRecognizer final : public dk::LineRecognizer {
public:
    explicit FakeRecognizer(std::deque<dk::OcrResult> results)
        : results_(std::move(results)) {}

    dk::OcrResult recognize(const cv::Mat& crop) override {
        REQUIRE_FALSE(results_.empty());
        crop_sizes.emplace_back(crop.cols, crop.rows);
        auto result = results_.front();
        results_.pop_front();
        if (after_recognize) {
            after_recognize();
        }
        return result;
    }

    std::function<void()> after_recognize;
    std::vector<std::pair<int, int>> crop_sizes;

private:
    std::deque<dk::OcrResult> results_;
};

class FakeInputSink final : public dk::InputSink {
public:
    explicit FakeInputSink(dk::SendStatus status = dk::SendStatus::sent)
        : status_(status) {}

    dk::SendStatus send_letters(std::string_view text) override {
        sent.emplace_back(text);
        return status_;
    }

    std::vector<std::string> sent;

private:
    dk::SendStatus status_;
};

}  // namespace

TEST_CASE("live app sends one confirmed target only once while it remains visible") {
    auto config = dk::AppConfig::defaults();
    config.live_input = true;
    FakeFrameSource frames{4};
    FakeDetector detector{{{80, 500, 330, 48}}};
    FakeRecognizer recognizer{{
        {"ROCK-'N'-ROLL", .96F},
        {"ROCK-'N'-ROLL", .96F},
        {"ROCK-'N'-ROLL", .96F},
        {"ROCK-'N'-ROLL", .96F},
    }};
    FakeInputSink input;
    dk::App app(config, frames, detector, recognizer, input, {}, complete_delay);

    CHECK(app.process_one_frame());
    CHECK(app.process_one_frame());
    REQUIRE(app.last_result());
    CHECK((app.last_result()->bounds == dk::Box{80, 500, 330, 48}));
    CHECK(app.process_one_frame());
    CHECK(app.process_one_frame());

    REQUIRE(input.sent.size() == 1);
    CHECK(input.sent.front() == "ROCKNROLL");
    CHECK_FALSE(app.last_result());
    CHECK((recognizer.crop_sizes.front() == std::pair{330, 48}));
}

TEST_CASE("dry run recognizes and locks without sending") {
    auto config = dk::AppConfig::defaults();
    config.live_input = false;
    FakeFrameSource frames{3};
    FakeDetector detector{{{50, 300, 300, 48}}};
    FakeRecognizer recognizer{{
        {"HYPERSTONE", .97F},
        {"HYPERSTONE", .97F},
        {"HYPERSTONE", .97F},
    }};
    FakeInputSink input;
    dk::App app(config, frames, detector, recognizer, input, {}, complete_delay);

    CHECK(app.process_one_frame());
    CHECK(app.process_one_frame());
    REQUIRE(app.last_result());
    CHECK(app.last_result()->normalized_text == "HYPERSTONE");
    CHECK(app.process_one_frame());

    CHECK(input.sent.empty());
    CHECK_FALSE(app.last_result());
}

TEST_CASE("app rejects empty and low confidence recognition") {
    auto config = dk::AppConfig::defaults();
    config.live_input = true;
    config.min_ocr_confidence = .90F;
    FakeFrameSource frames{2};
    FakeDetector detector{{{10, 20, 100, 30}, {20, 60, 120, 30}}};
    FakeRecognizer recognizer{{
        {"---", .99F}, {"VALID", .50F},
        {"---", .99F}, {"VALID", .50F},
    }};
    FakeInputSink input;
    dk::App app(config, frames, detector, recognizer, input, {}, complete_delay);

    CHECK(app.process_one_frame());
    CHECK(app.process_one_frame());

    CHECK_FALSE(app.last_result());
    CHECK(input.sent.empty());
}

TEST_CASE("app filters by length after normalizing punctuation") {
    auto config = dk::AppConfig::defaults();
    config.live_input = true;
    FakeFrameSource frames{2};
    FakeDetector detector{{{10, 60, 100, 30}, {20, 20, 120, 30}}};
    FakeRecognizer recognizer{{
        {"-C-", .99F}, {"I/O!", .99F},
        {"-C-", .99F}, {"I/O!", .99F},
    }};
    FakeInputSink input;
    dk::App app(config, frames, detector, recognizer, input, {}, complete_delay);

    CHECK(app.process_one_frame());
    CHECK(app.process_one_frame());

    REQUIRE(input.sent.size() == 1);
    CHECK(input.sent.front() == "IO");
}

TEST_CASE("app never dispatches mixed Cyrillic OCR") {
    auto config = dk::AppConfig::defaults();
    config.live_input = true;
    FakeFrameSource frames{2};
    FakeDetector detector{{{30, 200, 240, 48}}};
    FakeRecognizer recognizer{{
        {"BLADEМЕЧ", .99F},
        {"BLADEМЕЧ", .99F},
    }};
    FakeInputSink input;
    dk::App app(config, frames, detector, recognizer, input, {}, complete_delay);

    CHECK(app.process_one_frame());
    CHECK_FALSE(app.last_result());
    CHECK(app.process_one_frame());
    CHECK_FALSE(app.last_result());
    CHECK(input.sent.empty());
}

TEST_CASE("blocked or partial input makes process_one_frame request a stop") {
    for (const auto status : {dk::SendStatus::blocked, dk::SendStatus::partial}) {
        auto config = dk::AppConfig::defaults();
        config.live_input = true;
        FakeFrameSource frames{2};
        FakeDetector detector{{{30, 200, 180, 40}}};
        FakeRecognizer recognizer{{{"TARGET", .99F}, {"TARGET", .99F}}};
        FakeInputSink input{status};
        std::vector<std::chrono::milliseconds> delays;
        const dk::DelayFunction delay =
            [&delays](auto duration, const auto&) {
                delays.push_back(duration);
                return true;
            };
        dk::App app(config, frames, detector, recognizer, input, {}, delay);

        CHECK(app.process_one_frame());
        CHECK_FALSE(app.process_one_frame());
        REQUIRE(input.sent.size() == 1);
        CHECK(delays.empty());
    }
}

TEST_CASE("cancelled input is a nonfatal stop boundary and is not locked") {
    auto config = dk::AppConfig::defaults();
    config.live_input = true;
    FakeFrameSource frames{2};
    FakeDetector detector{{{30, 200, 180, 40}}};
    FakeRecognizer recognizer{{{"TARGET", .99F}, {"TARGET", .99F}}};
    FakeInputSink input{dk::SendStatus::cancelled};
    std::vector<std::chrono::milliseconds> delays;
    const dk::DelayFunction delay =
        [&delays](auto duration, const auto&) {
            delays.push_back(duration);
            return true;
        };
    dk::App app(config, frames, detector, recognizer, input, {}, delay);

    CHECK(app.process_one_frame());
    CHECK(app.process_one_frame());
    REQUIRE(input.sent.size() == 1);
    CHECK(delays.empty());
}

TEST_CASE("rejected input does not invoke the post-send delay") {
    for (const auto status :
         {dk::SendStatus::not_foreground, dk::SendStatus::invalid_text}) {
        auto config = dk::AppConfig::defaults();
        config.live_input = true;
        FakeFrameSource frames{2};
        FakeDetector detector{{{30, 200, 180, 40}}};
        FakeRecognizer recognizer{{{"TARGET", .99F}, {"TARGET", .99F}}};
        FakeInputSink input{status};
        std::vector<std::chrono::milliseconds> delays;
        const dk::DelayFunction delay =
            [&delays](auto duration, const auto&) {
                delays.push_back(duration);
                return true;
            };
        dk::App app(config, frames, detector, recognizer, input, {}, delay);

        CHECK(app.process_one_frame());
        CHECK(app.process_one_frame());
        REQUIRE(input.sent.size() == 1);
        CHECK(delays.empty());
    }
}

TEST_CASE("cancellation during OCR prevents the final input call") {
    auto config = dk::AppConfig::defaults();
    config.live_input = true;
    FakeFrameSource frames{2};
    FakeDetector detector{{{30, 200, 180, 40}}};
    FakeRecognizer recognizer{{{"TARGET", .99F}, {"TARGET", .99F}}};
    FakeInputSink input;
    std::atomic_bool processing_enabled{true};
    dk::CancellationPredicate cancellation = [&processing_enabled] {
        return !processing_enabled.load();
    };
    dk::App app(
        config, frames, detector, recognizer, input, cancellation, complete_delay);

    CHECK(app.process_one_frame());
    recognizer.after_recognize = [&processing_enabled] {
        processing_enabled = false;
    };
    CHECK(app.process_one_frame());
    CHECK(input.sent.empty());
}

TEST_CASE("cancellation during dry-run OCR skips pacing and target lock") {
    auto config = dk::AppConfig::defaults();
    config.live_input = false;
    FakeFrameSource frames{3};
    FakeDetector detector{{{30, 200, 180, 40}}};
    FakeRecognizer recognizer{{
        {"TARGET", .99F},
        {"TARGET", .99F},
        {"TARGET", .99F},
    }};
    FakeInputSink input;
    std::atomic_bool processing_enabled{true};
    const dk::CancellationPredicate cancellation = [&processing_enabled] {
        return !processing_enabled.load();
    };
    std::vector<std::chrono::milliseconds> delays;
    const dk::DelayFunction delay =
        [&delays](auto duration, const auto&) {
            delays.push_back(duration);
            return true;
        };
    dk::App app(config, frames, detector, recognizer, input, cancellation, delay);

    CHECK(app.process_one_frame());
    recognizer.after_recognize = [&processing_enabled] {
        processing_enabled = false;
    };
    CHECK(app.process_one_frame());
    CHECK(delays.empty());

    recognizer.after_recognize = {};
    processing_enabled = true;
    CHECK(app.process_one_frame());
    CHECK(delays == std::vector{100ms});
    CHECK(input.sent.empty());
}

TEST_CASE("accepted live and dry targets apply pacing before a fresh capture") {
    for (const bool live_input : {false, true}) {
        CAPTURE(live_input);
        auto config = dk::AppConfig::defaults();
        config.live_input = live_input;
        config.post_send_delay_ms = 275;
        FakeFrameSource frames{3};
        FakeDetector detector{{
            {30, 500, 180, 40},
            {700, 50, 180, 40},
        }};
        FakeRecognizer recognizer{{
            {"LOWER", .99F}, {"UPPER", .99F},
            {"LOWER", .99F}, {"UPPER", .99F},
            {"LOWER", .99F}, {"UPPER", .99F},
        }};
        FakeInputSink input;
        std::vector<std::chrono::milliseconds> delays;
        const dk::DelayFunction delay =
            [&](auto duration, const auto&) {
                delays.push_back(duration);
                CHECK(frames.returned_frames ==
                      static_cast<int>(delays.size()) + 1);
                CHECK(input.sent.size() ==
                      (live_input ? delays.size() : 0));
                return true;
            };
        dk::App app(config, frames, detector, recognizer, input, {}, delay);

        CHECK(app.process_one_frame());
        CHECK(app.process_one_frame());
        CHECK(delays == std::vector{275ms});
        CHECK(frames.returned_frames == 2);

        CHECK(app.process_one_frame());
        CHECK(frames.returned_frames == 3);
        CHECK((delays == std::vector{275ms, 275ms}));
        if (live_input) {
            CHECK((input.sent ==
                   std::vector<std::string>{"LOWER", "UPPER"}));
        } else {
            CHECK(input.sent.empty());
        }
    }
}

TEST_CASE("app sends a stable multiline target as one normalized input") {
    auto config = dk::AppConfig::defaults();
    config.live_input = true;
    FakeFrameSource frames{4};
    FakeDetector detector{{
        {100, 100, 220, 40},
        {100, 155, 220, 40},
    }};
    FakeRecognizer recognizer{{
        {"PHANTOM", .98F}, {"ASSASSIN", .94F},
        {"PHANTOM", .98F}, {"ASSASSIN", .94F},
        {"PHANTOM", .98F}, {"ASSASSIN", .94F},
        {"PHANTOM", .98F}, {"ASSASSIN", .94F},
    }};
    FakeInputSink input;
    dk::App app(config, frames, detector, recognizer, input, {}, complete_delay);

    CHECK(app.process_one_frame());
    CHECK(input.sent.empty());
    CHECK(app.process_one_frame());
    CHECK(input.sent.empty());
    CHECK(app.process_one_frame());

    REQUIRE(input.sent == std::vector<std::string>{"PHANTOMASSASSIN"});
    REQUIRE(app.last_result());
    CHECK(app.last_result()->raw_text == "PHANTOM\nASSASSIN");
    CHECK(app.last_result()->normalized_text == "PHANTOMASSASSIN");
    CHECK(app.last_result()->confidence == Catch::Approx(.94F));
    CHECK((app.last_result()->bounds == dk::Box{100, 100, 220, 95}));

    CHECK(app.process_one_frame());
    CHECK(input.sent == std::vector<std::string>{"PHANTOMASSASSIN"});
    CHECK_FALSE(app.last_result());
}

TEST_CASE("app waits for two clean frames after multiline lines cross") {
    auto config = dk::AppConfig::defaults();
    config.live_input = true;
    FakeFrameSource frames{5};
    SequencedDetector detector{{
        {{100, 100, 220, 40}, {100, 155, 220, 40}},
        {{100, 110, 220, 40}, {100, 165, 220, 40}},
        {{100, 180, 220, 40}, {100, 125, 220, 40}},
        {{100, 190, 220, 40}, {100, 135, 220, 40}},
        {{100, 200, 220, 40}, {100, 145, 220, 40}},
    }};
    FakeRecognizer recognizer{{
        {"TOP", .99F}, {"BOTTOM", .99F},
        {"TOP", .99F}, {"BOTTOM", .99F},
        {"TOP", .99F}, {"BOTTOM", .99F},
        {"TOP", .99F}, {"BOTTOM", .99F},
        {"TOP", .99F}, {"BOTTOM", .99F},
    }};
    FakeInputSink input;
    dk::App app(config, frames, detector, recognizer, input, {}, complete_delay);

    for (int frame = 0; frame < 4; ++frame) {
        CHECK(app.process_one_frame());
        CHECK(input.sent.empty());
    }
    CHECK(app.process_one_frame());

    REQUIRE(input.sent == std::vector<std::string>{"BOTTOMTOP"});
}

TEST_CASE("app never sends a merged OCR line born during unresolved overlap") {
    auto config = dk::AppConfig::defaults();
    config.live_input = true;
    FakeFrameSource frames{5};
    SequencedDetector detector{{
        {{100, 100, 220, 40}, {100, 155, 220, 40}},
        {{100, 110, 220, 40}, {100, 165, 220, 40}},
        {{100, 120, 220, 95}},
        {{100, 120, 220, 95}},
        {{100, 120, 220, 95}},
    }};
    FakeRecognizer recognizer{{
        {"TOP", .99F}, {"BOTTOM", .99F},
        {"TOP", .99F}, {"BOTTOM", .99F},
        {"TOPBOTTOM", .99F},
        {"TOPBOTTOM", .99F},
        {"TOPBOTTOM", .99F},
    }};
    FakeInputSink input;
    dk::App app(config, frames, detector, recognizer, input, {}, complete_delay);

    for (int frame = 0; frame < 5; ++frame) {
        CHECK(app.process_one_frame());
        CHECK(input.sent.empty());
    }
}

TEST_CASE("app rejects a merged OCR line that appears after an overlap gap") {
    auto config = dk::AppConfig::defaults();
    config.live_input = true;
    FakeFrameSource frames{6};
    SequencedDetector detector{{
        {{100, 100, 220, 40}, {100, 155, 220, 40}},
        {{100, 110, 220, 40}, {100, 165, 220, 40}},
        {},
        {},
        {{100, 120, 220, 95}},
        {{100, 120, 220, 95}},
    }};
    FakeRecognizer recognizer{{
        {"TOP", .99F}, {"BOTTOM", .99F},
        {"TOP", .99F}, {"BOTTOM", .99F},
        {"TOPBOTTOM", .99F},
        {"TOPBOTTOM", .99F},
    }};
    FakeInputSink input;
    dk::App app(config, frames, detector, recognizer, input, {}, complete_delay);

    for (int frame = 0; frame < 6; ++frame) {
        CHECK(app.process_one_frame());
        CHECK(input.sent.empty());
    }
}

TEST_CASE("app keeps sent compound ownership when split lines merge") {
    auto config = dk::AppConfig::defaults();
    config.live_input = true;
    FakeFrameSource frames{5};
    SequencedDetector detector{{
        {{100, 100, 220, 40}, {100, 155, 220, 40}},
        {{100, 110, 220, 40}, {100, 165, 220, 40}},
        {{100, 120, 220, 40}, {100, 175, 220, 40}},
        {{100, 120, 220, 95}},
        {{100, 120, 220, 95}},
    }};
    FakeRecognizer recognizer{{
        {"PHANTOM", .99F}, {"ASSASSIN", .99F},
        {"PHANTOM", .99F}, {"ASSASSIN", .99F},
        {"PHANTOM", .99F}, {"ASSASSIN", .99F},
        {"PHANTOMASSASSIN", .99F},
        {"PHANTOMASSASSIN", .99F},
    }};
    FakeInputSink input;
    dk::App app(config, frames, detector, recognizer, input, {}, complete_delay);

    for (int frame = 0; frame < 5; ++frame) {
        CHECK(app.process_one_frame());
    }

    REQUIRE(input.sent ==
            std::vector<std::string>{"PHANTOMASSASSIN"});
}

TEST_CASE("sent compound alias releases when its OCR text changes") {
    auto config = dk::AppConfig::defaults();
    config.live_input = true;
    FakeFrameSource frames{7};
    SequencedDetector detector{{
        {{100, 100, 220, 40}, {100, 155, 220, 40}},
        {{100, 110, 220, 40}, {100, 165, 220, 40}},
        {{100, 120, 220, 40}, {100, 175, 220, 40}},
        {{100, 120, 220, 95}},
        {{100, 120, 220, 95}},
        {{100, 120, 220, 95}},
        {{100, 120, 220, 95}},
    }};
    FakeRecognizer recognizer{{
        {"PHANTOM", .99F}, {"ASSASSIN", .99F},
        {"PHANTOM", .99F}, {"ASSASSIN", .99F},
        {"PHANTOM", .99F}, {"ASSASSIN", .99F},
        {"PHANTOMASSASSIN", .99F},
        {"PHANTOMASSASSIN", .99F},
        {"FRESH", .99F},
        {"FRESH", .99F},
    }};
    FakeInputSink input;
    dk::App app(config, frames, detector, recognizer, input, {}, complete_delay);

    for (int frame = 0; frame < 7; ++frame) {
        CHECK(app.process_one_frame());
    }

    REQUIRE(input.sent ==
            std::vector<std::string>{"PHANTOMASSASSIN", "FRESH"});
}

TEST_CASE("sent compound rejects a new non-exact merged OCR line") {
    auto config = dk::AppConfig::defaults();
    config.live_input = true;
    FakeFrameSource frames{5};
    SequencedDetector detector{{
        {{100, 100, 220, 40}, {100, 155, 220, 40}},
        {{100, 110, 220, 40}, {100, 165, 220, 40}},
        {{100, 120, 220, 40}, {100, 175, 220, 40}},
        {{100, 120, 220, 95}},
        {{100, 120, 220, 95}},
    }};
    FakeRecognizer recognizer{{
        {"PHANTOM", .99F}, {"ASSASSIN", .99F},
        {"PHANTOM", .99F}, {"ASSASSIN", .99F},
        {"PHANTOM", .99F}, {"ASSASSIN", .99F},
        {"PHANTOMASSASSI", .99F},
        {"PHANTOMASSASSI", .99F},
    }};
    FakeInputSink input;
    dk::App app(config, frames, detector, recognizer, input, {}, complete_delay);

    for (int frame = 0; frame < 5; ++frame) {
        CHECK(app.process_one_frame());
    }

    REQUIRE(input.sent ==
            std::vector<std::string>{"PHANTOMASSASSIN"});
}

TEST_CASE("independently sent targets never own a later merged OCR line") {
    auto config = dk::AppConfig::defaults();
    config.live_input = true;
    FakeFrameSource frames{7};
    SequencedDetector detector{{
        {{30, 500, 180, 40}, {700, 50, 180, 40}},
        {{30, 500, 180, 40}, {700, 50, 180, 40}},
        {{30, 500, 180, 40}, {700, 50, 180, 40}},
        {{100, 100, 220, 40}, {100, 155, 220, 40}},
        {{100, 110, 220, 40}, {100, 165, 220, 40}},
        {{100, 120, 220, 95}},
        {{100, 120, 220, 95}},
    }};
    FakeRecognizer recognizer{{
        {"ALPHA", .99F}, {"BETA", .99F},
        {"ALPHA", .99F}, {"BETA", .99F},
        {"ALPHA", .99F}, {"BETA", .99F},
        {"ALPHA", .99F}, {"BETA", .99F},
        {"ALPHA", .99F}, {"BETA", .99F},
        {"ALPHABETA", .99F},
        {"ALPHABETA", .99F},
    }};
    FakeInputSink input;
    dk::App app(config, frames, detector, recognizer, input, {}, complete_delay);

    for (int frame = 0; frame < 7; ++frame) {
        CHECK(app.process_one_frame());
    }

    REQUIRE(input.sent ==
            std::vector<std::string>{"ALPHA", "BETA"});
}

TEST_CASE("post-send pacing receives live cancellation state") {
    auto config = dk::AppConfig::defaults();
    config.live_input = true;
    FakeFrameSource frames{2};
    FakeDetector detector{{{30, 200, 180, 40}}};
    FakeRecognizer recognizer{{{"TARGET", .99F}, {"TARGET", .99F}}};
    FakeInputSink input;
    std::atomic_bool processing_enabled{true};
    const dk::CancellationPredicate cancellation = [&processing_enabled] {
        return !processing_enabled.load();
    };
    std::vector<std::chrono::milliseconds> delays;
    const dk::DelayFunction delay =
        [&](auto duration, const auto& delay_cancellation) {
            delays.push_back(duration);
            CHECK_FALSE(delay_cancellation());
            processing_enabled = false;
            CHECK(delay_cancellation());
            CHECK(frames.returned_frames == 2);
            CHECK(input.sent.size() == 1);
            return false;
        };
    dk::App app(config, frames, detector, recognizer, input, cancellation, delay);

    CHECK(app.process_one_frame());
    CHECK(app.process_one_frame());
    CHECK(delays == std::vector{100ms});
    CHECK(input.sent.size() == 1);
}

TEST_CASE("frame timeout is a clean no-op") {
    auto config = dk::AppConfig::defaults();
    FakeFrameSource frames{0};
    FakeDetector detector{{}};
    FakeRecognizer recognizer{{}};
    FakeInputSink input;
    dk::App app(config, frames, detector, recognizer, input, {}, complete_delay);

    CHECK(app.process_one_frame());
    CHECK_FALSE(app.last_result());
    CHECK(app.metrics().summary().total.count == 0);
}

TEST_CASE("latency metrics retain 512 recent samples and summarize milliseconds") {
    dk::LatencyMetrics metrics;
    for (int sample = 1; sample <= 513; ++sample) {
        metrics.record(dk::LatencyStage::capture, sample * 1ms);
    }

    const auto capture = metrics.summary().capture;
    CHECK(capture.count == 512);
    CHECK(capture.mean_ms == Catch::Approx(257.5));
    CHECK(capture.median_ms == Catch::Approx(257.5));
    CHECK(capture.p95_ms == Catch::Approx(487.0));
}
