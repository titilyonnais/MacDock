#include "backdrop_sampler.h"

#include "../core/log.h"
#include "bar_color.h"

namespace md {

bool BackdropSampler::start(HWND notify, UINT notifyMsg, HMONITOR monitor, const RECT& strip) {
    stop();
    sawBlack_ = false;
    running_ = capture_.start(notify, notifyMsg, monitor, IRect{strip.left, strip.top, strip.right, strip.bottom});
    return running_;
}

void BackdropSampler::stop() {
    capture_.stop();
    running_ = false;
}

bool BackdropSampler::failed() const {
    auto s = capture_.status();
    return s == BackdropCapture::Status::Unavailable || s == BackdropCapture::Status::Failed;
}

std::optional<double> BackdropSampler::take(ID3D11Device* dev) {
    if (!running_ || !dev) return std::nullopt;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> ctx;
    dev->GetImmediateContext(&ctx);
    bool scRgb = false;
    float white = 1;
    bool got = capture_.takeLatest(dev, ctx.Get(),
                                   [&](UINT w, UINT h, bool hdr) -> ID3D11Texture2D* {
                                       D3D11_TEXTURE2D_DESC d{};
                                       if (frame_) frame_->GetDesc(&d);
                                       if (!frame_ || d.Width != w || d.Height != h || scRgb_ != hdr) {
                                           frame_.Reset();
                                           staging_.Reset();
                                           D3D11_TEXTURE2D_DESC n{};
                                           n.Width = w;
                                           n.Height = h;
                                           n.MipLevels = n.ArraySize = 1;
                                           n.Format = hdr ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_B8G8R8A8_UNORM;
                                           n.SampleDesc.Count = 1;
                                           n.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                                           if (FAILED(dev->CreateTexture2D(&n, nullptr, &frame_))) return nullptr;
                                           n.BindFlags = 0;
                                           n.Usage = D3D11_USAGE_STAGING;
                                           n.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                                           if (FAILED(dev->CreateTexture2D(&n, nullptr, &staging_))) {
                                               frame_.Reset();
                                               return nullptr;
                                           }
                                           scRgb_ = hdr;
                                       }
                                       return frame_.Get();
                                   },
                                   scRgb, white);
    if (!got || !frame_ || !staging_) return std::nullopt;
    ctx->CopyResource(staging_.Get(), frame_.Get());
    D3D11_MAPPED_SUBRESOURCE map{};
    if (FAILED(ctx->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &map))) return std::nullopt;
    D3D11_TEXTURE2D_DESC d{};
    staging_->GetDesc(&d);
    double lum;
    bool allZero = true;
    if (scRgb) {
        lum = stripLuminanceHalf(static_cast<const std::uint16_t*>(map.pData), int(d.Width), int(d.Height),
                                 int(map.RowPitch / 2), white);
        allZero = lum == 0;
    } else {
        const auto* p = static_cast<const std::uint8_t*>(map.pData);
        for (UINT y = 0; y < d.Height && allZero; ++y)
            for (UINT x = 0; x < d.Width * 4; ++x)
                if (p[std::size_t(y) * map.RowPitch + x]) {
                    allZero = false;
                    break;
                }
        lum = stripLuminance(p, int(d.Width), int(d.Height), int(map.RowPitch));
    }
    ctx->Unmap(staging_.Get(), 0);
    if (allZero) {   // première image noire de la duplication : on attend la suivante
        sawBlack_ = true;
        return std::nullopt;
    }
    stop();
    return lum;
}

} // namespace md
