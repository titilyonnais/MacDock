#include "sprite_window.h"

#include <cstring>

#include "../core/log.h"

namespace md {

namespace {
constexpr wchar_t kClass[] = L"MacDockSprite";
}

SpriteWindow::~SpriteWindow() {
    if (dc_) {
        if (old_) SelectObject(dc_, old_);
        DeleteDC(dc_);
    }
    if (dib_) DeleteObject(dib_);
    if (hwnd_) DestroyWindow(hwnd_);
}

bool SpriteWindow::create(HINSTANCE instance) {
    if (hwnd_) return true;
    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = instance;
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);   // déjà enregistrée : sans effet
    hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                            kClass, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, instance, nullptr);
    if (!hwnd_) {
        log::warn(L"Fenêtre de sprite impossible (%lu)", GetLastError());
        return false;
    }
    // Le sprite passe au-dessus du Dock : sans exclusion, le verre se redessinerait à chaque mouvement.
    SetWindowDisplayAffinity(hwnd_, WDA_EXCLUDEFROMCAPTURE);
    dc_ = CreateCompatibleDC(nullptr);
    return dc_ != nullptr;
}

bool SpriteWindow::ensureSurface(UINT w, UINT h) {
    if (dib_ && w == w_ && h == h_) return true;
    if (old_) SelectObject(dc_, old_);
    old_ = nullptr;
    if (dib_) DeleteObject(dib_);
    dib_ = nullptr;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = LONG(w);
    bi.bmiHeader.biHeight = -LONG(h);   // de haut en bas
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    dib_ = CreateDIBSection(dc_, &bi, DIB_RGB_COLORS, &bits_, nullptr, 0);
    if (!dib_) return false;
    old_ = SelectObject(dc_, dib_);
    w_ = w;
    h_ = h;
    return true;
}

void SpriteWindow::show(const std::vector<std::uint8_t>& bgra, UINT w, UINT h, POINT topLeft) {
    if (!hwnd_ || !w || !h || bgra.size() < size_t(w) * h * 4 || !ensureSurface(w, h)) return;
    std::memcpy(bits_, bgra.data(), size_t(w) * h * 4);
    POINT src{0, 0};
    SIZE size{LONG(w), LONG(h)};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(hwnd_, nullptr, &topLeft, &size, dc_, &src, 0, &blend, ULW_ALPHA);
    if (!visible_) ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    visible_ = true;
}

void SpriteWindow::move(POINT topLeft) {
    if (!hwnd_ || !visible_) return;
    UpdateLayeredWindow(hwnd_, nullptr, &topLeft, nullptr, nullptr, nullptr, 0, nullptr, 0);
}

void SpriteWindow::hide() {
    if (hwnd_ && visible_) ShowWindow(hwnd_, SW_HIDE);
    visible_ = false;
}

} // namespace md
