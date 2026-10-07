// Effet génie : géométrie des bandes, aperçu hors écran, coupure de l'animation de Windows.
#include "minitest.h"
#include "../src/anim/genie.h"

namespace {
const RECT kWin{100, 100, 900, 700};   // 800 x 600
const RECT kTile{1500, 1040, 1548, 1076};
const SIZE kSrc{800, 600};

bool same(const RECT& a, const RECT& b) { return EqualRect(&a, &b) != FALSE; }

bool contiguous(const std::vector<md::GenieSlice>& s, bool vertical) {
    for (std::size_t i = 1; i < s.size(); ++i) {
        if (vertical ? s[i].dst.top != s[i - 1].dst.bottom : false) return false;
        if (s[i].src.top < s[i - 1].src.top && vertical) return false;
    }
    return true;
}
} // namespace

TEST_CASE(genie_start_is_window) {
    auto s = md::minimizeFrame(md::MinimizeEffect::Genie, kSrc, kWin, kTile, md::DockPosition::Bottom, 0.0);
    REQUIRE(s.size() == 48);
    CHECK_EQ(s.front().dst.top, kWin.top);
    CHECK_EQ(s.back().dst.bottom, kWin.bottom);
    CHECK_EQ(s.front().src.top, 0L);
    CHECK_EQ(s.back().src.bottom, 600L);
    for (auto& x : s) {
        CHECK_EQ(x.dst.left, kWin.left);
        CHECK_EQ(x.dst.right, kWin.right);
        CHECK_EQ(x.src.left, 0L);
        CHECK_EQ(x.src.right, 800L);
    }
    CHECK(contiguous(s, true));
}

TEST_CASE(genie_end_is_tile) {
    auto s = md::minimizeFrame(md::MinimizeEffect::Genie, kSrc, kWin, kTile, md::DockPosition::Bottom, 1.0);
    REQUIRE(!s.empty());
    CHECK_EQ(s.front().dst.top, kTile.top);
    CHECK_EQ(s.back().dst.bottom, kTile.bottom);
    for (auto& x : s) {
        CHECK(x.dst.left >= kTile.left - 1 && x.dst.right <= kTile.right + 1);
        CHECK(x.dst.bottom >= x.dst.top);
    }
}

TEST_CASE(genie_middle_narrows_toward_dock) {
    auto s = md::minimizeFrame(md::MinimizeEffect::Genie, kSrc, kWin, kTile, md::DockPosition::Bottom, 0.5);
    REQUIRE(s.size() > 2);
    CHECK(contiguous(s, true));
    LONG prev = s.front().dst.right - s.front().dst.left;
    for (auto& x : s) {   // plus étroit en allant vers le Dock
        const LONG w = x.dst.right - x.dst.left;
        CHECK(w <= prev + 1);
        prev = w;
    }
    CHECK(s.back().dst.right - s.back().dst.left < 800);
}

TEST_CASE(genie_left_dock_uses_columns) {
    const RECT win{400, 100, 1000, 500};       // 600 x 400
    const RECT tile{8, 300, 56, 336};          // Dock à gauche
    auto s = md::minimizeFrame(md::MinimizeEffect::Genie, SIZE{600, 400}, win, tile, md::DockPosition::Left, 0.0);
    REQUIRE(!s.empty());
    for (auto& x : s) {   // colonnes : hauteur complète
        CHECK_EQ(x.src.top, 0L);
        CHECK_EQ(x.src.bottom, 400L);
        CHECK_EQ(x.dst.top, win.top);
        CHECK_EQ(x.dst.bottom, win.bottom);
    }
    // La première bande est la plus éloignée du Dock (bord droit de la fenêtre).
    CHECK_EQ(s.front().src.right, 600L);
    CHECK_EQ(s.front().dst.right, win.right);
    CHECK_EQ(s.back().dst.left, win.left);
    auto e = md::minimizeFrame(md::MinimizeEffect::Genie, SIZE{600, 400}, win, tile, md::DockPosition::Left, 1.0);
    for (auto& x : e) CHECK(x.dst.top >= tile.top - 1 && x.dst.bottom <= tile.bottom + 1 && x.dst.left >= tile.left - 1);
}

TEST_CASE(genie_right_dock_end_in_tile) {
    const RECT win{100, 100, 700, 500};
    const RECT tile{1860, 300, 1908, 336};
    auto e = md::minimizeFrame(md::MinimizeEffect::Genie, SIZE{600, 400}, win, tile, md::DockPosition::Right, 1.0);
    REQUIRE(!e.empty());
    CHECK_EQ(e.front().src.left, 0L);   // la colonne la plus éloignée du Dock (bord gauche) d'abord
    for (auto& x : e) CHECK(x.dst.left >= tile.left - 1 && x.dst.right <= tile.right + 1);
}

TEST_CASE(genie_scale_and_windows) {
    auto s = md::minimizeFrame(md::MinimizeEffect::Scale, kSrc, kWin, kTile, md::DockPosition::Bottom, 0.5);
    REQUIRE(s.size() == 1);
    CHECK(s[0].dst.left > kWin.left && s[0].dst.left < kTile.left);
    CHECK_EQ(s[0].src.right, 800L);
    auto e = md::minimizeFrame(md::MinimizeEffect::Scale, kSrc, kWin, kTile, md::DockPosition::Bottom, 1.0);
    CHECK(same(e[0].dst, kTile));
    CHECK(md::minimizeFrame(md::MinimizeEffect::Windows, kSrc, kWin, kTile, md::DockPosition::Bottom, 0.5).empty());
}

TEST_CASE(genie_degenerate_inputs) {
    CHECK(md::minimizeFrame(md::MinimizeEffect::Genie, SIZE{0, 0}, kWin, kTile, md::DockPosition::Bottom, 0.5).empty());
    auto tiny = md::minimizeFrame(md::MinimizeEffect::Genie, SIZE{800, 3}, kWin, kTile, md::DockPosition::Bottom, 0.5);
    CHECK(tiny.size() == 3);   // pas plus de bandes que de lignes
    for (auto& x : tiny) CHECK(x.src.bottom > x.src.top);
    // Case au-dessus de la fenêtre (Dock en bas mais fenêtre plus bas que la case) : pas de division par zéro.
    auto odd = md::minimizeFrame(md::MinimizeEffect::Genie, kSrc, RECT{100, 1040, 900, 1060}, kTile, md::DockPosition::Bottom, 0.5);
    for (auto& x : odd) CHECK(x.dst.right >= x.dst.left && x.dst.bottom >= x.dst.top);
}

TEST_CASE(genie_durations) {
    CHECK_NEAR(md::minimizeDuration(md::MinimizeEffect::Genie, false), 0.55, 1e-9);
    CHECK_NEAR(md::minimizeDuration(md::MinimizeEffect::Scale, false), 0.3, 1e-9);
    CHECK_NEAR(md::minimizeDuration(md::MinimizeEffect::Genie, true), 4.4, 1e-9);
    CHECK_EQ(md::minimizeDuration(md::MinimizeEffect::Windows, false), 0.0);
}

TEST_CASE(genie_restored_rect) {
    WINDOWPLACEMENT wp{sizeof wp};
    wp.rcNormalPosition = RECT{100, 50, 900, 650};
    const RECT monitor{0, 0, 1920, 1080}, work{0, 48, 1920, 1000};   // barre de menus en haut
    RECT r = md::restoredRect(wp, work, monitor, false, SIZE{800, 600});
    CHECK(same(r, RECT{100, 98, 900, 698}));   // décalé de la zone de travail
    r = md::restoredRect(wp, work, monitor, true, SIZE{800, 600});   // fenêtre outil : coordonnées écran
    CHECK(same(r, wp.rcNormalPosition));
    wp.flags = WPF_RESTORETOMAXIMIZED;   // agrandie : la zone de travail et ses bordures invisibles
    r = md::restoredRect(wp, work, monitor, false, SIZE{1936, 968});
    CHECK(same(r, RECT{-8, 40, 1928, 1008}));
}
