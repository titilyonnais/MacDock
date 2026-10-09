#include "mission_view.h"

#include <d2d1_3.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dwmapi.h>
#include <dwrite_3.h>
#include <dxgi1_2.h>
#include <shellscalingapi.h>
#include <shobjidl.h>
#include <wrl/client.h>

#pragma comment(lib, "shcore.lib")   // GetDpiForMonitor (aussi dans la cible des tests)

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>

#include "../anim/tahoe_timing.h"
#include "../calib/png_io.h"
#include "../core/log.h"
#include "../shell/shell_actions.h"
#include "../theme/wallpaper_art.h"

namespace md {

namespace {

template <class T> using Com = Microsoft::WRL::ComPtr<T>;

constexpr wchar_t kClass[] = L"MacDockMission";
constexpr double kAnimSeconds = tahoe::kMissionSeconds, kSlow = 5;   // Maj enfoncée : au ralenti, comme le génie
constexpr float kVeil = 0.22f, kBorder = 3, kBorderGap = 4, kRadius = 10, kTitleFont = 13;

double now() {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return double(c.QuadPart) / double(f.QuadPart);
}

D2D1_COLOR_F rgba(float r, float g, float b, float a) { return D2D1::ColorF(r, g, b, a); }

D2D1_RECT_F toD2D(const MissionRect& r) { return {float(r.x), float(r.y), float(r.x + r.w), float(r.y + r.h)}; }

// Contour bleu autour de la fenêtre survolée et pastille de son titre dessous (pixels ; sc : échelle de l'écran).
void drawHover(ID2D1DeviceContext* d, IDWriteFactory* dw, const MissionRect& r, const std::wstring& title, float sc) {
    Com<ID2D1SolidColorBrush> blue, pill, white;
    d->CreateSolidColorBrush(rgba(0x0A / 255.0f, 0x84 / 255.0f, 1.0f, 1), &blue);
    d->CreateSolidColorBrush(rgba(0, 0, 0, 0.55f), &pill);
    d->CreateSolidColorBrush(rgba(1, 1, 1, 0.96f), &white);
    if (!blue || !pill || !white) return;
    const float g = (kBorderGap + kBorder / 2) * sc;
    const D2D1_RECT_F b{float(r.x) - g, float(r.y) - g, float(r.x + r.w) + g, float(r.y + r.h) + g};
    d->DrawRoundedRectangle(D2D1::RoundedRect(b, kRadius * sc, kRadius * sc), blue.Get(), kBorder * sc);
    if (title.empty() || !dw) return;
    Com<IDWriteTextFormat> fmt;
    if (FAILED(dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
                                    DWRITE_FONT_STRETCH_NORMAL, kTitleFont * sc, L"", &fmt)))
        return;
    fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    Com<IDWriteInlineObject> ellipsis;
    dw->CreateEllipsisTrimmingSign(fmt.Get(), &ellipsis);
    DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    fmt->SetTrimming(&trim, ellipsis.Get());
    const float maxW = std::max(40 * sc, std::min(float(r.w) + 80 * sc, 600 * sc));
    Com<IDWriteTextLayout> l;
    if (FAILED(dw->CreateTextLayout(title.c_str(), UINT32(title.size()), fmt.Get(), maxW, 40 * sc, &l))) return;
    DWRITE_TEXT_METRICS tm{};
    l->GetMetrics(&tm);
    const float pw = std::min(tm.width, maxW) + 20 * sc, ph = tm.height + 8 * sc;
    const float cx = float(r.x + r.w / 2), top = b.bottom + 8 * sc;
    const D2D1_RECT_F pr{cx - pw / 2, top, cx + pw / 2, top + ph};
    d->FillRoundedRectangle(D2D1::RoundedRect(pr, ph / 2, ph / 2), pill.Get());
    d->DrawTextLayout({pr.left + 10 * sc, pr.top + 4 * sc}, l.Get(), white.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

// Fond d'écran d'un écran (IDesktopWallpaper) ; vide : couleur unie, diaporama ou erreur.
std::wstring wallpaperPath(const RECT& monitor) {
    Com<IDesktopWallpaper> dw;
    if (FAILED(CoCreateInstance(__uuidof(DesktopWallpaper), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dw)))) return {};
    UINT count = 0;
    if (FAILED(dw->GetMonitorDevicePathCount(&count))) return {};
    for (UINT i = 0; i < count; ++i) {
        LPWSTR id = nullptr;
        if (FAILED(dw->GetMonitorDevicePathAt(i, &id)) || !id) continue;
        RECT r{};
        std::wstring path;
        if (SUCCEEDED(dw->GetMonitorRECT(id, &r)) && EqualRect(&r, &monitor)) {
            LPWSTR p = nullptr;
            if (SUCCEEDED(dw->GetWallpaper(id, &p)) && p) {
                path = p;
                CoTaskMemFree(p);
            }
        }
        CoTaskMemFree(id);
        if (!path.empty()) return path;
    }
    return {};
}

// Fonds mis à la taille de l'écran, gardés d'une ouverture à l'autre (un JPEG 4K se décode en dizaines de
// millisecondes) ; jamais en pleine résolution.
std::shared_ptr<const BgraImage> cachedWallpaper(const std::wstring& path, int w, int h) {
    static std::map<std::wstring, std::shared_ptr<const BgraImage>> cache;
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (path.empty() || !GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa)) return nullptr;
    const std::wstring key = path + L"|" + std::to_wstring(fa.ftLastWriteTime.dwLowDateTime) + L"|" +
                             std::to_wstring(fa.ftLastWriteTime.dwHighDateTime) + L"|" + std::to_wstring(w) + L"x" +
                             std::to_wstring(h);
    if (auto it = cache.find(key); it != cache.end()) return it->second;
    auto im = std::make_shared<BgraImage>(wallpaperCover(path, w, h));
    if (im->px.empty()) return nullptr;
    if (cache.size() >= 4) cache.clear();
    cache[key] = im;
    return im;
}

struct Thumb {
    HWND src = nullptr;
    std::wstring title;
    HTHUMBNAIL id = nullptr;
    std::size_t screen = 0;
    MissionRect from, to;   // pixels de la vue de son écran
    bool alive = true;
    bool minimized = false;   // Exposé d'une app : rangée du bas
};

struct Screen {
    HMONITOR mon = nullptr;
    RECT rc{}, work{};
    float sc = 1;
    HWND hwnd = nullptr;
    Com<IDCompositionTarget> target;
    Com<IDCompositionVisual2> visual;
    Com<IDCompositionSurface> surface;
    Com<ID2D1Bitmap1> wall;
    D2D1_RECT_F wallSrc{};
    int hover = -1;   // indice dans Session::thumbs
    double shelfLine = -1;   // trait au-dessus des fenêtres réduites (pixels de la vue) ; -1 : aucun
    double shelfX0 = 0, shelfX1 = 0;   // étendue du trait : la zone de rangement (le Dock sur le côté la rétrécit)
    UINT w() const { return UINT(rc.right - rc.left); }
    UINT h() const { return UINT(rc.bottom - rc.top); }
};

struct Session;
Session* g_open = nullptr;

struct Session {
    MenuWindow::Env env;
    Com<ID2D1Factory3> factory;
    Com<ID2D1Device2> d2d;
    Com<ID2D1DeviceContext2> dc;
    Com<IDCompositionDesktopDevice> dcomp;
    Com<IDWriteFactory3> dwrite;
    std::vector<Screen> screens;
    std::vector<Thumb> thumbs;
    double q = 0;   // 0 : places réelles ; 1 : places rangées
    int dir = 1;    // 1 : ouverture ; -1 : fermeture
    double last = 0;
    bool done = false, pressed = false;
    std::optional<HWND> chosen;

    bool init();
    bool addScreen(HMONITOR mon);
    void place();
    bool animating() const { return (dir > 0 && q < 1) || (dir < 0 && q > 0); }
    void tick();
    void render(Screen& s);
    void renderAll();
    void close(std::optional<HWND> choice);
    int hitAt(std::size_t screen, double x, double y) const;
    bool isOurs(HWND h) const {
        for (const Screen& s : screens)
            if (s.hwnd == h) return true;
        return false;
    }
    LRESULT handle(std::size_t screen, UINT msg, WPARAM wp, LPARAM lp);
};

LRESULT CALLBACK missionProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (auto* s = reinterpret_cast<Session*>(GetWindowLongPtrW(h, GWLP_USERDATA)))
        for (std::size_t i = 0; i < s->screens.size(); ++i)
            if (s->screens[i].hwnd == h) return s->handle(i, msg, wp, lp);
    return DefWindowProcW(h, msg, wp, lp);
}

bool Session::init() {
    if (!env.device) return false;
    Com<IDXGIDevice> dxgi;
    D2D1_FACTORY_OPTIONS opts{};
    if (FAILED(env.device->QueryInterface(IID_PPV_ARGS(&dxgi))) ||
        FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory3), &opts,
                                 reinterpret_cast<void**>(factory.GetAddressOf()))) ||
        FAILED(factory->CreateDevice(dxgi.Get(), &d2d)) ||
        FAILED(d2d->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc)) ||
        FAILED(DCompositionCreateDevice2(d2d.Get(), IID_PPV_ARGS(&dcomp))))
        return false;
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory3), reinterpret_cast<IUnknown**>(dwrite.GetAddressOf()));
    return true;
}

bool Session::addScreen(HMONITOR mon) {
    Screen s;
    s.mon = mon;
    MONITORINFO mi{sizeof mi};
    if (!GetMonitorInfoW(mon, &mi)) return false;
    s.rc = mi.rcMonitor;
    s.work = mi.rcWork;
    UINT dx = 96, dy = 96;
    if (SUCCEEDED(GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dx, &dy))) s.sc = float(dx) / 96.0f;
    s.hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP, kClass, L"Mission Control",
                             WS_POPUP, s.rc.left, s.rc.top, int(s.w()), int(s.h()), nullptr, nullptr, env.instance, nullptr);
    if (!s.hwnd) return false;
    SetWindowLongPtrW(s.hwnd, GWLP_USERDATA, LONG_PTR(this));
    if (FAILED(dcomp->CreateTargetForHwnd(s.hwnd, TRUE, &s.target)) || FAILED(dcomp->CreateVisual(&s.visual)) ||
        FAILED(dcomp->CreateSurface(s.w(), s.h(), DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &s.surface))) {
        DestroyWindow(s.hwnd);
        return false;
    }
    s.visual->SetContent(s.surface.Get());
    s.target->SetRoot(s.visual.Get());

    // Fond : le fond d'écran de cet écran, « remplir » ; fond Tahoe sinon.
    const auto props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,
                                               D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    const std::wstring path = wallpaperPath(s.rc);
    if (auto wp = cachedWallpaper(path, int(s.w()), int(s.h())))
        dc->CreateBitmap(D2D1::SizeU(s.w(), s.h()), wp->px.data(), s.w() * 4, &props, &s.wall);
    if (!s.wall) {
        if (env.trace) log::info(L"[trace] mission : fond de repli (%s)", path.empty() ? L"pas de fichier" : path.c_str());
        const BgraImage t = macWallpaper(int(s.w()), int(s.h()), env.dark);
        dc->CreateBitmap(D2D1::SizeU(s.w(), s.h()), t.px.data(), s.w() * 4, &props, &s.wall);
    }
    s.wallSrc = {0, 0, float(s.w()), float(s.h())};
    screens.push_back(std::move(s));
    return true;
}

void Session::place() {
    for (std::size_t k = 0; k < screens.size(); ++k) {
        const Screen& s = screens[k];
        std::vector<std::size_t> mine, shelfOf;
        std::vector<MissionRect> wins, shelfWins;
        for (std::size_t i = 0; i < thumbs.size(); ++i) {
            if (thumbs[i].screen != k) continue;
            (thumbs[i].minimized ? shelfOf : mine).push_back(i);
            (thumbs[i].minimized ? shelfWins : wins).push_back(thumbs[i].from);
        }
        const MissionRect work{double(s.work.left - s.rc.left), double(s.work.top - s.rc.top),
                               double(s.work.right - s.work.left), double(s.work.bottom - s.work.top)};
        // Fenêtres réduites en rangée au bas de l'écran, sous un trait ; les ouvertes se rangent au-dessus.
        const MissionRect area = missionArea(work, s.sc);
        const MissionShelf shelf = missionShelf(shelfWins, area, kMissionGap * s.sc, kMissionLabelRoom * s.sc);
        screens[k].shelfLine = shelfOf.empty() ? -1 : shelf.lineY;
        screens[k].shelfX0 = area.x;
        screens[k].shelfX1 = area.x + area.w;
        const auto rects = missionLayout(wins, shelf.above, kMissionGap * s.sc, (kMissionGap + kMissionLabelRoom) * s.sc);
        for (std::size_t j = 0; j < mine.size(); ++j) thumbs[mine[j]].to = rects[j];
        for (std::size_t j = 0; j < shelfOf.size(); ++j) {   // elles montent du bas de l'écran
            Thumb& th = thumbs[shelfOf[j]];
            th.to = shelf.rects[j];
            th.from = {th.to.x, double(s.h()) + 8 * s.sc, th.to.w, th.to.h};
        }
    }
}

void Session::tick() {
    const double t = now(), dt = t - last;
    last = t;
    const double speed = (GetKeyState(VK_SHIFT) < 0 ? 1 / kSlow : 1.0) / kAnimSeconds;
    q = std::clamp(q + dir * dt * speed, 0.0, 1.0);
    if (dir < 0 && q <= 0) done = true;
    const double e = easeOut(q);
    for (std::size_t i = 0; i < thumbs.size(); ++i) {
        Thumb& th = thumbs[i];
        if (!th.alive) continue;
        if (!IsWindow(th.src)) {   // fermée pendant la vue : sa miniature disparaît
            DwmUnregisterThumbnail(th.id);
            th.id = nullptr;
            th.alive = false;
            Screen& s = screens[th.screen];
            if (s.hover == int(i)) {
                s.hover = -1;
                render(s);
                dcomp->Commit();
            }
            continue;
        }
        const MissionRect r = lerpRect(th.from, th.to, e);
        DWM_THUMBNAIL_PROPERTIES p{};
        p.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE | DWM_TNP_OPACITY | DWM_TNP_SOURCECLIENTAREAONLY;
        p.rcDestination = {LONG(std::lround(r.x)), LONG(std::lround(r.y)), LONG(std::lround(r.x + r.w)),
                           LONG(std::lround(r.y + r.h))};
        p.fVisible = TRUE;
        p.opacity = 255;
        p.fSourceClientAreaOnly = FALSE;
        DwmUpdateThumbnailProperties(th.id, &p);
    }
}

void Session::render(Screen& s) {
    POINT offset{};
    Com<ID2D1DeviceContext> d;
    if (FAILED(s.surface->BeginDraw(nullptr, IID_PPV_ARGS(&d), &offset))) return;
    d->SetDpi(96, 96);
    d->SetTransform(D2D1::Matrix3x2F::Translation(float(offset.x), float(offset.y)));
    d->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    d->Clear(rgba(0, 0, 0, 1));
    const D2D1_RECT_F all{0, 0, float(s.w()), float(s.h())};
    if (s.wall) d->DrawBitmap(s.wall.Get(), all, 1, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, &s.wallSrc);
    Com<ID2D1SolidColorBrush> veil;
    d->CreateSolidColorBrush(rgba(0, 0, 0, kVeil * float(easeOut(q))), &veil);
    if (veil) d->FillRectangle(all, veil.Get());
    if (s.shelfLine >= 0) {   // trait de séparation des fenêtres réduites
        Com<ID2D1SolidColorBrush> line;
        d->CreateSolidColorBrush(rgba(1, 1, 1, 0.28f * float(easeOut(q))), &line);
        if (line)
            d->FillRectangle(D2D1::RectF(float(s.shelfX0), float(s.shelfLine), float(s.shelfX1), float(s.shelfLine) + std::max(1.f, s.sc)),
                             line.Get());
    }
    if (s.hover >= 0 && dir > 0 && q >= 1) {
        const Thumb& th = thumbs[std::size_t(s.hover)];
        drawHover(d.Get(), dwrite.Get(), th.to, th.title, s.sc);
    }
    s.surface->EndDraw();
}

void Session::renderAll() {
    for (Screen& s : screens) render(s);
    dcomp->Commit();
}

void Session::close(std::optional<HWND> choice) {
    if (dir < 0) return;   // déjà en train de se fermer
    chosen = choice;
    dir = -1;
    last = now();
    for (Screen& s : screens) s.hover = -1;
}

int Session::hitAt(std::size_t screen, double x, double y) const {
    for (std::size_t i = 0; i < thumbs.size(); ++i) {
        const Thumb& th = thumbs[i];
        const MissionRect& r = th.to;
        if (th.alive && th.screen == screen && x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) return int(i);
    }
    return -1;
}

LRESULT Session::handle(std::size_t k, UINT msg, WPARAM wp, LPARAM lp) {
    Screen& s = screens[k];
    const double x = short(LOWORD(lp)), y = short(HIWORD(lp));
    switch (msg) {
        case WM_MOUSEMOVE: {
            const int h = dir > 0 && q >= 1 ? hitAt(k, x, y) : -1;
            if (h != s.hover) {
                s.hover = h;
                render(s);
                dcomp->Commit();
            }
            return 0;
        }
        case WM_LBUTTONDOWN: pressed = true; return 0;
        case WM_LBUTTONUP: {
            if (!pressed) return 0;
            pressed = false;
            const int h = hitAt(k, x, y);
            close(h >= 0 ? std::optional<HWND>(thumbs[std::size_t(h)].src) : std::nullopt);   // le vide : sans choix
            return 0;
        }
        case WM_RBUTTONUP: close(std::nullopt); return 0;
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) close(std::nullopt);
            else if (wp == VK_RETURN && s.hover >= 0) close(thumbs[std::size_t(s.hover)].src);
            return 0;
        case WM_ACTIVATE:
            // Une autre app passe devant : fermeture immédiate. Passer d'une de nos vues à l'autre ne ferme pas.
            if (LOWORD(wp) == WA_INACTIVE && !isOurs(reinterpret_cast<HWND>(lp))) {
                chosen.reset();
                done = true;
            }
            return 0;
        default: break;
    }
    return DefWindowProcW(s.hwnd, msg, wp, lp);
}

BOOL CALLBACK collectMonitor(HMONITOR mon, HDC, LPRECT, LPARAM lp) {
    reinterpret_cast<std::vector<HMONITOR>*>(lp)->push_back(mon);
    return TRUE;
}

BOOL CALLBACK collectZ(HWND h, LPARAM lp) {
    auto* z = reinterpret_cast<std::map<HWND, std::size_t>*>(lp);
    z->emplace(h, z->size());
    return TRUE;
}

std::uint32_t placeholderColor(std::size_t i) {   // rectangles du rendu hors écran
    static const std::uint32_t colors[] = {0xFF0A84FF, 0xFF30D158, 0xFFFF9F0A, 0xFFFF375F, 0xFFBF5AF2, 0xFF64D2FF, 0xFFFFD60A};
    return colors[i % std::size(colors)];
}

} // namespace

BgraImage wallpaperCover(const std::wstring& path, int width, int height) {
    BgraImage out{width, height, {}};
    UINT sw = 0, sh = 0;
    if (width <= 0 || height <= 0) return out;
    const std::vector<std::uint8_t> src = readPng(path, sw, sh);
    if (src.empty()) return out;
    const double k = std::max(double(width) / sw, double(height) / sh);   // « remplir » : recadré au centre
    const UINT cw = std::clamp(UINT(std::lround(width / k)), 1u, sw), ch = std::clamp(UINT(std::lround(height / k)), 1u, sh);
    const UINT cx = (sw - cw) / 2, cy = (sh - ch) / 2;
    std::vector<std::uint8_t> crop(std::size_t(cw) * ch * 4);
    for (UINT y = 0; y < ch; ++y)
        std::copy_n(&src[(std::size_t(cy + y) * sw + cx) * 4], std::size_t(cw) * 4, &crop[std::size_t(y) * cw * 4]);
    out.px = resizeBgra(crop, cw, ch, UINT(width), UINT(height));
    return out;
}

bool MissionView::isOpen() { return g_open != nullptr; }

void MissionView::closeOpen() {
    if (!g_open) return;
    g_open->close(std::nullopt);
    if (!g_open->screens.empty()) PostMessageW(g_open->screens.front().hwnd, WM_NULL, 0, 0);   // réveille la boucle
}

std::optional<HWND> MissionView::track(const MenuWindow::Env& env, const Request& request) {
    if (g_open) return std::nullopt;
    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = missionProc;
    wc.hInstance = env.instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);

    Session s;
    s.env = env;
    if (!s.init()) {
        log::warn(L"Mission Control : vue impossible (initialisation graphique)");
        return std::nullopt;
    }
    struct Cleanup {
        Session& s;
        ~Cleanup() {
            g_open = nullptr;
            for (Thumb& th : s.thumbs)
                if (th.id) DwmUnregisterThumbnail(th.id);
            for (Screen& sc : s.screens)
                if (sc.hwnd) DestroyWindow(sc.hwnd);
        }
    } cleanup{s};
    std::vector<HMONITOR> mons;
    EnumDisplayMonitors(nullptr, nullptr, collectMonitor, reinterpret_cast<LPARAM>(&mons));
    for (HMONITOR m : mons) s.addScreen(m);
    if (s.screens.empty()) {
        log::warn(L"Mission Control : aucune vue créée");
        return std::nullopt;
    }

    // Miniatures enregistrées du bas au haut de l'ordre Z : la fenêtre du dessus reste dessus pendant l'animation.
    std::map<HWND, std::size_t> z;
    EnumWindows(collectZ, reinterpret_cast<LPARAM>(&z));
    std::vector<MissionView::Window> wins;
    for (const auto& w : request.windows)
        if (IsWindow(w.hwnd) && !IsIconic(w.hwnd) && z.count(w.hwnd)) wins.push_back(w);
    std::sort(wins.begin(), wins.end(), [&](const auto& a, const auto& b) { return z[a.hwnd] > z[b.hwnd]; });
    for (const auto& w : wins) {
        const HMONITOR m = MonitorFromWindow(w.hwnd, MONITOR_DEFAULTTONEAREST);
        std::size_t k = 0;
        while (k < s.screens.size() && s.screens[k].mon != m) ++k;
        if (k == s.screens.size()) continue;
        RECT r{};
        GetWindowRect(w.hwnd, &r);
        Thumb th;
        th.src = w.hwnd;
        th.title = w.title;
        th.screen = k;
        th.from = {double(r.left - s.screens[k].rc.left), double(r.top - s.screens[k].rc.top), double(r.right - r.left),
                   double(r.bottom - r.top)};
        if (FAILED(DwmRegisterThumbnail(s.screens[k].hwnd, w.hwnd, &th.id))) continue;
        s.thumbs.push_back(std::move(th));
    }
    // Exposé d'une app : ses fenêtres réduites, sur l'écran du curseur, à leur taille normale (pas l'icône de -32000).
    POINT cursor{};
    GetCursorPos(&cursor);
    const HMONITOR cursorMon = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
    std::size_t shelfScreen = 0;
    while (shelfScreen < s.screens.size() && s.screens[shelfScreen].mon != cursorMon) ++shelfScreen;
    if (shelfScreen == s.screens.size()) shelfScreen = 0;
    for (const auto& w : request.minimized) {
        if (!IsWindow(w.hwnd) || !IsIconic(w.hwnd)) continue;
        Thumb th;
        th.src = w.hwnd;
        th.title = w.title;
        th.screen = shelfScreen;
        th.minimized = true;
        if (FAILED(DwmRegisterThumbnail(s.screens[shelfScreen].hwnd, w.hwnd, &th.id))) continue;
        // Taille de l'image que DWM garde (agrandie ou ancrée avant la réduction) : jamais déformée. Sans image : rien.
        SIZE src{};
        if (FAILED(DwmQueryThumbnailSourceSize(th.id, &src)) || src.cx <= 0 || src.cy <= 0) {
            DwmUnregisterThumbnail(th.id);
            continue;
        }
        th.from = {0, 0, double(src.cx), double(src.cy)};
        s.thumbs.push_back(std::move(th));
    }
    s.place();
    g_open = &s;
    if (env.trace)
        log::info(L"[trace] mission : %zu fenêtres sur %zu écrans", s.thumbs.size(), s.screens.size());

    s.last = now();
    s.tick();
    s.renderAll();
    POINT pt{};
    GetCursorPos(&pt);
    const HMONITOR here = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
    HWND focus = s.screens.front().hwnd;
    for (Screen& sc : s.screens) {
        ShowWindow(sc.hwnd, SW_SHOWNOACTIVATE);
        if (sc.mon == here) focus = sc.hwnd;
    }
    forceForeground(focus);   // Échap et Entrée
    SetFocus(focus);

    while (!s.done) {
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
        if (s.animating()) {
            s.tick();
            s.renderAll();
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 8, QS_ALLINPUT);
            continue;
        }
        s.tick();   // fenêtres fermées entre-temps
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 250, QS_ALLINPUT);
    }
    if (env.trace) log::info(L"[trace] mission : %s", s.chosen ? L"fenêtre choisie" : L"fermée sans choix");
    if (s.chosen && IsWindow(*s.chosen)) return s.chosen;
    return std::nullopt;
}

BgraImage missionSnapshot(const std::vector<MissionRect>& windows, bool dark, int width, int height, int hover) {
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
    Com<IDWriteFactory3> dwrite;
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory3), reinterpret_cast<IUnknown**>(dwrite.GetAddressOf()));
    const auto fmt = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    Com<ID2D1Bitmap1> target, readback, wallBmp;
    const auto size = D2D1::SizeU(UINT32(width), UINT32(height));
    if (FAILED(dc->CreateBitmap(size, nullptr, 0, D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, fmt), &target)) ||
        FAILED(dc->CreateBitmap(size, nullptr, 0,
                                D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, fmt),
                                &readback)))
        return out;
    const BgraImage wall = macWallpaper(width, height, dark);
    dc->CreateBitmap(size, wall.px.data(), UINT32(width * 4), D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE, fmt), &wallBmp);
    const auto rects = missionLayout(windows, missionArea({0, 0, double(width), double(height)}, 1), kMissionGap,
                                     kMissionGap + kMissionLabelRoom);

    dc->SetTarget(target.Get());
    dc->BeginDraw();
    dc->Clear(rgba(0, 0, 0, 1));
    if (wallBmp) dc->DrawBitmap(wallBmp.Get());
    Com<ID2D1SolidColorBrush> veil, bar;
    dc->CreateSolidColorBrush(rgba(0, 0, 0, kVeil), &veil);
    if (veil) dc->FillRectangle(D2D1::RectF(0, 0, float(width), float(height)), veil.Get());
    dc->CreateSolidColorBrush(rgba(1, 1, 1, 0.35f), &bar);
    for (std::size_t i = 0; i < rects.size(); ++i) {   // fenêtre factice : corps coloré, barre de titre claire
        Com<ID2D1SolidColorBrush> b;
        const std::uint32_t c = placeholderColor(i);
        dc->CreateSolidColorBrush(rgba(float((c >> 16) & 255) / 255, float((c >> 8) & 255) / 255, float(c & 255) / 255, 1), &b);
        const D2D1_RECT_F r = toD2D(rects[i]);
        if (b) dc->FillRoundedRectangle(D2D1::RoundedRect(r, 8, 8), b.Get());
        if (bar) dc->FillRectangle(D2D1::RectF(r.left, r.top + 4, r.right, r.top + std::min(24.0f, (r.bottom - r.top) / 4)), bar.Get());
    }
    if (hover >= 0 && std::size_t(hover) < rects.size())
        drawHover(dc.Get(), dwrite.Get(), rects[std::size_t(hover)], L"Fenêtre " + std::to_wstring(hover + 1), 1);
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
