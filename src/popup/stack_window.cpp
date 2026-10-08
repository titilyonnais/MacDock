#include "stack_window.h"

#include <d2d1_3.h>
#include <dcomp.h>
#include <dwrite_3.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>
#include <thread>

#include "../core/log.h"
#include "../geom/smooth_rect.h"
#include "../glass/glass_renderer.h"
#include "../shell/shell_actions.h"
#include "../stack/stack_layout.h"
#include "popup_glass.h"

namespace md {

namespace {

template <class T> using Com = Microsoft::WRL::ComPtr<T>;

constexpr wchar_t kClass[] = L"MacDockStack";
constexpr UINT WM_STACK_BACKDROP = WM_APP + 7;
constexpr UINT WM_STACK_ICON = WM_APP + 8;   // wParam : index ; lParam : IconProvider::ImagePtr* à reprendre
constexpr double kFanOpenSeconds = 0.21;   // les icônes jaillissent de la pile le long de l'arc
constexpr double kGridFadeSeconds = 0.12;
constexpr float kLabelFont = 13, kNameFont = 11, kTitleFont = 13;
constexpr float kLabelMaxWidth = 280;      // nom d'un élément de l'éventail (points)
constexpr float kLabelPadX = 8, kLabelHeight = 22, kLabelGap = 8;
constexpr float kPanelGap = 10;            // écart entre le Dock et le panneau de la grille
constexpr float kGridRadius = 18;
constexpr float kScrollStep = 40;          // défilement d'un cran de molette (points)

double now() {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return double(c.QuadPart) / double(f.QuadPart);
}

D2D1_COLOR_F rgba(float r, float g, float b, float a) { return D2D1::ColorF(r, g, b, a); }

double easeOut(double t) { return 1 - std::pow(1 - std::clamp(t, 0.0, 1.0), 3); }

struct Entry {
    std::wstring path, name;
    bool open = false;              // « Ouvrir dans l'Explorateur »
    bool loaded = false;
    IconProvider::ImagePtr image;
    Com<ID2D1Bitmap1> bitmap;
    Com<IDWriteTextLayout> label;   // éventail : nom sur une ligne ; grille : deux lignes centrées
    float labelW = 0;
    float cx = 0, cy = 0, angle = 0;   // éventail : centre de l'icône (fenêtre) et inclinaison
};

// Images extraites hors du thread de l'interface : une vignette peut prendre des secondes, et ce thread porte
// le hook souris bas niveau de la pile (Windows le retire s'il ne répond plus). hwnd nul = pile fermée.
struct Loader {
    std::mutex mutex;
    HWND hwnd = nullptr;
};

struct Session {
    StackWindow::Env env;
    const StackWindow::Request* req = nullptr;
    bool fan = true;
    float sc = 1;

    Com<ID2D1Factory3> d2dFactory;
    Com<ID2D1Device2> d2dDevice;
    Com<ID2D1DeviceContext2> dc;
    Com<IDCompositionDesktopDevice> dcomp;
    Com<IDCompositionTarget> target;
    Com<IDCompositionVisual2> visual;
    Com<IDCompositionSurface> surface;
    Com<IDWriteFactory3> dwrite;
    Com<IDWriteTextFormat> labelFormat, nameFormat, titleFormat, footerFormat;
    Com<IDWriteInlineObject> ellipsis;
    Com<IDWriteTextLayout> title, footer;

    GlassRenderer glass;
    bool glassReady = false;
    ScreenBackdrop screen;
    WindowBackdrop backdrop;
    GlassTarget glassTarget;

    HWND hwnd = nullptr;
    RECT rc{};                 // fenêtre (écran)
    std::vector<Entry> entries;
    std::shared_ptr<Loader> loader;
    int iconPx = 48;
    POINT origin{};            // éventail : centre de l'icône de la pile (fenêtre)

    // Grille.
    GridGeometry grid;
    float margin = 0;          // marge d'ombre autour du panneau
    D2D1_RECT_F panel{};
    float scroll = 0, maxScroll = 0;

    int hover = -1;
    double shownAt = 0;
    bool done = false;
    std::wstring result;
    UINT swallowUp = 0;        // relâchement du clic extérieur à absorber

    UINT width() const { return UINT(rc.right - rc.left); }
    UINT height() const { return UINT(rc.bottom - rc.top); }
    float pt(double v) const { return float(v) * sc; }

    bool init();
    Com<IDWriteTextFormat> makeFormat(float size, DWRITE_FONT_WEIGHT weight, bool center, bool wrap);
    void buildEntries(std::size_t fanSlots);
    void layoutFan(const RECT& mon);
    void layoutGrid(const RECT& mon);
    bool createWindow();
    void startLoading();
    void stopLoading();
    ID2D1Bitmap1* bitmapOf(Entry& e);
    double progress() const;
    void render();
    void renderFan(ID2D1DeviceContext* d);
    void renderGrid(ID2D1DeviceContext* d);
    int hitTest(POINT p) const;
    float contentTop() const { return panel.top + pt(grid.padding + grid.header); }
    float contentBottom() const { return contentTop() + pt(grid.visibleRows * grid.cellHeight); }
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);
};

thread_local Session* g_session = nullptr;

// Clic hors de la fenêtre : la pile se ferme et le clic n'atteint rien d'autre (ni l'icône du Dock dessous).
LRESULT CALLBACK outsideClickHook(int code, WPARAM wp, LPARAM lp) {
    Session* s = g_session;
    if (code == HC_ACTION && s && s->swallowUp && wp == s->swallowUp) {
        s->swallowUp = 0;
        return 1;
    }
    if (code == HC_ACTION && s && !s->done &&
        (wp == WM_LBUTTONDOWN || wp == WM_RBUTTONDOWN || wp == WM_MBUTTONDOWN || wp == WM_XBUTTONDOWN)) {
        POINT p = reinterpret_cast<MSLLHOOKSTRUCT*>(lp)->pt;
        if (!PtInRect(&s->rc, p)) {
            s->done = true;
            s->swallowUp = UINT(wp) + 1;
            PostMessageW(s->hwnd, WM_NULL, 0, 0);
            return 1;
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

LRESULT CALLBACK stackProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (auto* s = reinterpret_cast<Session*>(GetWindowLongPtrW(h, GWLP_USERDATA))) return s->handle(msg, wp, lp);
    return DefWindowProcW(h, msg, wp, lp);
}

bool Session::init() {
    sc = env.scale;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory3),
                                   reinterpret_cast<IUnknown**>(dwrite.GetAddressOf()))))
        return false;
    labelFormat = makeFormat(kLabelFont, DWRITE_FONT_WEIGHT_MEDIUM, false, false);
    nameFormat = makeFormat(kNameFont, DWRITE_FONT_WEIGHT_NORMAL, true, true);
    titleFormat = makeFormat(kTitleFont, DWRITE_FONT_WEIGHT_SEMI_BOLD, true, false);
    footerFormat = makeFormat(kNameFont + 1, DWRITE_FONT_WEIGHT_NORMAL, false, false);
    if (!labelFormat || !nameFormat || !titleFormat || !footerFormat || !env.device) return false;
    Com<IDXGIDevice> dxgi;
    D2D1_FACTORY_OPTIONS opts{};
    if (FAILED(env.device->QueryInterface(IID_PPV_ARGS(&dxgi))) ||
        FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory3), &opts,
                                 reinterpret_cast<void**>(d2dFactory.GetAddressOf()))) ||
        FAILED(d2dFactory->CreateDevice(dxgi.Get(), &d2dDevice)) ||
        FAILED(d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc)) ||
        FAILED(DCompositionCreateDevice2(d2dDevice.Get(), IID_PPV_ARGS(&dcomp))))
        return false;
    glassReady = !fan && env.glass && glass.init(env.device);
    return true;
}

Com<IDWriteTextFormat> Session::makeFormat(float size, DWRITE_FONT_WEIGHT weight, bool center, bool wrap) {
    Com<IDWriteTextFormat> f;
    const wchar_t* family = env.font.empty() ? L"Segoe UI" : env.font.c_str();
    if (FAILED(dwrite->CreateTextFormat(family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                        size * sc, L"", &f)))
        return nullptr;
    f->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_EMERGENCY_BREAK : DWRITE_WORD_WRAPPING_NO_WRAP);
    f->SetTextAlignment(center ? DWRITE_TEXT_ALIGNMENT_CENTER : DWRITE_TEXT_ALIGNMENT_LEADING);
    if (!ellipsis) dwrite->CreateEllipsisTrimmingSign(f.Get(), &ellipsis);
    DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    f->SetTrimming(&trim, ellipsis.Get());
    return f;
}

// Éléments affichés : ceux du dossier (plafonnés), puis « Ouvrir dans l'Explorateur » (en haut de l'éventail,
// en bas de la grille).
void Session::buildEntries(std::size_t fanSlots) {
    const auto& items = req->items;
    const std::size_t cap = fan ? std::max<std::size_t>(fanSlots, 1) - 1 : kGridMaxItems;
    const std::size_t n = std::min(items.size(), cap);
    for (std::size_t i = 0; i < n; ++i) entries.push_back({items[i].path, items[i].name});
    Entry open{req->folder, L"Ouvrir dans l'Explorateur"};
    open.open = true;
    if (fan && items.size() > n) open.name = std::to_wstring(items.size() - n) + L" de plus dans l'Explorateur";
    entries.push_back(std::move(open));
}

void Session::layoutFan(const RECT& mon) {
    iconPx = int(std::lround(req->tile * sc));
    auto slots = fanLayout(entries.size(), req->tile);
    const float pillH = pt(kLabelHeight);
    // Boîte englobante (écran) : icônes, noms à gauche, et la pile elle-même (point de départ de l'animation).
    float l = float(req->iconCenter.x - iconPx / 2), r = float(req->iconCenter.x + iconPx / 2);
    float t = float(req->iconCenter.y - iconPx / 2), b = float(req->iconCenter.y + iconPx / 2);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        Entry& e = entries[i];
        dwrite->CreateTextLayout(e.name.c_str(), UINT32(e.name.size()), labelFormat.Get(), pt(kLabelMaxWidth), pillH,
                                 &e.label);
        DWRITE_TEXT_METRICS tm{};
        if (e.label) e.label->GetMetrics(&tm);
        e.labelW = std::min(tm.widthIncludingTrailingWhitespace, pt(kLabelMaxWidth)) + 2 * pt(kLabelPadX);
        e.cx = float(req->iconCenter.x) + pt(slots[i].dx);
        e.cy = float(req->iconCenter.y) + pt(slots[i].dy);
        e.angle = float(slots[i].angle);
        l = std::min(l, e.cx - iconPx / 2.0f - pt(kLabelGap) - e.labelW);
        r = std::max(r, e.cx + iconPx / 2.0f);
        t = std::min(t, e.cy - std::max(float(iconPx), pillH) / 2);
    }
    const float pad = pt(24);   // inclinaison et débordements
    rc = {LONG(std::floor(l - pad)), LONG(std::floor(t - pad)), LONG(std::ceil(r + pad)), LONG(std::ceil(b + pad))};
    rc.left = std::max(rc.left, mon.left);
    rc.top = std::max(rc.top, mon.top);
    rc.right = std::min(rc.right, mon.right);
    rc.bottom = std::min(rc.bottom, mon.bottom);
    for (auto& e : entries) {
        e.cx -= float(rc.left);
        e.cy -= float(rc.top);
    }
    origin = {req->iconCenter.x - rc.left, req->iconCenter.y - rc.top};
}

void Session::layoutGrid(const RECT& mon) {
    const auto side = req->side;
    const double room = side == MenuWindow::Side::Above ? double(req->dockEdge - mon.top) / sc - 2 * kPanelGap - 20
                                                         : double(mon.bottom - mon.top) / sc - 40;
    grid = gridLayout(entries.size() - 1, req->tile, room);
    iconPx = int(std::lround(grid.iconSize * sc));
    margin = std::ceil(float(env.metrics.shadowBlur) * 1.5f * sc + 4);
    const LONG W = LONG(std::ceil(grid.width * sc)), H = LONG(std::ceil(grid.height * sc));
    const LONG gap = LONG(pt(kPanelGap)), edge = LONG(pt(8));
    LONG left, top;
    if (side == MenuWindow::Side::Right) {
        left = req->dockEdge + gap;
        top = req->iconCenter.y - H / 2;
    } else if (side == MenuWindow::Side::Left) {
        left = req->dockEdge - gap - W;
        top = req->iconCenter.y - H / 2;
    } else {
        left = req->iconCenter.x - W / 2;
        top = req->dockEdge - gap - H;
    }
    left = std::clamp(left, mon.left + edge, std::max(mon.left + edge, mon.right - edge - W));
    top = std::clamp(top, mon.top + edge, std::max(mon.top + edge, mon.bottom - edge - H));
    const LONG m = LONG(margin);
    rc = {left - m, top - m, left + W + m, top + H + m};
    panel = {margin, margin, margin + float(W), margin + float(H)};
    maxScroll = std::max(0.0f, pt((grid.rows - grid.visibleRows) * grid.cellHeight));
    const float nameW = pt(grid.cellWidth - 8), nameH = pt(kNameFont * 2.6);
    for (std::size_t i = 0; i + 1 < entries.size(); ++i)
        dwrite->CreateTextLayout(entries[i].name.c_str(), UINT32(entries[i].name.size()), nameFormat.Get(), nameW, nameH,
                                 &entries[i].label);
    dwrite->CreateTextLayout(req->title.c_str(), UINT32(req->title.size()), titleFormat.Get(),
                             panel.right - panel.left - pt(2 * grid.padding), pt(grid.header), &title);
    const std::wstring link = L"Ouvrir dans l'Explorateur  ›";
    dwrite->CreateTextLayout(link.c_str(), UINT32(link.size()), footerFormat.Get(), pt(400), pt(grid.footer), &footer);
}

bool Session::createWindow() {
    hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP, kClass, L"", WS_POPUP, rc.left,
                           rc.top, int(width()), int(height()), nullptr, nullptr, env.instance, nullptr);
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
    std::vector<std::wstring> paths;
    for (const auto& e : entries) paths.push_back(e.path);
    std::thread([l = loader, paths = std::move(paths), px = iconPx] {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        {
            IconProvider icons;   // propre à ce thread : le cache du Dock n'est pas partagé
            for (std::size_t i = 0; i < paths.size(); ++i) {
                {
                    std::lock_guard lock(l->mutex);
                    if (!l->hwnd) break;
                }
                auto* box = new IconProvider::ImagePtr(icons.file(paths[i], px));
                bool posted = false;
                {
                    std::lock_guard lock(l->mutex);
                    posted = l->hwnd && PostMessageW(l->hwnd, WM_STACK_ICON, i, reinterpret_cast<LPARAM>(box));
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
    while (hwnd && PeekMessageW(&msg, hwnd, WM_STACK_ICON, WM_STACK_ICON, PM_REMOVE))
        delete reinterpret_cast<IconProvider::ImagePtr*>(msg.lParam);
}


ID2D1Bitmap1* Session::bitmapOf(Entry& e) {
    if (e.bitmap || !e.image) return e.bitmap.Get();
    auto props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,
                                         D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    dc->CreateBitmap(D2D1::SizeU(UINT32(e.image->size), UINT32(e.image->size)), e.image->bgra.data(),
                     UINT32(e.image->size * 4), &props, &e.bitmap);
    return e.bitmap.Get();
}

double Session::progress() const {
    return std::clamp((now() - shownAt) / (fan ? kFanOpenSeconds : kGridFadeSeconds), 0.0, 1.0);
}

void Session::render() {
    bool glassDrawn = false;
    const float opacity = fan ? 1.0f : float(easeOut(progress()));
    const float radius = fan ? 0 : float(limitedCornerRadius(panel.right - panel.left, panel.bottom - panel.top, pt(kGridRadius)));
    if (!fan && glassReady && backdrop.valid && glassTarget.ensure(env.device, dc.Get(), width(), height())) {
        const Metrics& m = env.metrics;
        GlassParams gp = popupGlassParams(m, env.dark, sc, PopupMaterial::Menu);   // matériau Golden Gate
        gp.shadowBlurPx = float(m.shadowBlur) * sc;
        gp.shadowOffsetPx = 3 * sc;
        gp.backdropIsScRgb = backdrop.scRgb;
        gp.sdrWhiteScale = backdrop.white;
        GlassShape shape{panel.left, panel.top, panel.right, panel.bottom, radius, 0.7f,
                         float(m.shadowOpacity * (env.dark ? 2.0 : 1.4)), opacity};
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
    if (fan) {
        renderFan(d.Get());
    } else {
        D2D1_ROUNDED_RECT rr{panel, radius, radius};
        if (glassDrawn) {
            d->DrawBitmap(glassTarget.bitmap.Get());
        } else {
            Com<ID2D1SolidColorBrush> bg;
            d->CreateSolidColorBrush(env.dark ? rgba(0.15f, 0.15f, 0.16f, 0.94f * opacity)
                                              : rgba(0.96f, 0.96f, 0.97f, 0.94f * opacity),
                                     &bg);
            d->FillRoundedRectangle(rr, bg.Get());
        }
        Com<ID2D1SolidColorBrush> border;
        d->CreateSolidColorBrush(env.dark ? rgba(1, 1, 1, 0.14f * opacity) : rgba(0, 0, 0, 0.10f * opacity), &border);
        d->DrawRoundedRectangle(rr, border.Get(), std::max(1.0f, sc * 0.5f));
        renderGrid(d.Get());
    }
    surface->EndDraw();
    dcomp->Commit();
}

void Session::renderFan(ID2D1DeviceContext* d) {
    const float p = float(easeOut(progress()));
    const float labelAlpha = std::clamp((p - 0.55f) / 0.45f, 0.0f, 1.0f);   // les noms arrivent en fin de course
    Com<ID2D1SolidColorBrush> pill, pillHot, text;
    d->CreateSolidColorBrush(rgba(0.16f, 0.16f, 0.18f, 0.82f * labelAlpha), &pill);
    d->CreateSolidColorBrush(env.dark ? rgba(0.04f, 0.52f, 1.0f, labelAlpha) : rgba(0.0f, 0.48f, 1.0f, labelAlpha), &pillHot);
    d->CreateSolidColorBrush(rgba(1, 1, 1, labelAlpha), &text);
    D2D1_MATRIX_3X2_F base;
    d->GetTransform(&base);
    const float pillH = pt(kLabelHeight);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        Entry& e = entries[i];
        const float x = float(origin.x) + (e.cx - float(origin.x)) * p;
        const float y = float(origin.y) + (e.cy - float(origin.y)) * p;
        const float size = float(iconPx) * (0.45f + 0.55f * p);
        d->SetTransform(D2D1::Matrix3x2F::Rotation(e.angle * p, D2D1::Point2F(x, y)) * base);
        if (ID2D1Bitmap1* bmp = bitmapOf(e))
            d->DrawBitmap(bmp, D2D1::RectF(x - size / 2, y - size / 2, x + size / 2, y + size / 2), std::min(1.0f, p * 1.6f),
                          D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
        if (labelAlpha > 0 && e.label) {
            const float right = x - float(iconPx) / 2 - pt(kLabelGap);
            D2D1_RECT_F r{right - e.labelW, y - pillH / 2, right, y + pillH / 2};
            d->FillRoundedRectangle(D2D1::RoundedRect(r, pillH / 2, pillH / 2), int(i) == hover ? pillHot.Get() : pill.Get());
            DWRITE_TEXT_METRICS tm{};
            e.label->GetMetrics(&tm);
            d->DrawTextLayout({r.left + pt(kLabelPadX), y - tm.height / 2}, e.label.Get(), text.Get(),
                              D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
    }
    d->SetTransform(base);
}

void Session::renderGrid(ID2D1DeviceContext* d) {
    const float opacity = float(easeOut(progress()));
    Com<ID2D1SolidColorBrush> ink, hot, accent;
    d->CreateSolidColorBrush(env.dark ? rgba(1, 1, 1, 0.92f * opacity) : rgba(0, 0, 0, 0.86f * opacity), &ink);
    d->CreateSolidColorBrush(env.dark ? rgba(1, 1, 1, 0.14f * opacity) : rgba(0, 0, 0, 0.09f * opacity), &hot);
    d->CreateSolidColorBrush(env.dark ? rgba(0.04f, 0.52f, 1.0f, opacity) : rgba(0.0f, 0.48f, 1.0f, opacity), &accent);
    const float pad = pt(grid.padding);
    if (title) {
        DWRITE_TEXT_METRICS tm{};
        title->GetMetrics(&tm);
        d->DrawTextLayout({panel.left + pad, panel.top + pad + (pt(grid.header) - tm.height) / 2 - pt(4)}, title.Get(),
                          ink.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    // Cellules, découpées à la zone de contenu (défilement).
    d->PushAxisAlignedClip(D2D1::RectF(panel.left, contentTop(), panel.right, contentBottom()), D2D1_ANTIALIAS_MODE_ALIASED);
    const int cols = std::max(1, grid.columns);
    const float cw = pt(grid.cellWidth), ch = pt(grid.cellHeight), icon = pt(grid.iconSize);
    for (std::size_t i = 0; i + 1 < entries.size(); ++i) {
        Entry& e = entries[i];
        const float x0 = panel.left + pad + float(int(i) % cols) * cw;
        const float y0 = contentTop() + float(int(i) / cols) * ch - scroll;
        if (y0 + ch < contentTop() || y0 > contentBottom()) continue;
        if (int(i) == hover)
            d->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x0 + pt(2), y0, x0 + cw - pt(2), y0 + ch - pt(4)), pt(8), pt(8)),
                                    hot.Get());
        const float ix = x0 + (cw - icon) / 2, iy = y0 + pt(6);
        if (ID2D1Bitmap1* bmp = bitmapOf(e))
            d->DrawBitmap(bmp, D2D1::RectF(ix, iy, ix + icon, iy + icon), opacity, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
        if (e.label) d->DrawTextLayout({x0 + pt(4), iy + icon + pt(4)}, e.label.Get(), ink.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    d->PopAxisAlignedClip();
    if (footer) {
        DWRITE_TEXT_METRICS tm{};
        footer->GetMetrics(&tm);
        const float fy = contentBottom() + (panel.bottom - pad - contentBottom() - tm.height) / 2;
        const bool footerHot = hover == int(entries.size()) - 1;
        d->DrawTextLayout({panel.right - pad - tm.width - pt(4), fy}, footer.Get(), footerHot ? accent.Get() : ink.Get());
    }
}

int Session::hitTest(POINT p) const {
    const float mx = float(p.x), my = float(p.y);
    if (fan) {
        if (progress() < 0.6) return -1;
        const float pillH = pt(kLabelHeight), half = std::max(float(iconPx), pillH) / 2;
        for (std::size_t i = entries.size(); i-- > 0;) {   // du haut vers le bas : l'élément dessiné au-dessus d'abord
            const Entry& e = entries[i];
            // Point ramené dans le repère non incliné de l'élément.
            const float a = -e.angle * 3.14159265f / 180, dx = mx - e.cx, dy = my - e.cy;
            const float lx = e.cx + dx * std::cos(a) - dy * std::sin(a), ly = e.cy + dx * std::sin(a) + dy * std::cos(a);
            const float left = e.cx - float(iconPx) / 2 - pt(kLabelGap) - e.labelW, right = e.cx + float(iconPx) / 2;
            if (lx >= left && lx <= right && ly >= e.cy - half && ly <= e.cy + half) return int(i);
        }
        return -1;
    }
    const float pad = pt(grid.padding);
    if (mx < panel.left + pad || mx >= panel.right - pad) return -1;
    if (my >= contentTop() && my < contentBottom()) {
        const int col = int((mx - panel.left - pad) / pt(grid.cellWidth));
        const int row = int((my - contentTop() + scroll) / pt(grid.cellHeight));
        const int i = row * std::max(1, grid.columns) + col;
        if (col >= 0 && col < grid.columns && i >= 0 && i + 1 < int(entries.size())) return i;
        return -1;
    }
    if (my >= contentBottom() && my < panel.bottom - pad / 2 && footer) {
        DWRITE_TEXT_METRICS tm{};
        footer->GetMetrics(&tm);
        if (mx >= panel.right - pad - tm.width - pt(12)) return int(entries.size()) - 1;
    }
    return -1;
}

LRESULT Session::handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT tme{sizeof tme, TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            int h = hitTest({short(LOWORD(lp)), short(HIWORD(lp))});
            if (h != hover) {
                hover = h;
                render();
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            if (hover != -1) {
                hover = -1;
                render();
            }
            return 0;
        case WM_MOUSEWHEEL:
            if (!fan && maxScroll > 0) {
                scroll = std::clamp(scroll - float(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA * pt(kScrollStep), 0.0f, maxScroll);
                POINT c{short(LOWORD(lp)), short(HIWORD(lp))};
                ScreenToClient(hwnd, &c);
                hover = hitTest(c);
                render();
            }
            return 0;
        case WM_LBUTTONUP: {
            int h = hitTest({short(LOWORD(lp)), short(HIWORD(lp))});
            if (h >= 0) result = entries[std::size_t(h)].path;
            done = true;   // un clic dans le vide ferme la pile, comme sur macOS
            return 0;
        }
        case WM_RBUTTONUP: done = true; return 0;
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) done = true;
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE) done = true;
            return 0;
        case WM_STACK_ICON: {
            std::unique_ptr<IconProvider::ImagePtr> box(reinterpret_cast<IconProvider::ImagePtr*>(lp));
            if (wp < entries.size()) {
                entries[wp].image = *box;
                entries[wp].loaded = true;
                entries[wp].bitmap.Reset();
                render();
            }
            return 0;
        }
        case WM_STACK_BACKDROP:
            if (screen.take(env.device) && screen.copyTo(env.device, rc, backdrop)) render();
            return 0;
        default: break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

std::wstring StackWindow::track(const Env& env, const Request& request) {
    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = stackProc;
    wc.hInstance = env.instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);

    Session s;
    s.env = env;
    s.req = &request;
    s.fan = request.view != StackView::Grid;
    if (!s.init()) {
        log::warn(L"Pile : initialisation graphique impossible ; ouverture dans l'Explorateur");
        return request.folder;
    }
    HMONITOR mon = MonitorFromPoint(request.iconCenter, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(mon, &mi);
    // Éventail : seulement les emplacements qui tiennent sous le haut de l'écran (« N de plus » compris).
    const double room = double(request.iconCenter.y - mi.rcMonitor.top) / env.scale - 24;
    s.buildEntries(fanCapacity(request.tile, room));
    if (s.fan) s.layoutFan(mi.rcMonitor);
    else s.layoutGrid(mi.rcMonitor);
    if (!s.createWindow()) {
        log::warn(L"Pile : création de la fenêtre impossible ; ouverture dans l'Explorateur");
        if (s.hwnd) DestroyWindow(s.hwnd);
        s.hwnd = nullptr;
        return request.folder;
    }

    g_session = &s;
    HHOOK hook = SetWindowsHookExW(WH_MOUSE_LL, outsideClickHook, env.instance, 0);
    struct Cleanup {
        HHOOK h;
        Session& s;
        ~Cleanup() {
            if (h) UnhookWindowsHookEx(h);
            g_session = nullptr;
            s.stopLoading();
            s.screen.stop();
            if (s.hwnd) DestroyWindow(s.hwnd);
        }
    } cleanup{hook, s};

    if (s.glassReady) {
        s.screen.start(s.hwnd, WM_STACK_BACKDROP, mon, mi.rcMonitor);
        // Attendre brièvement la première image : la grille apparaît directement en verre.
        for (const double until = now() + 0.15; !s.backdrop.valid && now() < until && !s.screen.failed();) {
            MSG msg;
            if (PeekMessageW(&msg, s.hwnd, WM_STACK_BACKDROP, WM_STACK_BACKDROP, PM_REMOVE)) DispatchMessageW(&msg);
            else MsgWaitForMultipleObjects(0, nullptr, FALSE, 5, QS_POSTMESSAGE);
        }
    }
    s.shownAt = now();
    if (env.trace) {
        log::info(L"[trace] pile : %s, %zu éléments, fenêtre %ld,%ld-%ld,%ld, verre %s", s.fan ? L"éventail" : L"grille",
                  request.items.size(), s.rc.left, s.rc.top, s.rc.right, s.rc.bottom,
                  s.fan ? L"sans" : (s.backdrop.valid ? L"réel" : L"dépoli"));
        for (std::size_t i = 0; i < s.entries.size(); ++i) {
            LONG x, y;
            if (s.fan) {
                x = s.rc.left + LONG(s.entries[i].cx);
                y = s.rc.top + LONG(s.entries[i].cy);
            } else if (i + 1 < s.entries.size()) {
                const int cols = std::max(1, s.grid.columns);
                x = s.rc.left + LONG(s.panel.left + s.pt(s.grid.padding + (int(i) % cols + 0.5) * s.grid.cellWidth));
                y = s.rc.top + LONG(s.contentTop() + s.pt((int(i) / cols + 0.5) * s.grid.cellHeight));
            } else {
                x = s.rc.left + LONG(s.panel.right - s.pt(s.grid.padding + 60));
                y = s.rc.top + LONG((s.contentBottom() + s.panel.bottom) / 2);
            }
            log::info(L"[trace] pile : élément %zu (%s) x=%ld y=%ld", i, s.entries[i].name.c_str(), x, y);
        }
    }
    s.startLoading();
    s.render();
    ShowWindow(s.hwnd, SW_SHOW);
    forceForeground(s.hwnd);   // Échap et la molette vont à la pile
    SetFocus(s.hwnd);

    // Boucle modale : animation d'ouverture et entrées (les images arrivent par WM_STACK_ICON).
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
    // Le hook reste posé jusqu'au relâchement du clic extérieur (au plus 2 s), pour l'absorber aussi.
    for (const double until = now() + 2; s.swallowUp && now() < until;) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                PostQuitMessage(int(msg.wParam));
                s.swallowUp = 0;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
    }
    if (env.trace) log::info(L"[trace] pile : choix %s", s.result.empty() ? L"(aucun)" : s.result.c_str());
    return s.result;
}

} // namespace md
