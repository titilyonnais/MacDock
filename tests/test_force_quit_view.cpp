// Forcer à quitter (plan 54) : mise en page et dessin de la fenêtre, hors écran (1 px = 1 pt).
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cmath>
#include <string>
#include <vector>

#include "minitest.h"
#include "../src/forcequit/force_quit_view.h"
#include "../src/ui/ui_draw.h"
#include "../src/calib/png_io.h"
#include "../src/forcequit/force_quit_window.h"

namespace {

struct Canvas {
    Microsoft::WRL::ComPtr<IWICBitmap> bmp;
    Microsoft::WRL::ComPtr<ID2D1RenderTarget> rt;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwrite;
    UINT w, h;
    std::vector<BYTE> px;
    Canvas(UINT width, UINT height) : w(width), h(height) {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        Microsoft::WRL::ComPtr<IWICImagingFactory> wic;
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
        Microsoft::WRL::ComPtr<ID2D1Factory> d2d;
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf());
        wic->CreateBitmap(w, h, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp);
        d2d->CreateWicBitmapRenderTarget(bmp.Get(), D2D1::RenderTargetProperties(), &rt);
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dwrite.GetAddressOf()));
    }
    void read() {
        px.resize(std::size_t(w) * h * 4);
        WICRect all{0, 0, int(w), int(h)};
        bmp->CopyPixels(&all, w * 4, UINT(px.size()), px.data());
    }
    const BYTE* at(int x, int y) const { return &px[(std::size_t(y) * w + x) * 4]; }   // B, G, R, A
};

bool sameColor(const BYTE* p, md::ui::Rgba c, int tol = 10) {
    return std::abs(int(p[2]) - int(std::lround(c.r * 255))) <= tol && std::abs(int(p[1]) - int(std::lround(c.g * 255))) <= tol &&
           std::abs(int(p[0]) - int(std::lround(c.b * 255))) <= tol;
}

bool reddish(const BYTE* p) { return p[2] > 180 && p[1] < 120 && p[0] < 120; }

bool anyIn(const Canvas& c, D2D1_RECT_F r, bool (*test)(const BYTE*)) {
    for (int y = int(r.top) + 1; y < int(r.bottom) - 1; ++y)
        for (int x = int(r.left) + 1; x < int(r.right) - 1; ++x)
            if (test(c.at(x, y))) return true;
    return false;
}

} // namespace

TEST_CASE(force_quit_layout_and_hits) {
    Canvas c(380, 420);
    const md::ui::Palette pal = md::ui::palette(false);
    md::ui::Painter p(c.rt.Get(), c.dwrite.Get(), pal, L"Segoe UI");
    md::FqView v;
    v.rows.resize(30);   // plus que la liste n'en montre : défilement
    v.hint = L"Vous pouvez ouvrir cette fenêtre en appuyant sur ⌥⊞⎋.";
    const md::FqLayout l = md::layoutForceQuit(p, v);
    // Titre dans la barre, texte dessous, liste, puis l'indice en bas à gauche et le bouton en bas à droite.
    CHECK(l.title.bottom <= md::kFqTitleBar);
    CHECK(l.intro.top >= md::kFqTitleBar);
    CHECK(l.list.top > l.intro.bottom);
    CHECK(std::abs(l.button.right - (md::kFqWidth - md::kFqMargin)) < 0.5f);
    CHECK(l.button.bottom <= md::kFqHeight - 12);
    CHECK(l.list.bottom < l.button.top);
    CHECK(l.hint.right < l.button.left);
    // Lignes : un point dans une ligne donne son index ; hors de la liste ou après la dernière, -1.
    const D2D1_RECT_F r0 = md::rowRect(l, 0, 0), r3 = md::rowRect(l, 3, 0);
    CHECK(r0.top >= l.list.top);
    CHECK(md::rowAt(l, 30, 0, (r0.left + r0.right) / 2, (r0.top + r0.bottom) / 2) == 0);
    CHECK(md::rowAt(l, 30, 0, r3.left + 10, r3.top + 2) == 3);
    CHECK(md::rowAt(l, 30, 0, l.list.left + 5, l.list.top - 4) == -1);
    CHECK(md::rowAt(l, 2, 0, (r3.left + r3.right) / 2, (r3.top + r3.bottom) / 2) == -1);
    // Défilement borné ; une ligne choisie hors de vue est ramenée dans la liste.
    const float max = md::maxScroll(l, 30);
    CHECK(max > 0);
    CHECK(md::maxScroll(l, 3) == 0);
    const float s = md::scrollToShow(l, 30, 0, 29);
    CHECK(std::abs(s - max) < 0.5f);
    const D2D1_RECT_F last = md::rowRect(l, 29, s);
    CHECK(last.bottom <= l.list.bottom + 0.5f);
    CHECK(md::rowAt(l, 30, s, (last.left + last.right) / 2, (last.top + last.bottom) / 2) == 29);
    CHECK(md::scrollToShow(l, 30, s, 0) == 0);
    // Pastilles : fermer et réduire ; agrandir est grisée.
    CHECK(md::lightAt(l, l.lights.x, l.lights.y) == 0);
    CHECK(md::lightAt(l, l.lights.x + md::ui::kLightSpacing, l.lights.y) == 1);
    CHECK(md::lightAt(l, l.lights.x + 2 * md::ui::kLightSpacing, l.lights.y) == -1);
    CHECK(md::lightAt(l, l.lights.x, l.lights.y + 30) == -1);
}

TEST_CASE(force_quit_draws_selection_hung_and_button) {
    for (const bool dark : {false, true}) {
        Canvas c(380, 420);
        const md::ui::Palette pal = md::ui::palette(dark);
        md::FqView v;
        v.rows = {{L"Bloc-notes"}, {L"Calculatrice", true}, {L"Courrier"}};
        v.selected = 2;
        v.buttonEnabled = true;
        v.hint = L"Vous pouvez ouvrir cette fenêtre en appuyant sur ⌥⊞⎋.";
        md::FqLayout l;
        c.rt->BeginDraw();
        {
            md::ui::Painter p(c.rt.Get(), c.dwrite.Get(), pal, L"Segoe UI");
            l = md::layoutForceQuit(p, v);
            md::drawForceQuit(p, l, v);
        }
        CHECK(SUCCEEDED(c.rt->EndDraw()));
        c.read();
        // Fond de la fenêtre ; ligne choisie sur l'accent ; « (ne répond pas) » en rouge sur la ligne figée seulement.
        CHECK(sameColor(c.at(4, 200), pal.window));
        const D2D1_RECT_F sel = md::rowRect(l, 2, 0);
        CHECK(sameColor(c.at(int(sel.right) - 6, int((sel.top + sel.bottom) / 2)), pal.accent));
        CHECK(anyIn(c, md::rowRect(l, 1, 0), reddish));
        CHECK(!anyIn(c, md::rowRect(l, 0, 0), reddish));
        // Bouton par défaut sur l'accent, près de son bord gauche (hors du texte).
        CHECK(sameColor(c.at(int(l.button.left) + 7, int((l.button.top + l.button.bottom) / 2)), pal.accent));
    }
}

TEST_CASE(force_quit_preselection_revealed_after_layout) {   // relecture du plan 54, important 3
    // Première ouverture : la liste est lue (l'app du premier plan choisie) avant la première mise en page ; dès que
    // celle-ci est connue, la ligne choisie est montrée, même la 18e sur 20.
    md::FqListState s;
    s.preselect = L"app17";
    std::vector<md::FqApp> apps(20);
    for (std::size_t i = 0; i < apps.size(); ++i) apps[i].appId = L"app" + std::to_wstring(i);
    s.reload(apps);
    CHECK(s.selected == 17);
    md::FqLayout l;
    l.list = D2D1::RectF(20, 80, 360, 364);   // 284 pt de haut, comme la vraie fenêtre
    s.afterLayout(l);
    const D2D1_RECT_F r = md::rowRect(l, 17, s.scroll);
    CHECK(r.top >= l.list.top);
    CHECK(r.bottom <= l.list.bottom);
    // Molette bornée ; la sélection au clavier reste visible.
    s.wheel(-10000, l);
    CHECK(std::abs(s.scroll - md::maxScroll(l, 20)) < 0.5f);
    s.select(0);
    s.afterLayout(l);
    CHECK(s.scroll == 0);
    s.select(99);   // bornée à la dernière
    CHECK(s.selected == 19);
}

TEST_CASE(force_quit_button_greyed_without_selection) {
    Canvas c(380, 420);
    const md::ui::Palette pal = md::ui::palette(false);
    md::FqView v;
    v.rows = {{L"Bloc-notes"}};
    v.selected = -1;
    v.buttonEnabled = false;
    md::FqLayout l;
    c.rt->BeginDraw();
    {
        md::ui::Painter p(c.rt.Get(), c.dwrite.Get(), pal, L"Segoe UI");
        l = md::layoutForceQuit(p, v);
        md::drawForceQuit(p, l, v);
    }
    c.rt->EndDraw();
    c.read();
    CHECK(!sameColor(c.at(int(l.button.left) + 7, int((l.button.top + l.button.bottom) / 2)), pal.accent));   // grisé
}

TEST_CASE(force_quit_snapshot_follows_accent) {   // relecture du plan 57 : sélection et bouton sur l'accent choisi
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    const std::wstring path = std::wstring(tmp) + L"macdock-fq-accent.png";
    REQUIRE(md::forceQuitSnapshot(path, false, false, md::AccentColor::Orange));
    UINT w = 0, h = 0;
    const std::vector<std::uint8_t> px = md::readPng(path, w, h);
    DeleteFileW(path.c_str());
    REQUIRE(!px.empty());
    int orange = 0;   // #FF8D28 (clair), opaque
    for (std::size_t i = 0; i + 3 < px.size(); i += 4)
        if (px[i + 3] > 250 && std::abs(int(px[i + 2]) - 0xFF) <= 8 && std::abs(int(px[i + 1]) - 0x8D) <= 8 &&
            std::abs(int(px[i]) - 0x28) <= 8)
            ++orange;
    CHECK(orange > 500);
    CoUninitialize();
}
