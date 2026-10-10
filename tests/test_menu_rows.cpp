// Menus en verre : lignes enrichies (intitulés, curseurs, interrupteurs, tuiles, lecture en cours) et pictogrammes.
#include <windows.h>
#include <d2d1.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <array>
#include <cmath>

#include "minitest.h"
#include "../src/calib/png_io.h"
#include "../src/popup/glyphs.h"
#include "../src/popup/menu_model.h"
#include "../src/popup/menu_window.h"
#include <d3d11.h>

namespace {

md::MenuItem row(md::MenuRow r, int id = 0) {
    md::MenuItem it;
    it.row = r;
    it.id = id;
    return it;
}

md::MenuModel controlCenter() {
    md::MenuModel m;
    m.width = 300;
    md::MenuItem tiles = row(md::MenuRow::Tiles, 10);
    tiles.tiles = {{L"Wi-Fi", L"Maison", md::Glyph::Wifi, true}, {L"Bluetooth", L"Désactivé", md::Glyph::Bluetooth, false}};
    md::MenuItem header = row(md::MenuRow::Header);
    header.text = L"Son";
    md::MenuItem slider = row(md::MenuRow::Slider, 11);
    slider.glyph = md::Glyph::Speaker;
    slider.value = 0.4;
    md::MenuItem toggle = row(md::MenuRow::Toggle, 12);
    toggle.text = L"Wi-Fi";
    md::MenuItem media = row(md::MenuRow::Media, 13);
    media.text = L"Titre";
    media.subtitle = L"Artiste";
    md::MenuItem normal;
    normal.id = 14;
    normal.text = L"Réglages du Centre de contrôle…";
    m.items = {tiles, header, slider, toggle, media, {}, normal};
    return m;
}

} // namespace

TEST_CASE(menu_rows_layout_heights_and_width) {
    auto m = controlCenter();
    auto l = md::layoutMenu(m, 500, 0);   // texte très long : la largeur fixe l'emporte
    CHECK_NEAR(l.width, 300, 1e-9);
    REQUIRE(l.top.size() == 7);
    CHECK_NEAR(l.top[0], md::kMenuPadding, 1e-9);
    CHECK_NEAR(l.top[1] - l.top[0], md::menuRowHeight(md::MenuRow::Tiles), 1e-9);
    CHECK_NEAR(l.top[3] - l.top[2], md::menuRowHeight(md::MenuRow::Slider), 1e-9);
    CHECK_NEAR(l.top[6] - l.top[5], md::kMenuSeparatorHeight, 1e-9);
    CHECK(md::menuRowHeight(md::MenuRow::Tiles) > md::menuRowHeight(md::MenuRow::Normal));
    CHECK(!m.items[0].separator());   // une ligne enrichie sans identifiant n'est pas un séparateur
    CHECK(!m.items[1].separator());
    CHECK_EQ(md::rowAt(l, m, l.top[2] + 1), 2);   // curseur : touché, bien que non sélectionnable
    CHECK_EQ(md::hitTestMenu(l, m, l.top[2] + 1), -1);
}

TEST_CASE(menu_rows_slider_value_clamped) {
    const double w = 290;
    CHECK_NEAR(md::sliderValueAt(w, 0), 0, 1e-9);
    CHECK_NEAR(md::sliderValueAt(w, -50), 0, 1e-9);
    CHECK_NEAR(md::sliderValueAt(w, 5000), 1, 1e-9);
    const double left = md::kMenuSliderLeft, right = w - md::kMenuSliderRight;
    CHECK_NEAR(md::sliderValueAt(w, (left + right) / 2), 0.5, 1e-9);
    CHECK_NEAR(md::sliderValueAt(w, left), 0, 1e-9);
}

TEST_CASE(menu_rows_tile_and_media_hit) {
    const double w = 290;
    CHECK_EQ(md::tileAt(2, w, 10), 0);
    CHECK_EQ(md::tileAt(2, w, w - 10), 1);
    CHECK_EQ(md::tileAt(2, w, w / 2), -1);   // dans l'espace entre les tuiles
    CHECK_EQ(md::tileAt(0, w, 10), -1);
    CHECK_EQ(md::mediaButtonAt(w, w - md::kMenuMediaRight - 1), 2);   // suivant, tout à droite
    CHECK_EQ(md::mediaButtonAt(w, w - md::kMenuMediaRight - md::kMenuMediaButton * 1.5), 1);
    CHECK_EQ(md::mediaButtonAt(w, w - md::kMenuMediaRight - md::kMenuMediaButton * 2.5), 0);
    CHECK_EQ(md::mediaButtonAt(w, 20), -1);   // sur le titre
}

TEST_CASE(menu_rows_header_not_selectable) {
    auto m = controlCenter();
    CHECK(!m.items[1].selectable());   // intitulé
    CHECK(!m.items[2].selectable());   // curseur : à la souris seulement
    CHECK_EQ(md::nextSelectable(m, -1, +1), 6);   // le clavier va droit aux entrées ordinaires
}

TEST_CASE(menu_rows_refresh_updates_model) {
    auto m = controlCenter();
    auto refresh = [](md::MenuModel& x) {
        x.items[2].value = 0.9;            // volume changé par les touches
        x.items[4].text = L"Suivant";      // morceau suivant
        x.items[0].tiles[1].on = true;
        return true;
    };
    CHECK(md::applyRefresh(m, refresh, 0));
    CHECK_NEAR(m.items[2].value, 0.9, 1e-9);
    CHECK(m.items[4].text == L"Suivant");
    CHECK(m.items[0].tiles[1].on);

    m.items[2].value = 0.3;   // l'utilisateur glisse le curseur : sa valeur l'emporte
    CHECK(md::applyRefresh(m, refresh, 11));
    CHECK_NEAR(m.items[2].value, 0.3, 1e-9);

    auto restructure = [](md::MenuModel& x) {   // la structure ne change jamais menu ouvert
        x.items.pop_back();
        return true;
    };
    CHECK(!md::applyRefresh(m, restructure, 0));
    CHECK_EQ(m.items.size(), std::size_t(7));
    CHECK(!md::applyRefresh(m, [](md::MenuModel&) { return false; }, 0));
}

TEST_CASE(glyphs_draw_every_glyph) {
    using Microsoft::WRL::ComPtr;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    {
        ComPtr<IWICImagingFactory> wic;
        REQUIRE(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))));
        ComPtr<ID2D1Factory> d2d;
        REQUIRE(SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf())));
        for (int g = int(md::Glyph::Speaker); g <= int(md::Glyph::Next); ++g) {
            ComPtr<IWICBitmap> bmp;
            REQUIRE(SUCCEEDED(wic->CreateBitmap(32, 32, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp)));
            ComPtr<ID2D1RenderTarget> rt;
            REQUIRE(SUCCEEDED(d2d->CreateWicBitmapRenderTarget(bmp.Get(), D2D1::RenderTargetProperties(), &rt)));
            ComPtr<ID2D1SolidColorBrush> ink;
            rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), &ink);
            rt->BeginDraw();
            rt->Clear(D2D1::ColorF(0, 0, 0, 0));
            md::drawGlyph(rt.Get(), md::Glyph(g), D2D1::RectF(4, 4, 28, 28), ink.Get(), 0.7f, true);
            CHECK(SUCCEEDED(rt->EndDraw()));
            WICRect all{0, 0, 32, 32};
            std::vector<BYTE> px(32 * 32 * 4);
            bmp->CopyPixels(&all, 32 * 4, UINT(px.size()), px.data());
            int opaque = 0;
            for (size_t i = 3; i < px.size(); i += 4) opaque += px[i] > 40;
            if (opaque < 12) fprintf(stderr, "pictogramme %d vide (%d)\n", g, opaque);
            CHECK(opaque >= 12);
        }
    }
    CoUninitialize();
}

namespace {
// Pictogramme dessiné à l'échelle k dans une case de 16 pt, encre opaque : couverture (0 à 1) de chaque pixel.
struct GlyphImage {
    int k = 1, n = 0;
    std::vector<float> cov;
    float at(double xPt, double yPt) const {   // couverture du pixel qui contient ce point
        const int x = int(xPt * k), y = int(yPt * k);
        return x >= 0 && y >= 0 && x < n && y < n ? cov[size_t(y) * n + x] : 0.0f;
    }
    // Encre (pt) le long d'un segment, échantillonnée finement (interpolation bilinéaire).
    double inkAlong(double x0, double y0, double x1, double y1) const {
        const double len = std::hypot(x1 - x0, y1 - y0), step = 0.02;
        double sum = 0;
        for (double t = step / 2; t < len; t += step) {
            const double x = (x0 + (x1 - x0) * t / len) * k - 0.5, y = (y0 + (y1 - y0) * t / len) * k - 0.5;
            const int xi = int(std::floor(x)), yi = int(std::floor(y));
            const double fx = x - xi, fy = y - yi;
            auto c = [&](int a, int b) { return a >= 0 && b >= 0 && a < n && b < n ? double(cov[size_t(b) * n + a]) : 0.0; };
            sum += (c(xi, yi) * (1 - fx) * (1 - fy) + c(xi + 1, yi) * fx * (1 - fy) + c(xi, yi + 1) * (1 - fx) * fy +
                    c(xi + 1, yi + 1) * fx * fy) * step;
        }
        return sum;
    }
    // Boîte de l'encre à 50 % (pt) : gauche, haut, droite, bas.
    std::array<double, 4> box() const {
        int l = n, t = n, r = -1, b = -1;
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
                if (cov[size_t(y) * n + x] >= 0.5f) {
                    l = std::min(l, x), t = std::min(t, y), r = std::max(r, x), b = std::max(b, y);
                }
        return {double(l) / k, double(t) / k, double(r + 1) / k, double(b + 1) / k};
    }
};

GlyphImage drawAlone(md::Glyph g, int k) {
    using Microsoft::WRL::ComPtr;
    GlyphImage out;
    out.k = k;
    out.n = 16 * k;
    const UINT n = UINT(out.n);
    ComPtr<IWICImagingFactory> wic;
    ComPtr<ID2D1Factory> d2d;
    ComPtr<IWICBitmap> bmp;
    ComPtr<ID2D1RenderTarget> rt;
    ComPtr<ID2D1SolidColorBrush> ink;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) ||
        FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf())) ||
        FAILED(wic->CreateBitmap(n, n, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp)) ||
        FAILED(d2d->CreateWicBitmapRenderTarget(
            bmp.Get(), D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(), 96, 96), &rt)))
        return out;
    rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), &ink);
    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(0, 0, 0, 0));
    md::drawGlyph(rt.Get(), g, D2D1::RectF(0, 0, float(n), float(n)), ink.Get());
    if (FAILED(rt->EndDraw())) return out;
    std::vector<BYTE> px(size_t(n) * n * 4);
    WICRect all{0, 0, int(n), int(n)};
    bmp->CopyPixels(&all, n * 4, UINT(px.size()), px.data());
    out.cov.resize(size_t(n) * n);
    for (size_t i = 0; i < out.cov.size(); ++i) out.cov[i] = float(px[i * 4 + 3]) / 255.0f;
    return out;
}
} // namespace

TEST_CASE(glyphs_search_measured_on_macos_26) {
    // Plan 64 : loupe de la barre de macOS 26, ajustée sur la capture de la VM (en points d'une case de 16, la case
    // centrée sur la barre de 24) : anneau fin, manche plus épais, un peu plus bas que le milieu de la barre.
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    {
        const GlyphImage g = drawAlone(md::Glyph::Search, 8);
        REQUIRE(!g.cov.empty());
        const auto b = g.box();
        CHECK_NEAR(b[2] - b[0], 12.52, 0.25);        // largeur
        CHECK_NEAR(b[3] - b[1], 12.66, 0.25);        // hauteur
        CHECK_NEAR((b[0] + b[2]) / 2, 8.0, 0.25);    // centrée dans sa case
        CHECK_NEAR(b[1], 2.36, 0.25);                // haut : 6,36 dans la barre
        CHECK_NEAR(g.inkAlong(0, 7.525, 6.9, 7.525), 1.32, 0.1);   // trait de l'anneau, à gauche
        // Manche à 45,6° : encre le long d'une perpendiculaire, à 7,5 pt du centre de l'anneau (6,9 ; 7,525).
        const double a = 45.64 * 3.14159265358979 / 180, mx = 6.905 + 7.5 * std::cos(a), my = 7.525 + 7.5 * std::sin(a);
        CHECK_NEAR(g.inkAlong(mx - 2 * std::sin(a), my + 2 * std::cos(a), mx + 2 * std::sin(a), my - 2 * std::cos(a)), 1.90, 0.12);
    }
    CoUninitialize();
}

TEST_CASE(glyphs_control_center_measured_on_macos_26) {
    // Plan 64 : deux interrupteurs, comme macOS 26 (ajustés sur la VM) : en haut, piste creuse et bouton plein à gauche ;
    // en bas, piste pleine et bouton creux à droite.
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    {
        const GlyphImage g = drawAlone(md::Glyph::ControlCenter, 8);
        REQUIRE(!g.cov.empty());
        const auto b = g.box();
        CHECK_NEAR(b[2] - b[0], 13.19, 0.25);
        CHECK_NEAR(b[3] - b[1], 13.30, 0.25);
        CHECK_NEAR((b[0] + b[2]) / 2, 8.0, 0.25);
        CHECK_NEAR(b[1], 1.69, 0.25);                               // haut : 5,69 dans la barre
        CHECK_NEAR(g.inkAlong(12, 0, 12, 8.9), 2 * 1.27, 0.15);     // piste du haut, à droite : deux traits de contour
        CHECK(g.at(5.7, 4.93) > 0.95f);                             // bouton plein
        CHECK(g.at(5.7, 3.2) < 0.25f);                              // jour entre le contour et le bouton
        CHECK(g.at(4.0, 12.08) > 0.95f);                            // piste du bas : pleine à gauche
        CHECK(g.at(10.65, 12.09) < 0.05f);                          // bouton creux : vide
        CHECK_NEAR(g.inkAlong(12.9, 12.09, 16, 12.09), 1.21, 0.1);  // bord à droite du bouton creux
    }
    CoUninitialize();
}

TEST_CASE(menu_rows_snapshot_offscreen) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    md::MenuWindow::Env env;
    env.scale = 2;
    env.dark = true;
    std::vector<std::uint8_t> px;
    UINT w = 0, h = 0;
    REQUIRE(md::MenuWindow::snapshot(env, controlCenter(), px, w, h));
    CHECK_EQ(w, UINT(600));   // largeur fixe 300 pt à l'échelle 2
    CHECK(h > 2 * (md::kMenuTilesHeight + md::kMenuSliderHeight));
    REQUIRE(px.size() == size_t(w) * h * 4);
    CHECK(px[3] < 40);                                   // coin arrondi : transparent
    CHECK(px[(size_t(h / 2) * w + w / 2) * 4 + 3] > 200);   // milieu du panneau : opaque
    wchar_t dump[MAX_PATH] = {};   // MACDOCK_DUMP=dossier : image du menu pour un contrôle à l'œil
    if (GetEnvironmentVariableW(L"MACDOCK_DUMP", dump, MAX_PATH)) {
        md::writePng(std::wstring(dump) + L"\\menu-rows-dark.png", px.data(), w, h);
        env.dark = false;
        if (md::MenuWindow::snapshot(env, controlCenter(), px, w, h))
            md::writePng(std::wstring(dump) + L"\\menu-rows-light.png", px.data(), w, h);
    }
    CoUninitialize();
}

TEST_CASE(menu_dump_edit_for_eyes) {   // MACDOCK_DUMP=dossier : menu Édition, à comparer à une capture de macOS 27
    wchar_t dump[MAX_PATH] = {};
    if (!GetEnvironmentVariableW(L"MACDOCK_DUMP", dump, MAX_PATH)) return;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    auto item = [](int id, const wchar_t* text, const wchar_t* shortcut = L"", bool enabled = true) {
        md::MenuItem it;
        it.id = id;
        it.text = text;
        it.shortcut = shortcut;
        it.enabled = enabled;
        return it;
    };
    md::MenuModel m;
    m.items = {item(1, L"Annuler", L"Ctrl+Z", false), item(2, L"Rétablir", L"Ctrl+Maj+Z", false), {},
               item(3, L"Couper", L"Ctrl+X"), item(4, L"Copier", L"Ctrl+C"), item(5, L"Coller", L"Ctrl+V"),
               item(6, L"Supprimer"), item(7, L"Tout sélectionner", L"Ctrl+A"), {}};
    md::MenuItem find = item(8, L"Rechercher");
    find.submenu = {item(9, L"Rechercher…", L"Ctrl+F")};
    m.items.push_back(find);
    md::MenuWindow::Env env;
    env.scale = 2;
    for (bool dark : {false, true}) {
        env.dark = dark;
        std::vector<std::uint8_t> px;
        UINT w = 0, h = 0;
        CHECK(md::MenuWindow::snapshot(env, m, px, w, h));
        CHECK(md::writePng(std::wstring(dump) + (dark ? L"\\menu-edit-dark.png" : L"\\menu-edit-light.png"), px.data(), w, h));
    }
    CoUninitialize();
}

TEST_CASE(menu_rows_layout_icons) {
    // Plan 51 : rangée d'icônes de disposition (menu de la pastille verte), cases égales sur la largeur de la ligne.
    using namespace md;
    CHECK(menuRowHeight(MenuRow::Layouts) > kMenuItemHeight);
    const double w = 200, slot = (w - 2 * kMenuLayoutSide) / 4;
    CHECK_NEAR(layoutIconLeft(4, w, 0), kMenuLayoutSide + (slot - kMenuLayoutIconW) / 2, 1e-9);
    CHECK_NEAR(layoutIconLeft(4, w, 3) - layoutIconLeft(4, w, 2), slot, 1e-9);
    CHECK_EQ(layoutIconAt(4, w, kMenuLayoutSide + 1), 0);   // bord de sa case, à côté de l'icône : elle encore
    CHECK_EQ(layoutIconAt(4, w, kMenuLayoutSide + slot + 1), 1);
    CHECK_EQ(layoutIconAt(4, w, w - kMenuLayoutSide - 1), 3);
    CHECK_EQ(layoutIconAt(4, w, kMenuLayoutSide - 1), -1);
    CHECK_EQ(layoutIconAt(4, w, w - kMenuLayoutSide + 1), -1);
    CHECK_EQ(layoutIconAt(0, w, 50), -1);
    CHECK_NEAR(layoutsRowWidth(4), 2 * kMenuLayoutSide + 4 * (kMenuLayoutIconW + kMenuLayoutGap), 1e-9);
    // Menu aux textes courts : élargi pour sa rangée.
    MenuModel m;
    MenuItem icons;
    icons.row = MenuRow::Layouts;
    icons.id = 5;
    icons.tiles.resize(4);
    m.items = {icons};
    const MenuLayout l = layoutMenu(m, 10, 0);
    CHECK(l.width >= 2 * kMenuPadding + layoutsRowWidth(4));
    CHECK(!m.items[0].selectable());   // à la souris seulement, comme les autres lignes enrichies
    CHECK(!m.items[0].separator());
    CHECK_EQ(rowAt(l, m, l.top[0] + 1), 0);
    CHECK_EQ(hitTestMenu(l, m, l.top[0] + 1), -1);
}

TEST_CASE(menu_rows_header_width_counts) {
    // Un intitulé long (« Déplacer et redimensionner ») élargit le menu au lieu d'être tronqué.
    md::MenuModel m;
    md::MenuItem header;
    header.row = md::MenuRow::Header;
    header.text = L"Déplacer et redimensionner";
    m.items = {header};
    CHECK(md::layoutMenu(m, 10, 0, 300).width >= 2 * md::kMenuPadding + 2 * md::kMenuHeaderInset + 300);
    CHECK_NEAR(md::layoutMenu(m, 10, 0, 0).width, md::kMenuMinWidth, 1e-9);
}

TEST_CASE(menu_rows_layout_icons_drawn) {
    // La case de la fenêtre est pleine, le reste de l'écran miniature vide, à la bonne place.
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    md::MenuModel m;
    md::MenuItem header = row(md::MenuRow::Header);
    header.text = L"Déplacer et redimensionner";
    md::MenuItem icons = row(md::MenuRow::Layouts, 20);
    icons.tiles.resize(2);
    icons.tiles[0].id = 21;
    icons.tiles[0].boxes = {{0, 0, 0.5f, 1}};   // moitié gauche
    icons.tiles[1].id = 22;
    icons.tiles[1].boxes = {{0.5f, 0, 1, 1}};   // moitié droite
    m.items = {header, icons};
    md::MenuWindow::Env env;
    env.scale = 2;
    std::vector<std::uint8_t> px;
    UINT w = 0, h = 0;
    REQUIRE(md::MenuWindow::snapshot(env, m, px, w, h));
    const double sc = 2, rowW = double(w) / sc - 2 * md::kMenuPadding;
    const double top = md::kMenuPadding + md::kMenuHeaderHeight;
    auto green = [&](double xPt, double yPt) { return px[(std::size_t(yPt * sc) * w + std::size_t(xPt * sc)) * 4 + 1]; };
    const double innerW = md::kMenuLayoutIconW - 2 * md::kMenuLayoutInset;
    const double cy = top + md::kMenuLayoutsHeight / 2;
    const double i0 = md::kMenuPadding + md::layoutIconLeft(2, rowW, 0) + md::kMenuLayoutInset;
    const double i1 = md::kMenuPadding + md::layoutIconLeft(2, rowW, 1) + md::kMenuLayoutInset;
    CHECK(green(i0 + innerW * 0.25, cy) < 150);   // thème clair : case pleine, sombre
    CHECK(green(i0 + innerW * 0.75, cy) > 200);   // l'autre moitié : le fond du menu
    CHECK(green(i1 + innerW * 0.75, cy) < 150);
    CHECK(green(i1 + innerW * 0.25, cy) > 200);
    wchar_t dump[MAX_PATH] = {};   // MACDOCK_DUMP=dossier : image pour un contrôle à l'œil
    if (GetEnvironmentVariableW(L"MACDOCK_DUMP", dump, MAX_PATH))
        md::writePng(std::wstring(dump) + L"\\menu-layouts-light.png", px.data(), w, h);
    CoUninitialize();
}

TEST_CASE(menu_close_returns_focus) {
    // Relecture du plan 51 : un menu fermé par Échap ou par un clic à côté rend le clavier à l'app d'avant ; fermé parce
    // que l'utilisateur est passé ailleurs (Alt+Tab, ⊞), le premier plan reste à ce qu'il a choisi.
    CHECK(md::menuCloseReturnsFocus(md::MenuClose::Escape));
    CHECK(md::menuCloseReturnsFocus(md::MenuClose::Outside));
    CHECK(!md::menuCloseReturnsFocus(md::MenuClose::Deactivated));
    CHECK(!md::menuCloseReturnsFocus(md::MenuClose::Chosen));     // l'action choisie décide
    CHECK(!md::menuCloseReturnsFocus(md::MenuClose::Switched));   // un autre menu de la barre s'ouvre
}

TEST_CASE(menu_rows_follow_accent) {   // plan 57 : la tuile allumée du Centre de contrôle sur l'accent choisi
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    auto count = [](const std::vector<std::uint8_t>& px, std::uint32_t rgb) {   // pixels opaques de cette teinte
        int n = 0;
        for (std::size_t i = 0; i + 3 < px.size(); i += 4) {
            if (px[i + 3] < 250) continue;
            if (std::abs(int(px[i + 2]) - int((rgb >> 16) & 255)) <= 6 && std::abs(int(px[i + 1]) - int((rgb >> 8) & 255)) <= 6 &&
                std::abs(int(px[i]) - int(rgb & 255)) <= 6)
                ++n;
        }
        return n;
    };
    md::MenuWindow::Env env;
    env.scale = 2;
    env.dark = true;
    std::vector<std::uint8_t> px;
    UINT w = 0, h = 0;
    env.accent = md::AccentColor::Purple;
    REQUIRE(md::MenuWindow::snapshot(env, controlCenter(), px, w, h));
    CHECK(count(px, 0xDB34F2) > 100);   // violet du thème sombre
    env.accent = md::AccentColor::Blue;
    REQUIRE(md::MenuWindow::snapshot(env, controlCenter(), px, w, h));
    CHECK(count(px, 0xDB34F2) == 0);
    CHECK(count(px, 0x0091FF) > 100);   // le bleu de macOS 26, le même que Réglages
    CoUninitialize();
}

namespace {
// Fenêtres de menu affichées (classe « MacDockMenu ») et touche envoyée au menu ouvert, depuis une minuterie du fil du
// menu (sa boucle modale distribue les WM_TIMER).
std::vector<HWND> menuWindows() {
    std::vector<HWND> out;
    EnumWindows([](HWND h, LPARAM l) {
        wchar_t cls[32] = {};
        GetClassNameW(h, cls, 32);
        if (wcscmp(cls, L"MacDockMenu") == 0 && IsWindowVisible(h)) reinterpret_cast<std::vector<HWND>*>(l)->push_back(h);
        return TRUE;
    }, LPARAM(&out));
    return out;
}
UINT g_keyToSend = 0;
std::vector<HWND> g_seenDuringMenu;
void CALLBACK sendKeyToMenu(HWND, UINT, UINT_PTR id, DWORD) {
    KillTimer(nullptr, id);
    g_seenDuringMenu = menuWindows();
    for (HWND h : g_seenDuringMenu) PostMessageW(h, WM_KEYDOWN, g_keyToSend, 0);
}
} // namespace

TEST_CASE(menu_window_keep_hands_off_between_bar_menus) {
    // Plan 71 : d'un titre de la barre à l'autre, chaque menu recréait tout (graphismes, verre, capture de l'écran),
    // ~90 ms sans aucun menu, puis un fondu. Avec une séance gardée, l'ancien menu reste affiché jusqu'au premier rendu
    // du suivant.
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    REQUIRE(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                        D3D11_SDK_VERSION, &device, nullptr, nullptr)));
    md::MenuWindow::Env env;
    env.instance = GetModuleHandleW(nullptr);
    env.device = device.Get();
    env.glass = false;      // verre dépoli : pas de capture de l'écran pendant les tests
    env.activate = false;   // le menu de test ne prend pas le clavier de l'utilisateur
    md::MenuModel m;
    md::MenuItem a, b;
    a.id = 1;
    a.text = L"Un";
    b.id = 2;
    b.text = L"Deux";
    m.items = {a, b};
    md::MenuWindow::BarLink link;
    link.titles = {RECT{0, 0, 60, 24}, RECT{60, 0, 120, 24}};
    link.current = 0;
    md::MenuWindow::Keep keep;
    md::MenuClose closed = md::MenuClose::Escape;
    g_keyToSend = VK_RIGHT;   // titre voisin
    SetTimer(nullptr, 0, 60, sendKeyToMenu);
    const int first = md::MenuWindow::track(env, m, POINT{10, 30}, md::MenuWindow::Side::Below, &link, nullptr, &closed, &keep);
    CHECK(md::menuSwitchTarget(first) == std::optional<int>(1));
    CHECK(closed == md::MenuClose::Switched);
    const std::vector<HWND> kept = menuWindows();
    REQUIRE(kept.size() == 1);   // l'ancien menu reste affiché en attendant le suivant
    link.current = 1;
    g_keyToSend = VK_ESCAPE;
    SetTimer(nullptr, 0, 60, sendKeyToMenu);
    md::MenuWindow::track(env, m, POINT{70, 30}, md::MenuWindow::Side::Below, &link, nullptr, &closed, &keep);
    CHECK(closed == md::MenuClose::Escape);
    CHECK(g_seenDuringMenu.size() == 1);   // un seul menu à l'écran : le nouveau
    CHECK(!IsWindow(kept[0]));             // l'ancien, retiré dès que le nouveau s'est affiché
    CHECK(menuWindows().empty());          // fin de séance : plus rien
}
