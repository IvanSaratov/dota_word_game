#include <string>

#include <catch2/catch_test_macros.hpp>

#include "dk/target_tracker.hpp"

using dk::Box;
using dk::TextCandidate;

static TextCandidate word(std::string text, int y, int x = 100) {
    return {text, text, 0.95F, Box{x, y, 240, 48}};
}

TEST_CASE("tracker requires two adjacent frames") {
    dk::TargetTracker tracker({.confirm_frames = 2, .max_center_distance_px = 90.0F,
                               .unlock_missing_frames = 2});
    CHECK_FALSE(tracker.update({word("FIRST", 100)}));
    const auto ready = tracker.update({word("FIRST", 108)});
    REQUIRE(ready);
    CHECK(ready->normalized_text == "FIRST");
}

TEST_CASE("tracker chooses the lower confirmed target") {
    dk::TargetTracker tracker;
    tracker.update({word("HIGH", 100), word("LOW", 500)});
    const auto ready = tracker.update({word("HIGH", 106), word("LOW", 508)});
    REQUIRE(ready);
    CHECK(ready->normalized_text == "LOW");
}

TEST_CASE("sent target stays locked until missing") {
    dk::TargetTracker tracker;
    tracker.update({word("AGAIN", 300)});
    auto ready = tracker.update({word("AGAIN", 306)});
    REQUIRE(ready);
    tracker.mark_sent(*ready);
    CHECK_FALSE(tracker.update({word("AGAIN", 312)}));
    CHECK_FALSE(tracker.update({}));
    CHECK_FALSE(tracker.update({}));
    CHECK_FALSE(tracker.update({word("AGAIN", 100)}));
    CHECK(tracker.update({word("AGAIN", 106)}));
}
