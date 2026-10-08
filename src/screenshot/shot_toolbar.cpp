#include "shot_toolbar.h"

#include <windowsx.h>

#include <cmath>
#include <vector>

#include "../core/diag.h"
#include "screen_grab.h"

using Microsoft::WRL::ComPtr;

namespace md {

namespace {
constexpr wchar_t kToolbarClass[] = L"MacDockShotToolbar";
constexpr wchar_t kPillClass[] = L"MacDockRecordingPill";

D2D1_COLOR_F rgba(float r, float g, float b, float a) { return D2D1::ColorF(r, g, b, a); }

// Pictogrammes des modes, dessinés au trait dans la case (x, y, s).
void drawMode(ID2D1RenderTarget* rt, ID2D1Brush* ink, ID2D1Brush* red, int item, float x, float y, float s, float sc) {
    const float w = 1.4f * sc, cx = x + s / 2, cy = y + s / 2;
    const D2D1_RECT_F box{cx - 10 * sc, cy - 7 * sc, cx + 10 * sc, cy + 7 * sc};
    Microsoft::WRL::ComPtr<ID2D1StrokeStyle> dash;
    if (item == kToolRegion || item == kToolRecordRegion) {
        Microsoft::WRL::ComPtr<ID2D1Factory> f;
        rt->GetFactory(&f);
        const float dashes[] = {2.f, 2.f};
        f->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT,
                                                         D2D1_LINE_JOIN_MITER, 10, D2D1_DASH_STYLE_CUSTOM),
                             dashes, 2, &dash);
    }
    switch (item) {
        case kToolClose: {
            const float g = 5 * sc;
            rt->DrawLine({cx - g, cy - g}, {cx + g, cy + g}, ink, w * 1.2f);
            rt->DrawLine({cx + g, cy - g}, {cx - g, cy + g}, ink, w * 1.2f);
            break;
        }
        case kToolScreen:
        case kToolRecordScreen:
            rt->DrawRoundedRectangle({box, 2 * sc, 2 * sc}, ink, w);
            break;
        case kToolWindow:
            rt->DrawRoundedRectangle({box, 2 * sc, 2 * sc}, ink, w);
            rt->DrawLine({box.left, box.top + 4 * sc}, {box.right, box.top + 4 * sc}, ink, w);
            break;
        case kToolRegion:
        case kToolRecordRegion:
            rt->DrawRectangle(box, ink, w, dash.Get());
            break;
        default: break;
    }
    if (item == kToolRecordScreen || item == kToolRecordRegion)   // pastille d'enregistrement dans le coin
        rt->FillEllipse(D2D1::Ellipse({box.right - 1 * sc, box.bottom - 1 * sc}, 3.5f * sc, 3.5f * sc), red);
}

} // namespace

// ---- Panneau en couches ----

LayeredPanel::~LayeredPanel() {
    if (hwnd_) DestroyWindow(hwnd_);
}

bool LayeredPanel::ensure(HINSTANCE instance, const wchar_t* cls, WNDPROC proc, void* self) {
    if (hwnd_) return true;
    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = cls;
    RegisterClassExW(&wc);
    hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, cls, L"", WS_POPUP, 0, 0, 1, 1,
                            nullptr, nullptr, instance, self);
    if (!hwnd_) return false;
    if (!diagnosticCapture()) SetWindowDisplayAffinity(hwnd_, WDA_EXCLUDEFROMCAPTURE);   // jamais dans la capture
    if (!d2d_) D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d_.GetAddressOf());
    if (!dwrite_)
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf()));
    if (!wic_) CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic_));
    return d2d_ && dwrite_ && wic_;
}

void LayeredPanel::paint(int w, int h, POINT topLeft, const std::function<void(ID2D1RenderTarget*, IDWriteFactory*)>& draw) {
    if (!hwnd_ || w <= 0 || h <= 0) return;
    ComPtr<IWICBitmap> bitmap;
    ComPtr<ID2D1RenderTarget> rt;
    const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                                                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
    if (FAILED(wic_->CreateBitmap(UINT(w), UINT(h), GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap)) ||
        FAILED(d2d_->CreateWicBitmapRenderTarget(bitmap.Get(), props, &rt)))
        return;
    rt->BeginDraw();
    rt->Clear(rgba(0, 0, 0, 0));
    draw(rt.Get(), dwrite_.Get());
    if (FAILED(rt->EndDraw())) return;
    BITMAPINFO bi{};
    bi.bmiHeader = {sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB};
    void* bits = nullptr;
    HDC mem = CreateCompatibleDC(nullptr);
    HBITMAP dib = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (dib && SUCCEEDED(bitmap->CopyPixels(nullptr, UINT(w) * 4, UINT(w * h * 4), static_cast<BYTE*>(bits)))) {
        HGDIOBJ old = SelectObject(mem, dib);
        POINT src{0, 0};
        SIZE size{w, h};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        UpdateLayeredWindow(hwnd_, nullptr, &topLeft, &size, mem, &src, 0, &blend, ULW_ALPHA);
        SelectObject(mem, old);
        if (!IsWindowVisible(hwnd_)) ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    if (dib) DeleteObject(dib);
    DeleteDC(mem);
}

void LayeredPanel::hide() {
    if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
}

// ---- Barre de ⊞⇧5 ----

bool ShotToolbar::open(HINSTANCE instance, Done done) {
    if (!panel_.ensure(instance, kToolbarClass, proc, this)) return false;
    done_ = std::move(done);
    POINT cursor;
    GetCursorPos(&cursor);
    const HMONITOR mon = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
    scale_ = monitorScale(mon);
    layout_ = shotToolbarLayout(scale_);
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(mon, &mi);
    // En bas au centre, au-dessus du Dock (comme ⌘⇧5).
    at_ = POINT{mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - LONG(layout_.width)) / 2,
                mi.rcWork.bottom - LONG(layout_.height) - LONG(std::lround(120 * scale_))};
    hover_ = -1;
    render();
    return true;
}

void ShotToolbar::close() {
    if (!isOpen()) return;
    panel_.hide();
    done_ = nullptr;
}

void ShotToolbar::finish(int item) {
    Done done = std::move(done_);
    done_ = nullptr;
    panel_.hide();
    if (done) done(item);
}

void ShotToolbar::render() {
    const float sc = float(scale_);
    panel_.paint(int(std::ceil(layout_.width)), int(std::ceil(layout_.height)), at_, [&](ID2D1RenderTarget* rt, IDWriteFactory* dw) {
        ComPtr<ID2D1SolidColorBrush> bg, ink, sel, hot, red, accent, white;
        rt->CreateSolidColorBrush(rgba(0.13f, 0.13f, 0.14f, 0.92f), &bg);
        rt->CreateSolidColorBrush(rgba(1, 1, 1, 0.92f), &ink);
        rt->CreateSolidColorBrush(rgba(1, 1, 1, 0.22f), &sel);
        rt->CreateSolidColorBrush(rgba(1, 1, 1, 0.10f), &hot);
        rt->CreateSolidColorBrush(rgba(1.0f, 0.27f, 0.23f, 1), &red);
        rt->CreateSolidColorBrush(rgba(0.04f, 0.52f, 1.0f, 1), &accent);
        rt->CreateSolidColorBrush(rgba(1, 1, 1, 1), &white);
        const D2D1_RECT_F all{0.5f, 0.5f, float(layout_.width) - 0.5f, float(layout_.height) - 0.5f};
        rt->FillRoundedRectangle({all, 14 * sc, 14 * sc}, bg.Get());
        for (std::size_t i = 0; i < layout_.buttons.size(); ++i) {
            const ShotToolbarButton& b = layout_.buttons[i];
            const D2D1_RECT_F r{float(b.x), float(b.y), float(b.x + b.w), float(b.y + b.h)};
            if (int(i) == kToolAction) {
                rt->FillRoundedRectangle({r, 8 * sc, 8 * sc}, accent.Get());
                ComPtr<IDWriteTextFormat> f;
                dw->CreateTextFormat(L"SF Pro Text", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
                                     DWRITE_FONT_STRETCH_NORMAL, 13 * sc, L"fr-FR", &f);
                if (f) {
                    f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                    const wchar_t* label = mode_ == kToolRecordScreen || mode_ == kToolRecordRegion ? L"Enregistrer" : L"Capturer";
                    rt->DrawTextW(label, UINT32(wcslen(label)), f.Get(), r, white.Get());
                }
                continue;
            }
            if (int(i) == mode_) rt->FillRoundedRectangle({r, 8 * sc, 8 * sc}, sel.Get());
            else if (int(i) == hover_) rt->FillRoundedRectangle({r, 8 * sc, 8 * sc}, hot.Get());
            drawMode(rt, ink.Get(), red.Get(), int(i), float(b.x), float(b.y), float(b.w), sc);
        }
    });
}

LRESULT CALLBACK ShotToolbar::proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) SetWindowLongPtrW(h, GWLP_USERDATA, LONG_PTR(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
    auto* self = reinterpret_cast<ShotToolbar*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (!self) return DefWindowProcW(h, msg, wp, lp);
    switch (msg) {
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, h, 0};
            TrackMouseEvent(&t);
            const int hit = shotToolbarHit(self->layout_, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            if (hit != self->hover_) {
                self->hover_ = hit;
                self->render();
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            self->hover_ = -1;
            self->render();
            return 0;
        case WM_LBUTTONUP: {
            const int hit = shotToolbarHit(self->layout_, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            if (hit == kToolClose) self->finish(kToolClose);
            else if (hit == kToolAction) self->finish(self->mode_);
            else if (hit >= kToolScreen && hit <= kToolRecordRegion) {
                self->mode_ = hit;
                self->render();
            }
            return 0;
        }
        default: break;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// ---- Pastille d'enregistrement ----

bool RecordingPill::show(HINSTANCE instance, HMONITOR monitor, std::function<void()> onStop) {
    if (!panel_.ensure(instance, kPillClass, proc, this)) return false;
    onStop_ = std::move(onStop);
    scale_ = monitorScale(monitor);
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(monitor, &mi);
    const LONG w = LONG(std::lround(96 * scale_));
    at_ = POINT{mi.rcMonitor.left + (mi.rcMonitor.right - mi.rcMonitor.left - w) / 2, mi.rcWork.top + LONG(std::lround(8 * scale_))};
    update(0);
    return true;
}

void RecordingPill::update(double seconds) {
    const float sc = float(scale_);
    const int w = int(std::lround(96 * scale_)), h = int(std::lround(30 * scale_));
    const int total = int(seconds);
    wchar_t text[16];
    swprintf_s(text, L"%d:%02d", total / 60, total % 60);
    panel_.paint(w, h, at_, [&](ID2D1RenderTarget* rt, IDWriteFactory* dw) {
        ComPtr<ID2D1SolidColorBrush> bg, red, ink;
        rt->CreateSolidColorBrush(rgba(0.13f, 0.13f, 0.14f, 0.92f), &bg);
        rt->CreateSolidColorBrush(rgba(1.0f, 0.27f, 0.23f, 1), &red);
        rt->CreateSolidColorBrush(rgba(1, 1, 1, 0.95f), &ink);
        rt->FillRoundedRectangle({{0.5f, 0.5f, float(w) - 0.5f, float(h) - 0.5f}, float(h) / 2, float(h) / 2}, bg.Get());
        const float s = 10 * sc, cy = float(h) / 2, x = 12 * sc;
        rt->FillRoundedRectangle({{x, cy - s / 2, x + s, cy + s / 2}, 2 * sc, 2 * sc}, red.Get());   // ⏹
        ComPtr<IDWriteTextFormat> f;
        dw->CreateTextFormat(L"SF Pro Text", nullptr, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_FONT_STYLE_NORMAL,
                             DWRITE_FONT_STRETCH_NORMAL, 13 * sc, L"fr-FR", &f);
        if (f) {
            f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            rt->DrawTextW(text, UINT32(wcslen(text)), f.Get(), D2D1::RectF(x + s + 8 * sc, 0, float(w), float(h)), ink.Get());
        }
    });
}

void RecordingPill::hide() { panel_.hide(); }

LRESULT CALLBACK RecordingPill::proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) SetWindowLongPtrW(h, GWLP_USERDATA, LONG_PTR(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
    auto* self = reinterpret_cast<RecordingPill*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (!self) return DefWindowProcW(h, msg, wp, lp);
    if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (msg == WM_LBUTTONUP && self->onStop_) {
        auto stop = self->onStop_;   // l'arrêt cache la pastille
        stop();
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

} // namespace md
