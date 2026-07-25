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

class FakeFrameSource final : public dk::FrameSource {
public:
    explicit FakeFrameSource(int count) : remaining_(count) {}

    std::optional<dk::CapturedFrame> next_frame() override {
        if (remaining_-- <= 0) {
            return std::nullopt;
        }
        return dk::CapturedFrame{
            cv::Mat(720, 1000, CV_8UC4, cv::Scalar{}),
            std::chrono::steady_clock::now(),
        };
    }

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
    dk::App app(config, frames, detector, recognizer, input);

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
    dk::App app(config, frames, detector, recognizer, input);

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
    dk::App app(config, frames, detector, recognizer, input);

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
    dk::App app(config, frames, detector, recognizer, input);

    CHECK(app.process_one_frame());
    CHECK(app.process_one_frame());

    REQUIRE(input.sent.size() == 1);
    CHECK(input.sent.front() == "IO");
}

TEST_CASE("blocked or partial input makes process_one_frame request a stop") {
    for (const auto status : {dk::SendStatus::blocked, dk::SendStatus::partial}) {
        auto config = dk::AppConfig::defaults();
        config.live_input = true;
        FakeFrameSource frames{2};
        FakeDetector detector{{{30, 200, 180, 40}}};
        FakeRecognizer recognizer{{{"TARGET", .99F}, {"TARGET", .99F}}};
        FakeInputSink input{status};
        dk::App app(config, frames, detector, recognizer, input);

        CHECK(app.process_one_frame());
        CHECK_FALSE(app.process_one_frame());
        REQUIRE(input.sent.size() == 1);
    }
}

TEST_CASE("cancelled input is a nonfatal stop boundary and is not locked") {
    auto config = dk::AppConfig::defaults();
    config.live_input = true;
    FakeFrameSource frames{2};
    FakeDetector detector{{{30, 200, 180, 40}}};
    FakeRecognizer recognizer{{{"TARGET", .99F}, {"TARGET", .99F}}};
    FakeInputSink input{dk::SendStatus::cancelled};
    dk::App app(config, frames, detector, recognizer, input);

    CHECK(app.process_one_frame());
    CHECK(app.process_one_frame());
    REQUIRE(input.sent.size() == 1);
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
    dk::App app(config, frames, detector, recognizer, input, cancellation);

    CHECK(app.process_one_frame());
    recognizer.after_recognize = [&processing_enabled] {
        processing_enabled = false;
    };
    CHECK(app.process_one_frame());
    CHECK(input.sent.empty());
}

TEST_CASE("frame timeout is a clean no-op") {
    auto config = dk::AppConfig::defaults();
    FakeFrameSource frames{0};
    FakeDetector detector{{}};
    FakeRecognizer recognizer{{}};
    FakeInputSink input;
    dk::App app(config, frames, detector, recognizer, input);

    CHECK(app.process_one_frame());
    CHECK_FALSE(app.last_result());
    CHECK(app.metrics().summary().total.count == 0);
}

TEST_CASE("latency metrics retain 512 recent samples and summarize milliseconds") {
    dk::LatencyMetrics metrics;
    using namespace std::chrono_literals;
    for (int sample = 1; sample <= 513; ++sample) {
        metrics.record(dk::LatencyStage::capture, sample * 1ms);
    }

    const auto capture = metrics.summary().capture;
    CHECK(capture.count == 512);
    CHECK(capture.mean_ms == Catch::Approx(257.5));
    CHECK(capture.median_ms == Catch::Approx(257.5));
    CHECK(capture.p95_ms == Catch::Approx(487.0));
}
