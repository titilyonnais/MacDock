#include "spotlight_window.h"

#include <d2d1_3.h>
#include <d2d1effects.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dwrite_3.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>

#include "../core/log.h"
#include "../geom/smooth_rect.h"
#include "../glass/glass_renderer.h"
#include "../icons/icon_provider.h"
#include "../popup/popup_glass.h"
#include "../shell/shell_actions.h"
#include "../theme/wallpaper_art.h"
#include "file_search.h"

namespace md {

namespace {

template <class T> using Com = Microsoft::WRL::ComPtr<T>;

constexpr wchar_t kClass[] = L"MacDockSpotlight";
constexpr UINT WM_SPOT_BACKDROP = WM_APP + 31;
constexpr UINT WM_SPOT_ICON = WM_APP + 32;    // lParam : IconPost* à reprendre
constexpr UINT WM_SPOT_FILES = WM_APP + 33;   // FileSearcher::take
constexpr UINT_PTR kCaretTimer = 1, kSearchTimer = 2;
constexpr UINT kSearchDelayMs = 150;
constexpr double kAppearSeconds = 0.12;

// Mesures en points (spec : Spotlight sur Tahoe).
constexpr float kPanelW = 680, kFieldH = 52, kRadius = 24;
constexpr float kListTop = 6, kListBottom = 8, kSectionH = 26, kRowH = 40, kIcon = 28;
constexpr float kRowInset = 8, kIconX = 16, kTextX = 54;
constexpr float kShadow = 14;   // marge de l'ombre autour du panneau (fenêtre et région)
constexpr float kFieldFont = 22, kTitleFont = 14, kSubFont = 11, kSectionFont = 11;
constexpr double kTopRatio = 0.22;
constexpr std::size_t kMaxRows = 12, kMaxSections = 3;

double now() {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return double(c.QuadPart) / double(f.QuadPart);
}

D2D1_COLOR_F rgba(float r, float g, float b, float a) { return D2D1::ColorF(r, g, b, a); }

float panelHeight(const std::vector<SpotSection>& sections) {
    if (sections.empty()) return kFieldH;
    float h = kFieldH + kListTop + kListBottom;
    for (const auto& s : sections) h += kSectionH + kRowH * float(s.items.size());
    return h;
}

float maxPanelHeight(std::size_t rows) {
    return kFieldH + kListTop + kListBottom + kSectionH * float(kMaxSections) + kRowH * float(rows);
}

// Ligne sous (x, y), en points depuis le coin du panneau ; -1 ailleurs.
int rowAt(const std::vector<SpotSection>& sections, double x, double y) {
    if (x < kRowInset || x > kPanelW - kRowInset) return -1;
    double top = kFieldH + kListTop;
    int index = 0;
    for (const auto& s : sections) {
        top += kSectionH;
        for (std::size_t i = 0; i < s.items.size(); ++i, ++index, top += kRowH)
            if (y >= top && y < top + kRowH) return index;
    }
    return -1;
}

struct View {
    std::wstring query;
    std::vector<SpotSection> sections;
    int selected = -1;
    bool caret = true, dark = false;
    float opacity = 1;
};

using IconDraw = std::function<void(ID2D1DeviceContext*, const SpotItem&, std::size_t index, const D2D1_RECT_F&)>;

// Polices partagées par la fenêtre et le rendu hors écran ; dessin en points, origine au coin du panneau.
struct Painter {
    Com<IDWriteFactory3> dwrite;
    Com<IDWriteTextFormat> field, title, sub, section;
    Com<IDWriteInlineObject> ellipsis;

    bool init(const std::wstring& font) {
        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory3),
                                       reinterpret_cast<IUnknown**>(dwrite.GetAddressOf()))))
            return false;
        const wchar_t* family = font.empty() ? L"Segoe UI" : font.c_str();
        auto make = [&](float size, DWRITE_FONT_WEIGHT w) {
            Com<IDWriteTextFormat> f;
            if (FAILED(dwrite->CreateTextFormat(family, nullptr, w, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                                size, L"", &f)))
                return f;
            f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            if (!ellipsis) dwrite->CreateEllipsisTrimmingSign(f.Get(), &ellipsis);
            DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
            f->SetTrimming(&trim, ellipsis.Get());
            return f;
        };
        field = make(kFieldFont, DWRITE_FONT_WEIGHT_NORMAL);
        title = make(kTitleFont, DWRITE_FONT_WEIGHT_NORMAL);
        sub = make(kSubFont, DWRITE_FONT_WEIGHT_NORMAL);
        section = make(kSectionFont, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        return field && title && sub && section;
    }

    // Texte centré verticalement dans [y, y + h] ; renvoie la largeur dessinée.
    float text(ID2D1DeviceContext* d, IDWriteTextFormat* fmt, const std::wstring& s, float x, float y, float maxW, float h,
               ID2D1Brush* ink) {
        if (s.empty() || maxW <= 1) return 0;
        Com<IDWriteTextLayout> l;
        if (FAILED(dwrite->CreateTextLayout(s.c_str(), UINT32(s.size()), fmt, maxW, h, &l))) return 0;
        DWRITE_TEXT_METRICS tm{};
        l->GetMetrics(&tm);
        d->DrawTextLayout({x, y + (h - tm.height) / 2}, l.Get(), ink, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        return std::min(tm.widthIncludingTrailingWhitespace, maxW);
    }

    void paint(ID2D1DeviceContext* d, const View& v, const IconDraw& icon) {
        const float a = std::clamp(v.opacity, 0.0f, 1.0f);
        Com<ID2D1SolidColorBrush> ink, grey, line, accent, white;
        d->CreateSolidColorBrush(v.dark ? rgba(1, 1, 1, 0.92f * a) : rgba(0, 0, 0, 0.86f * a), &ink);
        d->CreateSolidColorBrush(v.dark ? rgba(1, 1, 1, 0.50f * a) : rgba(0, 0, 0, 0.45f * a), &grey);
        d->CreateSolidColorBrush(v.dark ? rgba(1, 1, 1, 0.10f * a) : rgba(0, 0, 0, 0.08f * a), &line);
        d->CreateSolidColorBrush(rgba(0x0A / 255.0f, 0x84 / 255.0f, 1.0f, a), &accent);   // bleu système #0A84FF
        d->CreateSolidColorBrush(rgba(1, 1, 1, a), &white);
        if (!ink || !grey || !line || !accent || !white) return;

        // Champ : loupe, texte ou invite grisée, curseur.
        const float my = kFieldH / 2, mx = 25;
        d->DrawEllipse(D2D1::Ellipse({mx, my - 1.5f}, 7.5f, 7.5f), grey.Get(), 2);
        d->DrawLine({mx + 5.4f, my + 3.9f}, {mx + 10.5f, my + 9}, grey.Get(), 2.4f);
        const bool placeholder = v.query.empty();
        const float textX = 46, textW = kPanelW - textX - 20;
        const float w = text(d, field.Get(), placeholder ? L"Recherche Spotlight" : v.query, textX, 0, textW, kFieldH,
                             placeholder ? grey.Get() : ink.Get());
        if (v.caret) {
            const float x = placeholder ? textX - 2 : textX + w + 1;   // avant l'invite, comme macOS
            d->DrawLine({x, my - 13}, {x, my + 13}, accent.Get(), 2);
        }
        if (v.sections.empty()) return;
        d->FillRectangle(D2D1::RectF(0, kFieldH - 0.5f, kPanelW, kFieldH + 0.5f), line.Get());

        float top = kFieldH + kListTop;
        std::size_t index = 0;
        for (const auto& s : v.sections) {
            std::wstring caps = s.title;
            CharUpperBuffW(caps.data(), DWORD(caps.size()));
            text(d, section.Get(), caps, 18, top + 4, kPanelW - 36, kSectionH - 4, grey.Get());
            top += kSectionH;
            for (const SpotItem& it : s.items) {
                const bool sel = int(index) == v.selected;
                const D2D1_RECT_F row{kRowInset, top, kPanelW - kRowInset, top + kRowH};
                if (sel) d->FillRoundedRectangle(D2D1::RoundedRect(row, 8, 8), accent.Get());
                const D2D1_RECT_F ir{kIconX, top + (kRowH - kIcon) / 2, kIconX + kIcon, top + (kRowH + kIcon) / 2};
                if (it.kind == SpotKind::Calc) {   // touche « = » orange de la Calculatrice
                    Com<ID2D1SolidColorBrush> orange;
                    d->CreateSolidColorBrush(rgba(1.0f, 0.62f, 0.04f, a), &orange);
                    if (orange) d->FillRoundedRectangle(D2D1::RoundedRect(ir, 6.5f, 6.5f), orange.Get());
                    const float cx = (ir.left + ir.right) / 2, cy = (ir.top + ir.bottom) / 2;
                    d->DrawLine({cx - 6, cy - 3}, {cx + 6, cy - 3}, white.Get(), 2.2f);
                    d->DrawLine({cx - 6, cy + 3}, {cx + 6, cy + 3}, white.Get(), 2.2f);
                } else if (icon) {
                    icon(d, it, index, ir);
                }
                const float right = kPanelW - kRowInset - 12;
                const float tw = text(d, title.Get(), it.title, kTextX, top, right - kTextX, kRowH, sel ? white.Get() : ink.Get());
                if (!it.subtitle.empty()) {
                    white->SetOpacity(0.8f * a);
                    const float sx = kTextX + tw + 10;
                    text(d, sub.Get(), L"— " + it.subtitle, sx, top + 1, right - sx, kRowH, sel ? white.Get() : grey.Get());
                    white->SetOpacity(a);
                }
                top += kRowH;
                ++index;
            }
        }
    }
};

// Icônes extraites hors du fil de l'interface. hwnd nul = panneau fermé.
struct Loader {
    std::mutex mutex;
    std::condition_variable cv;
    HWND hwnd = nullptr;
    std::deque<std::pair<std::wstring, SpotItem>> queue;   // (clé, élément)
};

struct IconPost {
    std::wstring key;
    IconProvider::ImagePtr image;
};

std::wstring iconKey(const SpotItem& it) { return (it.kind == SpotKind::App ? L"a:" : L"f:") + it.target; }

struct Session;
Session* g_open = nullptr;

struct Session {
    MenuWindow::Env env;
    const SpotlightWindow::Request* req = nullptr;
    float sc = 1;
    View view;
    Painter painter;
    DocFeed docs;
    std::unique_ptr<FileSearcher> searcher;
    std::size_t maxRows = kMaxRows;

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
    RECT rc{};   // fenêtre (pixels d'écran)
    std::shared_ptr<Loader> loader;
    int iconPx = 28;
    std::map<std::wstring, IconProvider::ImagePtr> images;
    std::map<std::wstring, Com<ID2D1Bitmap1>> bitmaps;
    std::set<std::wstring> requested;
    double shownAt = 0;
    bool pressed = false, done = false;
    std::optional<SpotlightWindow::Choice> result;

    UINT width() const { return UINT(rc.right - rc.left); }
    UINT height() const { return UINT(rc.bottom - rc.top); }
    float margin() const { return kShadow * sc; }   // coin du panneau dans la fenêtre (pixels)
    std::pair<double, double> toPanel(LPARAM lp) const {
        return {(short(LOWORD(lp)) - margin()) / sc, (short(HIWORD(lp)) - margin()) / sc};
    }

    bool init();
    bool createWindow();
    void startLoading();
    void stopLoading();
    void wantIcons();
    ID2D1Bitmap1* bitmapOf(const std::wstring& key);
    double progress() const { return std::clamp((now() - shownAt) / kAppearSeconds, 0.0, 1.0); }
    void updateRegion();
    void recompute(bool keepSelection);
    void queryChanged();
    void render();
    void choose(int index, bool reveal);
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);
};

LRESULT CALLBACK spotProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (auto* s = reinterpret_cast<Session*>(GetWindowLongPtrW(h, GWLP_USERDATA))) return s->handle(msg, wp, lp);
    return DefWindowProcW(h, msg, wp, lp);
}

bool Session::init() {
    sc = env.scale;
    view.dark = env.dark;
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
    hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP, kClass, L"Spotlight", WS_POPUP,
                           rc.left, rc.top, int(width()), int(height()), nullptr, nullptr, env.instance, nullptr);
    if (!hwnd) return false;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, LONG_PTR(this));
    SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE);   // sinon le verre se verrait lui-même
    if (FAILED(dcomp->CreateTargetForHwnd(hwnd, TRUE, &target)) || FAILED(dcomp->CreateVisual(&visual)) ||
        FAILED(dcomp->CreateSurface(width(), height(), DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED,
                                    &surface)))
        return false;
    visual->SetContent(surface.Get());
    target->SetRoot(visual.Get());
    return true;
}

void Session::startLoading() {
    loader = std::make_shared<Loader>();
    loader->hwnd = hwnd;
    std::thread([l = loader, px = iconPx, style = req->icons] {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        {
            IconProvider icons;   // propre à ce fil
            icons.setStrictTahoe(style.strict);
            icons.setDark(style.dark);
            icons.setGrid(style.shapeRatio, style.cornerRatio, style.jailInset, style.shadowOpacity);
            if (!style.customDir.empty()) icons.setCustomDir(style.customDir);
            for (;;) {
                std::pair<std::wstring, SpotItem> job;
                {
                    std::unique_lock lock(l->mutex);
                    l->cv.wait(lock, [&] { return !l->hwnd || !l->queue.empty(); });
                    if (!l->hwnd) break;
                    job = std::move(l->queue.front());
                    l->queue.pop_front();
                }
                auto* post = new IconPost{job.first, job.second.kind == SpotKind::App
                                                         ? icons.get(L"apps:" + job.second.target, job.second.target, px)
                                                         : icons.fileIcon(job.second.target, px)};
                bool posted = false;
                {
                    std::lock_guard lock(l->mutex);
                    posted = l->hwnd && PostMessageW(l->hwnd, WM_SPOT_ICON, 0, reinterpret_cast<LPARAM>(post));
                }
                if (!posted) delete post;
            }
        }
        CoUninitialize();
    }).detach();
}

void Session::stopLoading() {
    if (loader) {
        std::lock_guard lock(loader->mutex);
        loader->hwnd = nullptr;
        loader->queue.clear();
        loader->cv.notify_all();
    }
    MSG msg;
    while (hwnd && PeekMessageW(&msg, hwnd, WM_SPOT_ICON, WM_SPOT_ICON, PM_REMOVE)) delete reinterpret_cast<IconPost*>(msg.lParam);
    while (hwnd && PeekMessageW(&msg, hwnd, WM_SPOT_FILES, WM_SPOT_FILES, PM_REMOVE)) {
        unsigned gen = 0;
        FileSearcher::take(msg.wParam, msg.lParam, gen);
    }
}

void Session::wantIcons() {
    if (!loader) return;
    std::lock_guard lock(loader->mutex);
    for (const auto& s : view.sections)
        for (const SpotItem& it : s.items) {
            if (it.kind == SpotKind::Calc) continue;
            const std::wstring key = iconKey(it);
            if (requested.insert(key).second) loader->queue.emplace_back(key, it);
        }
    loader->cv.notify_all();
}

ID2D1Bitmap1* Session::bitmapOf(const std::wstring& key) {
    auto im = images.find(key);
    if (im == images.end() || !im->second) return nullptr;
    auto& b = bitmaps[key];
    if (!b) {
        const auto& i = *im->second;
        auto props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,
                                             D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        dc->CreateBitmap(D2D1::SizeU(UINT32(i.size), UINT32(i.size)), i.bgra.data(), UINT32(i.size * 4), &props, &b);
    }
    return b.Get();
}

// Région de la fenêtre : le panneau affiché et son ombre ; un clic au-delà atteint la fenêtre dessous.
void Session::updateRegion() {
    const int w = int(std::lround((kPanelW + 2 * kShadow) * sc));
    const int h = int(std::lround((panelHeight(view.sections) + 2 * kShadow) * sc));
    const int r = int(std::lround(2 * (kRadius + kShadow) * sc));
    SetWindowRgn(hwnd, CreateRoundRectRgn(0, 0, w + 1, h + 1, r, r), TRUE);
}

void Session::recompute(bool keepSelection) {
    view.sections = spotTrim(spotlightResults(view.query, req->apps, docs.shown), maxRows);
    const int count = int(spotCount(view.sections));
    if (!keepSelection || view.selected >= count) view.selected = count ? 0 : -1;
    if (view.selected < 0 && count) view.selected = 0;
    updateRegion();
    wantIcons();
    render();
}

void Session::queryChanged() {
    docs.typed(wantsFileSearch(view.query));   // anciens documents gardés jusqu'à la nouvelle réponse
    KillTimer(hwnd, kSearchTimer);
    if (wantsFileSearch(view.query)) SetTimer(hwnd, kSearchTimer, kSearchDelayMs, nullptr);
    view.caret = true;
    recompute(false);
}

void Session::render() {
    view.opacity = float(1 - std::pow(1 - progress(), 3));
    const float m = margin();
    const D2D1_RECT_F panel{m, m, m + kPanelW * sc, m + panelHeight(view.sections) * sc};
    const float radius = float(limitedCornerRadius(panel.right - panel.left, panel.bottom - panel.top, kRadius * sc));
    bool glassDrawn = false;
    if (glassReady && backdrop.valid && glassTarget.ensure(env.device, dc.Get(), width(), height())) {
        const Metrics& mt = env.metrics;   // mêmes réglages que les menus
        GlassParams gp;
        gp.scale = sc;
        gp.dark = env.dark;
        gp.blurSigmaPx = float(mt.glassBlur) * 2.2f * sc;
        gp.bevelPx = float(mt.glassBevel) * 0.6f * sc;
        gp.refraction = float(mt.glassRefraction) * 0.5f;
        gp.chromatic = float(mt.glassChromatic) * 0.5f;
        gp.fresnel = float(mt.glassFresnel);
        gp.specular = float(mt.glassSpecular);
        gp.tint = std::min(1.0f, float(env.dark ? mt.glassTintDark : mt.glassTintLight) * 2.4f);
        gp.saturation = float(mt.glassSaturation);
        gp.shadowBlurPx = std::min(float(mt.shadowBlur), kShadow * 0.7f) * sc;
        gp.shadowOffsetPx = 3 * sc;
        gp.backdropIsScRgb = backdrop.scRgb;
        gp.sdrWhiteScale = backdrop.white;
        GlassShape shape{panel.left, panel.top, panel.right, panel.bottom, radius, 0.6f,
                         float(mt.shadowOpacity * (env.dark ? 2.0 : 1.4)), view.opacity};
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
    const D2D1_ROUNDED_RECT rr{panel, radius, radius};
    if (glassDrawn) {
        d->DrawBitmap(glassTarget.bitmap.Get());
    } else {   // verre dépoli de repli, comme les menus
        Com<ID2D1SolidColorBrush> bg;
        d->CreateSolidColorBrush(env.dark ? rgba(0.15f, 0.15f, 0.16f, 0.94f * view.opacity)
                                          : rgba(0.96f, 0.96f, 0.97f, 0.94f * view.opacity),
                                 &bg);
        if (bg) d->FillRoundedRectangle(rr, bg.Get());
    }
    Com<ID2D1SolidColorBrush> border;
    d->CreateSolidColorBrush(env.dark ? rgba(1, 1, 1, 0.14f * view.opacity) : rgba(0, 0, 0, 0.10f * view.opacity), &border);
    if (border) d->DrawRoundedRectangle(rr, border.Get(), std::max(1.0f, sc * 0.5f));
    d->SetTransform(D2D1::Matrix3x2F::Scale(sc, sc) * D2D1::Matrix3x2F::Translation(m, m) * base);
    painter.paint(d.Get(), view, [this](ID2D1DeviceContext* dd, const SpotItem& it, std::size_t, const D2D1_RECT_F& r) {
        if (ID2D1Bitmap1* b = bitmapOf(iconKey(it)))
            dd->DrawBitmap(b, r, view.opacity, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
    });
    surface->EndDraw();
    dcomp->Commit();
}

void Session::choose(int index, bool reveal) {
    const SpotItem* it = spotAt(view.sections, index < 0 ? 0 : std::size_t(index));
    if (!it) return;
    result = SpotlightWindow::Choice{*it, reveal && it->kind == SpotKind::File};
    done = true;
}

std::wstring clipboardLine(HWND owner) {   // lecture seule
    std::wstring out;
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(owner)) return out;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT))
        if (const auto* p = static_cast<const wchar_t*>(GlobalLock(h))) {
            out = p;
            GlobalUnlock(h);
        }
    CloseClipboard();
    return spotPasteLine(out);
}

LRESULT Session::handle(UINT msg, WPARAM wp, LPARAM lp) {
    const bool ctrl = GetKeyState(VK_CONTROL) < 0;
    switch (msg) {
        case WM_CHAR:
            if (spotAcceptChar(view.query, wchar_t(wp))) {
                view.query.push_back(wchar_t(wp));
                queryChanged();
            }
            return 0;
        case WM_KEYDOWN: {
            const int count = int(spotCount(view.sections));
            switch (wp) {
                case VK_ESCAPE:
                    if (view.query.empty()) {
                        done = true;
                    } else {
                        view.query.clear();
                        queryChanged();
                    }
                    return 0;
                case VK_BACK:
                    if (!view.query.empty()) {
                        if (ctrl) view.query.clear();
                        else spotEraseLast(view.query);
                        queryChanged();
                    }
                    return 0;
                case VK_UP:
                case VK_DOWN:
                    if (count) {
                        const int next = std::clamp(view.selected + (wp == VK_DOWN ? 1 : -1), 0, count - 1);
                        if (next != view.selected) {
                            view.selected = next;
                            render();
                        }
                    }
                    return 0;
                case VK_RETURN: choose(view.selected, ctrl); return 0;
                case 'V':
                    if (ctrl) {
                        const std::wstring add = clipboardLine(hwnd);
                        if (!add.empty()) {
                            view.query += add;
                            spotClip(view.query, 128);
                            queryChanged();
                        }
                    }
                    return 0;
                default: return 0;
            }
        }
        case WM_MOUSEMOVE: {
            const auto [x, y] = toPanel(lp);
            const int r = rowAt(view.sections, x, y);
            if (r >= 0 && r != view.selected) {
                view.selected = r;
                render();
            }
            return 0;
        }
        case WM_LBUTTONDOWN: {
            pressed = true;
            const auto [x, y] = toPanel(lp);
            if (x < 0 || y < 0 || x > kPanelW || y > panelHeight(view.sections)) done = true;   // dans l'ombre
            return 0;
        }
        case WM_LBUTTONUP: {
            if (!pressed) return 0;
            pressed = false;
            const auto [x, y] = toPanel(lp);
            if (const int r = rowAt(view.sections, x, y); r >= 0) choose(r, ctrl);
            return 0;
        }
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE) done = true;   // clic ailleurs : il atteint sa cible
            return 0;
        case WM_TIMER:
            if (wp == kCaretTimer) {
                view.caret = !view.caret;
                render();
            } else if (wp == kSearchTimer) {
                KillTimer(hwnd, kSearchTimer);
                if (wantsFileSearch(view.query) && searcher) docs.asked(searcher->request(view.query, hwnd, WM_SPOT_FILES));
            }
            return 0;
        case WM_SPOT_FILES: {
            unsigned gen = 0;
            std::vector<SpotItem> found = FileSearcher::take(wp, lp, gen);
            if (docs.arrived(gen, std::move(found))) recompute(true);   // seulement la réponse à la dernière frappe
            return 0;
        }
        case WM_SPOT_ICON: {
            std::unique_ptr<IconPost> post(reinterpret_cast<IconPost*>(lp));
            images[post->key] = post->image;
            bitmaps.erase(post->key);
            render();
            return 0;
        }
        case WM_SPOT_BACKDROP:
            if (screen.take(env.device) && screen.copyTo(env.device, rc, backdrop)) render();
            return 0;
        default: break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

std::uint32_t placeholderColor(std::size_t i) {   // cases de couleur du rendu hors écran
    static const std::uint32_t colors[] = {0xFF0A84FF, 0xFF30D158, 0xFFFF9F0A, 0xFFFF375F, 0xFFBF5AF2, 0xFF64D2FF, 0xFFFFD60A};
    return colors[i % std::size(colors)];
}

} // namespace

bool SpotlightWindow::isOpen() { return g_open != nullptr; }

void SpotlightWindow::closeOpen() {
    if (!g_open) return;
    g_open->done = true;
    PostMessageW(g_open->hwnd, WM_NULL, 0, 0);   // réveille la boucle
}

std::optional<SpotlightWindow::Choice> SpotlightWindow::track(const MenuWindow::Env& env, const Request& request) {
    if (g_open) return std::nullopt;   // déjà ouvert (appel réentrant)
    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = spotProc;
    wc.hInstance = env.instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);

    Session s;
    s.env = env;
    s.req = &request;
    if (!s.init()) {
        log::warn(L"Spotlight : panneau impossible (initialisation graphique)");
        return std::nullopt;
    }
    MONITORINFO mi{sizeof mi};
    HMONITOR mon = request.monitor ? request.monitor : MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
    GetMonitorInfoW(mon, &mi);
    const RECT& m = mi.rcMonitor;
    const double monH = m.bottom - m.top;
    const double panelTop = m.top + monH * kTopRatio;   // pixels
    const double room = (m.bottom - panelTop) / s.sc - kShadow - 16;
    const double fixed = maxPanelHeight(0);
    s.maxRows = std::size_t(std::clamp((room - fixed) / kRowH, 1.0, double(kMaxRows)));
    const int w = int(std::lround((kPanelW + 2 * kShadow) * s.sc));
    const int h = int(std::lround((maxPanelHeight(s.maxRows) + 2 * kShadow) * s.sc));
    s.rc.left = (m.left + m.right) / 2 - w / 2;
    s.rc.top = int(std::lround(panelTop - kShadow * s.sc));
    s.rc.right = s.rc.left + w;
    s.rc.bottom = s.rc.top + h;
    s.iconPx = int(std::lround(kIcon * s.sc));
    if (!request.profile.empty()) s.searcher = std::make_unique<FileSearcher>(request.profile, 8);
    if (!s.createWindow()) {
        log::warn(L"Spotlight : création de la fenêtre impossible");
        if (s.hwnd) DestroyWindow(s.hwnd);
        return std::nullopt;
    }
    g_open = &s;
    struct Cleanup {
        Session& s;
        ~Cleanup() {
            g_open = nullptr;
            s.stopLoading();
            s.screen.stop();
            if (s.hwnd) {
                KillTimer(s.hwnd, kCaretTimer);
                KillTimer(s.hwnd, kSearchTimer);
                DestroyWindow(s.hwnd);
            }
        }
    } cleanup{s};

    if (s.glassReady) {
        s.screen.start(s.hwnd, WM_SPOT_BACKDROP, mon, m);
        for (const double until = now() + 0.15; !s.backdrop.valid && now() < until && !s.screen.failed();) {
            MSG msg;
            if (PeekMessageW(&msg, s.hwnd, WM_SPOT_BACKDROP, WM_SPOT_BACKDROP, PM_REMOVE)) DispatchMessageW(&msg);
            else MsgWaitForMultipleObjects(0, nullptr, FALSE, 5, QS_POSTMESSAGE);
        }
    }
    s.shownAt = now();
    if (env.trace)
        log::info(L"[trace] spotlight : %zu apps, %zu lignes au plus, verre %s", request.apps.size(), s.maxRows,
                  s.backdrop.valid ? L"réel" : L"dépoli");
    s.startLoading();
    s.updateRegion();
    s.render();
    ShowWindow(s.hwnd, SW_SHOW);
    forceForeground(s.hwnd);   // la frappe va au champ
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
    if (env.trace) log::info(L"[trace] spotlight : choix %s", s.result ? s.result->item.title.c_str() : L"(aucun)");
    return s.result;
}

BgraImage spotlightSnapshot(const std::wstring& query, const std::vector<AppEntry>& apps,
                            const std::vector<SpotItem>& files, bool dark, int width, int height) {
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
        blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, 24.0f);
        blur->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);
    }

    Painter painter;
    if (!painter.init(L"")) return out;
    View v;
    v.query = query;
    v.dark = dark;
    v.caret = true;
    v.sections = spotTrim(spotlightResults(query, apps, files), kMaxRows);
    v.selected = spotCount(v.sections) ? 0 : -1;

    const float left = std::floor((float(width) - kPanelW) / 2), top = std::floor(float(height * kTopRatio));
    const D2D1_RECT_F panel{left, top, left + kPanelW, top + panelHeight(v.sections)};
    const D2D1_ROUNDED_RECT rr{panel, kRadius, kRadius};
    Com<ID2D1RoundedRectangleGeometry> clip;
    factory->CreateRoundedRectangleGeometry(rr, &clip);

    dc->SetTarget(target.Get());
    dc->BeginDraw();
    dc->Clear(rgba(0, 0, 0, 1));
    if (wallBmp) dc->DrawBitmap(wallBmp.Get());
    Com<ID2D1SolidColorBrush> shadow, tint, border;
    dc->CreateSolidColorBrush(rgba(0, 0, 0, 0.16f), &shadow);
    if (shadow) {   // ombre douce : quelques contours élargis
        for (int k = 1; k <= 6; ++k) {
            shadow->SetOpacity(0.05f);
            const float g = float(k) * 2;
            dc->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(panel.left - g, panel.top - g + 3, panel.right + g,
                                                                   panel.bottom + g + 3),
                                                       kRadius + g, kRadius + g),
                                     shadow.Get());
        }
    }
    if (blur && clip) {   // verre dépoli : le fond flouté dans le panneau
        dc->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), clip.Get()), nullptr);
        dc->DrawImage(blur.Get());
        dc->PopLayer();
    }
    dc->CreateSolidColorBrush(dark ? rgba(0.12f, 0.12f, 0.13f, 0.62f) : rgba(1, 1, 1, 0.62f), &tint);
    if (tint) dc->FillRoundedRectangle(rr, tint.Get());
    dc->CreateSolidColorBrush(dark ? rgba(1, 1, 1, 0.14f) : rgba(0, 0, 0, 0.10f), &border);
    if (border) dc->DrawRoundedRectangle(rr, border.Get(), 1);
    dc->SetTransform(D2D1::Matrix3x2F::Translation(left, top));
    painter.paint(dc.Get(), v, [](ID2D1DeviceContext* d, const SpotItem&, std::size_t index, const D2D1_RECT_F& r) {
        Com<ID2D1SolidColorBrush> b;
        const std::uint32_t c = placeholderColor(index + 1);   // pas le bleu de la sélection en tête
        d->CreateSolidColorBrush(rgba(float((c >> 16) & 255) / 255, float((c >> 8) & 255) / 255, float(c & 255) / 255, 1), &b);
        if (b) d->FillRoundedRectangle(D2D1::RoundedRect(r, 6.5f, 6.5f), b.Get());
    });
    dc->SetTransform(D2D1::Matrix3x2F::Identity());
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
