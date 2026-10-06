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
    // Le bord du verre est anticrénelé sur ±0,5 px et porte un liseré clair : un pixel couvert au quart reste
    // sous 120, alors qu'un arc de cercle le couvrirait aux trois quarts (≈ 190).
    CHECK(img[(size_t(y) * kW + x) * 4 + 1] < 120);
}

TEST_CASE(render_glass_refracts_near_edge) {
    // Bandes horizontales de 8 px : près du bord haut, la réfraction (vers le centre, donc verticale)
    // fait lire une autre bande ; sans réfraction, le verre montre la bande située juste derrière.
    ComScope com;
    md::DockRenderer r;
    REQUIRE(r.initOffscreen());
    auto f = sampleFrame(false, 2);
    md::Metrics m;
    m.glassBlur = 0.5;
    auto stripes = stripedWallpaper(kW, kH, 8, true);
    m.glassRefraction = 0;
    auto flat = r.renderToBgra(f, m, L"", kW, kH, stripes);
    m.glassRefraction = 1.5;
    auto bent = r.renderToBgra(f, m, L"", kW, kH, stripes);
    REQUIRE(!flat.empty());
    REQUIRE(!bent.empty());
    int y = int(f.bgTop) + 6, diff = 0;
    for (int x = int(f.bgLeft) + 60; x < int(f.bgRight) - 60; ++x)
        diff += std::abs(int(flat[(size_t(y) * kW + x) * 4]) - int(bent[(size_t(y) * kW + x) * 4]));
    CHECK(diff > 2000);
}

TEST_CASE(render_glass_readable_on_white_and_black) {
    ComScope com;
    md::DockRenderer r;
    REQUIRE(r.initOffscreen());
    md::Metrics m;
    for (bool dark : {false, true})
        for (int v : {0, 255}) {
            auto f = sampleFrame(dark, 2);
            auto img = r.renderToBgra(f, m, L"", kW, kH, flatWallpaper(kW, kH, std::uint8_t(v), std::uint8_t(v), std::uint8_t(v)));
            REQUIRE(!img.empty());
            // Verre « profond » (hors biseau, sans icône) : à côté du séparateur, à mi-hauteur.
            int glassY = int((f.bgTop + f.bgBottom) / 2), x = int(f.icons[3].cx) + 6;
            double lum = img[(size_t(glassY) * kW + x) * 4 + 1] / 255.0;
            if (dark) {
                CHECK(lum >= 0.05);
                CHECK(lum <= 0.45);
            } else {
                CHECK(lum >= 0.28);
                CHECK(lum <= 0.95);
            }
            const auto& ic = f.icons[1];   // le point contraste avec le verre (écart ≥ 60)
            int dot = img[(size_t(ic.indicatorY) * kW + size_t(ic.cx)) * 4 + 1];
            CHECK(std::abs(dot - int(lum * 255)) >= 60);
        }
}

TEST_CASE(render_glass_hdr_backdrop_not_blown_out) {
    // Fond scRGB à 0,6 avec un blanc SDR à 3 (240 nits) : 0,2 en linéaire, soit 124 en sRGB. Le rendu doit égaler
    // celui d'un fond SDR à 124 (écart ≤ 8) ; si l'échelle du blanc était ignorée (1), il serait nettement plus clair.
    ComScope com;
    md::DockRenderer r;
    REQUIRE(r.initOffscreen());
    md::Metrics m;
    auto f = sampleFrame(false, 2);
    auto sdr = r.renderToBgra(f, m, L"", kW, kH, flatWallpaper(kW, kH, 124, 124, 124));
    auto hdr = r.renderToBgraScRgb(f, m, L"", kW, kH, 0.6f, 3.0f);
    auto unscaled = r.renderToBgraScRgb(f, m, L"", kW, kH, 0.6f, 1.0f);
    REQUIRE(!sdr.empty());
    REQUIRE(!hdr.empty());
    REQUIRE(!unscaled.empty());
    int x = int(f.icons[3].cx) + 6, y = int((f.bgTop + f.bgBottom) / 2);   // à côté du séparateur
    auto g = [&](const std::vector<std::uint8_t>& img) { return int(img[(size_t(y) * kW + x) * 4 + 1]); };
    CHECK(std::abs(g(sdr) - g(hdr)) <= 8);
    CHECK(g(unscaled) - g(sdr) > 20);
}

TEST_CASE(render_glass_shadow_outside_only) {
    ComScope com;
    md::DockRenderer r;
    REQUIRE(r.initOffscreen());
    md::Metrics m;
    auto f = sampleFrame(false, 2);
    auto img = r.renderToBgra(f, m, L"", kW, kH, flatWallpaper(kW, kH, 230, 230, 230));
    REQUIRE(!img.empty());
    int below = img[(size_t(f.bgBottom) + 4) * kW * 4 + size_t(kW / 2) * 4 + 1];
    CHECK(below < 230);   // ombre sous le Dock
    CHECK(below > 150);   // douce
}
