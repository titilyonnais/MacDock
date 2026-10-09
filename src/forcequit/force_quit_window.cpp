#include "force_quit_window.h"

#include <dwmapi.h>
#include <shellscalingapi.h>
#include <wincodec.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <set>
#include <thread>

#include "../calib/png_io.h"
#include "../core/log.h"

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shcore.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

using Microsoft::WRL::ComPtr;

namespace md {

namespace {

constexpr wchar_t kClass[] = L"MacDockForceQuitWindow";
constexpr UINT_PTR kReloadTimer = 1, kSheetTimer = 2;
constexpr UINT kMsgIcon = WM_APP + 40;   // icône chargée (IconPost*, à libérer)

struct IconPost {
    std::wstring appId;
    IconProvider::ImagePtr image;
};

// Thème des apps de Windows (AppsUseLightTheme) : 0 = sombre.
bool appsDark() {
    DWORD light = 1, size = sizeof light;
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme",
                 RRF_RT_REG_DWORD, nullptr, &light, &size);
    return light == 0;
}

bool inside(const D2D1_RECT_F& r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }

ComPtr<ID2D1Bitmap> bitmapOf(ID2D1RenderTarget* rt, const IconProvider::Image& im) {
    ComPtr<ID2D1Bitmap> b;
    if (im.size <= 0 || im.bgra.size() < std::size_t(im.size) * im.size * 4) return b;
    rt->CreateBitmap(D2D1::SizeU(UINT32(im.size), UINT32(im.size)), im.bgra.data(), UINT32(im.size * 4),
                     D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)), &b);
    return b;
}

std::wstring hintFor(const std::wstring& shortcut) {
    return shortcut.empty() ? std::wstring() : L"Vous pouvez ouvrir cette fenêtre en appuyant sur " + shortcut + L".";
}

ui::SheetSpec confirmSpec(const FqApp& a) {
    if (a.explorer)
        return {L"Voulez-vous relancer l'Explorateur de fichiers ?", L"Ses fenêtres ouvertes seront fermées, puis le bureau reviendra.",
                {L"Annuler", L"Relancer"}, 1};
    return {L"Voulez-vous forcer « " + a.name + L" » à quitter ?", L"Les modifications non enregistrées seront perdues.",
            {L"Annuler", L"Forcer à quitter"}, 1};
}

}  // namespace

// Icônes chargées sur un fil à part (le Dock ne s'arrête pas), avec un fournisseur d'icônes propre à ce fil.
struct ForceQuitWindow::Loader {
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<std::pair<std::wstring, std::wstring>> queue;   // appId, nom Shell
    std::set<std::wstring> requested;
    HWND hwnd = nullptr;
    int px = 0;
};

ForceQuitWindow::~ForceQuitWindow() { close(); }

bool ForceQuitWindow::open(HINSTANCE instance, Env env, const std::wstring& preselect) {
    env_ = std::move(env);
    if (hwnd_) {   // déjà ouverte : au premier plan, liste relue
        if (IsIconic(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);
        reload();
        SetForegroundWindow(hwnd_);
        return true;
    }
    instance_ = instance;
    preselect_ = preselect;
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{sizeof wc};
        wc.lpfnWndProc = proc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClass;
        registered = RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }
    // Écran du pointeur, un peu au-dessus du centre de sa zone de travail.
    POINT pt{};
    GetCursorPos(&pt);
    const HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    UINT dpiX = 96, dpiY = 96;
    GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    scale_ = float(dpiX) / 96.f;
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(mon, &mi);
    const int w = int(std::lround(kFqWidth * scale_)), h = int(std::lround(kFqHeight * scale_));
    const RECT& wa = mi.rcWork;
    const int x = wa.left + (wa.right - wa.left - w) / 2, y = wa.top + (wa.bottom - wa.top - h) / 3;
    hwnd_ = CreateWindowExW(WS_EX_TOPMOST, kClass, kFqTitle, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX,
                            x, y, w, h, nullptr, nullptr, instance, this);
    if (!hwnd_) {
        log::error(L"Forcer à quitter : fenêtre impossible (%lu)", GetLastError());
        return false;
    }
    const DWORD corner = 2;   // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd_, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &corner, sizeof corner);
    updateTheme();
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    reload();
    SetTimer(hwnd_, kReloadTimer, 1000, nullptr);
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    log::info(L"Forcer à quitter : fenêtre ouverte (%zu apps)", apps_.size());
    return true;
}

void ForceQuitWindow::close() {
    if (!hwnd_) return;
    if (loader_) {
        std::lock_guard lock(loader_->mutex);
        loader_->hwnd = nullptr;
        loader_->cv.notify_all();
    }
    loader_.reset();
    HWND h = hwnd_;
    hwnd_ = nullptr;
    rt_.Reset();
    bitmaps_.clear();
    images_.clear();
    sheet_.reset();
    apps_.clear();
    selected_ = -1;
    scroll_ = 0;
    lightsHover_ = buttonPressed_ = false;
    lightPressed_ = -1;
    DestroyWindow(h);
    log::info(L"Forcer à quitter : fenêtre fermée");
}

LRESULT CALLBACK ForceQuitWindow::proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {   // dès la création : le cadre est retiré avant le premier affichage
        auto* created = reinterpret_cast<ForceQuitWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, LONG_PTR(created));
        created->hwnd_ = h;
    }
    auto* self = reinterpret_cast<ForceQuitWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (msg == WM_NCDESTROY) SetWindowLongPtrW(h, GWLP_USERDATA, 0);
    if (!self || self->hwnd_ != h) return DefWindowProcW(h, msg, wp, lp);   // fenêtre fermée
    return self->handle(msg, wp, lp);
}

LRESULT ForceQuitWindow::handle(UINT msg, WPARAM wp, LPARAM lp) {
    const HWND h = hwnd_;
    switch (msg) {
        case WM_NCCALCSIZE:
            if (wp) return 0;   // pas de cadre Windows : toute la fenêtre est à nous (DWM garde l'ombre et les coins)
            break;
        case WM_NCHITTEST: {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(h, &pt);
            const float x = float(pt.x) / scale_, y = float(pt.y) / scale_;
            if (!sheet_ && lightAt(layout_, x, y) >= 0) return HTCLIENT;
            return y < kFqTitleBar && !sheet_ ? HTCAPTION : HTCLIENT;
        }
        case WM_DPICHANGED: {
            scale_ = float(HIWORD(wp)) / 96.f;
            const RECT* r = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            if (rt_) rt_->SetDpi(96 * scale_, 96 * scale_);
            bitmaps_.clear();
            images_.clear();   // icônes refaites à la taille de ce nouvel écran
            if (loader_) {
                std::lock_guard lock(loader_->mutex);
                loader_->px = int(std::lround(kFqIcon * scale_));
                loader_->requested.clear();
                loader_->queue.clear();
            }
            requestIcons();
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        case WM_SIZE:
            if (rt_) rt_->Resize(D2D1::SizeU(LOWORD(lp), HIWORD(lp)));
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(h, &ps);
            render();
            EndPaint(h, &ps);
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_ACTIVATE:
            active_ = LOWORD(wp) != WA_INACTIVE;
            InvalidateRect(h, nullptr, FALSE);
            break;   // Windows donne aussi le clavier à la fenêtre
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
            onMouse(msg, float(GET_X_LPARAM(lp)) / scale_, float(GET_Y_LPARAM(lp)) / scale_);
            return 0;
        case WM_MOUSELEAVE:
            lightsHover_ = false;
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_MOUSEWHEEL:
            if (!sheet_) {
                scroll_ = std::clamp(scroll_ - float(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA * kFqRow * 3, 0.f,
                                     maxScroll(layout_, apps_.size()));
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            onKey(UINT(wp), (lp & (1 << 30)) != 0);
            return 0;
        case WM_TIMER:
            if (wp == kReloadTimer) {
                reload();
            } else if (wp == kSheetTimer) {
                if (sheet_ && sheet_->progress < 1) sheet_->progress = std::min(1.f, sheet_->progress + 16.f / 180.f);
                if (!sheet_ || sheet_->progress >= 1) KillTimer(h, kSheetTimer);
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        case kMsgIcon: {
            std::unique_ptr<IconPost> post(reinterpret_cast<IconPost*>(lp));
            if (post->image) {
                images_[post->appId] = post->image;
                bitmaps_.erase(post->appId);
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        }
        case WM_SETTINGCHANGE:
            if (lp && wcscmp(reinterpret_cast<const wchar_t*>(lp), L"ImmersiveColorSet") == 0) {
                updateTheme();
                InvalidateRect(h, nullptr, FALSE);
            }
            break;
        case WM_CLOSE:
            close();
            return 0;
        default: break;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

bool ForceQuitWindow::createTarget() {
    if (rt_) return true;
    if (!d2d_ && FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d_.GetAddressOf()))) return false;
    if (!dwrite_) {
        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                       reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf()))))
            return false;
        font_ = ui::interfaceFont(dwrite_.Get());
    }
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                                                    96 * scale_, 96 * scale_);
    if (FAILED(d2d_->CreateHwndRenderTarget(props, D2D1::HwndRenderTargetProperties(hwnd_, D2D1::SizeU(rc.right, rc.bottom)),
                                            &rt_)))
        return false;
    bitmaps_.clear();
    return true;
}

void ForceQuitWindow::render() {
    if (!createTarget()) return;
    FqView v;
    for (const FqApp& a : apps_) {
        ID2D1Bitmap* icon = nullptr;
        if (auto im = images_.find(a.appId); im != images_.end() && im->second) {
            auto& b = bitmaps_[a.appId];
            if (!b) b = bitmapOf(rt_.Get(), *im->second);
            icon = b.Get();
        }
        v.rows.push_back({a.name, a.hung, icon});
    }
    v.selected = selected_;
    v.active = active_;
    v.lightsHover = lightsHover_;
    v.lightPressed = lightPressed_;
    const bool explorer = selected_ >= 0 && selected_ < int(apps_.size()) && apps_[std::size_t(selected_)].explorer;
    v.buttonLabel = explorer ? L"Relancer" : L"Forcer à quitter";
    v.buttonEnabled = selected_ >= 0;
    v.buttonPressed = buttonPressed_;
    v.hint = hintFor(env_.shortcutLabel);
    const ui::Palette pal = ui::palette(dark_);
    rt_->BeginDraw();
    rt_->SetTransform(D2D1::Matrix3x2F::Identity());
    {
        ui::Painter p(rt_.Get(), dwrite_.Get(), pal, font_, &formats_);
        layout_ = layoutForceQuit(p, v);
        scroll_ = std::clamp(scroll_, 0.f, maxScroll(layout_, apps_.size()));
        v.scroll = scroll_;
        drawForceQuit(p, layout_, v);
        if (sheet_) {
            sheet_->layout = ui::layoutSheet(p, kFqWidth, kFqTitleBar, sheet_->spec);
            ui::drawSheet(p, sheet_->layout, sheet_->spec, kFqWidth, kFqHeight, sheet_->hover, sheet_->pressed, sheet_->progress);
        }
    }
    if (rt_->EndDraw() == D2DERR_RECREATE_TARGET) {   // carte graphique réinitialisée : tout refait à la prochaine image
        rt_.Reset();
        bitmaps_.clear();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void ForceQuitWindow::reload() {
    if (!hwnd_) return;
    std::vector<FqApp> next = env_.list ? env_.list() : std::vector<FqApp>{};
    int sel = -1;
    if (!preselect_.empty()) {   // première liste : l'app qui était au premier plan
        for (std::size_t i = 0; i < next.size(); ++i)
            if (next[i].appId == preselect_) sel = int(i);
        if (!next.empty()) preselect_.clear();
    } else {
        sel = selectionAfter(apps_, selected_, next);
    }
    const bool moved = sel != selected_;
    apps_ = std::move(next);
    selected_ = sel;
    if (moved && sel >= 0) scroll_ = scrollToShow(layout_, apps_.size(), scroll_, sel);
    requestIcons();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void ForceQuitWindow::requestIcons() {
    if (!hwnd_) return;
    if (!loader_) {
        loader_ = std::make_shared<Loader>();
        loader_->hwnd = hwnd_;
        loader_->px = int(std::lround(kFqIcon * scale_));
        std::thread([l = loader_, setup = env_.iconSetup] {
            CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            {
                IconProvider icons;   // propre à ce fil
                if (setup) setup(icons);
                for (;;) {
                    std::pair<std::wstring, std::wstring> job;
                    int px = 0;
                    {
                        std::unique_lock lock(l->mutex);
                        l->cv.wait(lock, [&] { return !l->hwnd || !l->queue.empty(); });
                        if (!l->hwnd) break;
                        job = std::move(l->queue.front());
                        l->queue.pop_front();
                        px = l->px;
                    }
                    auto* post = new IconPost{job.first, icons.get(job.first, job.second, px)};
                    bool posted = false;
                    {
                        std::lock_guard lock(l->mutex);
                        posted = l->hwnd && PostMessageW(l->hwnd, kMsgIcon, 0, reinterpret_cast<LPARAM>(post));
                    }
                    if (!posted) delete post;
                }
            }
            CoUninitialize();
        }).detach();
    }
    std::lock_guard lock(loader_->mutex);
    for (const FqApp& a : apps_)
        if (!images_.count(a.appId) && loader_->requested.insert(a.appId).second)
            loader_->queue.emplace_back(a.appId, a.launch.empty() ? a.exePath : a.launch);
    loader_->cv.notify_all();
}

void ForceQuitWindow::select(int i) {
    if (apps_.empty()) return;
    selected_ = std::clamp(i, 0, int(apps_.size()) - 1);
    scroll_ = scrollToShow(layout_, apps_.size(), scroll_, selected_);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void ForceQuitWindow::openConfirm() {
    if (selected_ < 0 || selected_ >= int(apps_.size())) return;
    Sheet s;
    s.target = apps_[std::size_t(selected_)];
    s.spec = confirmSpec(s.target);
    sheet_ = std::move(s);
    buttonPressed_ = false;
    SetTimer(hwnd_, kSheetTimer, 16, nullptr);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void ForceQuitWindow::closeSheet() {
    sheet_.reset();
    if (hwnd_) {
        KillTimer(hwnd_, kSheetTimer);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void ForceQuitWindow::choose(int button) {
    if (!sheet_) return;
    const Sheet s = *sheet_;
    closeSheet();
    if (s.kind != Sheet::Kind::Confirm || button != s.spec.primary) return;   // Annuler, ou OK d'une erreur
    std::wstring error;
    const bool ok = env_.forceQuit && env_.forceQuit(s.target, &error);
    if (ok) {
        log::info(s.target.explorer ? L"Forcer à quitter : Explorateur relancé" : L"Forcer à quitter : « %s » arrêtée",
                  s.target.name.c_str());
    } else {
        log::warn(L"Forcer à quitter : « %s » refusée (%s)", s.target.name.c_str(), error.c_str());
        Sheet e;
        e.kind = Sheet::Kind::Error;
        e.target = s.target;
        e.spec = {s.target.explorer ? L"Impossible de relancer l'Explorateur de fichiers"
                                    : L"Impossible de forcer « " + s.target.name + L" » à quitter",
                  error.empty() ? L"Windows a refusé." : error, {L"OK"}, 0};
        e.progress = 1;
        sheet_ = std::move(e);
    }
    reload();
}

void ForceQuitWindow::onMouse(UINT msg, float x, float y) {
    if (sheet_) {
        const int b = ui::sheetButtonAt(sheet_->layout, x, y);
        if (msg == WM_MOUSEMOVE) {
            if (b != sheet_->hover) {
                sheet_->hover = b;
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
        } else if (msg == WM_LBUTTONDOWN) {
            sheet_->pressed = b;
            SetCapture(hwnd_);
            InvalidateRect(hwnd_, nullptr, FALSE);
        } else {
            ReleaseCapture();
            const int pressed = sheet_->pressed;
            sheet_->pressed = -1;
            InvalidateRect(hwnd_, nullptr, FALSE);
            if (pressed >= 0 && pressed == b) choose(pressed);
        }
        return;
    }
    const int light = lightAt(layout_, x, y);
    if (msg == WM_MOUSEMOVE) {
        const bool over = x >= layout_.lights.x - 10 && x <= layout_.lights.x + 2 * ui::kLightSpacing + 10 &&
                          std::fabs(y - layout_.lights.y) <= 10;
        if (over != lightsHover_) {
            lightsHover_ = over;
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, hwnd_, 0};
        TrackMouseEvent(&t);
        return;
    }
    if (msg == WM_LBUTTONDOWN) {
        SetCapture(hwnd_);
        if (light >= 0) {
            lightPressed_ = light;
        } else if (const int row = rowAt(layout_, apps_.size(), scroll_, x, y); row >= 0) {
            select(row);
        } else if (selected_ >= 0 && inside(layout_.button, x, y)) {
            buttonPressed_ = true;
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    ReleaseCapture();   // WM_LBUTTONUP
    if (lightPressed_ >= 0) {
        const int pressed = lightPressed_;
        lightPressed_ = -1;
        InvalidateRect(hwnd_, nullptr, FALSE);
        if (pressed == light) {
            if (pressed == 0) close();
            else ShowWindow(hwnd_, SW_MINIMIZE);
        }
        return;
    }
    if (buttonPressed_) {
        buttonPressed_ = false;
        InvalidateRect(hwnd_, nullptr, FALSE);
        if (inside(layout_.button, x, y)) openConfirm();
    }
}

void ForceQuitWindow::onKey(UINT vk, bool repeat) {
    if (sheet_) {
        if (vk == VK_RETURN && !repeat) choose(sheet_->spec.primary);   // Entrée tenue : jamais une seconde validation
        else if (vk == VK_ESCAPE) choose(sheet_->kind == Sheet::Kind::Error ? 0 : -1);
        return;
    }
    const int last = int(apps_.size()) - 1;
    switch (vk) {
        case VK_UP: select(selected_ < 0 ? last : selected_ - 1); break;
        case VK_DOWN: select(selected_ < 0 ? 0 : selected_ + 1); break;
        case VK_HOME: select(0); break;
        case VK_END: select(last); break;
        case VK_RETURN:
            if (!repeat) openConfirm();
            break;
        case VK_ESCAPE: close(); break;
        case 'W':
            if (GetKeyState(VK_CONTROL) < 0) close();
            break;
        default: break;
    }
}

void ForceQuitWindow::updateTheme() {
    dark_ = env_.dark ? *env_.dark : appsDark();
    const BOOL dark = dark_;
    if (hwnd_) DwmSetWindowAttribute(hwnd_, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof dark);
}

RECT ForceQuitWindow::toScreen(D2D1_RECT_F r) const {
    RECT out{LONG(std::lround(r.left * scale_)), LONG(std::lround(r.top * scale_)), LONG(std::lround(r.right * scale_)),
             LONG(std::lround(r.bottom * scale_))};
    if (hwnd_) MapWindowPoints(hwnd_, nullptr, reinterpret_cast<POINT*>(&out), 2);
    return out;
}

RECT ForceQuitWindow::rowOnScreen(int i) const { return toScreen(rowRect(layout_, i, scroll_)); }
RECT ForceQuitWindow::buttonOnScreen() const { return toScreen(layout_.button); }
RECT ForceQuitWindow::sheetButtonOnScreen(int i) const {
    if (!sheet_ || i < 0 || i >= int(sheet_->layout.buttons.size())) return RECT{};
    return toScreen(sheet_->layout.buttons[std::size_t(i)]);
}

// ---- Image hors écran ----

bool forceQuitSnapshot(const std::wstring& path, bool dark, bool sheet) {
    const float scale = 2;
    const UINT w = UINT(kFqWidth * scale), h = UINT(kFqHeight * scale);
    ComPtr<IWICImagingFactory> wic;
    ComPtr<IWICBitmap> bmp;
    ComPtr<ID2D1Factory> d2d;
    ComPtr<ID2D1RenderTarget> rt;
    ComPtr<IDWriteFactory> dwrite;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) ||
        FAILED(wic->CreateBitmap(w, h, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp)) ||
        FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf())) ||
        FAILED(d2d->CreateWicBitmapRenderTarget(
            bmp.Get(), D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                                                    96 * scale, 96 * scale),
            &rt)) ||
        FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dwrite.GetAddressOf()))))
        return false;
    FqView v;
    v.rows = {{L"Bloc-notes"}, {L"Calculatrice", true}, {L"Courrier"}, {L"Photos"}, {L"Explorateur de fichiers"}};
    v.selected = 2;
    v.buttonEnabled = true;
    v.hint = hintFor(L"⌥⊞⎋");
    const ui::Palette pal = ui::palette(dark);
    rt->BeginDraw();
    {
        ui::Painter p(rt.Get(), dwrite.Get(), pal, ui::interfaceFont(dwrite.Get()));
        const FqLayout l = layoutForceQuit(p, v);
        drawForceQuit(p, l, v);
        if (sheet) {
            FqApp a;
            a.name = L"Courrier";
            const ui::SheetSpec spec = confirmSpec(a);
            const ui::SheetLayout sl = ui::layoutSheet(p, kFqWidth, kFqTitleBar, spec);
            ui::drawSheet(p, sl, spec, kFqWidth, kFqHeight, -1, -1, 1);
        }
    }
    if (FAILED(rt->EndDraw())) return false;
    std::vector<std::uint8_t> px(std::size_t(w) * h * 4);
    WICRect all{0, 0, int(w), int(h)};
    if (FAILED(bmp->CopyPixels(&all, w * 4, UINT(px.size()), px.data()))) return false;
    return writePng(path, px.data(), w, h);
}

} // namespace md
