#include <catch2/catch_test_macros.hpp>
#include "dk/text_normalizer.hpp"

TEST_CASE("normalization keeps only uppercase ASCII letters") {
    CHECK(dk::normalize_for_input("HYPERSTONE") == "HYPERSTONE");
    CHECK(dk::normalize_for_input("Don't panic!") == "DONTPANIC");
    CHECK(dk::normalize_for_input("ROCK-'N'-ROLL") == "ROCKNROLL");
    CHECK(dk::normalize_for_input(" two words ") == "TWOWORDS");
    CHECK(dk::normalize_for_input("123 -- !") == "");
}

TEST_CASE("normalization rejects Cyrillic and mixed-script OCR") {
    CHECK(dk::normalize_for_input("МЕЧ").empty());
    CHECK(dk::normalize_for_input("BLADEМЕЧ").empty());
    CHECK(dk::normalize_for_input("МЕЧBLADE").empty());
    CHECK(dk::normalize_for_input("IO") == "IO");
}
