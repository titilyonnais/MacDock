#include "screen_grab.h"

#include <dwmapi.h>
#include <shellscalingapi.h>
#include <shlobj.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "../core/log.h"

using Microsoft::WRL::ComPtr;

namespace md {

namespace {

// DIB 32 bits de haut en bas, sélectionnée dans un DC mémoire.
struct Dib {
    HDC dc = nullptr;
    HBITMAP bmp = nullptr;
    HGDIOBJ old = nullptr;
    void* bits = nullptr;
    Dib(int w, int h) {
        dc = CreateCompatibleDC(nullptr);
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof bi.bmiHeader;
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        if (dc) bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (bmp) old = SelectObject(dc, bmp);
    }
    ~Dib() {
        if (old) SelectObject(dc, old);
        if (bmp) DeleteObject(bmp);
        if (dc) DeleteDC(dc);
    }
    bool ok() const { return bits != nullptr; }
};

BgraImage fromDib(const Dib& d, int x0, int y0, int cw, int ch, int stridePx) {
    BgraImage img;
    img.w = cw;
    img.h = ch;
    img.px.resize(std::size_t(cw) * ch * 4);
    const auto* src = static_cast<const std::uint8_t*>(d.bits);
    for (int y = 0; y < ch; ++y) {
        std::memcpy(&img.px[std::size_t(y) * cw * 4], src + (std::size_t(y + y0) * stridePx + x0) * 4, std::size_t(cw) * 4);
    }
    for (std::size_t i = 3; i < img.px.size(); i += 4) img.px[i] = 255;   // GDI laisse l'alpha à 0
    return img;
}

bool cloaked(HWND h) {
    DWORD c = 0;
    return SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &c, sizeof c)) && c != 0;
}

bool intersects(const RECT& a, const RECT& b) {
    RECT r;
    return IntersectRect(&r, &a, &b) != FALSE;
}

} // namespace

BgraImage grabScreen(const RECT& r) {
    const int w = r.right - r.left, h = r.bottom - r.top;
    if (w <= 0 || h <= 0) return {};
    Dib dib(w, h);
    HDC screen = GetDC(nullptr);
    const bool ok = dib.ok() && screen && BitBlt(dib.dc, 0, 0, w, h, screen, r.left, r.top, SRCCOPY | CAPTUREBLT);
    if (screen) ReleaseDC(nullptr, screen);
    if (!ok) {
        log::warn(L"Capture d'écran : copie du bureau impossible (%lu)", GetLastError());
        return {};
    }
    return fromDib(dib, 0, 0, w, h, w);
}

RECT windowFrameBounds(HWND window) {
    RECT r{};
    if (FAILED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof r)) || r.right <= r.left)
        GetWindowRect(window, &r);
    return r;
}

double monitorScale(HMONITOR monitor) {
    UINT x = 96, y = 96;
    if (!monitor || FAILED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &x, &y))) x = 96;
    return x / 96.0;
}

int windowCornerRadius(HWND window) {
    if (IsZoomed(window)) return 0;
    DWORD pref = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof pref)) &&
        pref == DWMWCP_DONOTROUND)
        return 0;
    return int(std::lround(8 * monitorScale(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST))));
}

BgraImage grabWindow(HWND window, RECT& frame) {
    RECT wr{};
    if (!GetWindowRect(window, &wr)) return {};
    frame = windowFrameBounds(window);
    const int w = wr.right - wr.left, h = wr.bottom - wr.top;
    if (w <= 0 || h <= 0) return {};
    Dib dib(w, h);
    if (!dib.ok() || !PrintWindow(window, dib.dc, PW_RENDERFULLCONTENT)) {
        log::warn(L"Capture d'écran : vue de la fenêtre refusée (%lu)", GetLastError());
        return {};
    }
    // Bords visibles dans l'image de la fenêtre entière (marge de redimensionnement invisible retirée).
    RECT crop{frame.left - wr.left, frame.top - wr.top, frame.right - wr.left, frame.bottom - wr.top};
    crop.left = std::clamp<LONG>(crop.left, 0, w);
    crop.top = std::clamp<LONG>(crop.top, 0, h);
    crop.right = std::clamp<LONG>(crop.right, crop.left, w);
    crop.bottom = std::clamp<LONG>(crop.bottom, crop.top, h);
    if (crop.right - crop.left <= 0 || crop.bottom - crop.top <= 0) return {};
    return fromDib(dib, crop.left, crop.top, crop.right - crop.left, crop.bottom - crop.top, w);
}

bool windowUnobscured(HWND window, const RECT& frame, const std::function<bool(HWND)>& ignore) {
    for (HWND h = GetWindow(window, GW_HWNDPREV); h; h = GetWindow(h, GW_HWNDPREV)) {
        if (!IsWindowVisible(h) || IsIconic(h) || cloaked(h) || (ignore && ignore(h))) continue;
        if (GetWindowLongPtrW(h, GWL_EXSTYLE) & WS_EX_TRANSPARENT) continue;   // calque décoratif (overlays)
        if (intersects(windowFrameBounds(h), frame)) return false;
    }
    return true;
}

HWND topWindowAt(POINT pt, const std::function<bool(HWND)>& ignore) {
    for (HWND h = GetTopWindow(nullptr); h; h = GetWindow(h, GW_HWNDNEXT)) {
        if (!IsWindowVisible(h) || IsIconic(h) || cloaked(h) || (ignore && ignore(h))) continue;
        if (GetWindowLongPtrW(h, GWL_EXSTYLE) & WS_EX_TRANSPARENT) continue;
        const RECT r = windowFrameBounds(h);
        if (r.right - r.left < 4 || r.bottom - r.top < 4 || !PtInRect(&r, pt)) continue;
        wchar_t cls[64] = {};
        GetClassNameW(h, cls, 64);
        if (!_wcsicmp(cls, L"Progman") || !_wcsicmp(cls, L"WorkerW") || !_wcsicmp(cls, L"Shell_TrayWnd")) return nullptr;
        return h;
    }
    return nullptr;
}

std::wstring desktopFolder() {
    PWSTR p = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &p)) && p) out = p;
    CoTaskMemFree(p);
    return out;
}

bool saveScreenshotPng(const BgraImage& img, const std::wstring& path) {
    if (img.w <= 0 || img.h <= 0 || img.px.size() < std::size_t(img.w) * img.h * 4) return false;
    ComPtr<IWICImagingFactory> wic;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;   // alpha non prémultiplié, comme le PNG
    const UINT stride = UINT(img.w) * 4, size = stride * UINT(img.h);
    const bool ok =
        SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) &&
        SUCCEEDED(wic->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
        SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
        SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
        SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
        SUCCEEDED(frame->SetSize(UINT(img.w), UINT(img.h))) && SUCCEEDED(frame->SetPixelFormat(&fmt)) &&
        IsEqualGUID(fmt, GUID_WICPixelFormat32bppBGRA) &&
        SUCCEEDED(frame->WritePixels(UINT(img.h), stride, size, const_cast<BYTE*>(img.px.data()))) &&
        SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
    if (!ok) {
        stream.Reset();
        DeleteFileW(path.c_str());   // pas de fichier à moitié écrit sur le Bureau
    }
    return ok;
}

bool copyImageToClipboard(HWND owner, const BgraImage& img) {
    if (img.w <= 0 || img.h <= 0 || img.px.size() < std::size_t(img.w) * img.h * 4) return false;
    // CF_DIB de bas en haut (le plus compris des apps), 32 bits sans alpha.
    const std::size_t pixels = std::size_t(img.w) * img.h * 4;
    HGLOBAL dib = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + pixels);
    if (!dib) return false;
    auto* head = static_cast<BITMAPINFOHEADER*>(GlobalLock(dib));
    *head = BITMAPINFOHEADER{};
    head->biSize = sizeof(BITMAPINFOHEADER);
    head->biWidth = img.w;
    head->biHeight = img.h;
    head->biPlanes = 1;
    head->biBitCount = 32;
    head->biCompression = BI_RGB;
    head->biSizeImage = DWORD(pixels);
    auto* dst = reinterpret_cast<std::uint8_t*>(head + 1);
    for (int y = 0; y < img.h; ++y)
        std::memcpy(dst + std::size_t(img.h - 1 - y) * img.w * 4, &img.px[std::size_t(y) * img.w * 4], std::size_t(img.w) * 4);
    GlobalUnlock(dib);
    if (!OpenClipboard(owner)) {
        GlobalFree(dib);
        return false;
    }
    EmptyClipboard();
    const bool ok = SetClipboardData(CF_DIB, dib) != nullptr;
    if (!ok) GlobalFree(dib);
    CloseClipboard();
    return ok;
}

} // namespace md
