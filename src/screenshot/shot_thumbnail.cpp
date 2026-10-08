#include "shot_thumbnail.h"

#include <shellapi.h>
#include <shellscalingapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <windowsx.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "../calib/png_io.h"
#include "../core/diag.h"
#include "../core/log.h"
#include "screenshot_logic.h"

using Microsoft::WRL::ComPtr;

namespace md {

namespace {
constexpr wchar_t kClass[] = L"MacDockShotThumbnail";
constexpr UINT_PTR kStepTimer = 1;
}

ShotThumbnail::~ShotThumbnail() {
    if (dc_) {
        if (old_) SelectObject(dc_, old_);
        DeleteDC(dc_);
    }
    if (dib_) DeleteObject(dib_);
    if (hwnd_) DestroyWindow(hwnd_);
}

bool ShotThumbnail::ensureWindow(HINSTANCE instance) {
    if (hwnd_) return true;
    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);
    hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, kClass, L"", WS_POPUP, 0, 0, 1,
                            1, nullptr, nullptr, instance, this);
    if (!hwnd_) {
        log::warn(L"Capture d'écran : vignette impossible (%lu)", GetLastError());
        return false;
    }
    // Comme sur macOS, la vignette n'apparaît pas dans la capture suivante.
    if (!diagnosticCapture()) SetWindowDisplayAffinity(hwnd_, WDA_EXCLUDEFROMCAPTURE);
    dc_ = CreateCompatibleDC(nullptr);
    return dc_ != nullptr;
}

void ShotThumbnail::show(HINSTANCE instance, const BgraImage& shot, const std::wstring& path, HMONITOR monitor) {
    close();
    if (shot.w <= 0 || shot.h <= 0 || !ensureWindow(instance)) return;
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(monitor, &mi);
    UINT dpiX = 96, dpiY = 96;
    if (FAILED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) dpiX = 96;
    scale_ = dpiX / 96.0;
    area_ = mi.rcMonitor;
    const RECT img = thumbnailRect(mi.rcWork, SIZE{shot.w, shot.h}, scale_);
    // Réduction de qualité (WIC, cubique) ; l'alpha sert peu ici (captures d'écran opaques, ombre des fenêtres).
    BgraImage reduced;
    reduced.w = img.right - img.left;
    reduced.h = img.bottom - img.top;
    reduced.px = reduced.w == shot.w && reduced.h == shot.h
                   ? shot.px
                   : resizeBgra(shot.px, UINT(shot.w), UINT(shot.h), UINT(reduced.w), UINT(reduced.h));
    if (reduced.px.size() != std::size_t(reduced.w) * reduced.h * 4) return;
    const std::vector<std::uint8_t> pixels = thumbnailPixels(reduced, scale_, w_, h_, margin_);
    if (pixels.empty()) return;
    if (old_) SelectObject(dc_, old_);
    old_ = nullptr;
    if (dib_) DeleteObject(dib_);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w_;
    bi.bmiHeader.biHeight = -h_;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    dib_ = CreateDIBSection(dc_, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib_) return;
    old_ = SelectObject(dc_, dib_);
    std::memcpy(bits, pixels.data(), pixels.size());
    home_ = POINT{img.left - margin_, img.top - margin_};
    path_ = path;
    saved_ = openPending_ = false;
    hover_ = pressed_ = flicking_ = false;
    state_ = State::In;
    phaseStart_ = lastTick_ = GetTickCount64();
    stayLeft_ = kThumbStay;
    offset_ = area_.right - home_.x;   // entièrement hors de l'écran
    place(offset_);
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetTimer(hwnd_, kStepTimer, 15, nullptr);
}

void ShotThumbnail::fileSaved(const std::wstring& path, bool ok) {
    if (path != path_) return;
    saved_ = ok;
    if (ok && openPending_) open();
}

void ShotThumbnail::close() {
    if (state_ == State::Hidden) return;
    state_ = State::Hidden;
    pressed_ = flicking_ = false;   // avant ReleaseCapture (WM_CAPTURECHANGED les lit)
    if (hwnd_) {
        KillTimer(hwnd_, kStepTimer);
        if (GetCapture() == hwnd_) ReleaseCapture();
        ShowWindow(hwnd_, SW_HIDE);
    }
}

void ShotThumbnail::place(double offset) {
    offset_ = offset;
    const LONG x = home_.x + LONG(std::lround(offset));
    const LONG visible = std::min<LONG>(w_, area_.right - x);   // jamais sur l'écran voisin
    if (visible <= 0) {
        ShowWindow(hwnd_, SW_HIDE);
        return;
    }
    POINT pos{x, home_.y}, src{0, 0};
    SIZE size{visible, h_};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(hwnd_, nullptr, &pos, &size, dc_, &src, 0, &blend, ULW_ALPHA);
    if (!IsWindowVisible(hwnd_) && state_ != State::Hidden) ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
}

void ShotThumbnail::leave() {
    state_ = State::Out;
    fromOffset_ = offset_;
    phaseStart_ = GetTickCount64();
}

void ShotThumbnail::step() {
    const ULONGLONG now = GetTickCount64();
    const double t = (now - phaseStart_) / 1000.0, dt = (now - lastTick_) / 1000.0;
    lastTick_ = now;
    const double distance = area_.right - home_.x;
    switch (state_) {
        case State::In:
            place(thumbnailOffset(ThumbPhase::In, t, distance));
            if (t >= kThumbIn) state_ = State::Stay;
            break;
        case State::Stay:
            if (!hover_ && !pressed_) stayLeft_ -= dt;
            if (stayLeft_ <= 0) leave();
            break;
        case State::Back:   // glisser relâché trop tôt : retour en place
            place(fromOffset_ * (1 - std::min(1.0, t / 0.2)));
            if (t >= 0.2) state_ = State::Stay;
            break;
        case State::Out:
            place(fromOffset_ + thumbnailOffset(ThumbPhase::Out, t, distance - fromOffset_));
            if (t >= kThumbOut) close();
            break;
        default: break;
    }
}

void ShotThumbnail::open() {
    openPending_ = false;
    if (!saved_) {
        openPending_ = true;   // l'écriture se termine : ouvert juste après
        return;
    }
    AllowSetForegroundWindow(ASFW_ANY);   // l'app ouverte passe devant (le clic nous en donne le droit)
    ShellExecuteW(nullptr, nullptr, path_.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    close();
}

void ShotThumbnail::dragOut() {
    // Le fichier est déposé ailleurs, comme une icône du Bureau (copie, déplacement ou lien, au choix de la cible).
    ComPtr<IShellItem> item;
    ComPtr<IDataObject> data;
    if (!saved_ || FAILED(SHCreateItemFromParsingName(path_.c_str(), nullptr, IID_PPV_ARGS(&item))) ||
        FAILED(item->BindToHandler(nullptr, BHID_DataObject, IID_PPV_ARGS(&data))))
        return;
    ShowWindow(hwnd_, SW_HIDE);
    KillTimer(hwnd_, kStepTimer);
    DWORD effect = 0;
    SHDoDragDrop(hwnd_, data.Get(), nullptr, DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK, &effect);
    close();
}

LRESULT CALLBACK ShotThumbnail::proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE)
        SetWindowLongPtrW(h, GWLP_USERDATA, LONG_PTR(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
    auto* self = reinterpret_cast<ShotThumbnail*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    return self && self->hwnd_ == h ? self->handle(msg, wp, lp) : DefWindowProcW(h, msg, wp, lp);
}

LRESULT ShotThumbnail::handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_TIMER:
            if (wp == kStepTimer) step();
            return 0;
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_MOUSEMOVE: {
            if (!hover_) {
                hover_ = true;
                TRACKMOUSEEVENT tme{sizeof tme, TME_LEAVE, hwnd_, 0};
                TrackMouseEvent(&tme);
            }
            if (!pressed_) return 0;
            POINT pt;
            GetCursorPos(&pt);
            const int dx = pt.x - pressAt_.x, dy = pt.y - pressAt_.y;
            const int slop = int(std::lround(4 * scale_));
            if (!flicking_ && (std::abs(dx) > slop || std::abs(dy) > slop)) {
                if (dx > 0 && std::abs(dx) >= std::abs(dy)) {
                    flicking_ = true;   // vers la droite : la vignette suit le doigt
                } else {
                    pressed_ = false;
                    ReleaseCapture();
                    dragOut();
                    return 0;
                }
            }
            if (flicking_) place(std::max(0, dx));
            return 0;
        }
        case WM_MOUSELEAVE: hover_ = false; return 0;
        case WM_LBUTTONDOWN:
            if (state_ == State::Out || state_ == State::Hidden) return 0;
            pressed_ = true;
            flicking_ = false;
            GetCursorPos(&pressAt_);
            SetCapture(hwnd_);
            if (state_ == State::In) {   // attrapée en route : elle se pose
                place(0);
                state_ = State::Stay;
            }
            return 0;
        case WM_LBUTTONUP: {
            if (!pressed_) return 0;
            // Lu avant ReleaseCapture : WM_CAPTURECHANGED arrive pendant l'appel et remet l'état à zéro.
            const bool flicked = flicking_;
            pressed_ = flicking_ = false;
            ReleaseCapture();
            if (!flicked) {
                open();
            } else if (offset_ > 40 * scale_) {
                leave();
            } else {
                state_ = State::Back;
                fromOffset_ = offset_;
                phaseStart_ = GetTickCount64();
            }
            return 0;
        }
        case WM_CAPTURECHANGED:
            if (pressed_ && flicking_) {
                state_ = State::Back;
                fromOffset_ = offset_;
                phaseStart_ = GetTickCount64();
            }
            pressed_ = flicking_ = false;
            return 0;
        default: break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

} // namespace md
