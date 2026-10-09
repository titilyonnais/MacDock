#include "app_icon.h"

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>

#include "../icons/squircle.h"
#include "../ui/ui_draw.h"
#include "../ui/ui_theme.h"
#include "pane_icons.h"

namespace md {

using Microsoft::WRL::ComPtr;

namespace {
// Couleur de la tuile et pictogramme : gris de Réglages Système pour l'app Réglages, sombre pour le Dock et la barre.
std::uint32_t tileColor(AppIconKind kind) { return kind == AppIconKind::Settings ? 0x8E8E93 : 0x1C1C1E; }
PaneIcon pictogram(AppIconKind kind) {
    switch (kind) {
        case AppIconKind::Settings: return PaneIcon::Gear;
        case AppIconKind::Dock: return PaneIcon::Dock;
        case AppIconKind::MenuBar: return PaneIcon::MenuBar;
    }
    return PaneIcon::Gear;
}
void put16(std::vector<std::uint8_t>& b, std::uint32_t v) {
    b.push_back(std::uint8_t(v & 0xFF));
    b.push_back(std::uint8_t((v >> 8) & 0xFF));
}
void put32(std::vector<std::uint8_t>& b, std::uint32_t v) {
    put16(b, v & 0xFFFF);
    put16(b, v >> 16);
}
} // namespace

BgraImage renderAppIcon(AppIconKind kind, int size) {
    BgraImage out, failed;
    out.w = out.h = std::max(1, size);
    out.px.assign(std::size_t(out.w) * out.h * 4, 0);
    ComPtr<IWICImagingFactory> wic;
    ComPtr<IWICBitmap> bmp;
    ComPtr<ID2D1Factory> d2d;
    ComPtr<ID2D1RenderTarget> rt;
    ComPtr<IDWriteFactory> dwrite;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) ||
        FAILED(wic->CreateBitmap(UINT(out.w), UINT(out.h), GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp)) ||
        FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf())) ||
        FAILED(d2d->CreateWicBitmapRenderTarget(bmp.Get(), D2D1::RenderTargetProperties(), &rt)) ||
        FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(dwrite.GetAddressOf()))))
        return failed;
    const ui::Palette pal = ui::palette(false);
    {
        ui::Painter p(rt.Get(), dwrite.Get(), pal, L"Segoe UI");
        rt->BeginDraw();
        rt->Clear(D2D1::ColorF(0, 0, 0, 0));
        drawPaneTile(p, D2D1::RectF(0, 0, float(out.w), float(out.h)), tileColor(kind), pictogram(kind),
                     float(kIconCornerRatio));
        if (FAILED(rt->EndDraw())) return failed;
    }
    WICRect all{0, 0, out.w, out.h};
    if (FAILED(bmp->CopyPixels(&all, UINT(out.w * 4), UINT(out.px.size()), out.px.data()))) return failed;
    for (std::size_t i = 0; i < out.px.size(); i += 4) {   // alpha prémultiplié → droit (PNG, ICO)
        const unsigned a = out.px[i + 3];
        if (a == 0 || a == 255) continue;
        for (int c = 0; c < 3; ++c) out.px[i + c] = std::uint8_t(std::min(255u, (out.px[i + c] * 255u + a / 2) / a));
    }
    return out;
}

std::vector<std::uint8_t> icoFile(const std::vector<std::pair<int, std::vector<std::uint8_t>>>& pngs) {
    std::vector<std::uint8_t> out;
    put16(out, 0);   // réservé
    put16(out, 1);   // icône
    put16(out, std::uint32_t(pngs.size()));
    std::uint32_t offset = std::uint32_t(6 + 16 * pngs.size());
    for (const auto& [size, png] : pngs) {
        const std::uint8_t side = size >= 256 ? 0 : std::uint8_t(size);
        out.push_back(side);   // largeur
        out.push_back(side);   // hauteur
        out.push_back(0);      // couleurs de la palette
        out.push_back(0);      // réservé
        put16(out, 1);         // plans
        put16(out, 32);        // bits par pixel
        put32(out, std::uint32_t(png.size()));
        put32(out, offset);
        offset += std::uint32_t(png.size());
    }
    for (const auto& [size, png] : pngs) out.insert(out.end(), png.begin(), png.end());
    return out;
}

} // namespace md
