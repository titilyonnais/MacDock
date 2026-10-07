// Placement des miniatures DWM dans la case d'une fenêtre réduite.
#include "minitest.h"
#include "../src/app/thumbnails.h"

TEST_CASE(thumbnail_fit_landscape) {
    RECT r = md::fitThumbnail(1600, 900, RECT{0, 0, 100, 100});
    CHECK_EQ(r.right - r.left, 80L);
    CHECK_EQ(r.bottom - r.top, 45L);
    CHECK_EQ(r.left, 10L);
    CHECK(r.top == 27L || r.top == 28L);   // centré (arrondi)
}

TEST_CASE(thumbnail_fit_portrait) {
    RECT r = md::fitThumbnail(900, 1600, RECT{200, 50, 300, 150});
    CHECK_EQ(r.bottom - r.top, 80L);
    CHECK_EQ(r.right - r.left, 45L);
    CHECK_EQ(r.top, 60L);
    CHECK(r.left == 227L || r.left == 228L);
}

TEST_CASE(thumbnail_fit_zero_source) {
    RECT r = md::fitThumbnail(0, 900, RECT{0, 0, 100, 100});
    CHECK(r.right <= r.left);
    RECT s = md::fitThumbnail(1600, 900, RECT{0, 0, 0, 0});
    CHECK(s.right <= s.left);
}
