#include <vector>

#include "minitest.h"
#include "../src/calib/image_diff.h"

TEST_CASE(diff_identical_is_zero) {
    std::vector<std::uint8_t> a(4 * 4 * 4, 100);
    auto s = md::diffImages(a.data(), a.data(), 4, 4, 10);
    CHECK(s.sameSize);
    CHECK_NEAR(s.meanAbs, 0, 1e-9);
    CHECK_EQ(s.maxAbs, 0);
    CHECK_NEAR(s.fractionAbove, 0, 1e-9);
}

TEST_CASE(diff_counts_pixels_above_threshold) {
    std::vector<std::uint8_t> a(2 * 2 * 4, 100), b = a;
    b[0] = 160;   // un pixel, canal B, écart 60
    auto s = md::diffImages(a.data(), b.data(), 2, 2, 24);
    CHECK_EQ(s.maxAbs, 60);
    CHECK_NEAR(s.fractionAbove, 0.25, 1e-9);
    CHECK_NEAR(s.meanAbs, 60.0 / 12, 1e-9);   // 12 canaux B, G, R
}

TEST_CASE(diff_heatmap_colors) {
    std::vector<std::uint8_t> a(4, 0), b(4, 0);
    a[2] = 200;   // a plus clair (rouge)
    auto h = md::diffHeatmap(a.data(), b.data(), 1, 1);
    CHECK(h[2] > h[0]);
    auto g = md::diffHeatmap(a.data(), a.data(), 1, 1);
    CHECK_EQ(int(g[0]), int(g[2]));
}

TEST_CASE(diff_null_inputs_are_safe) {
    auto s = md::diffImages(nullptr, nullptr, 0, 0, 10);
    CHECK(!s.sameSize);
    CHECK(md::diffHeatmap(nullptr, nullptr, 0, 0).empty());
}
