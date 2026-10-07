#include "icon_provider.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <commoncontrols.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <map>

#include "../core/log.h"
#include "../geom/smooth_rect.h"
#include "icon_grid.h"
#include "../stack/stack_icon.h"
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
// Pixels BGRA d'une image 32 bits (carrée après centrage) ; libère hbmp.
Pixels bitmapPixels(HBITMAP hbmp, int& size) {
    size = 0;
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

Pixels shellImage(const std::wstring& parsingName, int& size) {
    size = 0;
    ComPtr<IShellItemImageFactory> factory;
    if (FAILED(SHCreateItemFromParsingName(parsingName.c_str(), nullptr, IID_PPV_ARGS(&factory)))) return {};
    HBITMAP hbmp = nullptr;
    SIZE req{256, 256};
    if (FAILED(factory->GetImage(req, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &hbmp)) &&
        FAILED(factory->GetImage(req, SIIGBF_RESIZETOFIT, &hbmp)))
        return {};
    return bitmapPixels(hbmp, size);
}

// Pixels d'une icône (détruite au passage), prémultipliés.
Pixels iconPixels(HICON icon, int& size) {
    size = 0;
    ICONINFO ii{};
    BOOL ok = GetIconInfo(icon, &ii);
    DestroyIcon(icon);
    if (!ok) return {};
    if (ii.hbmMask) DeleteObject(ii.hbmMask);
    if (!ii.hbmColor) return {};
    Pixels px = bitmapPixels(ii.hbmColor, size);
    // Couleurs d'icône en alpha droit : prémultipliées comme celles du Shell.
    for (size_t i = 0; i + 3 < px.size(); i += 4)
        for (int c = 0; c < 3; ++c) px[i + c] = std::uint8_t((px[i + c] * px[i + 3] + 127) / 255);
    return px;
}

// Icône système (SHSTOCKICONID) extraite en 256 px depuis son emplacement (imageres.dll…).
Pixels stockImage(SHSTOCKICONID id, int& size) {
    size = 0;
    SHSTOCKICONINFO info{sizeof info};
    if (FAILED(SHGetStockIconInfo(id, SHGSI_ICONLOCATION, &info))) return {};
    HICON icon = nullptr;
    if (SHDefExtractIconW(info.szPath, info.iIcon, 0, &icon, nullptr, MAKELONG(256, 0)) != S_OK || !icon) return {};
    return iconPixels(icon, size);
}

// Vignette (images, vidéos, PDF…) ou, à défaut, icône du fichier.
Pixels thumbnailImage(const std::wstring& path, int& size) {
    size = 0;
    ComPtr<IShellItemImageFactory> factory;
    if (FAILED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&factory)))) return {};
    HBITMAP hbmp = nullptr;
    SIZE req{256, 256};
    if (FAILED(factory->GetImage(req, SIIGBF_RESIZETOFIT | SIIGBF_BIGGERSIZEOK, &hbmp)) &&
        FAILED(factory->GetImage(req, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &hbmp)))
        return {};
    return bitmapPixels(hbmp, size);
}

// Icône générique du type de fichier (d'après l'extension seule), en 256 px.
Pixels typeIcon(const std::wstring& path, int& size) {
    size = 0;
    SHFILEINFOW sfi{};
    if (!SHGetFileInfoW(path.c_str(), FILE_ATTRIBUTE_NORMAL, &sfi, sizeof sfi, SHGFI_SYSICONINDEX | SHGFI_USEFILEATTRIBUTES))
        return {};
    ComPtr<IImageList> list;
    HICON icon = nullptr;
    if (FAILED(SHGetImageList(SHIL_JUMBO, IID_PPV_ARGS(&list))) || FAILED(list->GetIcon(sfi.iIcon, ILD_TRANSPARENT, &icon)) ||
        !icon)
        return {};
    return iconPixels(icon, size);
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

// Table de couverture de la forme d'icône (coins continus, anticrénelée), calculée une fois par taille et rayon.
const std::vector<float>& maskFor(int size, double ratio) {
    static std::map<std::pair<int, double>, std::vector<float>> cache;
    auto& m = cache[{size, ratio}];
    if (m.empty()) m = smoothSquareMask(size, ratio);
    return m;
}

void applySquircleMask(Pixels& px, int size, double ratio) {
    const auto& mask = maskFor(size, ratio);
    for (size_t i = 0; i < mask.size(); ++i) {
        float a = mask[i];
        if (a >= 1.0f) continue;
        for (int c = 0; c < 4; ++c) px[i * 4 + c] = std::uint8_t(std::lround(px[i * 4 + c] * a));
    }
}

// Plaque squircle avec dégradé vertical et liseré clair sur le pourtour.
Pixels plate(int size, double ratio, Rgb top, Rgb bottom, double rimOpacity) {
    const auto& mask = maskFor(size, ratio);
    auto at = [&](int x, int y) { return (x < 0 || y < 0 || x >= size || y >= size) ? 0.0f : mask[size_t(y) * size + x]; };
    Pixels px(size_t(size) * size * 4, 0);
    for (int y = 0; y < size; ++y) {
        double t = size > 1 ? double(y) / (size - 1) : 0;
        Rgb c{top.r + (bottom.r - top.r) * t, top.g + (bottom.g - top.g) * t, top.b + (bottom.b - top.b) * t};
        for (int x = 0; x < size; ++x) {
            double a = at(x, y);
            if (a <= 0) continue;
            // Liseré : écart entre la couverture du pixel et celle de son voisin le moins couvert.
            double inner = a - std::min({at(x - 1, y), at(x + 1, y), at(x, y - 1), at(x, y + 1)});
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

Pixels jailPlate(int size, double ratio, bool dark) {
    return dark ? plate(size, ratio, hex(0x48484A), hex(0x2C2C2E), 0.18)
                : plate(size, ratio, hex(0xFBFBFD), hex(0xE3E3E8), 0.65);
}

Pixels genericIcon(int size, double ratio, bool dark) {
    Pixels px = jailPlate(size, ratio, dark);
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

void IconProvider::setGrid(double shapeRatio, double cornerRatio, double jailInset, double shadowOpacity) {
    shapeRatio = std::clamp(std::isfinite(shapeRatio) ? shapeRatio : kIconShapeRatio, 0.5, 1.0);
    cornerRatio = std::clamp(std::isfinite(cornerRatio) ? cornerRatio : kIconCornerRatio, 0.0, 0.5);
    jailInset = std::clamp(std::isfinite(jailInset) ? jailInset : 0.16, 0.0, 0.4);
    shadowOpacity = std::clamp(std::isfinite(shadowOpacity) ? shadowOpacity : 0.0, 0.0, 1.0);
    if (shapeRatio == shapeRatio_ && cornerRatio == cornerRatio_ && jailInset == jailInset_ &&
        shadowOpacity == shadowOpacity_)
        return;
    shapeRatio_ = shapeRatio;
    cornerRatio_ = cornerRatio;
    jailInset_ = jailInset;
    shadowOpacity_ = shadowOpacity;
    clear();
}

IconProvider::ImagePtr IconProvider::finish(Pixels shaped, int shape, int px) const {
    auto img = std::make_shared<Image>();
    img->size = px;
    img->bgra = placeOnGrid(shaped, shape, px);
    addDropShadow(img->bgra, px, px * 14.0 / 1024, px * 12.0 / 1024, shadowOpacity_);
    return img;
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
    const int s = iconShapePx(px, shapeRatio_);   // forme visible, centrée dans la case (grille Apple)

    int srcSize = 0;
    Pixels src;
    bool custom = false;
    if (!customDir_.empty()) {
        src = loadPng(customDir_ + L"\\" + safeFileName(key) + L".png", srcSize);
        custom = !src.empty();
    }
    if (src.empty() && parsingName.starts_with(L"stock:"))
        src = stockImage(SHSTOCKICONID(_wtoi(parsingName.c_str() + 6)), srcSize);
    else if (src.empty() && !parsingName.empty())
        src = shellImage(parsingName, srcSize);
    if (src.empty()) {
        log::warn(L"Icône introuvable pour %s", parsingName.c_str());
        return finish(genericIcon(s, cornerRatio_, dark_), s, px);
    }

    if (custom) {
        // Icône personnalisée : dessinée sur la grille Apple (toile complète), respectée telle quelle.
        auto img = std::make_shared<Image>();
        img->size = px;
        img->bgra = resize(src, srcSize, px);
        return img;
    }
    if (!strict_) return finish(resize(src, srcSize, s), s, px);
    if (iconFitsSquircle(src.data(), srcSize, srcSize, srcSize * 4)) {
        Pixels shaped = resize(src, srcSize, s);
        applySquircleMask(shaped, s, cornerRatio_);
        return finish(std::move(shaped), s, px);
    }
    Pixels shaped = jailPlate(s, cornerRatio_, dark_);
    int inner = std::max(1, int(std::lround(s * (1 - 2 * jailInset_))));
    Pixels icon = resize(src, srcSize, inner);
    blendOver(shaped, s, icon, inner, (s - inner) / 2, (s - inner) / 2);
    return finish(std::move(shaped), s, px);
}

IconProvider::ImagePtr IconProvider::trash(bool full, int px) {
    return full ? get(L"trash-full", L"stock:" + std::to_wstring(int(SIID_RECYCLERFULL)), px)
                : get(L"trash", L"stock:" + std::to_wstring(int(SIID_RECYCLER)), px);
}

IconProvider::ImagePtr IconProvider::file(const std::wstring& path, int px) {
    px = std::clamp(px, 16, 512);
    std::wstring cacheKey = L"#file|" + path + L"|" + std::to_wstring(px);
    if (auto it = cache_.find(cacheKey); it != cache_.end()) return it->second;
    if (cache_.size() > 1500) clear();   // les piles changent sans cesse : pas de croissance sans fin
    int srcSize = 0;
    Pixels src = thumbnailImage(path, srcSize);
    if (src.empty()) src = typeIcon(path, srcSize);
    if (src.empty() || srcSize <= 0) return nullptr;
    auto img = std::make_shared<Image>();
    img->size = px;
    img->bgra = srcSize == px ? std::move(src) : resize(src, srcSize, px);
    cache_[cacheKey] = img;
    return img;
}

namespace {

// Pose src (carré ss, prémultiplié) dans dst (carré ds), centré en (cx, cy), côté side, incliné de angleDeg
// (sens horaire), par échantillonnage bilinéaire et composition « par-dessus ».
void drawLayer(Pixels& dst, int ds, const Pixels& src, int ss, double cx, double cy, double side, double angleDeg) {
    const double a = angleDeg * 3.14159265358979 / 180, c = std::cos(a), sn = std::sin(a);
    const double half = side / 2, k = ss / side, reach = half * 1.415;
    const int x0 = std::max(0, int(std::floor(cx - reach))), x1 = std::min(ds - 1, int(std::ceil(cx + reach)));
    const int y0 = std::max(0, int(std::floor(cy - reach))), y1 = std::min(ds - 1, int(std::ceil(cy + reach)));
    auto at = [&](int x, int y, int ch) -> double {
        x = std::clamp(x, 0, ss - 1);
        y = std::clamp(y, 0, ss - 1);
        return src[(size_t(y) * ss + x) * 4 + ch];
    };
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const double dx = x + 0.5 - cx, dy = y + 0.5 - cy;
            const double u = dx * c + dy * sn + half, v = -dx * sn + dy * c + half;   // repère de l'image
            if (u < 0 || v < 0 || u >= side || v >= side) continue;
            const double fx = u * k - 0.5, fy = v * k - 0.5;
            const int ix = int(std::floor(fx)), iy = int(std::floor(fy));
            const double tx = fx - ix, ty = fy - iy;
            double px[4];
            for (int ch = 0; ch < 4; ++ch)
                px[ch] = (at(ix, iy, ch) * (1 - tx) + at(ix + 1, iy, ch) * tx) * (1 - ty) +
                         (at(ix, iy + 1, ch) * (1 - tx) + at(ix + 1, iy + 1, ch) * tx) * ty;
            const double inv = 1 - px[3] / 255;
            std::uint8_t* d = &dst[(size_t(y) * ds + x) * 4];
            for (int ch = 0; ch < 4; ++ch) d[ch] = std::uint8_t(std::clamp(px[ch] + d[ch] * inv + 0.5, 0.0, 255.0));
        }
}

} // namespace

IconProvider::ImagePtr IconProvider::composeStack(const std::wstring& key, const std::vector<std::wstring>& paths,
                                                  int px) {
    px = std::clamp(px, 16, 512);
    if (paths.empty()) return nullptr;
    std::wstring cacheKey = L"#stack|" + key + L"|" + std::to_wstring(px);
    for (auto& p : paths) cacheKey += L"|" + p;
    if (auto it = cache_.find(cacheKey); it != cache_.end()) return it->second;
    const int s = iconShapePx(px, shapeRatio_);   // même forme que les icônes d'apps, même ombre
    Pixels shaped(size_t(s) * s * 4, 0);
    const auto layers = stackIconLayers(paths.size());
    bool any = false;
    // Couches du dessous vers le dessus : paths[0] (le premier selon le tri) est au-dessus.
    for (std::size_t i = 0; i < layers.size(); ++i) {
        const StackLayer& l = layers[i];
        const int side = std::max(8, int(std::lround(s * l.scale)));
        ImagePtr img = file(paths[layers.size() - 1 - i], side);
        if (!img) continue;
        drawLayer(shaped, s, img->bgra, img->size, s / 2.0 + l.dx * s, s / 2.0 + l.dy * s, side, l.angle);
        any = true;
    }
    if (!any) return nullptr;
    auto img = finish(std::move(shaped), s, px);
    cache_[cacheKey] = img;
    return img;
}

IconProvider::ImagePtr IconProvider::appsButton(int px) {
    px = std::clamp(px, 16, 512);
    std::wstring cacheKey = L"#apps|" + std::to_wstring(px);
    if (auto it = cache_.find(cacheKey); it != cache_.end()) return it->second;
    const int s = iconShapePx(px, shapeRatio_);
    Pixels shaped = dark_ ? plate(s, cornerRatio_, hex(0x3A3A3C), hex(0x1C1C1E), 0.2)
                          : plate(s, cornerRatio_, hex(0xFFFFFF), hex(0xE9E9EE), 0.7);
    static const unsigned colors[9] = {0xFF5F57, 0xFFBD2E, 0x28C840, 0x0A84FF, 0xBF5AF2, 0xFF375F,
                                       0x64D2FF, 0xFF9F0A, 0x30D158};
    double cell = s * 0.17, gap = s * 0.055;
    double start = (s - (3 * cell + 2 * gap)) / 2;
    for (int j = 0; j < 3; ++j)
        for (int i = 0; i < 3; ++i) {
            double x = start + i * (cell + gap), y = start + j * (cell + gap);
            fillRoundRect(shaped, s, x, y, x + cell, y + cell, cell * 0.28, hex(colors[j * 3 + i]));
        }
    auto img = finish(std::move(shaped), s, px);
    cache_[cacheKey] = img;
    return img;
}

} // namespace md
