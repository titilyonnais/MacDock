#include "genie_surface.h"

#include <d3d10.h>

#include <algorithm>

#include "../core/log.h"
#include "../glass/backdrop_capture.h"

namespace md {

namespace {
constexpr wchar_t kClass[] = L"MacDockGenieGpu";
}

GenieSurface::~GenieSurface() {
    end();
    reset();
    if (hwnd_) DestroyWindow(hwnd_);
}

void GenieSurface::reset() {
    capture_.stop();
    gpu_ = GenieGpu{};
    swap_.Reset();
    visual_.Reset();
    target_.Reset();
    dcomp_.Reset();
    ctx_.Reset();
    dev_.Reset();
    sw_ = sh_ = 0;
}

bool GenieSurface::prepare(HINSTANCE instance) {
    if (dev_) return true;
    if (failed_ || !WindowCapture::supported()) return false;
    if (!hwnd_) {
        WNDCLASSEXW wc{sizeof wc};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = instance;
        wc.lpszClassName = kClass;
        RegisterClassExW(&wc);
        // Clics traversants (en couches + transparente), jamais activée, hors barre des tâches ; sans surface propre.
        hwnd_ = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW |
                                    WS_EX_NOACTIVATE,
                                kClass, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, instance, nullptr);
        if (!hwnd_) return false;
        SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA);
        SetWindowDisplayAffinity(hwnd_, WDA_EXCLUDEFROMCAPTURE);   // ni le verre du Dock ni les captures ne la voient
    }
    Com<IDXGIDevice> dxgi;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                 D3D11_SDK_VERSION, &dev_, nullptr, &ctx_)) ||
        FAILED(dev_.As(&dxgi)) || FAILED(DCompositionCreateDevice(dxgi.Get(), IID_PPV_ARGS(&dcomp_))) ||
        FAILED(dcomp_->CreateTargetForHwnd(hwnd_, TRUE, &target_)) || FAILED(dcomp_->CreateVisual(&visual_)) ||
        FAILED(target_->SetRoot(visual_.Get())) || !gpu_.init(dev_.Get())) {
        log::warn(L"Génie : rendu GPU indisponible, bandes DWM seulement");
        failed_ = true;
        reset();
        return false;
    }
    Com<ID3D10Multithread> mt;   // la capture se sert du device depuis ses propres fils
    if (SUCCEEDED(dev_.As(&mt))) mt->SetMultithreadProtected(TRUE);
    capture_.prepare(instance);
    return true;
}

bool GenieSurface::begin(HINSTANCE instance, HWND source, const RECT& box) {
    end();
    if (!prepare(instance)) return false;
    const UINT w = UINT(std::max<LONG>(1, box.right - box.left)), h = UINT(std::max<LONG>(1, box.bottom - box.top));
    if (!swap_ || w != sw_ || h != sh_) {
        bool ok = false;
        if (swap_) {
            ok = SUCCEEDED(swap_->ResizeBuffers(2, w, h, DXGI_FORMAT_B8G8R8A8_UNORM, 0));
        } else {
            Com<IDXGIDevice> dxgi;
            Com<IDXGIAdapter> adapter;
            Com<IDXGIFactory2> factory;
            DXGI_SWAP_CHAIN_DESC1 d{};
            d.Width = w;
            d.Height = h;
            d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            d.SampleDesc.Count = 1;
            d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            d.BufferCount = 2;
            d.Scaling = DXGI_SCALING_STRETCH;
            d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
            d.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
            ok = SUCCEEDED(dev_.As(&dxgi)) && SUCCEEDED(dxgi->GetAdapter(&adapter)) &&
                 SUCCEEDED(adapter->GetParent(IID_PPV_ARGS(&factory))) &&
                 SUCCEEDED(factory->CreateSwapChainForComposition(dev_.Get(), &d, nullptr, &swap_)) &&
                 SUCCEEDED(visual_->SetContent(swap_.Get())) && SUCCEEDED(dcomp_->Commit());
        }
        if (!ok) {
            log::warn(L"Génie : chaîne d'échange %ux%u impossible", w, h);
            reset();
            return false;
        }
        sw_ = w;
        sh_ = h;
    }
    box_ = box;
    gpu_.setWhite(BackdropCapture::querySdrWhite(MonitorFromRect(&box, MONITOR_DEFAULTTONEAREST)));
    SetWindowPos(hwnd_, HWND_TOPMOST, box.left, box.top, LONG(w), LONG(h), SWP_NOACTIVATE);
    capturing_ = capture_.start(instance, dev_.Get(), source);   // démarrage de la capture sur un fil : retour immédiat
    return capturing_;
}

bool GenieSurface::frame(const std::vector<GenieVertex>& mesh) {
    if (!swap_ || (!capturing_ && !gpu_.hasSource())) return false;
    if (!gpu_.hasSource()) {
        capture_.poll([&](ID3D11Texture2D* tex, UINT w, UINT h) { gpu_.setSource(ctx_.Get(), tex, w, h); });
        if (!gpu_.hasSource()) return false;
        capture_.stop();   // une seule image suffit : le relais disparaît
        capturing_ = false;
    }
    Com<ID3D11Texture2D> back;
    if (FAILED(swap_->GetBuffer(0, IID_PPV_ARGS(&back))) ||
        !gpu_.draw(ctx_.Get(), back.Get(), sw_, sh_, POINT{box_.left, box_.top}, mesh)) {
        end();
        return false;
    }
    back.Reset();
    const HRESULT hr = swap_->Present(1, 0);
    if (FAILED(hr)) {
        log::warn(L"Génie : présentation impossible (0x%08X), retour aux bandes", unsigned(hr));
        end();
        reset();   // périphérique perdu : recréé à la prochaine animation
        return false;
    }
    if (!frames_) SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ++frames_;
    return true;
}

void GenieSurface::end() {
    if (hwnd_ && frames_) ShowWindow(hwnd_, SW_HIDE);
    frames_ = 0;
    capture_.stop();
    capturing_ = false;
    gpu_.dropSource();
}

} // namespace md
