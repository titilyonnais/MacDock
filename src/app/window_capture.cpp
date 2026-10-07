#include "window_capture.h"

#include <d3d10.h>
#include <dwmapi.h>
#include <dxgi.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <wrl/client.h>

#include <DirectXPackedVector.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <vector>

#include "../anim/genie.h"
#include "../anim/genie_preview.h"
#include "../calib/png_io.h"
#include "../core/log.h"
#include "genie_gpu.h"

namespace md {

namespace {

namespace wgc = winrt::Windows::Graphics::Capture;
namespace wdx = winrt::Windows::Graphics::DirectX;
namespace d3d = winrt::Windows::Graphics::DirectX::Direct3D11;
template <class T> using Com = Microsoft::WRL::ComPtr<T>;

constexpr wchar_t kRelayClass[] = L"MacDockGenieRelay";

// À gauche de tous les écrans : jamais visible, quel que soit l'agencement.
POINT offscreen(SIZE size) {
    return POINT{GetSystemMetrics(SM_XVIRTUALSCREEN) - size.cx - 256, GetSystemMetrics(SM_YVIRTUALSCREEN)};
}

} // namespace

struct WindowCapture::Impl {
    HWND relay = nullptr;
    HTHUMBNAIL thumb = nullptr;
    SIZE size{};
    wgc::GraphicsCaptureItem item{nullptr};
    wgc::Direct3D11CaptureFramePool pool{nullptr};
    wgc::GraphicsCaptureSession session{nullptr};

    ~Impl() { close(); }

    void close() {
        try {
            if (session) session.Close();
            if (pool) pool.Close();
        } catch (...) {
        }
        session = nullptr;
        pool = nullptr;
        item = nullptr;
        if (thumb) DwmUnregisterThumbnail(thumb);
        thumb = nullptr;
        if (relay) ShowWindow(relay, SW_HIDE);   // gardé pour la prochaine fois
    }

    bool ensureRelay(HINSTANCE instance) {
        if (relay) return true;
        WNDCLASSEXW wc{sizeof wc};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = instance;
        wc.lpszClassName = kRelayClass;
        RegisterClassExW(&wc);
        // Ni activable, ni dans la barre des tâches ou Alt+Tab, sans surface propre : seule la miniature y vit.
        relay = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP, kRelayClass, L"", WS_POPUP, 0, 0,
                                1, 1, nullptr, nullptr, instance, nullptr);
        return relay != nullptr;
    }
};

WindowCapture::WindowCapture() : impl_(std::make_unique<Impl>()) {}
WindowCapture::~WindowCapture() = default;

bool WindowCapture::supported() {
    static const bool ok = [] {
        try {
            return winrt::Windows::Foundation::Metadata::ApiInformation::IsTypePresent(L"Windows.Graphics.Capture.GraphicsCaptureSession") &&
                   wgc::GraphicsCaptureSession::IsSupported();
        } catch (...) {
            return false;
        }
    }();
    return ok;
}

bool WindowCapture::active() const { return impl_->session != nullptr; }

void WindowCapture::stop() { impl_->close(); }

bool WindowCapture::start(HINSTANCE instance, ID3D11Device* dev, HWND source) {
    stop();
    Impl& m = *impl_;
    if (!dev || !IsWindow(source) || !supported() || !m.ensureRelay(instance)) return false;
    if (FAILED(DwmRegisterThumbnail(m.relay, source, &m.thumb))) {
        m.thumb = nullptr;
        return false;
    }
    if (FAILED(DwmQueryThumbnailSourceSize(m.thumb, &m.size)) || m.size.cx <= 0 || m.size.cy <= 0) {
        stop();
        return false;
    }
    const POINT at = offscreen(m.size);
    SetWindowPos(m.relay, HWND_BOTTOM, at.x, at.y, m.size.cx, m.size.cy, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    DWM_THUMBNAIL_PROPERTIES p{};
    p.dwFlags = DWM_TNP_VISIBLE | DWM_TNP_RECTDESTINATION | DWM_TNP_OPACITY | DWM_TNP_SOURCECLIENTAREAONLY;
    p.fVisible = TRUE;
    p.opacity = 255;
    p.fSourceClientAreaOnly = FALSE;
    p.rcDestination = RECT{0, 0, m.size.cx, m.size.cy};
    DwmUpdateThumbnailProperties(m.thumb, &p);
    try {
        Com<IDXGIDevice> dxgi;
        winrt::com_ptr<::IInspectable> insp;
        if (FAILED(dev->QueryInterface(IID_PPV_ARGS(&dxgi))) || FAILED(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(), insp.put()))) {
            stop();
            return false;
        }
        const auto device = insp.as<d3d::IDirect3DDevice>();
        auto interop = winrt::get_activation_factory<wgc::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        if (FAILED(interop->CreateForWindow(m.relay, winrt::guid_of<wgc::GraphicsCaptureItem>(), winrt::put_abi(m.item)))) {
            stop();
            return false;
        }
        // scRGB : sur un écran HDR, une capture 8 bits écrête le blanc SDR (couleurs délavées, × 3 environ).
        m.pool = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(device, wdx::DirectXPixelFormat::R16G16B16A16Float, 2,
                                                                     m.item.Size());
        m.session = m.pool.CreateCaptureSession(m.item);
        try {
            m.session.IsBorderRequired(false);   // Windows 11 : pas de cadre jaune (le relais est hors écran de toute façon)
            m.session.IsCursorCaptureEnabled(false);
        } catch (...) {
        }
        m.session.StartCapture();
        return true;
    } catch (const winrt::hresult_error& e) {
        log::warn(L"Génie : capture impossible (0x%08X)", unsigned(e.code()));
    } catch (...) {
    }
    stop();
    return false;
}

bool WindowCapture::poll(const std::function<void(ID3D11Texture2D*, UINT, UINT)>& use) {
    Impl& m = *impl_;
    if (!m.pool) return false;
    try {
        wgc::Direct3D11CaptureFrame frame{nullptr};
        for (auto next = m.pool.TryGetNextFrame(); next; next = m.pool.TryGetNextFrame()) frame = next;   // la plus récente
        if (!frame) return false;
        auto access = frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        Com<ID3D11Texture2D> tex;
        if (FAILED(access->GetInterface(IID_PPV_ARGS(&tex)))) return false;
        D3D11_TEXTURE2D_DESC d{};
        tex->GetDesc(&d);
        const UINT w = std::min<UINT>(d.Width, UINT(std::min(m.size.cx, LONG(frame.ContentSize().Width))));
        const UINT h = std::min<UINT>(d.Height, UINT(std::min(m.size.cy, LONG(frame.ContentSize().Height))));
        if (!w || !h) return false;
        use(tex.Get(), w, h);
        frame.Close();
        return true;
    } catch (...) {
        return false;
    }
}

namespace {

constexpr wchar_t kProbeClass[] = L"MacDockCaptureProbe";

double nowMs() {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return double(c.QuadPart) * 1000.0 / double(f.QuadPart);
}

void pump(double ms) {
    const double end = nowMs() + ms;
    while (nowMs() < end) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
        Sleep(1);
    }
}

} // namespace

bool genieCaptureProbe(HINSTANCE instance, const std::wstring& png) {
    // Fenêtre factice en couches (contenu posé par UpdateLayeredWindow, donc défini même hors écran).
    const BgraImage look = syntheticWindow(640, 400);
    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = instance;
    wc.lpszClassName = kProbeClass;
    RegisterClassExW(&wc);
    HWND probe = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED, kProbeClass, L"Sonde", WS_POPUP, 0, 0, look.w,
                                 look.h, nullptr, nullptr, instance, nullptr);
    if (!probe) return false;
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader = {sizeof bi.bmiHeader, look.w, -look.h, 1, 32, BI_RGB};
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bits) std::memcpy(bits, look.px.data(), look.px.size());   // opaque : prémultiplié tel quel
    HGDIOBJ old = SelectObject(mem, dib);
    POINT at = offscreen(SIZE{look.w * 3, look.h});
    SIZE size{look.w, look.h};
    POINT zero{0, 0};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(probe, screen, &at, &size, mem, &zero, 0, &blend, ULW_ALPHA);
    SelectObject(mem, old);
    DeleteObject(dib);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    ShowWindow(probe, SW_SHOWNOACTIVATE);
    pump(100);
    ShowWindow(probe, SW_SHOWMINNOACTIVE);
    pump(100);
    const bool iconic = IsIconic(probe) != FALSE;

    Com<ID3D11Device> dev;
    Com<ID3D11DeviceContext> ctx;
    bool ok = false;
    if (SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                    D3D11_SDK_VERSION, &dev, nullptr, &ctx))) {
        Com<ID3D10Multithread> mt;
        if (SUCCEEDED(dev.As(&mt))) mt->SetMultithreadProtected(TRUE);
        WindowCapture cap;
        GenieGpu gpu;
        const double t0 = nowMs();
        if (gpu.init(dev.Get()) && cap.start(instance, dev.Get(), probe)) {
            UINT w = 0, h = 0;
            while (!gpu.hasSource() && nowMs() - t0 < 1000) {
                cap.poll([&](ID3D11Texture2D* tex, UINT tw, UINT th) {
                    w = tw;
                    h = th;
                    gpu.setSource(ctx.Get(), tex, tw, th);
                });
                if (!gpu.hasSource()) pump(1);
            }
            const double waited = nowMs() - t0;
            cap.stop();
            log::info(L"Sonde de capture : réduite %d, image %ux%u en %.1f ms", int(iconic), w, h, waited);
            if (gpu.hasSource()) {
                const UINT ow = 900, oh = 700;
                const auto mesh = genieMesh(MinimizeEffect::Genie, SIZE{LONG(w), LONG(h)}, RECT{130, 20, 130 + LONG(w), 20 + LONG(h)},
                                            RECT{426, 620, 474, 668}, DockPosition::Bottom, 0.5, 192);
                D3D11_TEXTURE2D_DESC d{};
                d.Width = ow;
                d.Height = oh;
                d.MipLevels = d.ArraySize = 1;
                d.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                d.SampleDesc.Count = 1;
                d.BindFlags = D3D11_BIND_RENDER_TARGET;
                Com<ID3D11Texture2D> dst, stage;
                dev->CreateTexture2D(&d, nullptr, &dst);
                d.BindFlags = 0;
                d.Usage = D3D11_USAGE_STAGING;
                d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                dev->CreateTexture2D(&d, nullptr, &stage);
                D3D11_MAPPED_SUBRESOURCE map{};
                if (dst && stage && gpu.draw(ctx.Get(), dst.Get(), ow, oh, POINT{0, 0}, mesh)) {
                    ctx->CopyResource(stage.Get(), dst.Get());
                    if (SUCCEEDED(ctx->Map(stage.Get(), 0, D3D11_MAP_READ, 0, &map))) {
                        // scRGB → sRGB 8 bits, ramené au blanc de la fenêtre (le plus clair de l'image).
                        using namespace DirectX::PackedVector;
                        std::vector<float> lin(std::size_t(ow) * oh * 4);
                        float white = 1e-3f;
                        for (UINT y = 0; y < oh; ++y) {
                            const auto* row = reinterpret_cast<const HALF*>(static_cast<const std::uint8_t*>(map.pData) + std::size_t(y) * map.RowPitch);
                            for (UINT i = 0; i < ow * 4; ++i) {
                                lin[std::size_t(y) * ow * 4 + i] = XMConvertHalfToFloat(row[i]);
                                if (i % 4 != 3) white = std::max(white, lin[std::size_t(y) * ow * 4 + i]);
                            }
                        }
                        ctx->Unmap(stage.Get(), 0);
                        log::info(L"Sonde de capture : blanc scRGB %.2f", white);
                        const auto srgb = [](float c) {
                            c = std::clamp(c, 0.0f, 1.0f);
                            return std::uint8_t(std::lround(255 * (c <= 0.0031308f ? 12.92f * c : 1.055f * std::pow(c, 1 / 2.4f) - 0.055f)));
                        };
                        std::vector<std::uint8_t> out(lin.size());
                        for (std::size_t i = 0; i < lin.size(); i += 4) {
                            const float a = lin[i + 3];
                            for (int c = 0; c < 3; ++c) out[i + 2 - c] = srgb(a > 0 ? lin[i + c] / a / white : 0);   // RGBA → BGRA, non prémultiplié
                            out[i + 3] = std::uint8_t(std::lround(std::clamp(a, 0.0f, 1.0f) * 255));
                        }
                        ok = writePng(png, out.data(), ow, oh);
                    }
                }
            }
        }
    }
    DestroyWindow(probe);
    return ok;
}

} // namespace md
