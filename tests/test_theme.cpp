// Thème macOS : dessin des curseurs, fichiers .cur/.ani, fond d'écran, application et rétablissement.
#include <string>

#include "minitest.h"
#include "../src/theme/cursor_art.h"
#include "../src/theme/vector_art.h"

namespace {
const std::uint8_t* px(const md::BgraImage& im, int x, int y) { return &im.px[(std::size_t(y) * im.w + x) * 4]; }
}

TEST_CASE(theme_rasterize_fill_outline_clear) {
    md::Layer square{{md::Poly{{{8, 8}, {24, 8}, {24, 24}, {8, 24}}}}, 0xFF000000, 0xFFFFFFFF, 2.0};
    auto im = md::rasterize({square}, 32, 1.0, 0.0);
    REQUIRE(im.w == 32 && im.h == 32);
    CHECK(px(im, 16, 16)[3] == 255 && px(im, 16, 16)[0] < 10);     // cœur noir
    CHECK(px(im, 7, 16)[3] == 255 && px(im, 7, 16)[0] > 245);      // bordure blanche
    CHECK(px(im, 2, 2)[3] == 0);                                    // loin : transparent
}

TEST_CASE(theme_arrow_cursor) {
    auto f = md::cursorFrames(md::CursorKind::Arrow, 64);
    REQUIRE(f.size() == 1);
    CHECK(f[0].image.w == 64);
    CHECK(f[0].hotspot.x == 6 && f[0].hotspot.y == 4);              // pointe (3, 2) × 2
    const std::uint8_t* tip = px(f[0].image, 10, 16);                // dans le corps de la flèche
    CHECK(tip[3] == 255 && tip[0] < 30);
    CHECK(px(f[0].image, 60, 4)[3] == 0);
}

TEST_CASE(theme_cursor_kinds) {
    CHECK(md::cursorFrames(md::CursorKind::Wait, 32).size() == 12);
    CHECK(md::cursorFrames(md::CursorKind::AppStarting, 32).size() == 12);
    auto ns = md::cursorFrames(md::CursorKind::SizeNS, 32);
    REQUIRE(ns.size() == 1);
    CHECK(ns[0].hotspot.x == 16 && ns[0].hotspot.y == 16);
    CHECK(px(ns[0].image, 16, 16)[3] == 255);                        // tige au centre
    CHECK(px(ns[0].image, 4, 16)[3] == 0);                           // rien sur les côtés
    auto we = md::cursorFrames(md::CursorKind::SizeWE, 32);
    CHECK(px(we[0].image, 4, 16)[3] > 0);                            // tournée de 90°
    CHECK(std::wstring(md::cursorRegistryName(md::CursorKind::SizeNWSE)) == L"SizeNWSE");
    CHECK(std::wstring(md::cursorRegistryName(md::CursorKind::No)) == L"No");
}
