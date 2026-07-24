#include <catch2/catch_test_macros.hpp>
#include <opencv2/imgcodecs.hpp>

#include "dk/ocr_recognizer.hpp"
#include "dk/text_normalizer.hpp"

TEST_CASE("recognizer reads the supplied target crop") {
    const cv::Mat image = cv::imread(DK_FIXTURE_DIR "/game_single_hyperstone.jpg");
    REQUIRE_FALSE(image.empty());
    const cv::Mat crop = image(cv::Rect{140, 565, 350, 65});
    dk::OcrRecognizer recognizer(DK_MODEL_PATH, DK_DICTIONARY_PATH);

    const auto result = recognizer.recognize(crop);

    CHECK(dk::normalize_for_input(result.text) == "HYPERSTONE");
    CHECK(result.confidence >= 0.70F);
}
