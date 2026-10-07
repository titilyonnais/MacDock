#include "cursor_art.h"

#include <cmath>

#include "vector_art.h"

namespace md {

namespace {

constexpr std::uint32_t kBlack = 0xFF000000, kWhite = 0xFFFFFFFF;

Poly arrowShape() {
    return Poly{{{3, 2}, {3, 20.5}, {7.2, 16.6}, {10.1, 23.2}, {12.8, 22.1}, {10, 15.6}, {15.6, 15.6}}};
}

Poly doubleArrow(double degrees) {   // vertical à 0°, centré en (16, 16)
    Poly p{{{0, -11}, {4.5, -6.5}, {1, -6.5}, {1, 6.5}, {4.5, 6.5}, {0, 11}, {-4.5, 6.5}, {-1, 6.5}, {-1, -6.5}, {-4.5, -6.5}}};
    return rotated(translated(p, 16, 16), degrees, 16, 16);
}

// Anneau d'attente : 12 segments autour de (cx, cy), le plus opaque à l'index « head ».
std::vector<Layer> spinner(double cx, double cy, double scale, int head) {
    std::vector<Layer> out;
    for (int k = 0; k < 12; ++k) {
        const int age = (head - k + 12) % 12;
        const double opacity = 1.0 - age / 12.0 * 0.85;
        const std::uint32_t a = std::uint32_t(std::lround(opacity * 255)) << 24;
        const Poly seg = rect(cx - 1.1 * scale, cy - 10 * scale, cx + 1.1 * scale, cy - 5 * scale);
        out.push_back(Layer{{rotated(seg, k * 30.0, cx, cy)}, a | 0x3A3A3A, a | 0xFFFFFF, 0.8 * scale});
    }
    return out;
}

} // namespace

const wchar_t* cursorRegistryName(CursorKind k) {
    switch (k) {
        case CursorKind::Arrow: return L"Arrow";
        case CursorKind::AppStarting: return L"AppStarting";
        case CursorKind::Wait: return L"Wait";
        case CursorKind::SizeNS: return L"SizeNS";
        case CursorKind::SizeWE: return L"SizeWE";
        case CursorKind::SizeNWSE: return L"SizeNWSE";
        case CursorKind::SizeNESW: return L"SizeNESW";
        case CursorKind::SizeAll: return L"SizeAll";
        case CursorKind::Crosshair: return L"Crosshair";
        default: return L"No";
    }
}

const wchar_t* cursorFileName(CursorKind k) {
    switch (k) {
        case CursorKind::Arrow: return L"arrow.cur";
        case CursorKind::AppStarting: return L"appstarting.ani";
        case CursorKind::Wait: return L"wait.ani";
        case CursorKind::SizeNS: return L"sizens.cur";
        case CursorKind::SizeWE: return L"sizewe.cur";
        case CursorKind::SizeNWSE: return L"sizenwse.cur";
        case CursorKind::SizeNESW: return L"sizenesw.cur";
        case CursorKind::SizeAll: return L"sizeall.cur";
        case CursorKind::Crosshair: return L"crosshair.cur";
        default: return L"no.cur";
    }
}

bool cursorAnimated(CursorKind k) { return k == CursorKind::Wait || k == CursorKind::AppStarting; }

std::vector<CursorFrame> cursorFrames(CursorKind k, int size) {
    const double unit = size / 32.0;
    const auto at = [&](double x, double y) { return POINT{LONG(std::lround(x * unit)), LONG(std::lround(y * unit))}; };
    const Layer arrow{{arrowShape()}, kBlack, kWhite, 1.6};
    std::vector<CursorFrame> out;
    const auto one = [&](std::vector<Layer> layers, POINT hot) { out.push_back({rasterize(layers, size, unit, 1.0), hot}); };
    switch (k) {
        case CursorKind::Arrow: one({arrow}, at(3, 2)); break;
        case CursorKind::SizeNS: one({Layer{{doubleArrow(0)}, kBlack, kWhite, 1.4}}, at(16, 16)); break;
        case CursorKind::SizeWE: one({Layer{{doubleArrow(90)}, kBlack, kWhite, 1.4}}, at(16, 16)); break;
        case CursorKind::SizeNWSE: one({Layer{{doubleArrow(-45)}, kBlack, kWhite, 1.4}}, at(16, 16)); break;
        case CursorKind::SizeNESW: one({Layer{{doubleArrow(45)}, kBlack, kWhite, 1.4}}, at(16, 16)); break;
        case CursorKind::SizeAll: one({Layer{{doubleArrow(0), doubleArrow(90)}, kBlack, kWhite, 1.4}}, at(16, 16)); break;
        case CursorKind::Crosshair:
            one({Layer{{rect(15.4, 7, 16.6, 25), rect(7, 15.4, 25, 16.6)}, kBlack, kWhite, 1.0}}, at(16, 16));
            break;
        case CursorKind::No: {
            const Poly slash = rotated(rect(17.4, 21.4, 26.6, 22.6), 45, 22, 22);
            one({arrow, Layer{{ring(22, 22, 3.2, 4.6), slash}, kBlack, kWhite, 1.0}}, at(3, 2));
            break;
        }
        case CursorKind::Wait:
            for (int f = 0; f < 12; ++f) out.push_back({rasterize(spinner(16, 16, 1.0, f), size, unit, 0.6), at(16, 16)});
            break;
        case CursorKind::AppStarting:
            for (int f = 0; f < 12; ++f) {
                std::vector<Layer> layers{arrow};
                for (Layer& l : spinner(23, 23, 0.5, f)) layers.push_back(l);
                out.push_back({rasterize(layers, size, unit, 1.0), at(3, 2)});
            }
            break;
    }
    return out;
}

} // namespace md
