#include "backdrop_capture.h"

#include <d3d11_1.h>
#include <dxgi1_6.h>

#include <vector>

#include "../core/log.h"

#pragma comment(lib, "dxgi.lib")

namespace md {

namespace {
IRect toIRect(const RECT& r) { return {r.left, r.top, r.right, r.bottom}; }
} // namespace

Microsoft::WRL::ComPtr<IDXGIAdapter1> BackdropCapture::adapterFor(HMONITOR monitor) {
    Com<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return nullptr;
    Com<IDXGIAdapter1> adapter;
    for (UINT a = 0; factory->EnumAdapters1(a, adapter.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; ++a) {
        Com<IDXGIOutput> output;
        for (UINT o = 0; adapter->EnumOutputs(o, output.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; ++o) {
            DXGI_OUTPUT_DESC d{};
            if (SUCCEEDED(output->GetDesc(&d)) && d.Monitor == monitor) return adapter;
        }
    }
    return nullptr;
}

float BackdropCapture::querySdrWhite(HMONITOR monitor) {
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof mi;
    if (!GetMonitorInfoW(monitor, &mi)) return 1;
    UINT32 nPaths = 0, nModes = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &nPaths, &nModes) != ERROR_SUCCESS) return 1;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(nPaths);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(nModes);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &nPaths, paths.data(), &nModes, modes.data(), nullptr) != ERROR_SUCCESS)
        return 1;
    for (UINT32 i = 0; i < nPaths; ++i) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        source.header.size = sizeof source;
        source.header.adapterId = paths[i].sourceInfo.adapterId;
        source.header.id = paths[i].sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS) continue;
        if (wcscmp(source.viewGdiDeviceName, mi.szDevice) != 0) continue;
        DISPLAYCONFIG_SDR_WHITE_LEVEL white{};
        white.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
        white.header.size = sizeof white;
        white.header.adapterId = paths[i].targetInfo.adapterId;
        white.header.id = paths[i].targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&white.header) == ERROR_SUCCESS) return sdrWhiteScale(white.SDRWhiteLevel);
    }
    return 1;
}

bool BackdropCapture::start(HWND notify, UINT notifyMsg, HMONITOR monitor, IRect regionScreen) {
    stop();
    notify_ = notify;
    notifyMsg_ = notifyMsg;
    monitor_ = monitor;
    {
        std::lock_guard g(lock_);
        region_ = regionScreen;
        regionChanged_ = true;
        hasFrame_ = false;
    }
    pending_ = false;
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stopEvent_) return false;
    status_ = Status::Off;
    thread_ = std::thread([this] { run(); });
    return true;
}

void BackdropCapture::setRegion(IRect regionScreen) {
    std::lock_guard g(lock_);
    region_ = regionScreen;
    regionChanged_ = true;
}

void BackdropCapture::stop() {
    if (thread_.joinable()) {
        SetEvent(stopEvent_);
        thread_.join();
    }
    if (stopEvent_) CloseHandle(stopEvent_);
    stopEvent_ = nullptr;
    releaseShared();
    uiShared_.Reset();
    uiMutex_.Reset();
    uiGeneration_ = 0;
    uiDevice_ = nullptr;
    status_ = Status::Off;
}

void BackdropCapture::releaseShared() {
    std::lock_guard g(lock_);
    if (sharedHandle_) CloseHandle(sharedHandle_);
    sharedHandle_ = nullptr;
    hasFrame_ = false;
}

void BackdropCapture::run() {
    CaptureBackoff backoff;
    bool unavailableLogged = false;
    auto waitStop = [&](DWORD ms) { return WaitForSingleObject(stopEvent_, ms) == WAIT_OBJECT_0; };

    while (!waitStop(0)) {
        // Device sur la carte de l'écran, puis duplication (HDR : RGBA16F accepté).
        Com<IDXGIAdapter1> adapter = adapterFor(monitor_);
        Com<ID3D11Device> dev;
        Com<IDXGIOutputDuplication> dup;
        IRect outputDesktop{};
        bool ok = false;
        if (adapter && SUCCEEDED(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                                                   D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &dev,
                                                   nullptr, nullptr))) {
            Com<IDXGIOutput> output;
            for (UINT o = 0; adapter->EnumOutputs(o, output.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; ++o) {
                DXGI_OUTPUT_DESC d{};
                if (FAILED(output->GetDesc(&d)) || d.Monitor != monitor_) continue;
                outputDesktop = toIRect(d.DesktopCoordinates);
                Com<IDXGIOutput5> out5;
                Com<IDXGIOutput1> out1;
                const DXGI_FORMAT formats[] = {DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_R16G16B16A16_FLOAT};
                HRESULT hr = E_FAIL;
                if (SUCCEEDED(output.As(&out5))) hr = out5->DuplicateOutput1(dev.Get(), 0, 2, formats, &dup);
                if (FAILED(hr) && SUCCEEDED(output.As(&out1))) hr = out1->DuplicateOutput(dev.Get(), &dup);
                if (SUCCEEDED(hr)) {
                    DXGI_OUTDUPL_DESC dd{};
                    dup->GetDesc(&dd);
                    if (rotationSupported(int(dd.Rotation))) ok = true;
                    else log::warn(L"Capture : écran tourné, non pris en charge (verre dépoli)");
                } else if (!unavailableLogged) {
                    log::warn(L"Capture : duplication impossible (0x%08X), verre dépoli en attendant", hr);
                }
                break;
            }
        }
        if (!ok) {
            status_ = Status::Unavailable;
            unavailableLogged = true;
            if (waitStop(backoff.nextDelayMs())) break;
            continue;
        }
        sdrWhite_ = querySdrWhite(monitor_);
        {
            std::lock_guard g(lock_);
            regionChanged_ = true;   // nouvelle duplication : copie complète
        }
        if (unavailableLogged) log::info(L"Capture : reprise");
        unavailableLogged = false;
        backoff.reset();
        status_ = Status::Running;

        bool lost = captureLoop(dev, dup, outputDesktop);
        shared_.Reset();
        sharedMutex_.Reset();
        if (!lost) break;   // arrêt demandé
        status_ = Status::Unavailable;
        if (waitStop(backoff.nextDelayMs())) break;
    }
    status_ = Status::Off;
}

// Boucle d'acquisition ; true si la duplication est perdue (à recréer), false si l'arrêt est demandé.
bool BackdropCapture::captureLoop(Com<ID3D11Device>& dev, Com<IDXGIOutputDuplication>& dup, const IRect& outputDesktop) {
    Com<ID3D11DeviceContext> ctx;
    dev->GetImmediateContext(&ctx);
    std::vector<BYTE> meta;
    bool desktopValid = false;   // la surface de duplication ne contient l'image qu'après une vraie présentation

    for (;;) {
        if (WaitForSingleObject(stopEvent_, 0) == WAIT_OBJECT_0) return false;
        DXGI_OUTDUPL_FRAME_INFO info{};
        Com<IDXGIResource> resource;
        HRESULT hr = dup->AcquireNextFrame(100, &info, &resource);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) continue;
        if (FAILED(hr)) {
            if (hr != DXGI_ERROR_ACCESS_LOST) log::warn(L"Capture : AcquireNextFrame a échoué (0x%08X)", hr);
            return true;
        }

        if (info.LastPresentTime.QuadPart != 0) desktopValid = true;
        IRect regionScreen;
        bool changed;
        {
            std::lock_guard g(lock_);
            regionScreen = region_;
            changed = regionChanged_ || !hasFrame_;
        }
        IRect r = toOutputRect(regionScreen, outputDesktop);
        Com<ID3D11Texture2D> frame;
        bool copy = false;
        if (r.right > r.left && SUCCEEDED(resource.As(&frame)) && desktopValid &&
            (info.LastPresentTime.QuadPart != 0 || changed)) {
            D3D11_TEXTURE2D_DESC fd{};
            frame->GetDesc(&fd);
            const UINT w = UINT(r.right - r.left), h = UINT(r.bottom - r.top);

            // Texture partagée à la taille de la région et au format de la duplication.
            if (!shared_ || sharedFormat_ != fd.Format || sharedW_ != w || sharedH_ != h) {
                shared_.Reset();
                sharedMutex_.Reset();
                D3D11_TEXTURE2D_DESC sd{};
                sd.Width = w;
                sd.Height = h;
                sd.MipLevels = sd.ArraySize = 1;
                sd.Format = fd.Format;
                sd.SampleDesc.Count = 1;
                sd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                sd.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
                Com<IDXGIResource1> res1;
                HANDLE handle = nullptr;
                if (FAILED(dev->CreateTexture2D(&sd, nullptr, &shared_)) || FAILED(shared_.As(&sharedMutex_)) ||
                    FAILED(shared_.As(&res1)) ||
                    FAILED(res1->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                                                    nullptr, &handle))) {
                    log::warn(L"Capture : texture partagée impossible");
                    dup->ReleaseFrame();
                    return true;
                }
                std::lock_guard g(lock_);
                if (sharedHandle_) CloseHandle(sharedHandle_);
                sharedHandle_ = handle;
                sharedW_ = w;
                sharedH_ = h;
                sharedScRgb_ = fd.Format == DXGI_FORMAT_R16G16B16A16_FLOAT;
                sharedFormat_ = fd.Format;
                ++generation_;
                hasFrame_ = false;
                changed = true;
            }

            if (changed) {
                copy = true;
            } else if (info.TotalMetadataBufferSize > 0) {
                // Seules les zones modifiées ou déplacées qui touchent la région déclenchent une copie.
                meta.resize(info.TotalMetadataBufferSize);
                UINT used = 0;
                std::vector<IRect> rects;
                if (SUCCEEDED(dup->GetFrameMoveRects(UINT(meta.size()), reinterpret_cast<DXGI_OUTDUPL_MOVE_RECT*>(meta.data()),
                                                     &used)))
                    for (UINT i = 0; i < used / sizeof(DXGI_OUTDUPL_MOVE_RECT); ++i)
                        rects.push_back(toIRect(reinterpret_cast<DXGI_OUTDUPL_MOVE_RECT*>(meta.data())[i].DestinationRect));
                used = 0;
                if (SUCCEEDED(dup->GetFrameDirtyRects(UINT(meta.size()), reinterpret_cast<RECT*>(meta.data()), &used)))
                    for (UINT i = 0; i < used / sizeof(RECT); ++i) rects.push_back(toIRect(reinterpret_cast<RECT*>(meta.data())[i]));
                copy = anyIntersects(r, rects.data(), rects.size());
            }

            if (copy && SUCCEEDED(sharedMutex_->AcquireSync(0, 100))) {
                D3D11_BOX box{UINT(r.left), UINT(r.top), 0, UINT(r.right), UINT(r.bottom), 1};
                ctx->CopySubresourceRegion(shared_.Get(), 0, 0, 0, 0, frame.Get(), 0, &box);
                sharedMutex_->ReleaseSync(0);
                ctx->Flush();
                {
                    std::lock_guard g(lock_);
                    regionChanged_ = false;
                    hasFrame_ = true;
                }
                if (!pending_.exchange(true)) PostMessageW(notify_, notifyMsg_, 0, 0);
            }
        }
        dup->ReleaseFrame();
    }
}

bool BackdropCapture::takeLatest(ID3D11Device* uiDevice, ID3D11DeviceContext* ctx, const DestinationFn& dst, bool& scRgb,
                                 float& sdrWhite) {
    pending_ = false;
    if (!uiDevice || !ctx) return false;
    UINT w, h;
    {
        std::lock_guard g(lock_);
        if (!hasFrame_ || !sharedHandle_) return false;
        if (uiGeneration_ != generation_ || uiDevice_ != uiDevice || !uiShared_) {
            uiShared_.Reset();
            uiMutex_.Reset();
            Com<ID3D11Device1> dev1;
            if (FAILED(uiDevice->QueryInterface(IID_PPV_ARGS(&dev1))) ||
                FAILED(dev1->OpenSharedResource1(sharedHandle_, IID_PPV_ARGS(&uiShared_))) || FAILED(uiShared_.As(&uiMutex_))) {
                uiShared_.Reset();
                uiMutex_.Reset();
                log::warn(L"Capture : ouverture de la texture partagée impossible");
                return false;
            }
            uiGeneration_ = generation_;
            uiDevice_ = uiDevice;
        }
        w = sharedW_;
        h = sharedH_;
        scRgb = sharedScRgb_;
    }
    sdrWhite = sdrWhite_.load();
    ID3D11Texture2D* target = dst(w, h, scRgb);
    if (!target) return false;
    HRESULT hr = uiMutex_->AcquireSync(0, 5);
    if (hr != S_OK) {
        // Le thread de capture copie en ce moment : on redemande un passage.
        if (!pending_.exchange(true)) PostMessageW(notify_, notifyMsg_, 0, 0);
        return false;
    }
    ctx->CopyResource(target, uiShared_.Get());
    uiMutex_->ReleaseSync(0);
    return true;
}

} // namespace md
