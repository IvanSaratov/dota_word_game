#include <algorithm>

#include <catch2/catch_test_macros.hpp>
#include <opencv2/imgcodecs.hpp>

#include "dk/candidate_detector.hpp"

namespace {
cv::Mat make_text_row(int type, int y = 50) {
    cv::Mat image(200, 200, type, cv::Scalar::all(0));
    for (const int x : {10, 22, 34}) {
        image(cv::Rect{x, y, 5, 10}).setTo(cv::Scalar::all(255));
    }
    return image;
}
}  // namespace

TEST_CASE("detector accepts grayscale BGR and BGRA frames") {
    for (const int type : {CV_8UC1, CV_8UC3, CV_8UC4}) {
        CAPTURE(type);
        const auto boxes = dk::CandidateDetector{}.detect(make_text_row(type));
        REQUIRE(boxes.size() == 1);
        CHECK((boxes.front() == dk::Box{4, 44, 41, 22}));
    }
}

TEST_CASE("detector masks configured normalized ignored regions") {
    dk::DetectorConfig config;
    config.ignored_regions.emplace_back(0.0F, 0.2F, 0.3F, 0.2F);

    CHECK(dk::CandidateDetector{config}.detect(make_text_row(CV_8UC1)).empty());
}

TEST_CASE("detector returns lower rows first") {
    cv::Mat image = make_text_row(CV_8UC1);
    make_text_row(CV_8UC1, 100).copyTo(image);
    for (const int x : {10, 22, 34}) {
        image(cv::Rect{x, 50, 5, 10}).setTo(cv::Scalar::all(255));
    }

    const auto boxes = dk::CandidateDetector{}.detect(image);
    REQUIRE(boxes.size() == 2);
    CHECK(boxes[0].bottom() > boxes[1].bottom());
}

TEST_CASE("detector finds the large HYPERSTONE row") {
    const cv::Mat image = cv::imread(DK_FIXTURE_DIR "/game_single_hyperstone.jpg");
    REQUIRE_FALSE(image.empty());
    const auto boxes = dk::CandidateDetector{}.detect(image);
    REQUIRE_FALSE(boxes.empty());

    const auto is_target = [](const dk::Box& box) {
        return box.center_x() > 250 && box.center_x() < 380 &&
               box.center_y() > 560 && box.center_y() < 640 &&
               box.width > 250 && box.height > 30;
    };
    CHECK(std::ranges::any_of(boxes, is_target));
}

TEST_CASE("detector rejects the small green duplicate") {
    const cv::Mat image = cv::imread(DK_FIXTURE_DIR "/game_single_hyperstone.jpg");
    REQUIRE_FALSE(image.empty());
    const auto boxes = dk::CandidateDetector{}.detect(image);
    CHECK(std::ranges::none_of(boxes, [](const dk::Box& box) {
        return box.center_y() > 525 && box.center_y() < 570 &&
               box.height < 30;
    }));
}
