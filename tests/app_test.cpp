#include <chrono>
#include <cstddef>
#include <deque>
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
        return result;
    }

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

TEST_CASE("app confirms normalizes and sends one lower target") {
    auto config = dk::AppConfig::defaults();
    config.live_input = true;
    FakeFrameSource frames{2};
    FakeDetector detector{{{50, 100, 250, 48}, {80, 500, 330, 48}}};
    FakeRecognizer recognizer{{
        {"DON'T PANIC!", .94F}, {"ROCK-'N'-ROLL", .96F},
        {"DON'T PANIC!", .94F}, {"ROCK-'N'-ROLL", .96F},
    }};
    FakeInputSink input;
    dk::App app(config, frames, detector, recognizer, input);

    CHECK(app.process_one_frame());
    CHECK(app.process_one_frame());

    REQUIRE(input.sent.size() == 1);
    CHECK(input.sent.front() == "ROCKNROLL");
    REQUIRE(app.last_result());
    CHECK((app.last_result()->bounds == dk::Box{80, 500, 330, 48}));
    CHECK((recognizer.crop_sizes.front() == std::pair{250, 48}));
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
    CHECK(app.process_one_frame());

    CHECK(input.sent.empty());
    REQUIRE(app.last_result());
    CHECK(app.last_result()->normalized_text == "HYPERSTONE");
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

TEST_CASE("blocked or partial input stops processing and is not marked sent") {
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
