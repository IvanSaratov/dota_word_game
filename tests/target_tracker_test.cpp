#include <initializer_list>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "dk/target_tracker.hpp"

using dk::Box;
using dk::TextCandidate;

static TextCandidate word(std::string text, int y, int x = 100) {
    return {text, text, 0.95F, Box{x, y, 240, 48}};
}

static TextCandidate boxed_word(
    std::string text, Box bounds, float confidence = 0.99F) {
    return {text, text, confidence, bounds};
}

static std::optional<TextCandidate> update_tracker(
    dk::TargetTracker& tracker,
    std::initializer_list<TextCandidate> candidates) {
    return tracker.update(
        std::span<const TextCandidate>{candidates.begin(), candidates.size()});
}

TEST_CASE("tracker requires two adjacent frames") {
    dk::TargetTracker tracker({.confirm_frames = 2, .max_center_distance_px = 90.0F,
                               .unlock_missing_frames = 2});
    CHECK_FALSE(update_tracker(tracker, {word("FIRST", 100)}));
    const auto ready = update_tracker(tracker, {word("FIRST", 108)});
    REQUIRE(ready);
    CHECK(ready->normalized_text == "FIRST");
}

TEST_CASE("tracker requires consecutive frames after an unsent miss") {
    dk::TargetTracker tracker({.confirm_frames = 2, .max_center_distance_px = 90.0F,
                               .unlock_missing_frames = 2});
    CHECK_FALSE(update_tracker(tracker, {word("BANE", 100)}));
    CHECK_FALSE(update_tracker(tracker, {}));
    CHECK_FALSE(update_tracker(tracker, {word("BANE", 108)}));
    const auto ready = update_tracker(tracker, {word("BANE", 116)});
    REQUIRE(ready);
    CHECK(ready->normalized_text == "BANE");
}

TEST_CASE("tracker chooses the lower confirmed target") {
    dk::TargetTracker tracker;
    update_tracker(tracker, {word("HIGH", 100), word("LOW", 500)});
    const auto ready =
        update_tracker(tracker, {word("HIGH", 106), word("LOW", 508)});
    REQUIRE(ready);
    CHECK(ready->normalized_text == "LOW");
}

TEST_CASE("sent target stays locked until missing") {
    dk::TargetTracker tracker({.unlock_missing_frames = 2});
    update_tracker(tracker, {word("AGAIN", 300)});
    auto ready = update_tracker(tracker, {word("AGAIN", 306)});
    REQUIRE(ready);
    tracker.mark_sent(*ready);
    CHECK_FALSE(update_tracker(tracker, {word("AGAIN", 312)}));
    CHECK_FALSE(update_tracker(tracker, {}));
    CHECK_FALSE(update_tracker(tracker, {}));
    CHECK_FALSE(update_tracker(tracker, {word("AGAIN", 100)}));
    CHECK(update_tracker(tracker, {word("AGAIN", 106)}));
}

TEST_CASE("sent target stays locked across exactly one missing frame") {
    dk::TargetTracker tracker;
    CHECK_FALSE(update_tracker(tracker, {word("BANE", 100)}));
    auto ready = update_tracker(tracker, {word("BANE", 150)});
    REQUIRE(ready);
    tracker.mark_sent(*ready);

    CHECK_FALSE(update_tracker(tracker, {}));
    CHECK_FALSE(update_tracker(tracker, {word("BANE", 200)}));
    CHECK_FALSE(update_tracker(tracker, {word("BANE", 250)}));
}

TEST_CASE("moving targets retain independent identities") {
    dk::TargetTracker tracker;
    CHECK_FALSE(update_tracker(
        tracker, {word("HIGH", 100), word("LOW", 400)}));
    auto lower = update_tracker(
        tracker, {word("HIGH", 150), word("LOW", 450)});
    REQUIRE(lower);
    CHECK(lower->normalized_text == "LOW");
    tracker.mark_sent(*lower);

    const auto upper = update_tracker(
        tracker, {word("HIGH", 200), word("LOW", 500)});
    REQUIRE(upper);
    CHECK(upper->normalized_text == "HIGH");
}

TEST_CASE("sent target remains locked while moving farther than one match radius") {
    dk::TargetTracker tracker;
    CHECK_FALSE(update_tracker(tracker, {word("BANE", 100)}));
    auto ready = update_tracker(tracker, {word("BANE", 170)});
    REQUIRE(ready);
    tracker.mark_sent(*ready);

    CHECK_FALSE(update_tracker(tracker, {word("BANE", 240)}));
    CHECK_FALSE(update_tracker(tracker, {word("BANE", 310)}));
}

TEST_CASE("temporary partial OCR does not unlock a sent moving target") {
    dk::TargetTracker tracker;
    CHECK_FALSE(update_tracker(tracker, {word("BANE", 100)}));
    auto ready = update_tracker(tracker, {word("BANE", 150)});
    REQUIRE(ready);
    tracker.mark_sent(*ready);

    CHECK_FALSE(update_tracker(
        tracker, {boxed_word("ANE", {220, 200, 80, 48})}));
    CHECK_FALSE(update_tracker(tracker, {word("BANE", 250)}));
    CHECK_FALSE(update_tracker(tracker, {word("BANE", 300)}));
}

TEST_CASE("sent exact ownership precedes a competing unsent spatial match") {
    dk::TargetTracker tracker;
    CHECK_FALSE(update_tracker(tracker, {word("BANE", 100)}));
    auto ready = update_tracker(tracker, {word("BANE", 150)});
    REQUIRE(ready);
    tracker.mark_sent(*ready);

    CHECK_FALSE(update_tracker(tracker, {word("DECOY", 300)}));
    CHECK_FALSE(update_tracker(tracker, {word("BANE", 300)}));
    CHECK_FALSE(update_tracker(tracker, {word("BANE", 300)}));
}

TEST_CASE("same word can be selected again after the old target disappears") {
    dk::TargetTracker tracker({.unlock_missing_frames = 2});
    CHECK_FALSE(update_tracker(tracker, {word("BANE", 100)}));
    auto ready = update_tracker(tracker, {word("BANE", 150)});
    REQUIRE(ready);
    tracker.mark_sent(*ready);

    CHECK_FALSE(update_tracker(tracker, {}));
    CHECK_FALSE(update_tracker(tracker, {}));
    CHECK_FALSE(update_tracker(tracker, {word("BANE", 100)}));
    CHECK(update_tracker(tracker, {word("BANE", 150)}));
}

TEST_CASE("sent exact text reacquires after a short gap and position jump") {
    dk::TargetTracker tracker;
    CHECK_FALSE(update_tracker(
        tracker, {boxed_word("MEDUSA", {213, 357, 229, 51})}));
    auto ready = update_tracker(
        tracker, {boxed_word("MEDUSA", {215, 356, 229, 51})});
    REQUIRE(ready);
    tracker.mark_sent(*ready);

    for (int frame = 0; frame < 14; ++frame) {
        CHECK_FALSE(update_tracker(tracker, {}));
    }
    CHECK_FALSE(update_tracker(
        tracker, {boxed_word("MEDUSA", {455, 695, 229, 51})}));
    CHECK_FALSE(update_tracker(
        tracker, {boxed_word("MEDUSA", {462, 716, 229, 52})}));
}

TEST_CASE("sent word consumes nearby stable cropped fragments") {
    struct Scenario {
        const char* full;
        Box full_bounds;
        const char* fragment;
        Box fragment_bounds;
    };
    const Scenario scenarios[] = {
        {"HYPERSTONE", {380, 583, 341, 52}, "HYPE", {398, 658, 134, 51}},
        {"BLADEMAIL", {393, 581, 315, 66}, "EMA", {542, 579, 89, 49}},
        {"BLOODTHORN", {715, 0, 380, 34}, "RN", {1015, 0, 78, 33}},
        {"BLOODTHORN", {715, 0, 380, 34}, "DIH", {857, 0, 109, 33}},
    };

    for (const auto& scenario : scenarios) {
        dk::TargetTracker tracker;
        CHECK_FALSE(update_tracker(
            tracker, {boxed_word(scenario.full, scenario.full_bounds)}));
        auto ready = update_tracker(
            tracker, {boxed_word(scenario.full, scenario.full_bounds)});
        REQUIRE(ready);
        tracker.mark_sent(*ready);

        CHECK_FALSE(update_tracker(
            tracker,
            {boxed_word(scenario.fragment, scenario.fragment_bounds)}));
        CHECK_FALSE(update_tracker(
            tracker,
            {boxed_word(scenario.fragment, scenario.fragment_bounds)}));
    }
}

TEST_CASE("different full-size word near a sent track stays eligible") {
    dk::TargetTracker tracker;
    CHECK_FALSE(update_tracker(
        tracker, {boxed_word("HYPERSTONE", {380, 583, 341, 52})}));
    auto sent = update_tracker(
        tracker, {boxed_word("HYPERSTONE", {382, 585, 341, 52})});
    REQUIRE(sent);
    tracker.mark_sent(*sent);

    CHECK_FALSE(update_tracker(
        tracker, {boxed_word("BLADEMAIL", {390, 590, 315, 52})}));
    const auto distinct = update_tracker(
        tracker, {boxed_word("BLADEMAIL", {394, 594, 315, 52})});
    REQUIRE(distinct);
    CHECK(distinct->normalized_text == "BLADEMAIL");
}

TEST_CASE("same word unlocks after fifteen completely missing frames") {
    dk::TargetTracker tracker;
    CHECK_FALSE(update_tracker(tracker, {word("BANE", 100)}));
    auto ready = update_tracker(tracker, {word("BANE", 150)});
    REQUIRE(ready);
    tracker.mark_sent(*ready);

    for (int frame = 0; frame < 15; ++frame) {
        CHECK_FALSE(update_tracker(tracker, {}));
    }
    CHECK_FALSE(update_tracker(tracker, {word("BANE", 100)}));
    CHECK(update_tracker(tracker, {word("BANE", 150)}));
}
