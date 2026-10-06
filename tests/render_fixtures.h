// Images de test pour le renderer : icônes unies, fonds d'écran, Dock d'exemple (rendu hors écran).
#pragma once
#include <windows.h>
#include <objbase.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "../src/config/metrics.h"
#include "../src/geom/smooth_rect.h"
#include "../src/icons/icon_grid.h"
#include "../src/layout/dock_geometry.h"
#include "../src/layout/dock_layout.h"
#include "../src/render/dock_renderer.h"

namespace fixtures {

constexpr UINT kW = 900, kH = 220;

struct ComScope {
    ComScope() { CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); }
    ~ComScope() { CoUninitialize(); }
};

// Icône unie (forme visible carrée, opaque) posée sur la grille Apple.
inline md::IconProvider::ImagePtr solidIcon(int px, std::uint8_t b, std::uint8_t g, std::uint8_t r) {
    int s = md::iconShapePx(px, 0.8046875);
    std::vector<std::uint8_t> shape(size_t(s) * s * 4);
    for (size_t i = 0; i < shape.size(); i += 4) {
        shape[i] = b;
        shape[i + 1] = g;
        shape[i + 2] = r;
        shape[i + 3] = 255;
    }
    auto img = std::make_shared<md::IconProvider::Image>();
    img->size = px;
    img->bgra = md::placeOnGrid(shape, s, px);
    return img;
}

inline std::vector<std::uint8_t> flatWallpaper(UINT w, UINT h, std::uint8_t b, std::uint8_t g, std::uint8_t r) {
    std::vector<std::uint8_t> px(size_t(w) * h * 4);
    for (size_t i = 0; i < px.size(); i += 4) {
        px[i] = b;
        px[i + 1] = g;
        px[i + 2] = r;
        px[i + 3] = 255;
    }
    return px;
}

// Rayures verticales noires et blanches de period px.
inline std::vector<std::uint8_t> stripedWallpaper(UINT w, UINT h, int period) {
    std::vector<std::uint8_t> px(size_t(w) * h * 4);
    for (UINT y = 0; y < h; ++y)
        for (UINT x = 0; x < w; ++x) {
            std::uint8_t v = (int(x) / period) % 2 ? 255 : 0;
            auto* p = &px[(size_t(y) * w + x) * 4];
            p[0] = p[1] = p[2] = v;
            p[3] = 255;
        }
    return px;
}

// Dock d'exemple : 3 icônes (la 2e ouverte), un séparateur, une 4e icône ; au repos, centré dans kW x kH.
inline md::RenderFrame sampleFrame(bool dark, float scale) {
    md::Metrics m;
    md::LayoutInput in;
    in.items = {{}, {}, {}, {true}, {}};
    in.tileSize = 48;
    in.gap = m.iconGap;
    in.padding = m.dockPadding;
    in.separatorWidth = m.separatorWidth;
    in.separatorMargin = m.separatorMargin;
    auto r = md::computeLayout(in);
    auto g = md::dockGeometry(48, m);

    md::RenderFrame f;
    f.scale = scale;
    f.dark = dark;
    f.bgBottom = float(kH - m.dockScreenMargin * scale);
    f.bgTop = f.bgBottom - float(r.thickness) * scale;
    f.bgLeft = float(kW / 2.0 + r.bgStart * scale);
    f.bgRight = float(kW / 2.0 + r.bgEnd * scale);
    f.cornerRadius = float(g.cornerRadius) * scale;
    const std::uint8_t colors[4][3] = {{200, 80, 40}, {40, 160, 60}, {60, 60, 220}, {30, 180, 220}};
    int c = 0;
    for (size_t i = 0; i < r.items.size(); ++i) {
        md::RenderIcon icon;
        icon.cx = float(kW / 2.0 + r.items[i].center * scale);
        if (in.items[i].separator) {
            icon.separator = true;
            icon.cy = (f.bgTop + f.bgBottom) / 2;
            icon.sepLength = float(g.separatorLength) * scale;
        } else {
            icon.size = float(48 * scale);
            icon.cy = f.bgBottom - float(m.dockPadding) * scale - icon.size / 2;
            icon.indicatorY = f.bgBottom - float(g.indicatorCenter) * scale;
            icon.indicator = i == 1;
            icon.image = solidIcon(int(icon.size), colors[c][0], colors[c][1], colors[c][2]);
            ++c;
        }
        f.icons.push_back(icon);
    }
    return f;
}

} // namespace fixtures
