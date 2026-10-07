#include "bar_color.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace md {
namespace {

constexpr double kR = 0.2126, kG = 0.7152, kB = 0.0722;   // coefficients de la luminance relative (Rec. 709)

const std::array<double, 256>& srgbTable() {
    static const std::array<double, 256> table = [] {
        std::array<double, 256> t{};
        for (int i = 0; i < 256; ++i) t[std::size_t(i)] = srgbToLinear(i / 255.0);
        return t;
    }();
    return table;
}

} // namespace

double srgbToLinear(double c) {
    c = std::clamp(c, 0.0, 1.0);
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double stripLuminance(const std::uint8_t* bgra, int w, int h, int strideBytes) {
    if (!bgra || w <= 0 || h <= 0) return 0;
    const auto& lut = srgbTable();
    double sum = 0;
    for (int y = 0; y < h; ++y) {
        const std::uint8_t* row = bgra + std::size_t(y) * std::size_t(strideBytes);
        for (int x = 0; x < w; ++x) {
            const std::uint8_t* p = row + std::size_t(x) * 4;
            sum += kR * lut[p[2]] + kG * lut[p[1]] + kB * lut[p[0]];
        }
    }
    return sum / (double(w) * double(h));
}

float halfToFloat(std::uint16_t h) {
    const std::uint32_t sign = std::uint32_t(h & 0x8000) << 16;
    const std::uint32_t exp = (h >> 10) & 0x1F;
    const std::uint32_t mant = h & 0x3FF;
    float f;
    if (exp == 0) {
        f = std::ldexp(float(mant), -24);   // dénormalisé (ou zéro)
    } else if (exp == 31) {
        f = mant ? std::nanf("") : INFINITY;
    } else {
        f = std::ldexp(float(mant | 0x400), int(exp) - 25);
    }
    return sign ? -f : f;
}

double stripLuminanceHalf(const std::uint16_t* rgba, int w, int h, int strideElems, double sdrWhite) {
    if (!rgba || w <= 0 || h <= 0) return 0;
    const double white = sdrWhite > 0 ? sdrWhite : 1;
    double sum = 0;
    for (int y = 0; y < h; ++y) {
        const std::uint16_t* row = rgba + std::size_t(y) * std::size_t(strideElems);
        for (int x = 0; x < w; ++x) {
            const std::uint16_t* p = row + std::size_t(x) * 4;
            double r = std::clamp(double(halfToFloat(p[0])) / white, 0.0, 1.0);
            double g = std::clamp(double(halfToFloat(p[1])) / white, 0.0, 1.0);
            double b = std::clamp(double(halfToFloat(p[2])) / white, 0.0, 1.0);
            if (std::isnan(r)) r = 0;
            if (std::isnan(g)) g = 0;
            if (std::isnan(b)) b = 0;
            sum += kR * r + kG * g + kB * b;
        }
    }
    return sum / (double(w) * double(h));
}

bool chooseDarkText(double luminance, bool currentlyDark) {
    if (luminance > kDarkTextAbove) return true;
    if (luminance < kLightTextBelow) return false;
    return currentlyDark;
}

} // namespace md
