#include <algorithm>
#include <initializer_list>
#include <span>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "dk/compound_target.hpp"

namespace {

dk::LineTrackSnapshot line(
    dk::TrackId id, std::string text, dk::Box bounds,
    bool sent = false, bool observed = true) {
    return {
        .id = id,
        .value = {text, text, 0.99F, bounds},
        .seen_frames = 3,
        .missing_frames = observed ? 0 : 1,
        .confirmed = true,
        .sent = sent,
        .observed_this_frame = observed,
    };
}

std::vector<dk::CompoundTarget> update(
    dk::CompoundTargetAssembler& assembler,
    std::initializer_list<dk::LineTrackSnapshot> lines) {
    return assembler.update(
        std::span<const dk::LineTrackSnapshot>{lines.begin(), lines.size()});
}

}  // namespace

TEST_CASE("common motion groups lines only after three observations") {
    dk::CompoundTargetAssembler assembler;

    auto provisional = update(assembler, {
        line(1, "PHANTOM", {100, 100, 220, 40}),
        line(2, "ASSASSIN", {100, 155, 220, 40}),
    });
    REQUIRE(provisional.size() == 2);
    CHECK(provisional.front().ambiguous);
    CHECK(provisional.back().ambiguous);

    provisional = update(assembler, {
        line(1, "PHANTOM", {100, 110, 220, 40}),
        line(2, "ASSASSIN", {100, 165, 220, 40}),
    });
    REQUIRE(provisional.size() == 2);
    CHECK(provisional.front().ambiguous);
    CHECK(provisional.back().ambiguous);

    const auto grouped = update(assembler, {
        line(1, "PHANTOM", {100, 120, 220, 40}),
        line(2, "ASSASSIN", {100, 175, 220, 40}),
    });
    REQUIRE(grouped.size() == 1);
    CHECK(grouped.front().normalized_text == "PHANTOMASSASSIN");
    CHECK((grouped.front().bounds == dk::Box{100, 120, 220, 95}));
    CHECK((grouped.front().line_ids == std::vector<dk::TrackId>{1, 2}));
    CHECK_FALSE(grouped.front().ambiguous);
    CHECK(grouped.front().observed_this_frame);
    CHECK_FALSE(grouped.front().sent_owned);
}

TEST_CASE("compound text follows visual reading order") {
    dk::CompoundTargetAssembler assembler;

    for (int frame = 0; frame < 3; ++frame) {
        const auto targets = update(assembler, {
            line(20, "LIGHT", {140, 160 + frame * 8, 160, 40}),
            line(10, "KEEPEROFTHE", {100, 105 + frame * 8, 280, 40}),
        });
        if (frame == 2) {
            REQUIRE(targets.size() == 1);
            CHECK(targets.front().normalized_text == "KEEPEROFTHELIGHT");
            CHECK((targets.front().line_ids ==
                   std::vector<dk::TrackId>{10, 20}));
        }
    }
}

TEST_CASE("independent motion and temporary proximity stay separate") {
    SECTION("independent velocities") {
        dk::CompoundTargetAssembler assembler;
        for (int frame = 0; frame < 4; ++frame) {
            const auto targets = update(assembler, {
                line(1, "ONE", {100, 100 + frame * 8, 180, 40}),
                line(2, "TWO", {100 + frame * 20, 155, 180, 40}),
            });
            CHECK(targets.size() == 2);
        }
    }

    SECTION("one close frame") {
        dk::CompoundTargetAssembler assembler;
        CHECK(update(assembler, {
                  line(1, "ONE", {100, 100, 180, 40}),
                  line(2, "TWO", {600, 500, 180, 40}),
              }).size() == 2);
        CHECK(update(assembler, {
                  line(1, "ONE", {100, 108, 180, 40}),
                  line(2, "TWO", {100, 163, 180, 40}),
              }).size() == 2);
        CHECK(update(assembler, {
                  line(1, "ONE", {100, 116, 180, 40}),
                  line(2, "TWO", {600, 516, 180, 40}),
              }).size() == 2);
    }
}

TEST_CASE("crossed associations require two clean frames to recover") {
    dk::CompoundTargetAssembler assembler;
    for (int frame = 0; frame < 3; ++frame) {
        update(assembler, {
            line(1, "TOP", {100, 100 + frame * 10, 180, 40}),
            line(2, "BOTTOM", {100, 155 + frame * 10, 180, 40}),
        });
    }

    auto targets = update(assembler, {
        line(1, "TOP", {100, 180, 180, 40}),
        line(2, "BOTTOM", {100, 125, 180, 40}),
    });
    REQUIRE(targets.size() == 1);
    CHECK(targets.front().ambiguous);

    targets = update(assembler, {
        line(1, "TOP", {100, 190, 180, 40}),
        line(2, "BOTTOM", {100, 135, 180, 40}),
    });
    REQUIRE(targets.size() == 1);
    CHECK(targets.front().ambiguous);

    targets = update(assembler, {
        line(1, "TOP", {100, 200, 180, 40}),
        line(2, "BOTTOM", {100, 145, 180, 40}),
    });
    REQUIRE(targets.size() == 1);
    CHECK_FALSE(targets.front().ambiguous);
    CHECK(targets.front().normalized_text == "BOTTOMTOP");
    CHECK((targets.front().line_ids == std::vector<dk::TrackId>{2, 1}));
}

TEST_CASE("compound ownership and observation include every member line") {
    dk::CompoundTargetAssembler assembler;
    for (int frame = 0; frame < 3; ++frame) {
        update(assembler, {
            line(1, "FIRST", {100, 100 + frame * 8, 180, 40}),
            line(2, "SECOND", {100, 155 + frame * 8, 180, 40}),
        });
    }

    auto targets = update(assembler, {
        line(1, "FIRST", {100, 124, 180, 40}, true),
        line(2, "SECOND", {100, 179, 180, 40}),
    });
    REQUIRE(targets.size() == 1);
    CHECK(targets.front().sent_owned);
    CHECK(targets.front().observed_this_frame);

    targets = update(assembler, {
        line(1, "FIRST", {100, 124, 180, 40}, true),
        line(2, "SECOND", {100, 179, 180, 40}, false, false),
    });
    REQUIRE(targets.size() == 1);
    CHECK_FALSE(targets.front().observed_this_frame);
}

TEST_CASE("provisional disappearance releases the unchanged survivor") {
    dk::CompoundTargetAssembler assembler;
    update(assembler, {
        line(1, "SURVIVOR", {100, 100, 180, 40}),
        line(2, "MISSING", {100, 155, 180, 40}),
    });
    update(assembler, {
        line(1, "SURVIVOR", {100, 108, 180, 40}),
        line(2, "MISSING", {100, 163, 180, 40}),
    });

    auto targets = update(assembler, {
        line(1, "SURVIVOR", {100, 116, 180, 40}),
        line(2, "MISSING", {100, 163, 180, 40}, false, false),
    });
    const auto first_recovery = std::ranges::find_if(
        targets, [](const auto& target) {
            return target.line_ids == std::vector<dk::TrackId>{1};
        });
    REQUIRE(first_recovery != targets.end());
    CHECK(first_recovery->ambiguous);

    targets = update(assembler, {
        line(1, "SURVIVOR", {100, 124, 180, 40}),
        line(2, "MISSING", {100, 163, 180, 40}, false, false),
    });
    const auto recovered = std::ranges::find_if(
        targets, [](const auto& target) {
            return target.line_ids == std::vector<dk::TrackId>{1};
        });
    REQUIRE(recovered != targets.end());
    CHECK_FALSE(recovered->ambiguous);
    CHECK(recovered->observed_this_frame);
}

TEST_CASE("grouped lines dissolve after two uniquely separated frames") {
    dk::CompoundTargetAssembler assembler;
    for (int frame = 0; frame < 3; ++frame) {
        update(assembler, {
            line(1, "FIRST", {100, 100 + frame * 8, 180, 40}),
            line(2, "SECOND", {100, 155 + frame * 8, 180, 40}),
        });
    }

    auto targets = update(assembler, {
        line(1, "FIRST", {100, 124, 180, 40}),
        line(2, "SECOND", {600, 179, 180, 40}),
    });
    REQUIRE(targets.size() == 1);
    CHECK(targets.front().ambiguous);

    targets = update(assembler, {
        line(1, "FIRST", {100, 132, 180, 40}),
        line(2, "SECOND", {600, 187, 180, 40}),
    });
    REQUIRE(targets.size() == 2);
    CHECK_FALSE(targets.front().ambiguous);
    CHECK_FALSE(targets.back().ambiguous);
}

TEST_CASE("provisional proximity never transfers sent ownership") {
    dk::CompoundTargetAssembler assembler;
    update(assembler, {
        line(1, "SENT", {100, 100, 180, 40}),
        line(2, "OTHER", {100, 155, 180, 40}),
    });
    update(assembler, {
        line(1, "SENT", {100, 108, 180, 40}),
        line(2, "OTHER", {130, 155, 180, 40}),
    });

    const auto targets = update(assembler, {
        line(1, "SENT", {100, 108, 180, 40}, true, false),
        line(2, "OTHER", {130, 155, 180, 40}, false, false),
        line(3, "FRESH", {100, 110, 180, 40}),
    });
    const auto fresh = std::ranges::find_if(
        targets, [](const auto& target) {
            return target.line_ids == std::vector<dk::TrackId>{3};
        });
    REQUIRE(fresh != targets.end());
    CHECK_FALSE(fresh->sent_owned);
}
