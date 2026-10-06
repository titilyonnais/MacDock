#include "png_io.h"

#include <wincodec.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace md {

namespace {
ComPtr<IWICImagingFactory> factory() {
    ComPtr<IWICImagingFactory> f;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f));
    return f;
}
} // namespace

bool writePng(const std::wstring& path, const std::uint8_t* bgra, UINT w, UINT h) {
    if (!bgra || !w || !h) return false;
    auto wic = factory();
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppPBGRA;
    return wic && SUCCEEDED(wic->CreateStream(&stream)) &&
           SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
           SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
           SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
           SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
           SUCCEEDED(frame->SetSize(w, h)) && SUCCEEDED(frame->SetPixelFormat(&fmt)) &&
           SUCCEEDED(frame->WritePixels(h, w * 4, w * h * 4, const_cast<BYTE*>(bgra))) &&
           SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
}

std::vector<std::uint8_t> readPng(const std::wstring& path, UINT& w, UINT& h) {
    w = h = 0;
    auto wic = factory();
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> conv;
    if (!wic ||
        FAILED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
                                              &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame)) || FAILED(wic->CreateFormatConverter(&conv)) ||
        FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                WICBitmapPaletteTypeCustom)))
        return {};
    UINT iw = 0, ih = 0;
    conv->GetSize(&iw, &ih);
    if (!iw || !ih || iw > 16384 || ih > 16384) return {};
    std::vector<std::uint8_t> px(size_t(iw) * ih * 4);
    if (FAILED(conv->CopyPixels(nullptr, iw * 4, UINT(px.size()), px.data()))) return {};
    w = iw;
    h = ih;
    return px;
}

std::vector<std::uint8_t> resizeBgra(const std::vector<std::uint8_t>& src, UINT sw, UINT sh, UINT dw, UINT dh) {
    if (sw == dw && sh == dh) return src;
    std::vector<std::uint8_t> out(size_t(dw) * dh * 4, 0);
    auto wic = factory();
    ComPtr<IWICBitmap> bmp;
    ComPtr<IWICBitmapScaler> scaler;
    if (!wic || src.size() < size_t(sw) * sh * 4 ||
        FAILED(wic->CreateBitmapFromMemory(sw, sh, GUID_WICPixelFormat32bppPBGRA, sw * 4, UINT(src.size()),
                                           const_cast<BYTE*>(src.data()), &bmp)) ||
        FAILED(wic->CreateBitmapScaler(&scaler)) ||
        FAILED(scaler->Initialize(bmp.Get(), dw, dh, WICBitmapInterpolationModeHighQualityCubic)))
        return out;
    scaler->CopyPixels(nullptr, dw * 4, UINT(out.size()), out.data());
    return out;
}

} // namespace md
