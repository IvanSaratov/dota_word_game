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

TEST_CASE("interrupted delayed input is partial only after a complete sent prefix") {
    CHECK(
        dk::interrupted_send_status(0, dk::SendStatus::not_foreground) ==
        dk::SendStatus::not_foreground);
    CHECK(
        dk::interrupted_send_status(0, dk::SendStatus::blocked) ==
        dk::SendStatus::blocked);
    CHECK(
        dk::interrupted_send_status(1, dk::SendStatus::not_foreground) ==
        dk::SendStatus::partial);
    CHECK(
        dk::interrupted_send_status(2, dk::SendStatus::blocked) ==
        dk::SendStatus::partial);
}
