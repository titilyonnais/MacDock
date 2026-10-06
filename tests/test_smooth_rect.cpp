#include <algorithm>
#include <cmath>

#include "minitest.h"
#include "../src/geom/smooth_rect.h"

TEST_CASE(smooth_rect_radius_is_limited) {
    CHECK_NEAR(md::limitedCornerRadius(100, 40, 30), 40 / 2 / md::kCornerExtent, 1e-9);
    CHECK_NEAR(md::limitedCornerRadius(100, 100, 10), 10, 1e-9);
    CHECK_NEAR(md::limitedCornerRadius(100, 100, -5), 0, 1e-9);
    CHECK_NEAR(md::limitedCornerRadius(0, 0, 10), 0, 1e-9);
    CHECK(std::isfinite(md::limitedCornerRadius(NAN, 10, 10)));
}

TEST_CASE(smooth_rect_outline_inside_bounds_and_closed_shape) {
    auto p = md::smoothRectOutline(10, 20, 200, 64, 21.4);
    CHECK(p.size() > 40);
    for (auto& q : p) {
        CHECK(q.x >= 10 - 1e-9);
        CHECK(q.x <= 210 + 1e-9);
        CHECK(q.y >= 20 - 1e-9);
        CHECK(q.y <= 84 + 1e-9);
    }
    CHECK(md::pointInPolygon(p, 110, 52));
    CHECK(!md::pointInPolygon(p, 10.5, 20.5));   // sommet du coin : dehors
    CHECK(md::pointInPolygon(p, 110, 20.5));     // milieu du bord haut : dedans
}

TEST_CASE(smooth_rect_corner_is_symmetric) {
    auto p = md::smoothRectOutline(0, 0, 100, 100, 20, 16);
    // Chaque point a son symétrique par rapport à la diagonale y = x (même contour).
    for (auto& q : p) {
        double best = 1e9;
        for (auto& o : p) best = std::min(best, std::hypot(o.x - q.y, o.y - q.x));
        CHECK(best < 1e-6);
    }
}

TEST_CASE(smooth_rect_diagonal_matches_apple_curve) {
    // Le point diagonal du coin continu est à ≈ 0,2915·r du sommet sur chaque axe.
    auto p = md::smoothRectOutline(0, 0, 100, 100, 20, 64);
    double best = 1e9;
    for (auto& q : p)
        if (q.x < 50 && q.y < 50 && std::fabs(q.x - q.y) < 0.2) best = std::min(best, q.x);
    CHECK_NEAR(best / 20, 0.2915, 0.01);
}

TEST_CASE(smooth_rect_area_close_to_circular_corners) {
    // Un coin continu retire un peu plus d'aire qu'un quart de cercle ((4 - π)·r²), mais du même ordre.
    auto p = md::smoothRectOutline(0, 0, 200, 200, 20, 32);
    double removed = 200.0 * 200.0 - md::polygonArea(p);
    CHECK(removed > (4 - 3.14159265) * 400 * 0.95);
    CHECK(removed < (4 - 3.14159265) * 400 * 1.6);
}

TEST_CASE(smooth_rect_zero_radius_is_rectangle) {
    auto p = md::smoothRectOutline(0, 0, 10, 5, 0);
    CHECK_NEAR(md::polygonArea(p), 50, 1e-9);
}

TEST_CASE(smooth_square_mask_center_corner_edge) {
    auto m = md::smoothSquareMask(100, 0.225);
    CHECK_NEAR(m[50 * 100 + 50], 1, 1e-9);
    CHECK_NEAR(m[0], 0, 1e-9);
    CHECK_NEAR(m[0 * 100 + 50], 1, 1e-9);   // milieu du bord haut : plein
    double e = m[6 * 100 + 6];              // sur la courbe du coin (rayon 22,5 → diagonale ≈ 6,56)
    CHECK(e > 0.05);
    CHECK(e < 0.95);
}

TEST_CASE(corner_field_signs_and_edges) {
    auto f = md::cornerDistanceField(128, -2, 2);
    CHECK(md::sampleCornerField(f, 128, -2, 2, -1.5, -1.5) > 1.5);                   // dehors, loin
    CHECK(md::sampleCornerField(f, 128, -2, 2, 1.9, 1.9) < -1.5);                     // dedans, loin
    CHECK_NEAR(md::sampleCornerField(f, 128, -2, 2, 1.8, 0.5), -0.5, 0.03);           // bord horizontal droit
    CHECK_NEAR(md::sampleCornerField(f, 128, -2, 2, 0.2915, 0.2915), 0, 0.03);        // sur la courbe
}

TEST_CASE(corner_field_is_lipschitz) {
    const int n = 64;
    auto f = md::cornerDistanceField(n, -2, 2);
    double cell = 4.0 / n;
    for (int j = 0; j < n; ++j)
        for (int i = 0; i + 1 < n; ++i) {
            CHECK(std::fabs(f[j * n + i + 1] - f[j * n + i]) <= cell * 1.05);
            CHECK(std::fabs(f[i * n + j] - f[(i + 1) * n + j]) <= cell * 1.05);
        }
}
