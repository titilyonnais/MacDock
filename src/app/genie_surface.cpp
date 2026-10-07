#include "genie_surface.h"

#include <d3d10.h>

#include <algorithm>

#include "../core/diag.h"
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
        if (!diagnosticCapture())   // ni le verre du Dock ni les captures ne la voient
            SetWindowDisplayAffinity(hwnd_, WDA_EXCLUDEFROMCAPTURE);
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

bool GenieSurface::warm(HINSTANCE instance, HWND source, const RECT* box) {
    if (capturing_ && warmSource_ == source) return true;
    end();
    if (dev_ && FAILED(dev_->GetDeviceRemovedReason())) reset();
    if (!prepare(instance)) return false;
    // Réduction annoncée : chaîne d'échange à sa taille et fenêtre affichée d'avance, transparente (rien ne se voit,
    // rien de figé ne passe par-dessus la fenêtre vivante) ; begin() n'aura plus qu'à dessiner et présenter.
    if (box && ensureSwap(*box)) {
        Com<ID3D11Texture2D> back;
        if (SUCCEEDED(swap_->GetBuffer(0, IID_PPV_ARGS(&back))) &&
            gpu_.draw(ctx_.Get(), back.Get(), sw_, sh_, POINT{box_.left, box_.top}, {}) && SUCCEEDED(swap_->Present(0, 0))) {
            SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            shown_ = true;
        }
    }
    capturing_ = capture_.start(instance, dev_.Get(), source);   // sur un fil : retour immédiat
    if (capturing_) gpu_.setWhite(BackdropCapture::querySdrWhite(capture_.monitor()));
    warmSource_ = capturing_ ? source : nullptr;
    warmDrawn_ = false;
    if (diagnosticCapture()) log::info(L"[diag] génie GPU : capture d'avance %s", capturing_ ? L"lancée" : L"impossible");
    return capturing_;
}

void GenieSurface::pump(const std::vector<GenieVertex>& mesh) {
    if (!warmSource_ || !capturing_ || !swap_) return;
    const bool fresh = capture_.poll([&](ID3D11Texture2D* tex, UINT w, UINT h) { gpu_.setSource(ctx_.Get(), tex, w, h); });
    if (!fresh || warmDrawn_ || !shown_) return;
    // Tracé à blanc, jamais présenté : textures, shaders et pilote sont chauds quand la réduction part.
    LARGE_INTEGER fq, a, b;
    QueryPerformanceFrequency(&fq);
    QueryPerformanceCounter(&a);
    Com<ID3D11Texture2D> back;
    if (SUCCEEDED(swap_->GetBuffer(0, IID_PPV_ARGS(&back))))
        gpu_.draw(ctx_.Get(), back.Get(), sw_, sh_, POINT{box_.left, box_.top}, mesh);
    ctx_->Flush();
    warmDrawn_ = true;
    QueryPerformanceCounter(&b);
    if (diagnosticCapture())
        log::info(L"[diag] génie GPU : tracé à blanc en %.1f ms", double(b.QuadPart - a.QuadPart) * 1000.0 / double(fq.QuadPart));
}

void GenieSurface::cool() {
    if (warmSource_) end();
}

bool GenieSurface::begin(HINSTANCE instance, HWND source, const RECT& box) {
    const bool warmed = capturing_ && warmSource_ == source;   // capture déjà lancée : ses images sont là ou arrivent
    warmSource_ = nullptr;
    if (!warmed) end();
    if (!warmed && dev_ && FAILED(dev_->GetDeviceRemovedReason())) {   // perdu au repos (pilote, veille, changement de carte)
        log::info(L"Génie : périphérique GPU perdu, recréé");
        reset();
    }
    if (!prepare(instance) || !ensureSwap(box)) return false;
    if (diagnosticCapture()) log::info(L"[diag] génie GPU : départ, capture %s", warmed ? L"reprise" : L"à lancer");
    if (warmed) return true;
    capturing_ = capture_.start(instance, dev_.Get(), source);   // démarrage de la capture sur un fil : retour immédiat
    // La capture scRGB porte le blanc SDR de l'écran du relais (le plus à gauche), pas celui de l'animation.
    if (capturing_) gpu_.setWhite(BackdropCapture::querySdrWhite(capture_.monitor()));
    return capturing_;
}

bool GenieSurface::ensureSwap(const RECT& box) {
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
        if (shown_) {   // tampons neufs, contenu indéfini : cachée jusqu'à la prochaine image présentée
            ShowWindow(hwnd_, SW_HIDE);
            shown_ = false;
        }
    }
    if (!EqualRect(&box, &box_)) {
        box_ = box;
        SetWindowPos(hwnd_, HWND_TOPMOST, box.left, box.top, LONG(w), LONG(h), SWP_NOACTIVATE);
    }
    return true;
}

bool GenieSurface::frame(const std::vector<GenieVertex>& mesh) {
    if (!swap_ || (!capturing_ && !gpu_.hasSource())) return false;
    LARGE_INTEGER fq, f0, f1, f2, f3;
    QueryPerformanceFrequency(&fq);
    QueryPerformanceCounter(&f0);
    f1 = f2 = f0;
    // Image prise pendant l'appui (fenêtre encore à l'écran) si elle est là : après le relâchement, Windows retire la
    // fenêtre et la capture ne la montre plus.
    if (capturing_ && !gpu_.hasSource()) {
        capture_.poll([&](ID3D11Texture2D* tex, UINT w, UINT h) { gpu_.setSource(ctx_.Get(), tex, w, h); });
        if (!gpu_.hasSource()) return false;
        QueryPerformanceCounter(&f1);
        if (diagnosticCapture())
            log::info(L"[diag] génie GPU : image de la capture reçue (préparée en %.1f ms)",
                      double(f1.QuadPart - f0.QuadPart) * 1000.0 / double(fq.QuadPart));
        capturing_ = false;   // une seule image suffit ; l'arrêt (une dizaine de ms) attend la passation
    } else if (frames_ == 3) {
        capture_.stop();   // le relais disparaît
        capturing_ = false;
    }
    Com<ID3D11Texture2D> back;
    if (FAILED(swap_->GetBuffer(0, IID_PPV_ARGS(&back))) ||
        !gpu_.draw(ctx_.Get(), back.Get(), sw_, sh_, POINT{box_.left, box_.top}, mesh)) {
        back.Reset();
        end();
        if (FAILED(dev_->GetDeviceRemovedReason())) reset();   // recréé à la prochaine animation
        return false;
    }
    back.Reset();
    QueryPerformanceCounter(&f2);
    const HRESULT hr = swap_->Present(1, 0);
    QueryPerformanceCounter(&f3);
    if (diagnosticCapture() && frames_ < 3)
        log::info(L"[diag] génie GPU : image %d dessinée en %.1f ms, présentée en %.1f ms", frames_ + 1,
                  double(f2.QuadPart - f1.QuadPart) * 1000.0 / double(fq.QuadPart),
                  double(f3.QuadPart - f2.QuadPart) * 1000.0 / double(fq.QuadPart));
    if (FAILED(hr)) {
        log::warn(L"Génie : présentation impossible (0x%08X), retour aux bandes", unsigned(hr));
        end();
        reset();   // périphérique perdu : recréé à la prochaine animation
        return false;
    }
    // Au premier plan à la première image (affichée d'avance, le Dock a pu repasser devant depuis).
    if (!frames_)
        SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | (shown_ ? 0 : SWP_SHOWWINDOW));
    shown_ = true;
    ++frames_;
    return true;
}

void GenieSurface::end() {
    if (hwnd_ && shown_) ShowWindow(hwnd_, SW_HIDE);
    shown_ = false;
    warmDrawn_ = false;
    frames_ = 0;
    capture_.stop();
    capturing_ = false;
    warmSource_ = nullptr;
    gpu_.dropSource();
}

} // namespace md
