#include <catch2/catch_test_macros.hpp>
#include "dk/text_normalizer.hpp"

TEST_CASE("normalization keeps only uppercase ASCII letters") {
    CHECK(dk::normalize_for_input("HYPERSTONE") == "HYPERSTONE");
    CHECK(dk::normalize_for_input("Don't panic!") == "DONTPANIC");
    CHECK(dk::normalize_for_input("ROCK-'N'-ROLL") == "ROCKNROLL");
    CHECK(dk::normalize_for_input(" two words ") == "TWOWORDS");
    CHECK(dk::normalize_for_input("123 -- !") == "");
}
