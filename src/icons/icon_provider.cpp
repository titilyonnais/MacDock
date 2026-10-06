#include "icon_provider.h"

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>

#include "../core/log.h"
#include "squircle.h"

using Microsoft::WRL::ComPtr;

namespace md {
namespace {

using Pixels = std::vector<std::uint8_t>;

struct Rgb { double r, g, b; };

IWICImagingFactory* wic() {
    static ComPtr<IWICImagingFactory> factory;
    if (!factory)
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    return factory.Get();
}

// Redimensionne une image BGRA prémultipliée carrée (src x src) vers dst x dst.
Pixels resize(const Pixels& src, int srcSize, int dstSize) {
    Pixels out(size_t(dstSize) * dstSize * 4, 0);
    if (srcSize == dstSize) return src;
    ComPtr<IWICBitmap> bmp;
    if (!wic() || FAILED(wic()->CreateBitmapFromMemory(UINT(srcSize), UINT(srcSize), GUID_WICPixelFormat32bppPBGRA,
                                                       UINT(srcSize * 4), UINT(src.size()),
                                                       const_cast<BYTE*>(src.data()), &bmp)))
        return out;
    ComPtr<IWICBitmapScaler> scaler;
    if (FAILED(wic()->CreateBitmapScaler(&scaler)) ||
        FAILED(scaler->Initialize(bmp.Get(), UINT(dstSize), UINT(dstSize), WICBitmapInterpolationModeHighQualityCubic)))
        return out;
    scaler->CopyPixels(nullptr, UINT(dstSize * 4), UINT(out.size()), out.data());
    return out;
}

// Image Shell 256 px (BGRA prémultiplié) ; vide si échec.
Pixels shellImage(const std::wstring& parsingName, int& size) {
    size = 0;
    ComPtr<IShellItemImageFactory> factory;
    if (FAILED(SHCreateItemFromParsingName(parsingName.c_str(), nullptr, IID_PPV_ARGS(&factory)))) return {};
    HBITMAP hbmp = nullptr;
    SIZE req{256, 256};
    if (FAILED(factory->GetImage(req, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &hbmp)) &&
        FAILED(factory->GetImage(req, SIIGBF_RESIZETOFIT, &hbmp)))
        return {};
    BITMAP bm{};
    GetObjectW(hbmp, sizeof bm, &bm);
    int w = bm.bmWidth, h = std::abs(bm.bmHeight);
    Pixels px(size_t(w) * h * 4);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;   // de haut en bas
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC dc = GetDC(nullptr);
    int lines = GetDIBits(dc, hbmp, 0, UINT(h), px.data(), &bi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    DeleteObject(hbmp);
    if (lines != h || w <= 0) return {};

    bool anyAlpha = false;
    for (size_t i = 3; i < px.size(); i += 4) anyAlpha |= px[i] != 0;
    if (!anyAlpha)
        for (size_t i = 3; i < px.size(); i += 4) px[i] = 255;

    // Mise au carré (centrage) si l'image n'est pas carrée.
    int s = std::max(w, h);
    if (w != h) {
        Pixels sq(size_t(s) * s * 4, 0);
        int ox = (s - w) / 2, oy = (s - h) / 2;
        for (int y = 0; y < h; ++y)
            std::copy_n(&px[size_t(y) * w * 4], size_t(w) * 4, &sq[(size_t(y + oy) * s + ox) * 4]);
        px.swap(sq);
    }
    size = s;
    return px;
}

Pixels loadPng(const std::wstring& path, int& size) {
    size = 0;
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES || !wic()) return {};
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> conv;
    if (FAILED(wic()->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
                                                &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame)) || FAILED(wic()->CreateFormatConverter(&conv)) ||
        FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                WICBitmapPaletteTypeCustom)))
        return {};
    UINT w = 0, h = 0;
    conv->GetSize(&w, &h);
    if (w == 0 || h == 0 || w > 2048 || h > 2048) return {};
    Pixels px(size_t(w) * h * 4);
    if (FAILED(conv->CopyPixels(nullptr, w * 4, UINT(px.size()), px.data()))) return {};
    if (w != h) {   // centrer dans un carré
        UINT s = std::max(w, h);
        Pixels sq(size_t(s) * s * 4, 0);
        for (UINT y = 0; y < h; ++y)
            std::copy_n(&px[size_t(y) * w * 4], size_t(w) * 4, &sq[(size_t(y + (s - h) / 2) * s + (s - w) / 2) * 4]);
        px.swap(sq);
        w = s;
    }
    size = int(w);
    return px;
}

void applySquircleMask(Pixels& px, int size) {
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            double a = squircleMaskAlpha(x, y, size);
            if (a >= 1.0) continue;
            auto* p = &px[(size_t(y) * size + x) * 4];
            for (int c = 0; c < 4; ++c) p[c] = std::uint8_t(std::lround(p[c] * a));
        }
}

// Plaque squircle avec dégradé vertical et liseré clair.
Pixels plate(int size, Rgb top, Rgb bottom, double rimOpacity) {
    Pixels px(size_t(size) * size * 4, 0);
    for (int y = 0; y < size; ++y) {
        double t = size > 1 ? double(y) / (size - 1) : 0;
        Rgb c{top.r + (bottom.r - top.r) * t, top.g + (bottom.g - top.g) * t, top.b + (bottom.b - top.b) * t};
        for (int x = 0; x < size; ++x) {
            double a = squircleMaskAlpha(x, y, size);
            if (a <= 0) continue;
            // Liseré : pixels proches du bord de la forme (forme réduite de 1 px non couvrante).
            double inner = squircleMaskAlpha(x, y, size) - (x > 0 && y > 0 && x < size - 1 && y < size - 1
                               ? std::min(squircleMaskAlpha(x - 1, y, size), std::min(squircleMaskAlpha(x + 1, y, size),
                                 std::min(squircleMaskAlpha(x, y - 1, size), squircleMaskAlpha(x, y + 1, size))))
                               : 0.0);
            double rim = std::clamp(inner, 0.0, 1.0) * rimOpacity;
            double r = c.r + (1 - c.r) * rim, g = c.g + (1 - c.g) * rim, b = c.b + (1 - c.b) * rim;
            auto* p = &px[(size_t(y) * size + x) * 4];
            p[0] = std::uint8_t(std::lround(b * a * 255));
            p[1] = std::uint8_t(std::lround(g * a * 255));
            p[2] = std::uint8_t(std::lround(r * a * 255));
            p[3] = std::uint8_t(std::lround(a * 255));
        }
    }
    return px;
}

// dst = src par-dessus dst (prémultiplié), src de taille s placée en (ox, oy).
void blendOver(Pixels& dst, int dstSize, const Pixels& src, int s, int ox, int oy) {
    for (int y = 0; y < s; ++y) {
        int dy = y + oy;
        if (dy < 0 || dy >= dstSize) continue;
        for (int x = 0; x < s; ++x) {
            int dx = x + ox;
            if (dx < 0 || dx >= dstSize) continue;
            const auto* sp = &src[(size_t(y) * s + x) * 4];
            auto* dp = &dst[(size_t(dy) * dstSize + dx) * 4];
            int inv = 255 - sp[3];
            for (int c = 0; c < 4; ++c) dp[c] = std::uint8_t(sp[c] + (dp[c] * inv + 127) / 255);
        }
    }
}

void fillRoundRect(Pixels& px, int size, double x0, double y0, double x1, double y1, double radius, Rgb c) {
    for (int y = int(y0); y <= int(std::ceil(y1)) && y < size; ++y)
        for (int x = int(x0); x <= int(std::ceil(x1)) && x < size; ++x) {
            if (x < 0 || y < 0) continue;
            int hits = 0;
            for (int j = 0; j < 4; ++j)
                for (int i = 0; i < 4; ++i) {
                    double sx = x + (i + 0.5) / 4, sy = y + (j + 0.5) / 4;
                    double cx = std::clamp(sx, x0 + radius, x1 - radius), cy = std::clamp(sy, y0 + radius, y1 - radius);
                    if (sx >= x0 && sx <= x1 && sy >= y0 && sy <= y1 &&
                        (sx - cx) * (sx - cx) + (sy - cy) * (sy - cy) <= radius * radius)
                        ++hits;
                }
            if (!hits) continue;
            double a = hits / 16.0;
            auto* p = &px[(size_t(y) * size + x) * 4];
            double inv = 1 - a;
            p[0] = std::uint8_t(std::lround(c.b * a * 255 + p[0] * inv));
            p[1] = std::uint8_t(std::lround(c.g * a * 255 + p[1] * inv));
            p[2] = std::uint8_t(std::lround(c.r * a * 255 + p[2] * inv));
            p[3] = std::uint8_t(std::lround(a * 255 + p[3] * inv));
        }
}

Rgb hex(unsigned v) { return {((v >> 16) & 0xFF) / 255.0, ((v >> 8) & 0xFF) / 255.0, (v & 0xFF) / 255.0}; }

Pixels jailPlate(int size, bool dark) {
    return dark ? plate(size, hex(0x48484A), hex(0x2C2C2E), 0.18) : plate(size, hex(0xFBFBFD), hex(0xE3E3E8), 0.65);
}

Pixels genericIcon(int size, bool dark) {
    Pixels px = jailPlate(size, dark);
    double m = size * 0.27;
    fillRoundRect(px, size, m, m, size - m, size - m, size * 0.06, dark ? hex(0x8E8E93) : hex(0xAEAEB2));
    fillRoundRect(px, size, m, m, size - m, m + size * 0.09, size * 0.04, dark ? hex(0x636366) : hex(0x8E8E93));
    return px;
}

std::wstring safeFileName(const std::wstring& key) {
    std::wstring out;
    for (wchar_t c : key) out += (wcschr(L"\\/:*?\"<>|!", c) || c < 32) ? L'_' : c;
    return out;
}

} // namespace

void IconProvider::setStrictTahoe(bool strict) {
    if (strict_ == strict) return;
    strict_ = strict;
    clear();
}

void IconProvider::setJailInset(double inset) {
    inset = std::clamp(inset, 0.0, 0.4);
    if (inset == jailInset_) return;
    jailInset_ = inset;
    clear();
}

void IconProvider::setDark(bool dark) {
    if (dark_ == dark) return;
    dark_ = dark;
    clear();
}

IconProvider::ImagePtr IconProvider::get(const std::wstring& key, const std::wstring& parsingName, int px) {
    px = std::clamp(px, 16, 512);
    std::wstring cacheKey = key + L"|" + std::to_wstring(px);
    if (auto it = cache_.find(cacheKey); it != cache_.end()) return it->second;
    auto img = build(key, parsingName, px);
    cache_[cacheKey] = img;
    return img;
}

IconProvider::ImagePtr IconProvider::build(const std::wstring& key, const std::wstring& parsingName, int px) {
    auto img = std::make_shared<Image>();
    img->size = px;

    int srcSize = 0;
    Pixels src;
    bool custom = false;
    if (!customDir_.empty()) {
        src = loadPng(customDir_ + L"\\" + safeFileName(key) + L".png", srcSize);
        custom = !src.empty();
    }
    if (src.empty() && !parsingName.empty()) src = shellImage(parsingName, srcSize);
    if (src.empty()) {
        log::warn(L"Icône introuvable pour %s", parsingName.c_str());
        img->bgra = genericIcon(px, dark_);
        return img;
    }

    if (custom || !strict_) {
        // Icône personnalisée : déjà dessinée par l'utilisateur, on la respecte telle quelle.
        img->bgra = resize(src, srcSize, px);
    } else if (iconFitsSquircle(src.data(), srcSize, srcSize, srcSize * 4)) {
        img->bgra = resize(src, srcSize, px);
        applySquircleMask(img->bgra, px);
    } else {
        img->bgra = jailPlate(px, dark_);
        int inner = std::max(1, int(std::lround(px * (1 - 2 * jailInset_))));
        Pixels icon = resize(src, srcSize, inner);
        blendOver(img->bgra, px, icon, inner, (px - inner) / 2, (px - inner) / 2);
    }
    return img;
}

IconProvider::ImagePtr IconProvider::appsButton(int px) {
    px = std::clamp(px, 16, 512);
    std::wstring cacheKey = L"#apps|" + std::to_wstring(px);
    if (auto it = cache_.find(cacheKey); it != cache_.end()) return it->second;
    auto img = std::make_shared<Image>();
    img->size = px;
    img->bgra = dark_ ? plate(px, hex(0x3A3A3C), hex(0x1C1C1E), 0.2) : plate(px, hex(0xFFFFFF), hex(0xE9E9EE), 0.7);
    static const unsigned colors[9] = {0xFF5F57, 0xFFBD2E, 0x28C840, 0x0A84FF, 0xBF5AF2, 0xFF375F,
                                       0x64D2FF, 0xFF9F0A, 0x30D158};
    double cell = px * 0.17, gap = px * 0.055;
    double start = (px - (3 * cell + 2 * gap)) / 2;
    for (int j = 0; j < 3; ++j)
        for (int i = 0; i < 3; ++i) {
            double x = start + i * (cell + gap), y = start + j * (cell + gap);
            fillRoundRect(img->bgra, px, x, y, x + cell, y + cell, cell * 0.28, hex(colors[j * 3 + i]));
        }
    cache_[cacheKey] = img;
    return img;
}

} // namespace md
