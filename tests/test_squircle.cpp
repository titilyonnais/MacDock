#include <vector>

#include "minitest.h"
#include "../src/icons/squircle.h"

TEST_CASE(squircle_center_inside_corner_outside) {
    CHECK(md::insideSquircle(50, 50, 100));
    CHECK(!md::insideSquircle(1, 1, 100));
    CHECK(md::insideSquircle(50, 1, 100));
    CHECK(!md::insideSquircle(50, 50, 0));
}

TEST_CASE(squircle_full_square_fits) {
    std::vector<uint8_t> px(64 * 64 * 4, 255);
    CHECK(md::iconFitsSquircle(px.data(), 64, 64, 64 * 4));
}

TEST_CASE(squircle_small_circle_does_not_fit) {
    std::vector<uint8_t> px(64 * 64 * 4, 0);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            if ((x - 32) * (x - 32) + (y - 32) * (y - 32) < 16 * 16) px[(y * 64 + x) * 4 + 3] = 255;
    CHECK(!md::iconFitsSquircle(px.data(), 64, 64, 64 * 4));
}

TEST_CASE(squircle_empty_image_is_safe) {
    CHECK(!md::iconFitsSquircle(nullptr, 0, 0, 0));
    CHECK_NEAR(md::squircleCoverage(nullptr, 0, 0, 0), 0, 1e-9);
}

TEST_CASE(squircle_mask_alpha_is_antialiased) {
    // Couverture d'un pixel : 1 au centre, 0 au coin, intermédiaire sur la courbe du coin (diagonale).
    CHECK_NEAR(md::squircleMaskAlpha(50, 50, 100), 1, 1e-9);
    CHECK_NEAR(md::squircleMaskAlpha(0, 0, 100), 0, 1e-9);
    double edge = md::squircleMaskAlpha(6, 6, 100);
    CHECK(edge > 0.1);
    CHECK(edge < 1.0);
}

TEST_CASE(squircle_straight_edge_is_full) {
    // Loin des coins, le bord gauche est droit : le pixel du bord est entièrement couvert.
    CHECK_NEAR(md::squircleMaskAlpha(0, 50, 100), 1, 1e-9);
}
