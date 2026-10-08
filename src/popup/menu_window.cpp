#include "popup_glass.h"
#include "menu_window.h"

#include <d2d1_3.h>
#include <dcomp.h>
#include <dwrite_3.h>
#include <dxgi1_2.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "../core/diag.h"
#include "../core/log.h"
#include "../geom/smooth_rect.h"
#include "../glass/backdrop_capture.h"
#include "../glass/glass_renderer.h"
#include "glyphs.h"
#include "../shell/shell_actions.h"

namespace md {

namespace {

template <class T> using Com = Microsoft::WRL::ComPtr<T>;

constexpr wchar_t kClass[] = L"MacDockMenu";
constexpr UINT WM_MENU_BACKDROP = WM_APP + 7;
constexpr UINT_PTR kSubmenuTimer = 1;
constexpr UINT_PTR kRefreshTimer = 2;   // lignes enrichies : valeurs relues (Live::refresh)
constexpr double kFadeSeconds = 0.12;
constexpr double kSubmenuDelay = 0.2;

double now() {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return double(c.QuadPart) / double(f.QuadPart);
}

D2D1_COLOR_F rgba(float r, float g, float b, float a) { return D2D1::ColorF(r, g, b, a); }

struct Session;

// Un niveau de menu (le menu principal ou un sous-menu ouvert).
struct Panel {
    Session* session = nullptr;
    std::unique_ptr<MenuModel> owned;   // sous-menu : copie de ses entrées
    const MenuModel* model = nullptr;
    MenuLayout layout;
    std::vector<Com<IDWriteTextLayout>> texts;
    std::vector<Com<IDWriteTextLayout>> shortcuts;   // raccourcis (alignés à droite), nullptr si aucun
    std::vector<Com<ID2D1Bitmap1>> icons;   // icônes des entrées, créées au premier rendu
    HWND hwnd = nullptr;
    RECT rc{};                 // fenêtre (écran), marge d'ombre comprise
    float margin = 0;          // px autour du panneau (ombre)
    int hover = -1;
    int openSub = -1;          // index de l'entrée dont le sous-menu est ouvert
    Com<IDCompositionTarget> target;
    Com<IDCompositionVisual2> visual;
    Com<IDCompositionSurface> surface;
    Com<ID3D11Texture2D> backdrop;   // portion de Session::screen sous le panneau
    Com<ID3D11ShaderResourceView> backdropSrv;
    bool hasBackdrop = false, scRgb = false;
    float white = 1;
    Com<ID3D11Texture2D> glassTex;
    Com<ID3D11RenderTargetView> glassRtv;
    Com<ID2D1Bitmap1> glassBitmap;
    double shownAt = -1;

    UINT width() const { return UINT(rc.right - rc.left); }
    UINT height() const { return UINT(rc.bottom - rc.top); }
    ~Panel() {
        if (hwnd) DestroyWindow(hwnd);
    }
};

struct Session {
    MenuWindow::Env env;
    Com<ID2D1Factory3> d2dFactory;
    Com<ID2D1Device2> d2dDevice;
    Com<ID2D1DeviceContext2> dc;
    Com<IDCompositionDesktopDevice> dcomp;
    Com<IDWriteFactory3> dwrite;
    Com<IDWriteTextFormat> format, symbolFormat;
    GlassRenderer glass;
    bool glassReady = false;
    // Une seule duplication de l'écran par processus et par sortie (une seconde échoue avec E_INVALIDARG) :
    // la session capture tout l'écran du menu, chaque panneau en copie sa portion.
    BackdropCapture capture;
    RECT captureRc{};
    Com<ID3D11Texture2D> screen;
    bool screenScRgb = false;
    float screenWhite = 1;
    std::vector<std::unique_ptr<Panel>> panels;
    int result = 0;
    bool done = false;
    bool captureVisible = false;   // capture d'écran en cours : panneaux visibles aux captures
    ULONGLONG thawAt = 0;          // fin du gel du verre après la capture
    UINT swallowUp = 0;   // relâchement à absorber : celui du clic extérieur qui a fermé le menu
    MenuWindow::Side side = MenuWindow::Side::Above;   // ouverture du menu principal
    const MenuWindow::BarLink* bar = nullptr;           // barre de menus (titres voisins), sinon nullptr
    const MenuWindow::Live* live = nullptr;             // lignes enrichies, sinon nullptr
    int dragging = 0;                                   // identifiant du curseur glissé
    Com<IDWriteTextFormat> headerFormat, boldFormat, smallFormat;
    double hoverSince = 0;

    float s() const { return env.scale; }

    bool init() {
        if (!initText()) return false;
        if (!env.device) return false;
        Com<IDXGIDevice> dxgi;
        D2D1_FACTORY_OPTIONS opts{};
        if (FAILED(env.device->QueryInterface(IID_PPV_ARGS(&dxgi))) ||
            FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory3), &opts,
                                     reinterpret_cast<void**>(d2dFactory.GetAddressOf()))) ||
            FAILED(d2dFactory->CreateDevice(dxgi.Get(), &d2dDevice)) ||
            FAILED(d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc)) ||
            FAILED(DCompositionCreateDevice2(d2dDevice.Get(), IID_PPV_ARGS(&dcomp))))
            return false;
        glassReady = env.glass && glass.init(env.device);
        return true;
    }

    bool initTextImpl() {
        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory3),
                                       reinterpret_cast<IUnknown**>(dwrite.GetAddressOf()))))
            return false;
        const wchar_t* family = env.font.empty() ? L"Segoe UI" : env.font.c_str();
        if (FAILED(dwrite->CreateTextFormat(family, nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                            DWRITE_FONT_STRETCH_NORMAL, float(kMenuFontSize) * s(), L"", &format)))
            return false;
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        auto extra = [&](DWRITE_FONT_WEIGHT weight, double size, Com<IDWriteTextFormat>& out) {
            if (SUCCEEDED(dwrite->CreateTextFormat(family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                                   float(size) * s(), L"", &out))) {
                out->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
                Com<IDWriteInlineObject> ellipsis;
                dwrite->CreateEllipsisTrimmingSign(out.Get(), &ellipsis);
                out->SetTrimming(&trim, ellipsis.Get());
            }
        };
        extra(DWRITE_FONT_WEIGHT_SEMI_BOLD, 11, headerFormat);
        extra(DWRITE_FONT_WEIGHT_SEMI_BOLD, kMenuFontSize, boldFormat);
        extra(DWRITE_FONT_WEIGHT_NORMAL, 11, smallFormat);
        dwrite->CreateTextFormat(L"Segoe UI Symbol", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
                                 DWRITE_FONT_STRETCH_NORMAL, float(kMenuFontSize) * s(), L"", &symbolFormat);
        return true;
    }

    Panel* panelOf(HWND h) {
        for (auto& p : panels)
            if (p->hwnd == h) return p.get();
        return nullptr;
    }
    int depthOf(const Panel* p) const {
        for (size_t i = 0; i < panels.size(); ++i)
            if (panels[i].get() == p) return int(i);
        return -1;
    }

    // Ferme les sous-menus plus profonds que depth.
    void closeBelow(int depth) {
        while (int(panels.size()) > depth + 1) panels.pop_back();
        if (depth >= 0 && depth < int(panels.size())) panels[size_t(depth)]->openSub = -1;
    }

    Panel* open(const MenuModel& model, POINT anchor, bool above, const RECT* parentItem,
                std::unique_ptr<MenuModel> owned = nullptr);
    void render(Panel& p);
    void onBackdrop();
    void copyBackdrop(Panel& p);
    void renderAll() {
        for (auto& p : panels) render(*p);
    }
    void activate(Panel& p, int index);
    // Barre de menus : ferme le menu pour ouvrir le titre voisin (dir = -1 ou +1) ; false sans barre.
    bool switchTitle(int dir) {
        if (!bar || bar->titles.size() < 2 || bar->current < 0) return false;
        const int n = int(bar->titles.size());
        result = menuSwitchResult(((bar->current + dir) % n + n) % n);
        done = true;
        return true;
    }
    void openSubmenu(Panel& p, int index, bool selectFirst);
    LRESULT handle(Panel& p, UINT msg, WPARAM wp, LPARAM lp);
    void measureTexts(Panel& p, float& maxText, float& maxShortcut);
    void drawItems(ID2D1DeviceContext* d, Panel& p, const D2D1_RECT_F& panel, float opacity);
    bool initText();   // DirectWrite seulement (rendu hors écran)
    void drawRow(ID2D1DeviceContext* d, Panel& p, size_t i, float top, float x0, float x1, float opacity);
    // Lignes enrichies : x en points depuis le bord gauche de la ligne ; false si rien n'a réagi.
    bool pressRow(Panel& p, int idx, double rowX);
    bool releaseRow(Panel& p, int idx, double rowX);
    void dragSlider(Panel& p, double rowX);
    double rowWidth(const Panel& p) const { return p.layout.width - 2 * kMenuPadding; }
};

// Session ouverte sur ce thread : le hook souris ferme le menu à tout clic hors de ses panneaux,
// même quand Windows a refusé de l'activer (la perte d'activation ne suffit pas).
thread_local Session* g_session = nullptr;

LRESULT CALLBACK outsideClickHook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && g_session && g_session->swallowUp && wp == g_session->swallowUp) {
        g_session->swallowUp = 0;
        return 1;
    }
    if (code == HC_ACTION && g_session && wp == WM_MOUSEMOVE && g_session->bar && !g_session->done && !g_session->dragging) {
        // Barre de menus : le survol d'un autre titre ouvre son menu, comme sur macOS.
        POINT pt = reinterpret_cast<MSLLHOOKSTRUCT*>(lp)->pt;
        int k = barTitleAt(g_session->bar->titles, pt, g_session->bar->current);
        if (k >= 0) {
            g_session->result = menuSwitchResult(k);
            g_session->done = true;
            if (!g_session->panels.empty()) PostMessageW(g_session->panels.front()->hwnd, WM_NULL, 0, 0);
        }
    }
    if (code == HC_ACTION && g_session &&
        (wp == WM_LBUTTONDOWN || wp == WM_RBUTTONDOWN || wp == WM_MBUTTONDOWN || wp == WM_XBUTTONDOWN)) {
        POINT pt = reinterpret_cast<MSLLHOOKSTRUCT*>(lp)->pt;
        bool inside = false;
        for (auto& p : g_session->panels) {
            RECT r = p->rc;
            InflateRect(&r, -LONG(p->margin), -LONG(p->margin));   // la marge d'ombre n'est pas cliquable
            if (PtInRect(&r, pt)) inside = true;
        }
        if (!inside && !g_session->done) {
            // Comme sur macOS, le clic qui ferme le menu n'atteint rien d'autre (ni l'icône du Dock dessous) :
            // l'appui et son relâchement sont absorbés.
            g_session->done = true;
            g_session->swallowUp = UINT(wp) + 1;   // WM_xBUTTONDOWN + 1 = WM_xBUTTONUP
            if (!g_session->panels.empty()) PostMessageW(g_session->panels.front()->hwnd, WM_NULL, 0, 0);
            return 1;
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

LRESULT CALLBACK panelProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* session = reinterpret_cast<Session*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (session)
        if (Panel* p = session->panelOf(h)) return session->handle(*p, msg, wp, lp);
    if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return DefWindowProcW(h, msg, wp, lp);
}

Panel* Session::open(const MenuModel& model, POINT anchor, bool above, const RECT* parentItem,
                     std::unique_ptr<MenuModel> owned) {
    auto p = std::make_unique<Panel>();
    p->session = this;
    p->owned = std::move(owned);
    p->model = p->owned ? p->owned.get() : &model;
    float maxText = 0, maxShortcut = 0;
    measureTexts(*p, maxText, maxShortcut);
    p->layout = layoutMenu(*p->model, maxText / s(), maxShortcut / s());
    p->margin = std::ceil(float(env.metrics.shadowBlur) * 1.5f * s() + 4);
    const LONG w = LONG(std::ceil(p->layout.width * s() + 2 * p->margin));
    const LONG h = LONG(std::ceil(p->layout.height * s() + 2 * p->margin));

    HMONITOR mon = MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(mon, &mi);
    const RECT& m = mi.rcMonitor;
    LONG left, top;
    if (parentItem) {
        // Sous-menu : à droite de l'entrée, son première entrée alignée sur elle ; à gauche s'il n'y a pas la place.
        left = parentItem->right - LONG(p->margin) - LONG(kMenuPadding * s());
        if (left + w - LONG(p->margin) > m.right) left = parentItem->left - w + LONG(p->margin) + LONG(kMenuPadding * s());
        top = parentItem->top - LONG(p->margin) - LONG(kMenuPadding * s());
    } else {
        left = anchor.x - w / 2;
        top = above ? anchor.y - h + LONG(p->margin) : anchor.y - LONG(p->margin);
        if (side == MenuWindow::Side::Right) {
            left = anchor.x - LONG(p->margin);
            top = anchor.y - h / 2;
        } else if (side == MenuWindow::Side::Left) {
            left = anchor.x - w + LONG(p->margin);
            top = anchor.y - h / 2;
        } else if (side == MenuWindow::Side::Below) {
            left = anchor.x - LONG(p->margin);
            top = anchor.y - LONG(p->margin);
        }
    }
    left = std::clamp(left, m.left - LONG(p->margin), std::max(m.left, m.right - w + LONG(p->margin)));
    top = std::clamp(top, m.top - LONG(p->margin), std::max(m.top, m.bottom - h + LONG(p->margin)));
    p->rc = {left, top, left + w, top + h};

    DWORD ex = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP;
    if (!panels.empty()) ex |= WS_EX_NOACTIVATE;   // les sous-menus ne prennent pas le clavier : il reste au menu principal
    p->hwnd = CreateWindowExW(ex, kClass, L"", WS_POPUP, left, top, w, h, nullptr, nullptr, env.instance, nullptr);
    if (!p->hwnd) return nullptr;
    SetWindowLongPtrW(p->hwnd, GWLP_USERDATA, LONG_PTR(this));
    // Sinon le verre se verrait lui-même ; en diagnostic, visible aux enregistreurs (le verre peut alors se refléter).
    if (!diagnosticCapture()) SetWindowDisplayAffinity(p->hwnd, WDA_EXCLUDEFROMCAPTURE);

    if (FAILED(dcomp->CreateTargetForHwnd(p->hwnd, TRUE, &p->target)) || FAILED(dcomp->CreateVisual(&p->visual)) ||
        FAILED(dcomp->CreateSurface(UINT(w), UINT(h), DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED,
                                    &p->surface)))
        return nullptr;
    p->visual->SetContent(p->surface.Get());
    p->target->SetRoot(p->visual.Get());

    if (glassReady && panels.empty()) {
        captureRc = m;
        capture.start(p->hwnd, WM_MENU_BACKDROP, mon, {m.left, m.top, m.right, m.bottom});
    }
    panels.push_back(std::move(p));
    Panel* raw = panels.back().get();
    copyBackdrop(*raw);   // sous-menu : l'écran est déjà capturé

    // Attendre brièvement la première image de l'arrière-plan : le menu apparaît directement en verre.
    const double deadline = now() + 0.15;
    while (glassReady && !raw->hasBackdrop && now() < deadline &&
           capture.status() != BackdropCapture::Status::Unavailable &&
           capture.status() != BackdropCapture::Status::Failed) {
        MSG msg;
        if (PeekMessageW(&msg, panels.front()->hwnd, WM_MENU_BACKDROP, WM_MENU_BACKDROP, PM_REMOVE)) DispatchMessageW(&msg);
        else MsgWaitForMultipleObjects(0, nullptr, FALSE, 5, QS_POSTMESSAGE);
    }
    raw->shownAt = now();
    if (env.trace) {
        log::info(L"[trace] menu : panneau %ld,%ld-%ld,%ld, verre %s", raw->rc.left, raw->rc.top, raw->rc.right,
                  raw->rc.bottom, raw->hasBackdrop ? L"réel" : L"dépoli");
        for (size_t i = 0; i < raw->model->items.size(); ++i)
            if (!raw->model->items[i].separator())
                log::info(L"[trace] menu : entrée %zu (%s) x=%ld y=%ld", i, raw->model->items[i].text.c_str(),
                          (raw->rc.left + raw->rc.right) / 2,
                          raw->rc.top + LONG(raw->margin + float(raw->layout.top[i] + kMenuItemHeight / 2) * s()));
    }
    render(*raw);
    ShowWindow(raw->hwnd, panels.size() == 1 ? SW_SHOW : SW_SHOWNOACTIVATE);
    if (panels.size() == 1) {
        forceForeground(raw->hwnd);   // le clavier (flèches, Entrée, Échap) va au menu
        SetFocus(raw->hwnd);
    }
    return raw;
}

bool Session::initText() { return initTextImpl(); }

void Session::measureTexts(Panel& p, float& maxText, float& maxShortcut) {
    p.texts.clear();
    p.shortcuts.clear();
    for (auto& it : p.model->items) {
        Com<IDWriteTextLayout> t, k;
        const bool plain = it.row == MenuRow::Normal || it.row == MenuRow::Toggle;   // les autres : mis en page au dessin
        if (plain && !it.separator() &&
            SUCCEEDED(dwrite->CreateTextLayout(it.text.c_str(), UINT32(it.text.size()), format.Get(), 4000, 200, &t))) {
            DWRITE_TEXT_METRICS tm{};
            t->GetMetrics(&tm);
            maxText = std::max(maxText, tm.width);
        }
        const std::wstring keys = macShortcutLabel(it.shortcut);   // « ⌃⇧Z » plutôt que « Ctrl+Maj+Z »
        if (plain && !it.separator() && !keys.empty() &&
            SUCCEEDED(dwrite->CreateTextLayout(keys.c_str(), UINT32(keys.size()), format.Get(), 4000, 200, &k))) {
            DWRITE_TEXT_METRICS km{};
            k->GetMetrics(&km);
            maxShortcut = std::max(maxShortcut, km.width);
        }
        p.texts.push_back(t);
        p.shortcuts.push_back(k);
    }
}

void Session::drawItems(ID2D1DeviceContext* d, Panel& p, const D2D1_RECT_F& panel, float opacity) {
    const float sc = s();
    const bool dark = env.dark;
    // Entrées.
    Com<ID2D1SolidColorBrush> text, disabled, accent, white, sep, keyInk, keyHot;
    d->CreateSolidColorBrush(dark ? rgba(1, 1, 1, 0.92f * opacity) : rgba(0, 0, 0, 0.86f * opacity), &text);
    d->CreateSolidColorBrush(dark ? rgba(1, 1, 1, 0.30f * opacity) : rgba(0, 0, 0, 0.28f * opacity), &disabled);
    d->CreateSolidColorBrush(dark ? rgba(0.04f, 0.52f, 1.0f, opacity) : rgba(0.0f, 0.48f, 1.0f, opacity), &accent);   // bleu macOS
    d->CreateSolidColorBrush(rgba(1, 1, 1, opacity), &white);
    d->CreateSolidColorBrush(dark ? rgba(1, 1, 1, 0.12f * opacity) : rgba(0, 0, 0, 0.10f * opacity), &sep);
    d->CreateSolidColorBrush(dark ? rgba(1, 1, 1, 0.45f * opacity) : rgba(0, 0, 0, 0.45f * opacity), &keyInk);
    d->CreateSolidColorBrush(rgba(1, 1, 1, 0.70f * opacity), &keyHot);
    const float x0 = panel.left + float(kMenuPadding) * sc;
    const float x1 = panel.right - float(kMenuPadding) * sc;
    for (size_t i = 0; i < p.model->items.size(); ++i) {
        const MenuItem& it = p.model->items[i];
        const float top = p.margin + float(p.layout.top[i]) * sc;
        if (it.separator()) {
            float y = std::round(top + float(kMenuSeparatorHeight) * sc / 2) + 0.5f;
            d->DrawLine({x0 + 9 * sc, y}, {x1 - 9 * sc, y}, sep.Get(), std::max(1.0f, sc * 0.5f));
            continue;
        }
        if (it.row != MenuRow::Normal) {
            drawRow(d, p, i, top, x0, x1, opacity);
            continue;
        }
        const float rowH = float(kMenuItemHeight) * sc;
        const bool hot = int(i) == p.hover || int(i) == p.openSub;
        ID2D1SolidColorBrush* ink = !it.enabled ? disabled.Get() : hot ? white.Get() : text.Get();
        if (hot && it.enabled) {
            float r = float(kMenuHighlightRadius) * sc;
            d->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x0, top, x1, top + rowH), r, r), accent.Get());
        }
        if (it.checked && symbolFormat)
            d->DrawTextW(L"✓", 1, symbolFormat.Get(), D2D1::RectF(x0 + 5 * sc, top, x0 + 20 * sc, top + rowH), ink);
        const float textX = x0 + float(p.layout.textLeft) * sc - float(kMenuPadding) * sc + 4 * sc;
        if (it.icon) {
            if (p.icons.size() < p.model->items.size()) p.icons.resize(p.model->items.size());
            if (!p.icons[i]) {
                auto props = D2D1::BitmapProperties1(
                    D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
                d->CreateBitmap(D2D1::SizeU(UINT32(it.icon->size), UINT32(it.icon->size)), it.icon->bgra.data(),
                                UINT32(it.icon->size * 4), &props, &p.icons[i]);
            }
            if (p.icons[i]) {
                const float is = float(kMenuIconSize) * sc, iy = top + (rowH - is) / 2;
                d->DrawBitmap(p.icons[i].Get(), D2D1::RectF(textX, iy, textX + is, iy + is), opacity,
                              D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
            }
        }
        if (p.texts[i]) {
            DWRITE_TEXT_METRICS tm{};
            p.texts[i]->GetMetrics(&tm);
            d->DrawTextLayout({textX + float(p.layout.iconSpace) * sc, top + (rowH - tm.height) / 2}, p.texts[i].Get(), ink);
        }
        if (i < p.shortcuts.size() && p.shortcuts[i] && it.submenu.empty()) {   // raccourci, aligné à droite
            DWRITE_TEXT_METRICS km{};
            p.shortcuts[i]->GetMetrics(&km);
            ID2D1SolidColorBrush* kInk = !it.enabled ? disabled.Get() : hot ? keyHot.Get() : keyInk.Get();
            d->DrawTextLayout({x1 - 8 * sc - km.width, top + (rowH - km.height) / 2}, p.shortcuts[i].Get(), kInk);
        }
        if (!it.submenu.empty()) {   // chevron ›
            float cx = x1 - 10 * sc, cy = top + rowH / 2, a = 3.5f * sc;
            d->DrawLine({cx - a / 2, cy - a}, {cx + a / 2, cy}, ink, 1.4f * sc);
            d->DrawLine({cx + a / 2, cy}, {cx - a / 2, cy + a}, ink, 1.4f * sc);
        }
    }
}

void Session::onBackdrop() {
    bool scRgb = false;
    float white = 1;
    Com<ID3D11DeviceContext> ctx;
    env.device->GetImmediateContext(&ctx);
    bool got = capture.takeLatest(env.device, ctx.Get(),
                                  [&](UINT w, UINT h, bool hdr) -> ID3D11Texture2D* {
                                      D3D11_TEXTURE2D_DESC d{};
                                      if (screen) screen->GetDesc(&d);
                                      if (!screen || d.Width != w || d.Height != h || screenScRgb != hdr) {
                                          screen.Reset();
                                          D3D11_TEXTURE2D_DESC n{};
                                          n.Width = w;
                                          n.Height = h;
                                          n.MipLevels = n.ArraySize = 1;
                                          n.Format = hdr ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_B8G8R8A8_UNORM;
                                          n.SampleDesc.Count = 1;
                                          n.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                                          if (FAILED(env.device->CreateTexture2D(&n, nullptr, &screen))) return nullptr;
                                          screenScRgb = hdr;
                                      }
                                      return screen.Get();
                                  },
                                  scRgb, white);
    if (!got) return;
    // Pendant une capture d'écran (et juste après), l'image contient le menu lui-même : elle n'est pas appliquée.
    if (captureVisible || GetTickCount64() < thawAt) return;
    screenWhite = white;
    for (auto& p : panels) {
        copyBackdrop(*p);
        if (p->shownAt >= 0) render(*p);
    }
}

// Copie dans p.backdrop la portion de l'écran capturé située sous le panneau.
void Session::copyBackdrop(Panel& p) {
    if (!screen) return;
    D3D11_TEXTURE2D_DESC sd{};
    screen->GetDesc(&sd);
    D3D11_TEXTURE2D_DESC d{};
    if (p.backdrop) p.backdrop->GetDesc(&d);
    if (!p.backdrop || d.Width != p.width() || d.Height != p.height() || d.Format != sd.Format) {
        p.backdrop.Reset();
        p.backdropSrv.Reset();
        p.hasBackdrop = false;
        D3D11_TEXTURE2D_DESC n{};
        n.Width = p.width();
        n.Height = p.height();
        n.MipLevels = n.ArraySize = 1;
        n.Format = sd.Format;
        n.SampleDesc.Count = 1;
        n.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(env.device->CreateTexture2D(&n, nullptr, &p.backdrop)) ||
            FAILED(env.device->CreateShaderResourceView(p.backdrop.Get(), nullptr, &p.backdropSrv))) {
            p.backdrop.Reset();
            return;
        }
    }
    // La marge d'ombre peut déborder de l'écran : seule l'intersection est copiée.
    const LONG l = std::max(p.rc.left, captureRc.left), t = std::max(p.rc.top, captureRc.top);
    const LONG r = std::min({p.rc.right, captureRc.right, captureRc.left + LONG(sd.Width)});
    const LONG b = std::min({p.rc.bottom, captureRc.bottom, captureRc.top + LONG(sd.Height)});
    if (r <= l || b <= t) return;
    D3D11_BOX box{UINT(l - captureRc.left), UINT(t - captureRc.top), 0, UINT(r - captureRc.left), UINT(b - captureRc.top), 1};
    Com<ID3D11DeviceContext> ctx;
    env.device->GetImmediateContext(&ctx);
    ctx->CopySubresourceRegion(p.backdrop.Get(), 0, UINT(l - p.rc.left), UINT(t - p.rc.top), 0, screen.Get(), 0, &box);
    p.scRgb = screenScRgb;
    p.white = screenWhite;
    p.hasBackdrop = true;
}

void Session::render(Panel& p) {
    const UINT W = p.width(), H = p.height();
    const float sc = s();
    const double fadeT = p.shownAt < 0 ? 1.0 : std::clamp((now() - p.shownAt) / kFadeSeconds, 0.0, 1.0);
    const float opacity = float(fadeT);
    const D2D1_RECT_F panel{p.margin, p.margin, float(W) - p.margin, float(H) - p.margin};
    const float radius = float(limitedCornerRadius(panel.right - panel.left, panel.bottom - panel.top, kMenuRadius * sc));
    const bool dark = env.dark;

    // Verre : passe D3D dans glassTex, posée ensuite par Direct2D.
    bool glassDrawn = false;
    if (glassReady && p.hasBackdrop && p.backdropSrv) {
        D3D11_TEXTURE2D_DESC gd{};
        if (p.glassTex) p.glassTex->GetDesc(&gd);
        if (!p.glassTex || gd.Width != W || gd.Height != H) {
            p.glassTex.Reset();
            p.glassRtv.Reset();
            p.glassBitmap.Reset();
            D3D11_TEXTURE2D_DESC d{};
            d.Width = W;
            d.Height = H;
            d.MipLevels = d.ArraySize = 1;
            d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            d.SampleDesc.Count = 1;
            d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            Com<IDXGISurface> surf;
            auto props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,
                                                 D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
            if (FAILED(env.device->CreateTexture2D(&d, nullptr, &p.glassTex)) ||
                FAILED(env.device->CreateRenderTargetView(p.glassTex.Get(), nullptr, &p.glassRtv)) ||
                FAILED(p.glassTex.As(&surf)) || FAILED(dc->CreateBitmapFromDxgiSurface(surf.Get(), &props, &p.glassBitmap)))
                p.glassTex.Reset();
        }
        if (p.glassTex) {
            const Metrics& m = env.metrics;
            GlassParams gp = popupGlassParams(m, dark, sc, PopupMaterial::Menu);   // matériau Golden Gate
            gp.shadowBlurPx = float(m.shadowBlur) * sc;
            gp.shadowOffsetPx = 3 * sc;
            gp.backdropIsScRgb = p.scRgb;
            gp.sdrWhiteScale = p.white;
            GlassShape shape{panel.left, panel.top, panel.right, panel.bottom, radius, 0.6f,
                             popupShadowOpacity(dark), opacity};
            Com<ID3D11DeviceContext> ctx;
            env.device->GetImmediateContext(&ctx);
            glassDrawn = glass.render(ctx.Get(), p.backdropSrv.Get(), W, H, p.glassRtv.Get(), {&shape, 1}, gp);
        }
    }

    POINT offset{};
    Com<ID2D1DeviceContext> d;
    if (FAILED(p.surface->BeginDraw(nullptr, IID_PPV_ARGS(&d), &offset))) return;
    d->SetDpi(96, 96);
    d->SetTransform(D2D1::Matrix3x2F::Translation(float(offset.x), float(offset.y)));
    d->Clear(rgba(0, 0, 0, 0));
    d->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    D2D1_ROUNDED_RECT rr{panel, radius, radius};
    if (glassDrawn) {
        d->DrawBitmap(p.glassBitmap.Get());
    } else {
        // Verre dépoli de repli : panneau presque opaque, liseré fin.
        Com<ID2D1SolidColorBrush> bg;
        d->CreateSolidColorBrush(dark ? rgba(0.15f, 0.15f, 0.16f, 0.94f * opacity) : rgba(0.96f, 0.96f, 0.97f, 0.94f * opacity), &bg);
        d->FillRoundedRectangle(rr, bg.Get());
    }
    Com<ID2D1SolidColorBrush> border;
    d->CreateSolidColorBrush(dark ? rgba(1, 1, 1, 0.14f * opacity) : rgba(0, 0, 0, 0.10f * opacity), &border);
    d->DrawRoundedRectangle(rr, border.Get(), std::max(1.0f, sc * 0.5f));

    drawItems(d.Get(), p, panel, opacity);
    p.surface->EndDraw();
    dcomp->Commit();
}

namespace {
void drawText(ID2D1DeviceContext* d, IDWriteFactory3* dw, IDWriteTextFormat* fmt, const std::wstring& text, float x, float y,
              float maxW, float h, ID2D1Brush* ink, bool centerV = true) {
    if (!fmt || text.empty() || maxW <= 0) return;
    Com<IDWriteTextLayout> l;
    if (FAILED(dw->CreateTextLayout(text.c_str(), UINT32(text.size()), fmt, maxW, h, &l))) return;
    DWRITE_TEXT_METRICS tm{};
    l->GetMetrics(&tm);
    d->DrawTextLayout({x, centerV ? y + (h - tm.height) / 2 : y}, l.Get(), ink);
}
} // namespace

void Session::drawRow(ID2D1DeviceContext* d, Panel& p, size_t i, float top, float x0, float x1, float opacity) {
    const MenuItem& it = p.model->items[i];
    const float sc = s();
    const float rowH = float(menuRowHeight(it.row)) * sc;
    const bool dark = env.dark;
    const float dim = it.enabled ? 1.0f : 0.4f;
    Com<ID2D1SolidColorBrush> ink, grey, track, accent, white, tileBg, tileOff, line;
    d->CreateSolidColorBrush(dark ? rgba(1, 1, 1, 0.92f * opacity * dim) : rgba(0, 0, 0, 0.86f * opacity * dim), &ink);
    d->CreateSolidColorBrush(dark ? rgba(1, 1, 1, 0.50f * opacity) : rgba(0, 0, 0, 0.48f * opacity), &grey);
    d->CreateSolidColorBrush(dark ? rgba(1, 1, 1, 0.20f * opacity) : rgba(0, 0, 0, 0.13f * opacity), &track);
    d->CreateSolidColorBrush(dark ? rgba(0.04f, 0.52f, 1.0f, opacity * dim) : rgba(0.0f, 0.48f, 1.0f, opacity * dim), &accent);
    d->CreateSolidColorBrush(rgba(1, 1, 1, opacity), &white);
    d->CreateSolidColorBrush(dark ? rgba(1, 1, 1, 0.09f * opacity) : rgba(1, 1, 1, 0.50f * opacity), &tileBg);
    d->CreateSolidColorBrush(dark ? rgba(1, 1, 1, 0.16f * opacity) : rgba(0, 0, 0, 0.07f * opacity), &tileOff);
    d->CreateSolidColorBrush(dark ? rgba(1, 1, 1, 0.10f * opacity) : rgba(0, 0, 0, 0.08f * opacity), &line);
    IDWriteFactory3* dw = dwrite.Get();
    switch (it.row) {
        case MenuRow::Normal: break;
        case MenuRow::Header:
            drawText(d, dw, headerFormat.Get(), it.text, x0 + 9 * sc, top + 4 * sc, x1 - x0 - 18 * sc, rowH - 4 * sc, grey.Get());
            break;
        case MenuRow::Slider: {
            const float cy = top + rowH / 2, is = 16 * sc;
            drawGlyph(d, it.glyph, D2D1::RectF(x0 + 10 * sc, cy - is / 2, x0 + 10 * sc + is, cy + is / 2), ink.Get(), it.level,
                      it.on);
            const float a = x0 + float(kMenuSliderLeft) * sc, b = x1 - float(kMenuSliderRight) * sc;
            const float th = 5 * sc, knob = 9 * sc;
            const float v = float(std::clamp(it.value, 0.0, 1.0));
            const float kx = a + (b - a) * v;
            d->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(a, cy - th / 2, b, cy + th / 2), th / 2, th / 2), track.Get());
            d->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(a, cy - th / 2, kx, cy + th / 2), th / 2, th / 2), ink.Get());
            d->FillEllipse(D2D1::Ellipse({kx, cy}, knob, knob), white.Get());
            d->DrawEllipse(D2D1::Ellipse({kx, cy}, knob, knob), line.Get(), std::max(1.0f, 0.75f * sc));
            break;
        }
        case MenuRow::Toggle: {
            const float textX = x0 + float(p.layout.textLeft) * sc - float(kMenuPadding) * sc + 4 * sc;
            if (i < p.texts.size() && p.texts[i]) {
                DWRITE_TEXT_METRICS tm{};
                p.texts[i]->GetMetrics(&tm);
                d->DrawTextLayout({textX, top + (rowH - tm.height) / 2}, p.texts[i].Get(), ink.Get());
            }
            const float sw = 30 * sc, sh = 18 * sc, sx = x1 - 10 * sc - sw, sy = top + (rowH - sh) / 2;
            d->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(sx, sy, sx + sw, sy + sh), sh / 2, sh / 2),
                                    it.on ? accent.Get() : tileOff.Get());
            const float r = sh / 2 - 2 * sc, kx = it.on ? sx + sw - sh / 2 : sx + sh / 2;
            d->FillEllipse(D2D1::Ellipse({kx, sy + sh / 2}, r, r), white.Get());
            break;
        }
        case MenuRow::Tiles: {
            const size_t n = it.tiles.size();
            if (n == 0) break;
            const float inset = float(kMenuTileInset) * sc, gap = float(kMenuTileGap) * sc;
            const float w = ((x1 - x0) - 2 * inset - float(n - 1) * gap) / float(n);
            for (size_t k = 0; k < n; ++k) {
                const MenuTile& t = it.tiles[k];
                const float tx = x0 + inset + float(k) * (w + gap), ty = top + 4 * sc, th = rowH - 8 * sc;
                const float r = 14 * sc;
                d->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(tx, ty, tx + w, ty + th), r, r), tileBg.Get());
                const float cs = 30 * sc, cx = tx + 8 * sc + cs / 2, cy = ty + th / 2;
                d->FillEllipse(D2D1::Ellipse({cx, cy}, cs / 2, cs / 2), t.on ? accent.Get() : tileOff.Get());
                const float gs = 16 * sc;
                drawGlyph(d, t.glyph, D2D1::RectF(cx - gs / 2, cy - gs / 2, cx + gs / 2, cy + gs / 2),
                          t.on ? white.Get() : ink.Get(), 1, false);
                const float lx = cx + cs / 2 + 8 * sc, lw = tx + w - lx - 6 * sc;
                drawText(d, dw, boldFormat.Get(), t.title, lx, cy - 17 * sc, lw, 17 * sc, t.enabled ? ink.Get() : grey.Get());
                drawText(d, dw, smallFormat.Get(), t.subtitle, lx, cy, lw, 15 * sc, grey.Get(), false);
            }
            break;
        }
        case MenuRow::Media: {
            const float bx = x1 - float(kMenuMediaRight + 3 * kMenuMediaButton) * sc;
            const float lx = x0 + 10 * sc, lw = bx - lx - 6 * sc, cy = top + rowH / 2;
            drawText(d, dw, boldFormat.Get(), it.text, lx, cy - 18 * sc, lw, 17 * sc, ink.Get());
            drawText(d, dw, smallFormat.Get(), it.subtitle, lx, cy + 1 * sc, lw, 15 * sc, grey.Get(), false);
            const Glyph glyphs[3] = {Glyph::Previous, it.playing ? Glyph::Pause : Glyph::Play, Glyph::Next};
            for (int k = 0; k < 3; ++k) {
                const float cx = bx + (float(k) + 0.5f) * float(kMenuMediaButton) * sc, gs = (k == 1 ? 16 : 14) * sc;
                drawGlyph(d, glyphs[k], D2D1::RectF(cx - gs / 2, cy - gs / 2, cx + gs / 2, cy + gs / 2), ink.Get());
            }
            break;
        }
        case MenuRow::Calendar: {   // mois du Centre de notifications : aujourd'hui dans une pastille rouge
            auto centered = [&](IDWriteTextFormat* fmt, const std::wstring& text, float cx, float cy, ID2D1Brush* b) {
                Com<IDWriteTextLayout> l;
                if (!fmt || FAILED(dw->CreateTextLayout(text.c_str(), UINT32(text.size()), fmt, 200 * sc, 40 * sc, &l))) return;
                DWRITE_TEXT_METRICS tm{};
                l->GetMetrics(&tm);
                d->DrawTextLayout({cx - tm.width / 2, cy - tm.height / 2}, l.Get(), b);
            };
            const float pad = 10 * sc;
            drawText(d, dw, boldFormat.Get(), it.text, x0 + pad, top + 2 * sc, x1 - x0 - 2 * pad, 22 * sc, ink.Get());
            const float cellW = (x1 - x0 - 2 * pad) / 7, gridTop = top + 26 * sc, rowStep = 24 * sc;
            static const wchar_t* kInitials[] = {L"L", L"M", L"M", L"J", L"V", L"S", L"D"};
            for (int c = 0; c < 7; ++c)
                centered(smallFormat.Get(), kInitials[c], x0 + pad + (float(c) + 0.5f) * cellW, gridTop + 8 * sc, grey.Get());
            Com<ID2D1SolidColorBrush> red, faint;
            d->CreateSolidColorBrush(rgba(1.0f, 0.23f, 0.19f, opacity), &red);   // rouge du Calendrier
            d->CreateSolidColorBrush(dark ? rgba(1, 1, 1, 0.28f * opacity) : rgba(0, 0, 0, 0.26f * opacity), &faint);
            const int year = int(it.date / 10000), month = int(it.date / 100 % 100), day = int(it.date % 100);
            const auto cells = calendarCells(year, month, day);
            for (std::size_t k = 0; k < cells.size(); ++k) {
                const float cx = x0 + pad + (float(k % 7) + 0.5f) * cellW;
                const float cy = gridTop + 18 * sc + (float(k / 7) + 0.5f) * rowStep;
                if (cells[k].today) d->FillEllipse(D2D1::Ellipse({cx, cy}, 11 * sc, 11 * sc), red.Get());
                centered(smallFormat.Get(), std::to_wstring(cells[k].day), cx, cy,
                         cells[k].today ? white.Get() : cells[k].inMonth ? ink.Get() : faint.Get());
            }
            break;
        }
    }
}

bool Session::pressRow(Panel& p, int idx, double rowX) {
    if (idx < 0 || !p.owned) return false;
    MenuItem& it = p.owned->items[size_t(idx)];
    if (it.row != MenuRow::Slider || !it.enabled) return false;
    dragging = it.id;
    SetCapture(p.hwnd);
    dragSlider(p, rowX);
    return true;
}

void Session::dragSlider(Panel& p, double rowX) {
    if (!dragging || !p.owned) return;
    for (auto& it : p.owned->items) {
        if (it.id != dragging || it.row != MenuRow::Slider) continue;
        const double v = sliderValueAt(rowWidth(p), rowX);
        if (std::fabs(v - it.value) < 1e-4) return;
        it.value = v;
        if (live && live->slider) live->slider(it.id, v);
        render(p);
        return;
    }
}

bool Session::releaseRow(Panel& p, int idx, double rowX) {
    if (idx < 0 || !p.owned) return false;
    MenuItem& it = p.owned->items[size_t(idx)];
    if (!it.enabled) return it.row != MenuRow::Normal;
    switch (it.row) {
        case MenuRow::Normal: return false;
        case MenuRow::Toggle:
            it.on = !it.on;
            if (live && live->toggle) live->toggle(it.id, it.on);
            render(p);
            return true;
        case MenuRow::Tiles: {
            const int k = tileAt(it.tiles.size(), rowWidth(p), rowX);
            if (k < 0 || !it.tiles[size_t(k)].enabled) return true;
            if (live && live->tile && live->tile(it.id, k)) done = true;
            render(p);
            return true;
        }
        case MenuRow::Media: {
            const int b = mediaButtonAt(rowWidth(p), rowX);
            if (b < 0) return true;
            if (b == 1) it.playing = !it.playing;   // tout de suite ; le prochain rafraîchissement confirme
            if (live && live->media) live->media(it.id, b);
            render(p);
            return true;
        }
        default: return true;   // intitulé, curseur (relâché) : rien
    }
}

void Session::openSubmenu(Panel& p, int index, bool selectFirst) {
    if (index < 0 || index >= int(p.model->items.size())) return;
    const MenuItem& it = p.model->items[size_t(index)];
    if (it.submenu.empty() || !it.enabled) return;
    int depth = depthOf(&p);
    if (p.openSub == index && int(panels.size()) > depth + 1) return;
    closeBelow(depth);
    p.openSub = index;
    const float sc = s();
    RECT item{p.rc.left + LONG(p.margin), p.rc.top + LONG(p.margin + float(p.layout.top[size_t(index)]) * sc),
              p.rc.right - LONG(p.margin), 0};
    item.bottom = item.top + LONG(kMenuItemHeight * sc);
    auto sub = std::make_unique<MenuModel>(MenuModel{it.submenu});
    const MenuModel& subRef = *sub;
    Panel* child = open(subRef, POINT{item.right, item.top}, false, &item, std::move(sub));
    if (child && selectFirst) child->hover = nextSelectable(*child->model, -1, +1);
    render(p);
    if (child) render(*child);
}

void Session::activate(Panel& p, int index) {
    if (index < 0 || index >= int(p.model->items.size())) return;
    const MenuItem& it = p.model->items[size_t(index)];
    if (!it.selectable()) return;
    if (!it.submenu.empty()) {
        openSubmenu(p, index, true);
        return;
    }
    result = it.id;
    done = true;
}

LRESULT Session::handle(Panel& p, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_MOUSEACTIVATE:
            return depthOf(&p) == 0 ? MA_ACTIVATE : MA_NOACTIVATE;
        case WM_LBUTTONDOWN: {
            const double y = (double(short(HIWORD(lp))) - p.margin) / s();
            const double x = (double(short(LOWORD(lp))) - p.margin) / s();
            pressRow(p, rowAt(p.layout, *p.model, y), x - kMenuPadding);
            return 0;
        }
        case WM_CAPTURECHANGED:
            dragging = 0;
            return 0;
        case WM_MOUSEMOVE: {
            double y = (double(short(HIWORD(lp))) - p.margin) / s();
            double x = (double(short(LOWORD(lp))) - p.margin) / s();
            if (dragging) {
                dragSlider(p, x - kMenuPadding);
                return 0;
            }
            int idx = x >= 0 && x <= p.layout.width ? hitTestMenu(p.layout, *p.model, y) : -1;
            if (idx != p.hover) {
                p.hover = idx;
                KillTimer(p.hwnd, kSubmenuTimer);
                if (idx >= 0 && !p.model->items[size_t(idx)].submenu.empty())
                    SetTimer(p.hwnd, kSubmenuTimer, UINT(kSubmenuDelay * 1000), nullptr);
                else if (idx >= 0 && p.openSub >= 0)
                    SetTimer(p.hwnd, kSubmenuTimer, UINT(kSubmenuDelay * 1000), nullptr);   // refermera le sous-menu
                render(p);
            }
            return 0;
        }
        case WM_TIMER:
            if (wp == kRefreshTimer) {
                if (live && p.owned && applyRefresh(*p.owned, live->refresh, dragging)) {
                    float t = 0, k = 0;
                    measureTexts(p, t, k);
                    render(p);
                }
                return 0;
            }
            if (wp == kSubmenuTimer) {
                KillTimer(p.hwnd, kSubmenuTimer);
                if (p.hover >= 0 && !p.model->items[size_t(p.hover)].submenu.empty()) {
                    openSubmenu(p, p.hover, false);
                } else if (p.hover >= 0 && p.openSub >= 0) {
                    closeBelow(depthOf(&p));
                    render(p);
                }
            }
            return 0;
        case WM_LBUTTONUP: {
            double y = (double(short(HIWORD(lp))) - p.margin) / s();
            double x = (double(short(LOWORD(lp))) - p.margin) / s();
            if (dragging) {
                ReleaseCapture();   // WM_CAPTURECHANGED remet dragging à 0
                dragging = 0;
                return 0;
            }
            if (releaseRow(p, rowAt(p.layout, *p.model, y), x - kMenuPadding)) return 0;
            activate(p, hitTestMenu(p.layout, *p.model, y));
            return 0;
        }
        case WM_KEYDOWN: {
            Panel& deep = *panels.back();
            switch (wp) {
                case VK_DOWN:
                case VK_UP:
                    deep.hover = nextSelectable(*deep.model, deep.hover, wp == VK_DOWN ? +1 : -1);
                    render(deep);
                    break;
                case VK_RIGHT:
                    if (deep.hover >= 0 && !deep.model->items[size_t(deep.hover)].submenu.empty())
                        openSubmenu(deep, deep.hover, true);
                    else
                        switchTitle(+1);
                    break;
                case VK_LEFT:
                    if (panels.size() > 1) {
                        closeBelow(int(panels.size()) - 2);
                        render(*panels.back());
                    } else {
                        switchTitle(-1);
                    }
                    break;
                case VK_RETURN:
                case VK_SPACE: activate(deep, deep.hover); break;
                case VK_ESCAPE:
                    if (panels.size() > 1) {
                        closeBelow(int(panels.size()) - 2);
                        render(*panels.back());
                    } else {
                        done = true;
                    }
                    break;
                default: break;
            }
            return 0;
        }
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE && !panelOf(reinterpret_cast<HWND>(lp))) done = true;   // clic ailleurs
            return 0;
        case WM_MENU_BACKDROP:
            onBackdrop();
            return 0;
        default: break;
    }
    return DefWindowProcW(p.hwnd, msg, wp, lp);
}

} // namespace

void MenuWindow::setCaptureVisible(bool on) {
    Session* session = g_session;
    if (!session || session->captureVisible == on) return;
    session->captureVisible = on;
    if (!on) session->thawAt = GetTickCount64() + 150;
    if (diagnosticCapture()) return;   // déjà visibles aux captures
    for (auto& p : session->panels)
        if (p->hwnd) SetWindowDisplayAffinity(p->hwnd, on ? WDA_NONE : WDA_EXCLUDEFROMCAPTURE);
}

bool MenuWindow::snapshot(const Env& env, const MenuModel& model, std::vector<std::uint8_t>& bgra, UINT& w, UINT& h) {
    Session session;
    session.env = env;
    if (!session.initText()) return false;
    Panel p;
    p.session = &session;
    p.owned = std::make_unique<MenuModel>(model);
    p.model = p.owned.get();
    float maxText = 0, maxShortcut = 0;
    session.measureTexts(p, maxText, maxShortcut);
    const float sc = env.scale;
    p.layout = layoutMenu(*p.model, maxText / sc, maxShortcut / sc);
    w = UINT(std::ceil(p.layout.width * sc));
    h = UINT(std::ceil(p.layout.height * sc));
    Com<IWICImagingFactory> wic;
    Com<IWICBitmap> bmp;
    Com<ID2D1Factory1> factory;
    Com<ID2D1RenderTarget> rt;
    Com<ID2D1DeviceContext> d;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) ||
        FAILED(wic->CreateBitmap(w, h, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp)) ||
        FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf())) ||
        FAILED(factory->CreateWicBitmapRenderTarget(bmp.Get(), D2D1::RenderTargetProperties(), &rt)) ||
        FAILED(rt.As(&d)))
        return false;
    const D2D1_RECT_F panel{0, 0, float(w), float(h)};
    const float radius = float(limitedCornerRadius(panel.right, panel.bottom, kMenuRadius * sc));
    d->BeginDraw();
    d->Clear(rgba(0, 0, 0, 0));
    d->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    Com<ID2D1SolidColorBrush> bg, border;   // verre dépoli de repli (pas de capture hors écran)
    d->CreateSolidColorBrush(env.dark ? rgba(0.15f, 0.15f, 0.16f, 0.94f) : rgba(0.96f, 0.96f, 0.97f, 0.94f), &bg);
    d->CreateSolidColorBrush(env.dark ? rgba(1, 1, 1, 0.14f) : rgba(0, 0, 0, 0.10f), &border);
    d->FillRoundedRectangle(D2D1_ROUNDED_RECT{panel, radius, radius}, bg.Get());
    d->DrawRoundedRectangle(D2D1_ROUNDED_RECT{panel, radius, radius}, border.Get(), std::max(1.0f, sc * 0.5f));
    session.drawItems(d.Get(), p, panel, 1);
    if (FAILED(d->EndDraw())) return false;
    bgra.resize(size_t(w) * h * 4);
    WICRect all{0, 0, INT(w), INT(h)};
    return SUCCEEDED(bmp->CopyPixels(&all, w * 4, UINT(bgra.size()), bgra.data()));
}

int MenuWindow::track(const Env& env, const MenuModel& model, POINT anchor, Side side, const BarLink* bar, const Live* live) {
    if (model.items.empty()) return 0;
    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = panelProc;
    wc.hInstance = env.instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);

    Session session;
    session.env = env;
    session.side = side;
    session.bar = bar;
    session.live = live;
    g_session = &session;
    HHOOK hook = SetWindowsHookExW(WH_MOUSE_LL, outsideClickHook, env.instance, 0);
    struct Unhook {
        HHOOK h;
        ~Unhook() {
            if (h) UnhookWindowsHookEx(h);
            g_session = nullptr;
        }
    } unhook{hook};
    if (!session.init()) {
        log::warn(L"Menu : initialisation graphique impossible");
        return 0;
    }
    // Lignes enrichies : le menu principal travaille sur sa copie (valeurs modifiées et rafraîchies en direct).
    Panel* first = session.open(model, anchor, true, nullptr, live ? std::make_unique<MenuModel>(model) : nullptr);
    if (!first) {
        log::warn(L"Menu : création de la fenêtre impossible");
        return 0;
    }
    if (live && live->refresh) SetTimer(first->hwnd, kRefreshTimer, 500, nullptr);

    // Boucle modale : un clic hors des panneaux (Dock compris) ferme le menu et est absorbé.
    while (!session.done) {
        bool fading = false;
        for (auto& p : session.panels)
            if (p->shownAt >= 0 && now() - p->shownAt < kFadeSeconds + 0.02) fading = true;
        if (fading) session.renderAll();
        MSG msg;
        if (!PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            MsgWaitForMultipleObjects(0, nullptr, FALSE, fading ? 8 : INFINITE, QS_ALLINPUT);
            continue;
        }
        if (msg.message == WM_QUIT) {
            PostQuitMessage(int(msg.wParam));
            break;
        }
        bool press = msg.message == WM_LBUTTONDOWN || msg.message == WM_RBUTTONDOWN || msg.message == WM_MBUTTONDOWN ||
                     msg.message == WM_NCLBUTTONDOWN || msg.message == WM_NCRBUTTONDOWN;
        if (press && !session.panelOf(msg.hwnd)) {
            session.done = true;
            continue;
        }
        if (session.panelOf(msg.hwnd) && (msg.message == WM_KEYDOWN || msg.message == WM_KEYUP)) {
            DispatchMessageW(&msg);   // pas de TranslateMessage : les flèches et Entrée suffisent
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    // Le hook reste posé jusqu'au relâchement du clic extérieur (au plus 2 s), pour l'absorber aussi.
    for (const double until = now() + 2; session.swallowUp && now() < until;) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                PostQuitMessage(int(msg.wParam));
                session.swallowUp = 0;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
    }
    int result = session.result;
    session.capture.stop();
    session.panels.clear();
    if (env.trace) log::info(L"[trace] menu : choix %d", result);
    return result;
}

} // namespace md
