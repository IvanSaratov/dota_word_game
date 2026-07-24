#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>
#include <vector>

#include "dk/ctc_decoder.hpp"

TEST_CASE("CTC decoder removes blanks and repeated classes") {
    const std::vector<std::string> chars{"", "A", "B", "!", " "};
    const std::vector<int64_t> ids{0, 1, 1, 0, 2, 3, 4};
    const std::vector<float> scores{.9F, .95F, .8F, .9F, .92F, .88F, .9F};

    const auto result = dk::decode_ctc(ids, scores, chars, 0);

    CHECK(result.text == "AB! ");
    CHECK(result.confidence == Catch::Approx((.95F + .92F + .88F + .9F) / 4.0F));
}

TEST_CASE("CTC blank separates identical emitted classes") {
    const std::vector<std::string> chars{"", "A"};
    const std::vector<int64_t> ids{1, 0, 1};
    const std::vector<float> scores{.7F, .9F, .8F};

    const auto result = dk::decode_ctc(ids, scores, chars);

    CHECK(result.text == "AA");
    CHECK(result.confidence == Catch::Approx(.75F));
}

TEST_CASE("CTC decoder rejects mismatched ids and scores") {
    const std::vector<std::string> chars{"", "A"};
    const std::vector<int64_t> ids{1};
    const std::vector<float> scores{};

    CHECK_THROWS_AS(dk::decode_ctc(ids, scores, chars), std::invalid_argument);
}

TEST_CASE("CTC decoder rejects class ids outside the dictionary") {
    const std::vector<std::string> chars{"", "A"};
    const std::vector<int64_t> ids{2};
    const std::vector<float> scores{.9F};

    CHECK_THROWS_AS(dk::decode_ctc(ids, scores, chars), std::out_of_range);
}
