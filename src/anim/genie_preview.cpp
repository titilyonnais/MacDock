#include "genie_preview.h"

#include <algorithm>
#include <cmath>

namespace md {

namespace {

void put(BgraImage& img, int x, int y, std::uint8_t b, std::uint8_t g, std::uint8_t r, double a = 1.0) {
    if (x < 0 || y < 0 || x >= img.w || y >= img.h) return;
    std::uint8_t* p = &img.px[(std::size_t(y) * img.w + x) * 4];
    const double k = std::clamp(a, 0.0, 1.0);
    p[0] = std::uint8_t(std::lround(b * k + p[0] * (1 - k)));
    p[1] = std::uint8_t(std::lround(g * k + p[1] * (1 - k)));
    p[2] = std::uint8_t(std::lround(r * k + p[2] * (1 - k)));
    p[3] = std::uint8_t(std::lround(255 * k + p[3] * (1 - k)));
}

void fill(BgraImage& img, const RECT& r, std::uint8_t b, std::uint8_t g, std::uint8_t rr, double a = 1.0) {
    for (LONG y = r.top; y < r.bottom; ++y)
        for (LONG x = r.left; x < r.right; ++x) put(img, int(x), int(y), b, g, rr, a);
}

void outline(BgraImage& img, const RECT& r) {
    for (LONG x = r.left; x < r.right; ++x) {
        put(img, int(x), int(r.top), 255, 255, 255, 0.9);
        put(img, int(x), int(r.bottom - 1), 255, 255, 255, 0.9);
    }
    for (LONG y = r.top; y < r.bottom; ++y) {
        put(img, int(r.left), int(y), 255, 255, 255, 0.9);
        put(img, int(r.right - 1), int(y), 255, 255, 255, 0.9);
    }
}

} // namespace

void drawSlices(const BgraImage& src, const std::vector<GenieSlice>& slices, BgraImage& dst) {
    for (const GenieSlice& s : slices) {
        const LONG sw = s.src.right - s.src.left, sh = s.src.bottom - s.src.top;
        const LONG dw = s.dst.right - s.dst.left, dh = s.dst.bottom - s.dst.top;
        if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) continue;
        for (LONG y = std::max<LONG>(s.dst.top, 0); y < std::min<LONG>(s.dst.bottom, dst.h); ++y) {
            const LONG sy = std::clamp<LONG>(s.src.top + LONG((y - s.dst.top + 0.5) * sh / dh), s.src.top, s.src.bottom - 1);
            for (LONG x = std::max<LONG>(s.dst.left, 0); x < std::min<LONG>(s.dst.right, dst.w); ++x) {
                const LONG sx = std::clamp<LONG>(s.src.left + LONG((x - s.dst.left + 0.5) * sw / dw), s.src.left, s.src.right - 1);
                if (sx < 0 || sy < 0 || sx >= src.w || sy >= src.h) continue;
                const std::uint8_t* p = &src.px[(std::size_t(sy) * src.w + sx) * 4];
                put(dst, int(x), int(y), p[0], p[1], p[2], p[3] / 255.0);
            }
        }
    }
}

BgraImage syntheticWindow(int w, int h) {
    BgraImage img{w, h, std::vector<std::uint8_t>(std::size_t(w) * h * 4, 0)};
    fill(img, RECT{0, 0, w, h}, 255, 255, 255);
    const int title = std::max(8, h / 12);
    fill(img, RECT{0, 0, w, title}, 236, 236, 236);
    const int side = w / 4;
    fill(img, RECT{0, title, side, h}, 240, 232, 226);
    const double r = title / 4.0;
    const std::uint8_t lights[3][3] = {{87, 95, 255}, {46, 189, 254}, {64, 200, 40}};   // BGR : rouge, jaune, vert
    for (int i = 0; i < 3; ++i) {
        const double cx = title * 0.6 + i * title * 0.75, cy = title / 2.0;
        for (int y = 0; y < title; ++y)
            for (int x = 0; x < int(cx + r + 1); ++x)
                if ((x + 0.5 - cx) * (x + 0.5 - cx) + (y + 0.5 - cy) * (y + 0.5 - cy) <= r * r)
                    put(img, x, y, lights[i][0], lights[i][1], lights[i][2]);
    }
    for (int y = title + 14; y + 4 < h; y += 14)   // lignes de texte
        fill(img, RECT{side + 14, y, side + 14 + (w - side - 28) * (3 + (y / 14) % 4) / 6, y + 4}, 200, 200, 200);
    fill(img, RECT{side + 14, title + 14, w - 14, title + 14 + h / 5}, 230, 160, 60);   // image bleue
    return img;
}

BgraImage genieSheet(MinimizeEffect e, DockPosition edge) {
    constexpr int kW = 640, kH = 400;
    BgraImage sheet{3 * kW, 2 * kH, std::vector<std::uint8_t>(std::size_t(3 * kW) * 2 * kH * 4, 0)};
    RECT dock, tile, win, cellOf;
    switch (edge) {
        case DockPosition::Left: dock = {8, 80, 56, 320}; tile = {14, 250, 50, 286}; win = {200, 60, 600, 320}; break;
        case DockPosition::Right: dock = {584, 80, 632, 320}; tile = {590, 250, 626, 286}; win = {40, 60, 440, 320}; break;
        default: dock = {170, 344, 470, 392}; tile = {420, 350, 456, 386}; win = {120, 40, 520, 300}; break;
    }
    cellOf = tile;   // case du Dock (contour) ; tile devient la forme de la miniature
    const BgraImage src = syntheticWindow(win.right - win.left, win.bottom - win.top);
    {   // la miniature garde les proportions de la fenêtre dans sa case (comme fitThumbnail, 80 %)
        const double box = std::min(tile.right - tile.left, tile.bottom - tile.top) * 0.8;
        const double k = std::min(box / src.w, box / src.h);
        const LONG w = LONG(std::lround(src.w * k)), h = LONG(std::lround(src.h * k));
        const LONG cx = (tile.left + tile.right) / 2, cy = (tile.top + tile.bottom) / 2;
        tile = RECT{cx - w / 2, cy - h / 2, cx - w / 2 + w, cy - h / 2 + h};
    }
    const double ts[6] = {0, 0.2, 0.4, 0.6, 0.8, 1};
    for (int i = 0; i < 6; ++i) {
        BgraImage cell{kW, kH, std::vector<std::uint8_t>(std::size_t(kW) * kH * 4, 0)};
        for (int y = 0; y < kH; ++y)   // fond : dégradé bleu → violet
            for (int x = 0; x < kW; ++x) put(cell, x, y, std::uint8_t(160 + 60 * y / kH), std::uint8_t(90 + 40 * x / kW), std::uint8_t(60 + 80 * x / kW));
        fill(cell, dock, 255, 255, 255, 0.35);
        outline(cell, cellOf);
        drawSlices(src, minimizeFrame(e, SIZE{src.w, src.h}, win, tile, edge, ts[i]), cell);
        const int ox = (i % 3) * kW, oy = (i / 3) * kH;
        for (int y = 0; y < kH; ++y)
            std::copy_n(&cell.px[std::size_t(y) * kW * 4], kW * 4, &sheet.px[(std::size_t(oy + y) * sheet.w + ox) * 4]);
    }
    return sheet;
}

} // namespace md
