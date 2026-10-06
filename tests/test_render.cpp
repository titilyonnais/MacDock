#include <cstdlib>

#include "minitest.h"
#include "render_fixtures.h"

using namespace fixtures;

TEST_CASE(render_offscreen_corner_is_wallpaper_edge_is_glass) {
    ComScope com;
    md::DockRenderer r;
    REQUIRE(r.initOffscreen());
    auto f = sampleFrame(false, 2);
    md::Metrics m;
    auto img = r.renderToBgra(f, m, L"", kW, kH, flatWallpaper(kW, kH, 40, 40, 40));
    REQUIRE(img.size() == size_t(kW * kH * 4));
    auto at = [&](int x, int y) { return &img[(size_t(y) * kW + x) * 4]; };
    // Sommet du coin haut-gauche : fond d'écran intact (à l'ombre près : écart ≤ 12).
    int cx = int(f.bgLeft) + 1, cy = int(f.bgTop) + 1;
    CHECK(std::abs(int(at(cx, cy)[0]) - 40) <= 12);
    // Milieu du bord haut, 3 px sous le bord : verre clair (nettement plus clair que le fond gris foncé).
    CHECK(at(int(kW / 2), int(f.bgTop) + 3)[1] > 70);
}

TEST_CASE(render_indicator_drawn_below_visible_icon) {
    ComScope com;
    md::DockRenderer r;
    REQUIRE(r.initOffscreen());
    auto f = sampleFrame(false, 2);
    md::Metrics m;
    auto img = r.renderToBgra(f, m, L"", kW, kH, flatWallpaper(kW, kH, 200, 200, 200));
    const auto& ic = f.icons[1];
    auto px = [&](float x, float y) { return &img[(size_t(y) * kW + size_t(x)) * 4]; };
    CHECK(px(ic.cx, ic.indicatorY)[0] < 90);   // point sombre en mode clair
    float visibleBottom = ic.cy + ic.size / 2 - ic.size * float(1 - m.iconShapeRatio) / 2;
    float mid = (visibleBottom + ic.indicatorY - float(m.indicatorDiameter)) / 2;
    CHECK(px(ic.cx, mid)[0] > 120);            // espace libre entre l'icône et le point
}

TEST_CASE(render_dark_mode_indicator_is_light) {
    ComScope com;
    md::DockRenderer r;
    REQUIRE(r.initOffscreen());
    auto f = sampleFrame(true, 2);
    md::Metrics m;
    auto img = r.renderToBgra(f, m, L"", kW, kH, flatWallpaper(kW, kH, 30, 30, 30));
    const auto& ic = f.icons[1];
    CHECK(img[(size_t(ic.indicatorY) * kW + size_t(ic.cx)) * 4] > 170);
}

TEST_CASE(render_smooth_corner_not_circular) {
    // Coin continu : sur la première ligne du fond (Y ≈ 0,012·r), la courbe n'atteint le bord qu'à X ≈ 0,955·r
    // du sommet, contre 0,845·r pour un arc de cercle. À X = 0,90·r on doit donc encore voir le fond d'écran.
    ComScope com;
    md::DockRenderer r;
    REQUIRE(r.initOffscreen());
    auto f = sampleFrame(false, 2);
    md::Metrics m;
    m.shadowOpacity = 0;
    auto img = r.renderToBgra(f, m, L"", kW, kH, flatWallpaper(kW, kH, 0, 0, 0));
    float rl = float(md::limitedCornerRadius(f.bgRight - f.bgLeft, f.bgBottom - f.bgTop, f.cornerRadius));
    int x = int(f.bgLeft + 0.90f * rl), y = int(f.bgTop);
    CHECK(img[(size_t(y) * kW + x) * 4 + 1] < 60);
}
