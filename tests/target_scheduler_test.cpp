#include <initializer_list>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "dk/target_scheduler.hpp"

namespace {

dk::CompoundTarget target(
    std::initializer_list<dk::TrackId> ids, std::string text, dk::Box bounds,
    bool ambiguous = false, bool observed = true, bool sent = false) {
    return {
        .line_ids = ids,
        .normalized_text = std::move(text),
        .bounds = bounds,
        .ambiguous = ambiguous,
        .observed_this_frame = observed,
        .sent_owned = sent,
    };
}

std::optional<dk::CompoundTarget> select(
    std::initializer_list<dk::CompoundTarget> targets) {
    return dk::select_lowest_ready(
        std::span<const dk::CompoundTarget>{targets.begin(), targets.size()});
}

}  // namespace

TEST_CASE("scheduler excludes every non-ready target state") {
    const auto selected = select({
        target({1}, "AMBIGUOUS", {0, 500, 100, 40}, true),
        target({2}, "MISSING", {0, 450, 100, 40}, false, false),
        target({3}, "", {0, 400, 100, 40}),
        target({4}, "SENT", {0, 350, 100, 40}, false, true, true),
        target({5}, "READY", {0, 100, 100, 40}),
    });

    REQUIRE(selected);
    CHECK(selected->normalized_text == "READY");
    CHECK_FALSE(select({
        target({1}, "AMBIGUOUS", {0, 500, 100, 40}, true),
        target({2}, "MISSING", {0, 450, 100, 40}, false, false),
        target({3}, "", {0, 400, 100, 40}),
        target({4}, "SENT", {0, 350, 100, 40}, false, true, true),
    }));
}

TEST_CASE("scheduler chooses greatest bottom then greatest center") {
    SECTION("greatest bottom") {
        const auto selected = select({
            target({1}, "HIGH", {0, 100, 100, 40}),
            target({2}, "LOW", {0, 300, 100, 40}),
        });
        REQUIRE(selected);
        CHECK(selected->normalized_text == "LOW");
    }

    SECTION("greatest center for equal bottoms") {
        const auto selected = select({
            target({1}, "TALL", {0, 100, 100, 40}),
            target({2}, "SHORT", {0, 120, 100, 20}),
        });
        REQUIRE(selected);
        CHECK(selected->normalized_text == "SHORT");
    }
}

TEST_CASE("scheduler breaks exact geometry ties by line IDs") {
    const auto selected = select({
        target({8, 9}, "LARGER_IDS", {0, 100, 100, 40}),
        target({2, 7}, "SMALLER_IDS", {200, 100, 100, 40}),
        target({2, 8}, "MIDDLE_IDS", {400, 100, 100, 40}),
    });

    REQUIRE(selected);
    CHECK(selected->normalized_text == "SMALLER_IDS");
    CHECK((selected->line_ids == std::vector<dk::TrackId>{2, 7}));
}
