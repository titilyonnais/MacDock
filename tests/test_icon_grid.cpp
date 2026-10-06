#include "minitest.h"
#include "../src/icons/icon_grid.h"

namespace {
std::vector<std::uint8_t> opaque(int n) { return std::vector<std::uint8_t>(size_t(n) * n * 4, 255); }
int alphaAt(const std::vector<std::uint8_t>& p, int size, int x, int y) { return p[(size_t(y) * size + x) * 4 + 3]; }
} // namespace

TEST_CASE(icon_grid_shape_size) {
    CHECK_EQ(md::iconShapePx(96, 824.0 / 1024), 77);
    CHECK_EQ(md::iconShapePx(1, 0.8), 1);
}

TEST_CASE(icon_grid_place_centers_with_transparent_margin) {
    auto out = md::placeOnGrid(opaque(77), 77, 96);
    CHECK_EQ(out.size(), size_t(96 * 96 * 4));
    CHECK_EQ(alphaAt(out, 96, 2, 48), 0);
    CHECK_EQ(alphaAt(out, 96, 48, 48), 255);
    CHECK_EQ(alphaAt(out, 96, 93, 48), 0);
}

TEST_CASE(icon_grid_shadow_below_shape_only) {
    auto img = md::placeOnGrid(opaque(60), 60, 100);
    md::addDropShadow(img, 100, 100 * 14.0 / 1024, 100 * 12.0 / 1024, 0.5);
    CHECK(alphaAt(img, 100, 50, 82) > 0);                            // sous la forme : ombre
    CHECK(alphaAt(img, 100, 50, 82) < 128);
    CHECK_EQ(int(img[(82 * 100 + 50) * 4 + 0]), 0);                  // noire
    CHECK(alphaAt(img, 100, 50, 18) < alphaAt(img, 100, 50, 82));    // moins au-dessus (décalage vers le bas)
    CHECK_EQ(alphaAt(img, 100, 50, 50), 255);                        // forme intacte
}

TEST_CASE(icon_grid_shadow_zero_opacity_is_noop) {
    auto img = md::placeOnGrid(opaque(60), 60, 100), copy = img;
    md::addDropShadow(img, 100, 2, 2, 0);
    CHECK((img == copy));
}
