#include "apps_window.h"

#include <d2d1_3.h>
#include <d2d1effects.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dwrite_3.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

#include "../core/diag.h"
#include "../core/log.h"
#include "../glass/glass_renderer.h"
#include "../icons/icon_provider.h"
#include "../popup/popup_glass.h"
#include "../shell/shell_actions.h"
#include "../theme/wallpaper_art.h"
#include "apps_layout.h"

namespace md {

namespace {

template <class T> using Com = Microsoft::WRL::ComPtr<T>;

constexpr wchar_t kClass[] = L"MacDockApps";
constexpr UINT WM_APPS_BACKDROP = WM_APP + 21;
constexpr UINT WM_APPS_ICON = WM_APP + 22;   // wParam : indice de l'app ; lParam : IconProvider::ImagePtr* à reprendre
constexpr UINT_PTR kCaretTimer = 1;
constexpr double kAppearSeconds = 0.18;
constexpr double kWheelPause = 0.25;          // une page par geste de molette
constexpr float kNameFont = 12, kSearchFont = 15, kEmptyFont = 17;
constexpr float kLabelH = 32, kLabelGap = 6;  // nom sous l'icône, deux lignes au plus

double now() {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return double(c.QuadPart) / double(f.QuadPart);
}

D2D1_COLOR_F rgba(float r, float g, float b, float a) { return D2D1::ColorF(r, g, b, a); }

// Points des pages : centre du point p (points).
D2D1_POINT_2F dotCenter(const AppsGeometry& g, double screenW, int p) {
    const float step = 16, width = float(g.pages - 1) * step;
    return {float(screenW / 2) - width / 2 + float(p) * step, float(g.dotsY)};
}

int dotHit(const AppsGeometry& g, double screenW, double x, double y) {
    if (g.pages < 2) return -1;
    for (int p = 0; p < g.pages; ++p) {
        const D2D1_POINT_2F c = dotCenter(g, screenW, p);
        if (std::abs(x - c.x) <= 8 && std::abs(y - c.y) <= 10) return p;
    }
    return -1;
}

// Ce qui est montré (en points) : le dessin ne dépend que de cet état.
struct View {
    const std::vector<AppEntry>* apps = nullptr;
    std::vector<std::size_t> shown;   // indices dans *apps, ordre d'affichage
    AppsGeometry g;
    double W = 0, H = 0;
    int page = 0, selected = -1, hover = -1;   // selected et hover : positions dans shown
    std::wstring query;
    bool caret = true;
    float appear = 1;                  // ouverture : 0 → 1

    void filter() {
        shown = searchApps(*apps, query);
        g = appsLayout(W, H, shown.size());
        page = 0;
        selected = query.empty() || shown.empty() ? -1 : 0;   // en recherche, Entrée lance le premier résultat
        hover = -1;
    }
};

// Polices et noms mis en forme, partagés par la fenêtre et le rendu hors écran.
struct Painter {
    Com<IDWriteFactory3> dwrite;
    Com<IDWriteTextFormat> nameFormat, searchFormat, emptyFormat;
    Com<IDWriteInlineObject> ellipsis;
    std::map<std::size_t, Com<IDWriteTextLayout>> labels;   // par indice d'app
    float labelWidth = 0;

    bool init(const std::wstring& font) {
        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory3),
                                       reinterpret_cast<IUnknown**>(dwrite.GetAddressOf()))))
            return false;
        const wchar_t* family = font.empty() ? L"Segoe UI" : font.c_str();
        auto make = [&](float size, DWRITE_FONT_WEIGHT w, DWRITE_TEXT_ALIGNMENT align, bool wrap) {
            Com<IDWriteTextFormat> f;
            if (FAILED(dwrite->CreateTextFormat(family, nullptr, w, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                                size, L"", &f)))
                return f;
            f->SetTextAlignment(align);
            f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            f->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_EMERGENCY_BREAK : DWRITE_WORD_WRAPPING_NO_WRAP);
            if (!ellipsis) dwrite->CreateEllipsisTrimmingSign(f.Get(), &ellipsis);
            DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
            f->SetTrimming(&trim, ellipsis.Get());
            return f;
        };
        nameFormat = make(kNameFont, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_TEXT_ALIGNMENT_CENTER, true);
        searchFormat = make(kSearchFont, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, false);
        emptyFormat = make(kEmptyFont, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_TEXT_ALIGNMENT_CENTER, false);
        return nameFormat && searchFormat && emptyFormat;
    }

    IDWriteTextLayout* label(const View& v, std::size_t app, float width) {
        if (width != labelWidth) {
            labels.clear();
            labelWidth = width;
        }
        auto& l = labels[app];
        if (!l) {
            const std::wstring& name = (*v.apps)[app].name;
            dwrite->CreateTextLayout(name.c_str(), UINT32(name.size()), nameFormat.Get(), width, kLabelH, &l);
        }
        return l.Get();
    }

    // Tout le contenu, en points (la transformation du contexte porte l'échelle). iconOf : bitmap d'une app ou nullptr.
    void paint(ID2D1DeviceContext* d, View& v, const std::function<ID2D1Bitmap1*(std::size_t app)>& iconOf) {
        const float a = std::clamp(v.appear, 0.0f, 1.0f);
        Com<ID2D1SolidColorBrush> white, shadow;
        d->CreateSolidColorBrush(rgba(1, 1, 1, a), &white);
        d->CreateSolidColorBrush(rgba(0, 0, 0, 0.45f * a), &shadow);
        if (!white || !shadow) return;
        const AppsGeometry& g = v.g;

        // Champ de recherche.
        const float cx = float(v.W / 2);
        const D2D1_RECT_F sr{cx - float(g.searchW / 2), float(g.searchTop), cx + float(g.searchW / 2),
                             float(g.searchTop + g.searchH)};
        const float r = float(g.searchH / 2);
        white->SetOpacity(0.16f);
        d->FillRoundedRectangle(D2D1::RoundedRect(sr, r, r), white.Get());
        white->SetOpacity(0.30f);
        d->DrawRoundedRectangle(D2D1::RoundedRect(sr, r, r), white.Get(), 0.75f);
        const float my = (sr.top + sr.bottom) / 2, mx = sr.left + 17;
        white->SetOpacity(0.75f);
        d->DrawEllipse(D2D1::Ellipse({mx, my - 1}, 5, 5), white.Get(), 1.6f);
        d->DrawLine({mx + 3.6f, my + 2.6f}, {mx + 7.5f, my + 6.5f}, white.Get(), 1.8f);
        const bool placeholder = v.query.empty();
        const std::wstring text = placeholder ? std::wstring(L"Rechercher") : v.query;
        const float textLeft = sr.left + 30, textW = std::max(1.0f, sr.right - 14 - textLeft);
        Com<IDWriteTextLayout> st;
        dwrite->CreateTextLayout(text.c_str(), UINT32(text.size()), searchFormat.Get(), textW, float(g.searchH), &st);
        if (st) {
            DWRITE_TEXT_METRICS tm{};
            st->GetMetrics(&tm);
            const float ty = my - tm.height / 2;
            white->SetOpacity(placeholder ? 0.55f : 1.0f);
            d->DrawTextLayout({textLeft, ty}, st.Get(), white.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            if (v.caret) {
                const float x = textLeft + (placeholder ? 0 : std::min(tm.widthIncludingTrailingWhitespace, textW)) + 1;
                white->SetOpacity(0.9f);
                d->DrawLine({x, my - 8}, {x, my + 8}, white.Get(), 1.5f);
            }
        }
        white->SetOpacity(1);

        // Grille de la page : légère avancée à l'ouverture, comme Launchpad.
        D2D1_MATRIX_3X2_F base;
        d->GetTransform(&base);
        const float zoom = 0.94f + 0.06f * a;
        d->SetTransform(D2D1::Matrix3x2F::Scale(zoom, zoom, {cx, float(v.H / 2)}) * base);
        const std::size_t begin = std::size_t(v.page) * std::size_t(g.perPage);
        const float icon = float(g.icon);
        for (int k = 0; k < g.perPage; ++k) {
            const std::size_t pos = begin + std::size_t(k);
            if (pos >= v.shown.size()) break;
            const std::size_t app = v.shown[pos];
            const float x0 = float(g.gridLeft + (k % g.columns) * g.cellW), y0 = float(g.gridTop + (k / g.columns) * g.cellH);
            const float top = y0 + std::max(0.0f, (float(g.cellH) - icon - kLabelGap - kLabelH) / 2);
            const float ix = x0 + (float(g.cellW) - icon) / 2;
            if (int(pos) == v.selected || int(pos) == v.hover) {
                const D2D1_RECT_F halo{ix - 12, top - 8, ix + icon + 12, top + icon + kLabelGap + kLabelH + 4};
                white->SetOpacity(int(pos) == v.selected ? 0.22f : 0.12f);
                d->FillRoundedRectangle(D2D1::RoundedRect(halo, 16, 16), white.Get());
                white->SetOpacity(1);
            }
            if (ID2D1Bitmap1* bmp = iconOf(app))
                d->DrawBitmap(bmp, D2D1::RectF(ix, top, ix + icon, top + icon), a, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
            const float lw = float(g.cellW) - 12;
            if (IDWriteTextLayout* l = label(v, app, lw)) {
                const D2D1_POINT_2F at{x0 + 6, top + icon + kLabelGap};
                d->DrawTextLayout({at.x + 0.8f, at.y + 0.8f}, l, shadow.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
                d->DrawTextLayout(at, l, white.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
        }
        d->SetTransform(base);

        if (v.shown.empty() && !v.query.empty()) {
            const std::wstring none = L"Aucun résultat";
            Com<IDWriteTextLayout> l;
            dwrite->CreateTextLayout(none.c_str(), UINT32(none.size()), emptyFormat.Get(), float(v.W), 40, &l);
            white->SetOpacity(0.7f);
            if (l) d->DrawTextLayout({0, float(v.H / 2) - 20}, l.Get(), white.Get());
            white->SetOpacity(1);
        }

        // Points des pages.
        for (int p = 0; g.pages > 1 && p < g.pages; ++p) {
            white->SetOpacity(p == v.page ? 0.95f : 0.35f);
            d->FillEllipse(D2D1::Ellipse(dotCenter(g, v.W, p), 3.5f, 3.5f), white.Get());
        }
    }
};

// Images extraites hors du fil de l'interface : la page affichée d'abord. hwnd nul = vue fermée.
struct Loader {
    std::mutex mutex;
    HWND hwnd = nullptr;
    std::deque<std::size_t> priority;
};

struct Session {
    MenuWindow::Env env;
    const AppsWindow::Request* req = nullptr;
    float sc = 1;
    View view;
    Painter painter;

    Com<ID2D1Factory3> d2dFactory;
    Com<ID2D1Device2> d2dDevice;
    Com<ID2D1DeviceContext2> dc;
    Com<IDCompositionDesktopDevice> dcomp;
    Com<IDCompositionTarget> target;
    Com<IDCompositionVisual2> visual;
    Com<IDCompositionSurface> surface;

    GlassRenderer glass;
    bool glassReady = false;
    ScreenBackdrop screen;
    WindowBackdrop backdrop;
    GlassTarget glassTarget;

    HWND hwnd = nullptr;
    RECT rc{};
    std::shared_ptr<Loader> loader;
    int iconPx = 96;
    std::vector<IconProvider::ImagePtr> images;   // par indice d'app
    std::vector<Com<ID2D1Bitmap1>> bitmaps;
    double shownAt = 0, lastWheel = 0;
    PressGate press;   // un clic commencé hors de la vue (double-clic sur le bouton Apps) ne compte pas
    bool done = false;
    std::wstring result;

    UINT width() const { return UINT(rc.right - rc.left); }
    UINT height() const { return UINT(rc.bottom - rc.top); }

    bool init();
    bool createWindow();
    void startLoading();
    void stopLoading();
    void wantVisibleIcons();
    ID2D1Bitmap1* bitmapOf(std::size_t app);
    double progress() const { return std::clamp((now() - shownAt) / kAppearSeconds, 0.0, 1.0); }
    void render();
    void choose(int pos);
    void changed();   // recherche ou page : icônes à charger, nouveau dessin
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);
};

LRESULT CALLBACK appsProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (auto* s = reinterpret_cast<Session*>(GetWindowLongPtrW(h, GWLP_USERDATA))) return s->handle(msg, wp, lp);
    return DefWindowProcW(h, msg, wp, lp);
}

bool Session::init() {
    sc = env.scale;
    if (!painter.init(env.font) || !env.device) return false;
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

bool Session::createWindow() {
    hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP, kClass, L"Apps", WS_POPUP,
                           rc.left, rc.top, int(width()), int(height()), nullptr, nullptr, env.instance, nullptr);
    if (!hwnd) return false;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, LONG_PTR(this));
    // Sinon le verre se verrait lui-même ; en diagnostic, visible aux enregistreurs (le verre peut alors se refléter).
    if (!diagnosticCapture()) SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE);
    if (FAILED(dcomp->CreateTargetForHwnd(hwnd, TRUE, &target)) || FAILED(dcomp->CreateVisual(&visual)) ||
        FAILED(dcomp->CreateSurface(width(), height(), DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED,
                                    &surface)))
        return false;
    visual->SetContent(surface.Get());
    target->SetRoot(visual.Get());
    return true;
}

void Session::wantVisibleIcons() {
    if (!loader) return;
    std::vector<std::size_t> want;
    const std::size_t per = std::size_t(view.g.perPage);
    for (int p : {view.page, view.page + 1, view.page - 1}) {   // la page, puis ses voisines
        if (p < 0 || p >= view.g.pages) continue;
        for (std::size_t pos = std::size_t(p) * per; pos < std::min(view.shown.size(), std::size_t(p + 1) * per); ++pos)
            if (!images[view.shown[pos]]) want.push_back(view.shown[pos]);
    }
    std::lock_guard lock(loader->mutex);
    loader->priority.assign(want.begin(), want.end());
}

void Session::startLoading() {
    loader = std::make_shared<Loader>();
    loader->hwnd = hwnd;
    std::vector<std::pair<std::wstring, std::wstring>> keys;   // (clé, nom Shell)
    for (const AppEntry& e : req->apps) keys.emplace_back(L"apps:" + e.parsingName, launchTarget(e));
    std::vector<char> have(keys.size(), 0);
    if (req->cache)   // icônes d'une ouverture précédente : là tout de suite
        for (std::size_t i = 0; i < keys.size(); ++i) {
            images[i] = req->cache->find(keys[i].first, iconPx);
            have[i] = images[i] ? 1 : 0;
        }
    wantVisibleIcons();
    std::thread([l = loader, keys = std::move(keys), px = iconPx, style = req->icons, cache = req->cache,
                 have = std::move(have)] {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        {
            IconProvider icons;   // propre à ce fil : le cache du Dock n'est pas partagé
            icons.setStrictTahoe(style.strict);
            icons.setDark(style.dark);
            icons.setGrid(style.shapeRatio, style.cornerRatio, style.jailInset, style.shadowOpacity);
            if (!style.customDir.empty()) icons.setCustomDir(style.customDir);
            std::vector<char> done = have;   // déjà dans le cache
            std::size_t next = 0;
            for (;;) {
                std::size_t i = keys.size();
                {
                    std::lock_guard lock(l->mutex);
                    if (!l->hwnd) break;
                    while (!l->priority.empty() && i == keys.size()) {
                        const std::size_t p = l->priority.front();
                        l->priority.pop_front();
                        if (p < keys.size() && !done[p]) i = p;
                    }
                }
                while (i == keys.size() && next < keys.size()) {   // ensuite, toutes les autres dans l'ordre
                    if (!done[next]) i = next;
                    ++next;
                }
                if (i == keys.size()) break;
                done[i] = 1;
                IconProvider::ImagePtr image = icons.get(keys[i].first, keys[i].second, px);
                if (cache) cache->put(keys[i].first, px, image);
                auto* box = new IconProvider::ImagePtr(std::move(image));
                bool posted = false;
                {
                    std::lock_guard lock(l->mutex);
                    posted = l->hwnd && PostMessageW(l->hwnd, WM_APPS_ICON, i, reinterpret_cast<LPARAM>(box));
                }
                if (!posted) delete box;
            }
        }
        CoUninitialize();
    }).detach();
}

void Session::stopLoading() {
    if (loader) {
        std::lock_guard lock(loader->mutex);
        loader->hwnd = nullptr;
    }
    MSG msg;
    while (hwnd && PeekMessageW(&msg, hwnd, WM_APPS_ICON, WM_APPS_ICON, PM_REMOVE))
        delete reinterpret_cast<IconProvider::ImagePtr*>(msg.lParam);
}

ID2D1Bitmap1* Session::bitmapOf(std::size_t app) {
    if (app >= images.size() || !images[app]) return nullptr;
    if (!bitmaps[app]) {
        const auto& im = *images[app];
        auto props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,
                                             D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        dc->CreateBitmap(D2D1::SizeU(UINT32(im.size), UINT32(im.size)), im.bgra.data(), UINT32(im.size * 4), &props,
                         &bitmaps[app]);
    }
    return bitmaps[app].Get();
}

void Session::render() {
    view.appear = float(1 - std::pow(1 - progress(), 3));
    bool glassDrawn = false;
    if (glassReady && backdrop.valid && glassTarget.ensure(env.device, dc.Get(), width(), height())) {
        GlassParams gp;   // tout l'écran : flou fort et sombre, sans biseau ni réfraction
        gp.scale = sc;
        gp.dark = true;
        gp.blurSigmaPx = 40 * sc;
        gp.bevelPx = 0;
        gp.refraction = 0;
        gp.chromatic = 0;
        gp.fresnel = 0;
        gp.specular = 0;
        gp.tint = 0.35f;
        gp.saturation = 1.1f;
        gp.shadowBlurPx = 0;
        gp.backdropIsScRgb = backdrop.scRgb;
        gp.sdrWhiteScale = backdrop.white;
        GlassShape shape{0, 0, float(width()), float(height()), 0, 1, 0, view.appear};
        Com<ID3D11DeviceContext> ctx;
        env.device->GetImmediateContext(&ctx);
        glassDrawn = glass.render(ctx.Get(), backdrop.srv.Get(), width(), height(), glassTarget.rtv.Get(), {&shape, 1}, gp);
    }
    POINT offset{};
    Com<ID2D1DeviceContext> d;
    if (FAILED(surface->BeginDraw(nullptr, IID_PPV_ARGS(&d), &offset))) return;
    d->SetDpi(96, 96);
    const auto base = D2D1::Matrix3x2F::Translation(float(offset.x), float(offset.y));
    d->SetTransform(base);
    d->Clear(rgba(0, 0, 0, 0));
    d->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    Com<ID2D1SolidColorBrush> veil;
    if (glassDrawn) {
        d->DrawBitmap(glassTarget.bitmap.Get());
        d->CreateSolidColorBrush(rgba(0, 0, 0, 0.18f * view.appear), &veil);   // contraste du texte blanc
    } else {
        d->CreateSolidColorBrush(rgba(0.07f, 0.08f, 0.11f, 0.88f * view.appear), &veil);
    }
    if (veil) d->FillRectangle(D2D1::RectF(0, 0, float(width()), float(height())), veil.Get());
    d->SetTransform(D2D1::Matrix3x2F::Scale(sc, sc) * base);
    painter.paint(d.Get(), view, [this](std::size_t app) { return bitmapOf(app); });
    surface->EndDraw();
    dcomp->Commit();
}

void Session::choose(int pos) {
    if (pos < 0 || std::size_t(pos) >= view.shown.size()) return;
    result = (*view.apps)[view.shown[std::size_t(pos)]].parsingName;
    done = true;
}

void Session::changed() {
    wantVisibleIcons();
    render();
}

LRESULT Session::handle(UINT msg, WPARAM wp, LPARAM lp) {
    const auto toPt = [&](LPARAM l) { return std::pair<double, double>{short(LOWORD(l)) / sc, short(HIWORD(l)) / sc}; };
    switch (msg) {
        case WM_CHAR:
            if (wp >= 32 && wp != 127 && view.query.size() < 64) {
                view.query.push_back(wchar_t(wp));
                view.filter();
                changed();
            }
            return 0;
        case WM_KEYDOWN:
            switch (wp) {
                case VK_ESCAPE:
                    if (view.query.empty()) {
                        done = true;
                    } else {
                        view.query.clear();
                        view.filter();
                        changed();
                    }
                    return 0;
                case VK_BACK:
                    if (!view.query.empty()) {
                        view.query.pop_back();
                        view.filter();
                        changed();
                    }
                    return 0;
                case VK_RETURN:
                    choose(view.selected >= 0 ? view.selected : 0);
                    return 0;
                default: {
                    const AppsCursor c = appsKey(view.g, {view.page, view.selected}, UINT(wp), view.shown.size());
                    if (c.page != view.page || c.selected != view.selected) {
                        view.page = c.page;
                        view.selected = c.selected;
                        changed();
                    }
                    return 0;
                }
            }
        case WM_MOUSEWHEEL: {
            const double t = now();
            if (t - lastWheel < kWheelPause) return 0;
            const AppsCursor c = appsGoToPage(view.g, {view.page, view.selected},
                                              view.page + (GET_WHEEL_DELTA_WPARAM(wp) < 0 ? 1 : -1), view.shown.size());
            if (c.page != view.page) {
                lastWheel = t;
                view.page = c.page;
                view.selected = c.selected;   // la sélection suit la page (flèches et Entrée)
                view.hover = -1;
                changed();
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            const auto [x, y] = toPt(lp);
            const int h = appsHit(view.g, view.page, x, y, view.shown.size());
            if (h != view.hover) {
                view.hover = h;
                render();
            }
            return 0;
        }
        case WM_LBUTTONDOWN: press.press(); return 0;
        case WM_LBUTTONUP: {
            if (!press.release()) return 0;
            const auto [x, y] = toPt(lp);
            const int h = appsHit(view.g, view.page, x, y, view.shown.size());
            if (h >= 0) {
                choose(h);
            } else if (const int p = dotHit(view.g, view.W, x, y); p >= 0) {
                const AppsCursor cur = appsGoToPage(view.g, {view.page, view.selected}, p, view.shown.size());
                view.page = cur.page;
                view.selected = cur.selected;
                changed();
            } else {
                const double cx = view.W / 2;   // le champ de recherche ne ferme pas la vue
                const bool inSearch = std::abs(x - cx) <= view.g.searchW / 2 && y >= view.g.searchTop &&
                                      y <= view.g.searchTop + view.g.searchH;
                if (!inSearch) done = true;   // un clic dans le vide ferme, comme sur macOS
            }
            return 0;
        }
        case WM_RBUTTONUP: done = true; return 0;
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE) done = true;
            return 0;
        case WM_TIMER:
            if (wp == kCaretTimer) {
                view.caret = !view.caret;
                render();
            }
            return 0;
        case WM_APPS_ICON: {
            std::unique_ptr<IconProvider::ImagePtr> box(reinterpret_cast<IconProvider::ImagePtr*>(lp));
            if (wp < images.size()) {
                images[wp] = *box;
                bitmaps[wp].Reset();
                const std::size_t begin = std::size_t(view.page) * std::size_t(view.g.perPage);
                for (std::size_t pos = begin; pos < std::min(view.shown.size(), begin + std::size_t(view.g.perPage)); ++pos)
                    if (view.shown[pos] == wp) {   // seulement si elle est visible
                        render();
                        break;
                    }
            }
            return 0;
        }
        case WM_APPS_BACKDROP:
            if (screen.take(env.device) && screen.copyTo(env.device, rc, backdrop)) render();
            return 0;
        default: break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

std::uint32_t placeholderColor(std::size_t i) {   // cases de couleur du rendu sans icônes
    static const std::uint32_t colors[] = {0xFF0A84FF, 0xFF30D158, 0xFFFF9F0A, 0xFFFF375F, 0xFFBF5AF2, 0xFF64D2FF, 0xFFFFD60A};
    return colors[i % std::size(colors)];
}

} // namespace

std::optional<std::wstring> AppsWindow::track(const MenuWindow::Env& env, const Request& request) {
    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = appsProc;
    wc.hInstance = env.instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);

    Session s;
    s.env = env;
    s.req = &request;
    if (request.apps.empty() || !s.init()) {
        log::warn(L"Apps : vue impossible (%s)", request.apps.empty() ? L"catalogue vide" : L"initialisation graphique");
        return std::nullopt;
    }
    MONITORINFO mi{sizeof mi};
    HMONITOR mon = request.monitor ? request.monitor : MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
    GetMonitorInfoW(mon, &mi);
    s.rc = mi.rcMonitor;
    s.view.apps = &request.apps;
    s.view.W = double(s.width()) / s.sc;
    s.view.H = double(s.height()) / s.sc;
    s.view.filter();
    s.iconPx = int(std::lround(s.view.g.icon * s.sc));
    s.images.assign(request.apps.size(), nullptr);
    s.bitmaps.assign(request.apps.size(), nullptr);
    if (!s.createWindow()) {
        log::warn(L"Apps : création de la fenêtre impossible");
        if (s.hwnd) DestroyWindow(s.hwnd);
        return std::nullopt;
    }
    struct Cleanup {
        Session& s;
        ~Cleanup() {
            s.stopLoading();
            s.screen.stop();
            if (s.hwnd) {
                KillTimer(s.hwnd, kCaretTimer);
                DestroyWindow(s.hwnd);
            }
        }
    } cleanup{s};

    if (s.glassReady) {
        s.screen.start(s.hwnd, WM_APPS_BACKDROP, mon, mi.rcMonitor);
        for (const double until = now() + 0.15; !s.backdrop.valid && now() < until && !s.screen.failed();) {
            MSG msg;
            if (PeekMessageW(&msg, s.hwnd, WM_APPS_BACKDROP, WM_APPS_BACKDROP, PM_REMOVE)) DispatchMessageW(&msg);
            else MsgWaitForMultipleObjects(0, nullptr, FALSE, 5, QS_POSTMESSAGE);
        }
    }
    s.shownAt = now();
    if (env.trace)
        log::info(L"[trace] apps : %zu apps, %d × %d par page, %d pages, verre %s", request.apps.size(), s.view.g.columns,
                  s.view.g.rows, s.view.g.pages, s.backdrop.valid ? L"réel" : L"dépoli");
    s.startLoading();
    s.render();
    ShowWindow(s.hwnd, SW_SHOW);
    forceForeground(s.hwnd);   // la frappe va à la recherche
    SetFocus(s.hwnd);
    SetTimer(s.hwnd, kCaretTimer, GetCaretBlinkTime() == INFINITE ? 530 : GetCaretBlinkTime(), nullptr);

    while (!s.done) {
        const bool animating = s.progress() < 1;
        MSG msg;
        if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                PostQuitMessage(int(msg.wParam));
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            continue;
        }
        if (animating) {
            s.render();
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 8, QS_ALLINPUT);
            continue;
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, INFINITE, QS_ALLINPUT);
    }
    if (env.trace) log::info(L"[trace] apps : choix %s", s.result.empty() ? L"(aucun)" : s.result.c_str());
    return s.result;
}

BgraImage appsSnapshot(const std::vector<AppEntry>& apps, const std::wstring& query, int page, bool dark, int width,
                       int height, bool icons, const AppsIconStyle& style) {
    BgraImage out{width, height, std::vector<std::uint8_t>(std::size_t(std::max(width, 0)) * std::max(height, 0) * 4, 0)};
    if (width <= 0 || height <= 0) return out;
    Com<ID3D11Device> dev;
    const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, &dev,
                                 nullptr, nullptr)) &&
        FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, &dev,
                                 nullptr, nullptr)))
        return out;
    Com<IDXGIDevice> dxgi;
    Com<ID2D1Factory3> factory;
    Com<ID2D1Device2> d2d;
    Com<ID2D1DeviceContext2> dc;
    D2D1_FACTORY_OPTIONS opts{};
    if (FAILED(dev.As(&dxgi)) ||
        FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory3), &opts,
                                 reinterpret_cast<void**>(factory.GetAddressOf()))) ||
        FAILED(factory->CreateDevice(dxgi.Get(), &d2d)) ||
        FAILED(d2d->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc)))
        return out;
    const auto fmt = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    Com<ID2D1Bitmap1> target, readback, wallBmp;
    const auto size = D2D1::SizeU(UINT32(width), UINT32(height));
    if (FAILED(dc->CreateBitmap(size, nullptr, 0, D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, fmt), &target)) ||
        FAILED(dc->CreateBitmap(size, nullptr, 0,
                                D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, fmt),
                                &readback)))
        return out;
    const BgraImage wall = macWallpaper(width, height, dark);   // opaque : non prémultiplié = prémultiplié
    dc->CreateBitmap(size, wall.px.data(), UINT32(width * 4), D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE, fmt),
                     &wallBmp);
    Com<ID2D1Effect> blur;
    if (wallBmp && SUCCEEDED(dc->CreateEffect(CLSID_D2D1GaussianBlur, &blur))) {
        blur->SetInput(0, wallBmp.Get());
        blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, 30.0f);
        blur->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);
    }

    Painter painter;
    if (!painter.init(L"")) return out;
    View v;
    v.apps = &apps;
    v.W = width;
    v.H = height;
    v.query = query;
    v.filter();
    v.page = std::clamp(page, 0, v.g.pages - 1);
    v.caret = !query.empty();

    // Icônes de la page : réelles (rendu --apps-snapshot) ou cases de couleur (tests).
    std::map<std::size_t, Com<ID2D1Bitmap1>> bitmaps;
    IconProvider provider;
    provider.setStrictTahoe(style.strict);
    provider.setDark(style.dark);
    provider.setGrid(style.shapeRatio, style.cornerRatio, style.jailInset, style.shadowOpacity);
    if (!style.customDir.empty()) provider.setCustomDir(style.customDir);
    const int px = int(std::lround(v.g.icon));
    const auto iconOf = [&](std::size_t app) -> ID2D1Bitmap1* {
        auto& b = bitmaps[app];
        if (b) return b.Get();
        if (icons) {
            if (auto im = provider.get(L"apps:" + apps[app].parsingName, launchTarget(apps[app]), px))
                dc->CreateBitmap(D2D1::SizeU(UINT32(im->size), UINT32(im->size)), im->bgra.data(), UINT32(im->size * 4),
                                 D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE, fmt), &b);
            return b.Get();
        }
        std::vector<std::uint8_t> sq(std::size_t(px) * px * 4, 0);
        const std::uint32_t c = placeholderColor(app);
        const double radius = px * 0.225;
        for (int y = 0; y < px; ++y)
            for (int x = 0; x < px; ++x) {   // carré arrondi plein
                const double dx = std::max({radius - x - 0.5, x + 0.5 - (px - radius), 0.0});
                const double dy = std::max({radius - y - 0.5, y + 0.5 - (px - radius), 0.0});
                if (dx * dx + dy * dy > radius * radius) continue;
                std::uint8_t* p = &sq[(std::size_t(y) * px + x) * 4];
                p[0] = std::uint8_t(c);
                p[1] = std::uint8_t(c >> 8);
                p[2] = std::uint8_t(c >> 16);
                p[3] = 255;
            }
        dc->CreateBitmap(D2D1::SizeU(UINT32(px), UINT32(px)), sq.data(), UINT32(px * 4),
                         D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE, fmt), &b);
        return b.Get();
    };

    dc->SetTarget(target.Get());
    dc->BeginDraw();
    dc->Clear(rgba(0, 0, 0, 1));
    if (blur) dc->DrawImage(blur.Get());
    else if (wallBmp) dc->DrawBitmap(wallBmp.Get());
    Com<ID2D1SolidColorBrush> veil;
    dc->CreateSolidColorBrush(rgba(0, 0, 0, dark ? 0.35f : 0.28f), &veil);
    if (veil) dc->FillRectangle(D2D1::RectF(0, 0, float(width), float(height)), veil.Get());
    painter.paint(dc.Get(), v, iconOf);
    if (FAILED(dc->EndDraw())) return out;
    D2D1_POINT_2U origin{0, 0};
    D2D1_RECT_U all{0, 0, UINT32(width), UINT32(height)};
    D2D1_MAPPED_RECT mapped{};
    if (FAILED(readback->CopyFromBitmap(&origin, target.Get(), &all)) || FAILED(readback->Map(D2D1_MAP_OPTIONS_READ, &mapped)))
        return out;
    for (int y = 0; y < height; ++y)
        std::copy_n(mapped.bits + std::size_t(y) * mapped.pitch, std::size_t(width) * 4, &out.px[std::size_t(y) * width * 4]);
    readback->Unmap();
    return out;
}

} // namespace md
