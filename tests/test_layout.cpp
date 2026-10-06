#include <cmath>

#include "minitest.h"
#include "../src/layout/dock_layout.h"

static md::LayoutInput five() {
    md::LayoutInput in;
    in.items.resize(5);
    return in;
}

TEST_CASE(layout_empty_dock) {
    md::LayoutInput in;
    auto r = md::computeLayout(in);
    CHECK(r.items.empty());
    CHECK_NEAR(r.restLength, 16, 1e-9);
    CHECK(std::isfinite(r.bgStart));
    CHECK(std::isfinite(r.bgEnd));
}

TEST_CASE(layout_rest_is_centered) {
    auto r = md::computeLayout(five());
    CHECK_NEAR(r.restLength, 5 * 48 + 4 * 6 + 16, 1e-9);
    CHECK_NEAR(r.items[2].center, 0, 1e-9);
    CHECK_NEAR(r.bgStart, -r.bgEnd, 1e-9);
    CHECK_NEAR(r.thickness, 64, 1e-9);
    for (auto& it : r.items) CHECK_NEAR(it.size, 48, 1e-9);
}

TEST_CASE(layout_magnifiedSize_curve) {
    CHECK_NEAR(md::magnifiedSize(0, 48, 128, 144), 128, 1e-9);
    CHECK_NEAR(md::magnifiedSize(144, 48, 128, 144), 48, 1e-9);
    CHECK_NEAR(md::magnifiedSize(-72, 48, 128, 144), 88, 1e-9);
    CHECK_NEAR(md::magnifiedSize(10, 48, 128, 0), 48, 1e-9);
}

TEST_CASE(layout_hovered_icon_reaches_large_and_stays_under_cursor) {
    auto in = five();
    in.amount = 1;
    in.cursor = 0;
    auto r = md::computeLayout(in);
    CHECK_NEAR(r.items[2].size, 128, 1e-6);
    CHECK_NEAR(r.items[2].center, 0, 1e-6);
    CHECK_NEAR(r.items[1].size, r.items[3].size, 1e-6);
    CHECK(r.items[1].size < 128);
    CHECK(r.items[0].size <= r.items[1].size);
    CHECK_NEAR(r.maxSize, 128, 1e-6);
}

TEST_CASE(layout_hover_left_edge_grows_left) {
    auto in = five();
    auto rest = md::computeLayout(in);
    in.amount = 1;
    in.cursor = rest.items[0].center;
    auto r = md::computeLayout(in);
    CHECK(r.bgStart < rest.bgStart - 30);
    CHECK(r.bgEnd - rest.bgEnd < r.items[0].size);
    CHECK_NEAR(r.items[0].center, rest.items[0].center, 1e-6);
}

TEST_CASE(layout_cursor_beyond_edges_is_clamped) {
    auto in = five();
    in.amount = 1;
    in.cursor = 1e9;
    auto r = md::computeLayout(in);
    for (auto& it : r.items) {
        CHECK(std::isfinite(it.center));
        CHECK(std::isfinite(it.size));
    }
    in.cursor = std::nan("");
    r = md::computeLayout(in);
    for (auto& it : r.items) CHECK(std::isfinite(it.center));
}

TEST_CASE(layout_amount_zero_equals_rest) {
    auto in = five();
    in.cursor = 10;
    in.amount = 0;
    auto r = md::computeLayout(in);
    auto rest = md::computeLayout(five());
    for (size_t i = 0; i < 5; ++i) CHECK_NEAR(r.items[i].center, rest.items[i].center, 1e-9);
}

TEST_CASE(layout_separator_not_magnified) {
    md::LayoutInput in;
    in.items = {{}, {true}, {}};
    in.amount = 1;
    in.cursor = 0;
    auto r = md::computeLayout(in);
    CHECK_NEAR(r.items[1].size, 1, 1e-9);
    auto rest = md::computeLayout(md::LayoutInput{.items = {{}, {true}, {}}});
    CHECK_NEAR(rest.restLength, 48 * 2 + 15 + 2 * 6 + 16, 1e-9);
}

TEST_CASE(layout_no_overlap) {
    auto in = five();
    in.amount = 1;
    for (double c = -200; c <= 200; c += 7) {
        in.cursor = c;
        auto r = md::computeLayout(in);
        for (size_t i = 1; i < 5; ++i)
            CHECK(r.items[i].center - r.items[i].size / 2 >=
                  r.items[i - 1].center + r.items[i - 1].size / 2 + 6 - 1e-6);
    }
}

TEST_CASE(layout_background_encloses_items) {
    auto in = five();
    in.amount = 0.6;
    in.cursor = 40;
    auto r = md::computeLayout(in);
    CHECK_NEAR(r.bgStart, r.items.front().center - r.items.front().size / 2 - 8, 1e-6);
    CHECK_NEAR(r.bgEnd, r.items.back().center + r.items.back().size / 2 + 8, 1e-6);
}
