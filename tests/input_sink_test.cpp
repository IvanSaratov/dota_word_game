#include <catch2/catch_test_macros.hpp>

#include "dk/input_sink.hpp"

TEST_CASE("send text accepts nonempty uppercase ASCII letters") {
    CHECK(dk::validate_send_text("HYPERSTONE"));
}

TEST_CASE("send text rejects unsupported input") {
    CHECK_FALSE(dk::validate_send_text(""));
    CHECK_FALSE(dk::validate_send_text("TWO WORDS"));
    CHECK_FALSE(dk::validate_send_text("DON'T"));
    CHECK_FALSE(dk::validate_send_text("abc"));
}
