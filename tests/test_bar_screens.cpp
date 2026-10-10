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

TEST_CASE(bar_renderer_text_sits_on_its_baseline) {
    // Plan 64 : le texte est posé sur sa ligne de base, à 16 pt du haut d'une barre de 24 comme sur macOS 26, quelle que
    // soit la police (avant : boîte de ligne centrée, base à 16,5 pt avec SF Pro et 17,5 avec Segoe UI).
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    {
        md::BarRenderer r;
        REQUIRE(r.initOffscreen());
        // Première ligne sans encre sous « HIT » (pieds plats : l'encre s'arrête net sur la ligne de base), en pixels.
        auto inkBottom = [&](float scale, double barPt, double baselinePt, bool bold, double fontPt = 13) {
            r.setFont(L"Segoe UI", float(fontPt) * scale);
            md::BarFrame f;
            f.scale = scale;
            f.darkText = true;
            f.metrics.height = barPt;
            f.metrics.fontSize = fontPt;
            f.metrics.textBaseline = baselinePt;
            md::BarDrawItem it;
            it.text = L"HIT";
            it.bold = bold;
            it.x = 0;
            it.width = 100 * scale;
            f.items.push_back(it);
            const UINT w = UINT(100 * scale), h = UINT(std::lround(barPt * scale));
            std::vector<std::uint8_t> bg(size_t(w) * h * 4, 0), out;
            if (!r.renderToImage(f, bg, w, h, out)) return -1;
            int bottom = -1;
            for (UINT y = 0; y < h; ++y)
                for (UINT x = 0; x < w; ++x)
                    if (out[(size_t(y) * w + x) * 4 + 3] > 128) bottom = int(y) + 1;
            return bottom;
        };
        CHECK_EQ(inkBottom(2, 24, 16, false), 32);
        CHECK_EQ(inkBottom(2, 24, 16, true), 32);   // nom de l'app en gras : même ligne de base
        CHECK_EQ(inkBottom(1, 24, 16, false), 16);
        CHECK_EQ(inkBottom(2, 24, 15.5, false), 31);
        CHECK_EQ(inkBottom(2, 30, 16, false), 38);   // barre plus haute : même écart à son milieu (30 + 8)
        // Relecture : texte plus grand, écart au milieu à sa mesure (20/13 de 4 pt) ; sinon il remonte vers le haut.
        CHECK_EQ(inkBottom(2, 34, 16, false, 20), 46);   // 34 + 4 x 20/13 x 2 = 46,3
        CHECK_EQ(inkBottom(1, 24, 16, false, 9), 15);    // 12 + 4 x 9/13 = 14,8
    }
    CoUninitialize();
}

TEST_CASE(bar_renderer_multiline_title_keeps_first_line) {
    // Relecture du plan 64 : un titre à saut de ligne (description d'exécutable, menu) se dessinait sous la barre ; sa
    // première ligne se pose sur la ligne de base, comme un titre d'une ligne.
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    {
        md::BarRenderer r;
        REQUIRE(r.initOffscreen());
        r.setFont(L"Segoe UI", 26);
        md::BarFrame f;
        f.scale = 2;
        f.darkText = true;
        md::BarDrawItem it;
        it.text = L"HIT\nHIT";
        it.x = 0;
        it.width = 200;
        f.items.push_back(it);
        const UINT w = 200, h = 48;
        std::vector<std::uint8_t> bg(size_t(w) * h * 4, 0), out;
        REQUIRE(r.renderToImage(f, bg, w, h, out));
        auto rowInk = [&](UINT y) {
            int n = 0;
            for (UINT x = 0; x < w; ++x) n += out[(size_t(y) * w + x) * 4 + 3] > 128;
            return n;
        };
        CHECK(rowInk(31) > 0);    // pieds de la première ligne sur la ligne de base (32 px)
        CHECK(rowInk(20) > 0);    // corps des lettres dans la barre
        CHECK_EQ(rowInk(5), 0);   // rien au-dessus des majuscules
    }
    CoUninitialize();
}

TEST_CASE(bar_renderer_clock_uses_tabular_figures) {
    // Plan 64 : l'horloge de macOS 26 a des chiffres à chasse fixe (« 22:17 » : 35 pt sur la VM, 32 avec les chiffres
    // proportionnels de SF Pro) ; elle ne bouge plus d'une minute à l'autre. Segoe UI, comme SF Pro : chiffres
    // proportionnels par défaut, tabulaires avec tnum.
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    {
        md::BarRenderer r;
        REQUIRE(r.initOffscreen());
        REQUIRE(r.setFont(L"Segoe UI", 26) == L"Segoe UI");
        {
            CHECK(r.measure(L"1111", false) < r.measure(L"0000", false) - 2);   // proportionnels
            CHECK_NEAR(r.measure(L"1111", false, true), r.measure(L"0000", false, true), 0.01);
            // Dessin : « 1111 » tabulaire s'étale comme « 0000 ».
            auto inkWidth = [&](const wchar_t* text, bool tabular) {
                md::BarFrame f;
                f.scale = 2;
                f.darkText = true;
                md::BarDrawItem it;
                it.text = text;
                it.tabular = tabular;
                it.x = 0;
                it.width = 200;
                f.items.push_back(it);
                const UINT w = 200, h = 48;
                std::vector<std::uint8_t> bg(size_t(w) * h * 4, 0), out;
                if (!r.renderToImage(f, bg, w, h, out)) return -1;
                int l = w, rr = -1;
                for (UINT y = 0; y < h; ++y)
                    for (UINT x = 0; x < w; ++x)
                        if (out[(size_t(y) * w + x) * 4 + 3] > 128) l = std::min(l, int(x)), rr = std::max(rr, int(x));
                return rr - l;
            };
            CHECK(inkWidth(L"1111", true) > inkWidth(L"1111", false) + 4);
        }
    }
    CoUninitialize();
}

TEST_CASE(bar_renderer_boxed_code_caps_centered) {
    // Plan 64 : le texte de la barre se pose sur sa ligne de base, mais « FR » garde sa place : ses majuscules sont au
    // milieu de la boîte (à 0,5 px près avec SF Pro, Segoe UI, Inter Tight, Arial et Georgia).
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    {
        md::BarRenderer r;
        REQUIRE(r.initOffscreen());
        r.setFont(L"Segoe UI", 26);
        md::BarFrame f;
        f.scale = 2;
        f.darkText = true;
        md::BarDrawItem it;
        it.text = L"FR";
        it.boxed = true;
        it.x = 0;
        it.width = 72;
        f.items.push_back(it);
        const UINT w = 72, h = 48;
        std::vector<std::uint8_t> bg(size_t(w) * h * 4, 0), out;
        REQUIRE(r.renderToImage(f, bg, w, h, out));
        // Boîte de 30 px de haut, centrée (9 → 39), traits de 2,4 px ; lettres : colonnes du milieu, entre les traits.
        int top = -1, bottom = -1;
        for (UINT y = 13; y < 36; ++y)
            for (UINT x = 24; x < 48; ++x)
                if (out[(size_t(y) * w + x) * 4 + 3] > 128) {
                    if (top < 0) top = int(y);
                    bottom = int(y) + 1;
                }
        REQUIRE(top >= 0);
        CHECK_NEAR((top + bottom) / 2.0, 24.0, 0.75);   // milieu des majuscules au milieu de la boîte
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

TEST_CASE(menubar_tray_fit_counts_the_clock_gap) {
    // Plan 64 : l'écart avant l'horloge prend de la place aux icônes d'apps.
    md::BarLayoutInput in;
    in.barWidth = 1000;
    in.leftWidths = {34, 80};
    in.rightWidths = {150};
    CHECK_EQ(md::trayFit(in, 30, 36), std::size_t(19));   // 1000 - 10 - 10 - 114 - 20 - 150 = 696 : 19 cases de 36
    in.lastGap = 13;                                       // 683 : 18
    CHECK_EQ(md::trayFit(in, 30, 36), std::size_t(18));
}
