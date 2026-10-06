// --capture-test : capture réelle des 400 px du bas de l'écran principal (diagnostic de la capture du verre).
#include "capture_test.h"

#include <d3d11.h>
#include <DirectXPackedVector.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "../calib/png_io.h"
#include "../core/log.h"
#include "../glass/backdrop_capture.h"

namespace md {

namespace {
constexpr UINT kMsg = WM_APP + 7;

std::uint8_t toSrgb8(float linear) {
    linear = std::clamp(linear, 0.0f, 1.0f);
    float s = linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1 / 2.4f) - 0.055f;
    return std::uint8_t(std::lround(s * 255));
}
} // namespace

int runCaptureTest(const std::wstring& outPng) {
    using Microsoft::WRL::ComPtr;
    HMONITOR monitor = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(monitor, &mi);
    IRect region{mi.rcMonitor.left, std::max(mi.rcMonitor.top, mi.rcMonitor.bottom - 400), mi.rcMonitor.right,
                 mi.rcMonitor.bottom};

    HWND sink = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    auto adapter = BackdropCapture::adapterFor(monitor);
    if (!sink || !adapter ||
        FAILED(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev,
                                 nullptr, &ctx))) {
        log::error(L"capture-test : device impossible");
        return 2;
    }

    BackdropCapture capture;
    capture.start(sink, kMsg, monitor, region);
    ComPtr<ID3D11Texture2D> staging;
    bool scRgb = false, got = false;
    float sdrWhite = 1;
    const ULONGLONG deadline = GetTickCount64() + 3000;
    while (!got && GetTickCount64() < deadline) {
        MSG msg;
        if (!PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
            continue;
        }
        if (msg.message != kMsg) {
            DispatchMessageW(&msg);
            continue;
        }
        got = capture.takeLatest(dev.Get(), ctx.Get(),
                                 [&](UINT w, UINT h, bool hdr) -> ID3D11Texture2D* {
                                     D3D11_TEXTURE2D_DESC d{};
                                     d.Width = w;
                                     d.Height = h;
                                     d.MipLevels = d.ArraySize = 1;
                                     d.Format = hdr ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_B8G8R8A8_UNORM;
                                     d.SampleDesc.Count = 1;
                                     d.Usage = D3D11_USAGE_STAGING;
                                     d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                                     staging.Reset();
                                     return SUCCEEDED(dev->CreateTexture2D(&d, nullptr, &staging)) ? staging.Get() : nullptr;
                                 },
                                 scRgb, sdrWhite);
    }
    auto status = capture.status();
    capture.stop();
    DestroyWindow(sink);
    if (!got) {
        log::error(L"capture-test : aucune image en 3 s (statut %d)", int(status));
        return 3;
    }

    D3D11_TEXTURE2D_DESC d{};
    staging->GetDesc(&d);
    D3D11_MAPPED_SUBRESOURCE map{};
    if (FAILED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map))) return 4;
    std::vector<std::uint8_t> bgra(size_t(d.Width) * d.Height * 4);
    for (UINT y = 0; y < d.Height; ++y) {
        auto* row = static_cast<const std::uint8_t*>(map.pData) + size_t(y) * map.RowPitch;
        auto* out = &bgra[size_t(y) * d.Width * 4];
        if (!scRgb) {
            std::copy(row, row + d.Width * 4, out);
            for (UINT x = 0; x < d.Width; ++x) out[x * 4 + 3] = 255;
            continue;
        }
        auto* half = reinterpret_cast<const DirectX::PackedVector::HALF*>(row);
        for (UINT x = 0; x < d.Width; ++x) {
            for (int c = 0; c < 3; ++c)
                out[x * 4 + 2 - c] = toSrgb8(DirectX::PackedVector::XMConvertHalfToFloat(half[x * 4 + c]) / sdrWhite);
            out[x * 4 + 3] = 255;
        }
    }
    ctx->Unmap(staging.Get(), 0);

    // Image noire ou uniforme : la capture n'a pas vu l'écran.
    std::uint64_t sum = 0;
    for (size_t i = 0; i < bgra.size(); i += 4) sum += bgra[i] + bgra[i + 1] + bgra[i + 2];
    double mean = double(sum) / (bgra.size() / 4 * 3);
    log::info(L"capture-test : %ux%u, %s, blanc SDR %.2f, moyenne %.1f", d.Width, d.Height, scRgb ? L"scRGB" : L"BGRA8",
              sdrWhite, mean);
    if (!writePng(outPng, bgra.data(), d.Width, d.Height)) return 5;
    return mean < 1 ? 6 : 0;
}

} // namespace md
