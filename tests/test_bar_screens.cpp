// Barre de menus : une barre par écran (liste, écran actif, mise en page) et barres atténuées.
#include <windows.h>
#include <objbase.h>

#include "minitest.h"
#include "../src/menubar/bar_layout.h"
#include "../src/menubar/bar_renderer.h"
#include "../src/menubar/bar_screens.h"

namespace {

md::ScreenInfo screen(LONG l, LONG t, LONG r, LONG b, UINT dpi = 96, bool primary = false) {
    return md::ScreenInfo{RECT{l, t, r, b}, dpi, primary};
}

} // namespace

TEST_CASE(menubar_screens_order_and_active) {
    // Principal au milieu, un écran à gauche (coordonnées négatives), un à droite.
    auto list = md::orderScreens({screen(1920, 0, 3840, 1080), screen(-1280, 0, 0, 1024), screen(0, 0, 1920, 1080, 96, true)});
    REQUIRE(list.size() == 3);
    CHECK(list[0].primary);
    CHECK_EQ(list[1].rect.left, -1280L);
    CHECK_EQ(list[2].rect.left, 1920L);

    const RECT onRight{2000, 100, 3000, 900};
    CHECK_EQ(md::activeScreen(list, &onRight, POINT{10, 10}), std::size_t(2));
    const RECT straddle{1700, 100, 2500, 900};   // plus de la moitié à droite
    CHECK_EQ(md::activeScreen(list, &straddle, POINT{10, 10}), std::size_t(2));
    CHECK_EQ(md::activeScreen(list, nullptr, POINT{-50, 300}), std::size_t(1));   // bureau : écran du curseur
    const RECT nowhere{9000, 9000, 9100, 9100};
    CHECK_EQ(md::activeScreen(list, &nowhere, POINT{-50, 300}), std::size_t(1));  // hors de tout écran : le curseur
    CHECK_EQ(md::activeScreen(list, nullptr, POINT{99999, 0}), std::size_t(0));   // curseur perdu : le principal
    CHECK_EQ(md::activeScreen({}, nullptr, POINT{0, 0}), std::size_t(0));
}

TEST_CASE(menubar_screens_layout) {   // chaque barre est mise en page à la largeur de son écran, en points
    const auto wide = screen(0, 0, 3840, 2160, 192, true);   // 4K à 200 % : 1920 pt
    const auto narrow = screen(3840, 0, 5120, 1024, 96);       // 1280 pt
    CHECK_NEAR(md::barWidthPoints(wide), 1920.0, 1e-9);
    CHECK_NEAR(md::barWidthPoints(narrow), 1280.0, 1e-9);
    md::BarLayoutInput in;
    in.leftWidths = std::vector<double>(14, 90.0);   // logo, app et douze menus
    in.rightWidths = {30, 30, 30, 150};
    in.barWidth = md::barWidthPoints(wide);
    const auto a = md::layoutBar(in);
    in.barWidth = md::barWidthPoints(narrow);
    const auto b = md::layoutBar(in);
    CHECK_EQ(a.leftVisible, std::size_t(14));
    CHECK(b.leftVisible < a.leftVisible);   // l'écran étroit masque des menus
    CHECK(b.rightX.back() < a.rightX.back());
}

TEST_CASE(bar_renderer_dims_inactive) {   // barre d'un écran inactif : 60 %
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    {
        md::BarRenderer r;
        REQUIRE(r.initOffscreen());
        r.setFont(L"", 13);
        md::BarFrame f;
        f.darkText = true;
        md::BarDrawItem icon;
        icon.glyph = md::Glyph::ControlCenter;
        icon.x = 0;
        icon.width = 40;
        f.items.push_back(icon);
        const UINT w = 40, h = 24;
        std::vector<std::uint8_t> bg(size_t(w) * h * 4, 0), full, dim;
        REQUIRE(r.renderToImage(f, bg, w, h, full));
        f.opacity = 0.6f;
        REQUIRE(r.renderToImage(f, bg, w, h, dim));
        int maxFull = 0, maxDim = 0;
        for (size_t i = 3; i < full.size(); i += 4) {
            maxFull = std::max(maxFull, int(full[i]));
            maxDim = std::max(maxDim, int(dim[i]));
        }
        CHECK(maxFull > 150);
        CHECK_NEAR(double(maxDim) / maxFull, 0.6, 0.05);
    }
    CoUninitialize();
}

TEST_CASE(menubar_screens_plan_keeps_and_replaces) {   // écrans branchés ou débranchés : rien de nul en cours de route
    const std::vector<RECT> existing{{0, 0, 1920, 1080}, {1920, 0, 3840, 1080}};
    const std::vector<md::ScreenInfo> wanted{screen(0, 0, 1920, 1080, 96, true), screen(-1280, 0, 0, 1024)};
    const auto plan = md::planScreens(existing, wanted);
    REQUIRE(plan.keep.size() == 2);
    CHECK_EQ(plan.keep[0], 0);    // principal gardé
    CHECK_EQ(plan.keep[1], -1);   // nouvel écran : barre à créer
    REQUIRE(plan.drop.size() == 1);
    CHECK_EQ(plan.drop[0], std::size_t(1));   // écran débranché
    const auto same = md::planScreens(existing, {screen(0, 0, 1920, 1080, 96, true), screen(1920, 0, 3840, 1080)});
    CHECK(same.drop.empty());
    CHECK(same.keep == (std::vector<int>{0, 1}));
}

TEST_CASE(menubar_screens_rebuild_gate) {   // refaire les écrans : jamais imbriqué, jamais sous un menu
    md::RebuildGate g;
    CHECK(!g.tryBegin(true));   // menu ouvert : plus tard
    CHECK(g.pending);
    CHECK(g.tryBegin(false));
    CHECK(!g.pending);
    CHECK(!g.tryBegin(false));   // WM_DISPLAYCHANGE envoyé pendant SHAppBarMessage : pas d'imbrication
    CHECK(g.end());              // une demande est arrivée pendant : il faut recommencer
    CHECK(g.tryBegin(false));
    CHECK(!g.end());
}

TEST_CASE(menubar_screens_top_edge_stacked) {   // écran du bas sous un écran du haut : le curseur en haut de l'écran du
    const RECT top{0, 0, 1920, 1080};            // haut ne fait pas apparaître la barre du bas
    const RECT bottom{0, 1080, 1920, 2160};
    CHECK(md::cursorAtTopEdge(bottom, POINT{500, 1080}));
    CHECK(!md::cursorAtTopEdge(bottom, POINT{500, 10}));
    CHECK(md::cursorAtTopEdge(top, POINT{500, 0}));
    CHECK(md::cursorInBar(bottom, 48, POINT{500, 1100}));
    CHECK(!md::cursorInBar(bottom, 48, POINT{500, 20}));
}

TEST_CASE(menubar_tray_fit) {   // trop d'icônes d'apps : les plus à gauche sont écartées, le logo et l'app restent
    md::BarLayoutInput in;
    in.barWidth = 1280;
    in.leftWidths = {34, 80, 60, 60};
    in.rightWidths = {30, 30, 150};   // icônes système et horloge
    CHECK_EQ(md::trayFit(in, 5, 30), std::size_t(5));
    const std::size_t fit = md::trayFit(in, 60, 30);
    CHECK(fit < 60);
    in.rightWidths.insert(in.rightWidths.begin(), fit, 30.0);
    const auto l = md::layoutBar(in);
    CHECK(l.rightX.front() >= in.leftMargin + 34 + 80 + in.minGap - 1e-9);   // rien sous le logo ni le nom de l'app
}
