#include "dock_layout.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace md {

double magnifiedSize(double distance, double tile, double large, double range) {
    double d = std::fabs(distance);
    if (!(range > 0) || !(d < range)) return tile;
    return tile + (large - tile) * (1.0 + std::cos(std::numbers::pi * d / range)) / 2.0;
}

LayoutResult computeLayout(const LayoutInput& in) {
    LayoutResult r;
    const size_t n = in.items.size();
    const double sepSlot = in.separatorWidth + 2 * in.separatorMargin;
    r.thickness = in.tileSize + 2 * in.padding;

    // Présence de chaque élément (repli animé) et espace qui le précède.
    std::vector<double> presence(n), gapBefore(n);
    for (size_t i = 0; i < n; ++i) {
        double p = in.items[i].presence;
        presence[i] = std::isfinite(p) ? std::clamp(p, 0.0, 1.0) : 1.0;
        gapBefore[i] = i > 0 ? in.gap * presence[i] : 0;
    }

    // Largeur de chaque emplacement au repos.
    std::vector<double> restSlot(n);
    double total = 0;
    for (size_t i = 0; i < n; ++i) {
        restSlot[i] = (in.items[i].separator ? sepSlot : in.tileSize) * presence[i];
        total += restSlot[i] + gapBefore[i];
    }
    r.restLength = total + 2 * in.padding;
    r.items.resize(n);
    if (n == 0) {
        r.bgStart = -r.restLength / 2;
        r.bgEnd = r.restLength / 2;
        return r;
    }

    std::vector<double> restLeft(n);
    double x = -total / 2;
    for (size_t i = 0; i < n; ++i) {
        x += gapBefore[i];
        restLeft[i] = x;
        x += restSlot[i];
    }

    // Tailles agrandies.
    const double amount = std::clamp(std::isfinite(in.amount) ? in.amount : 0.0, 0.0, 1.0);
    const bool hasCursor = in.cursor && std::isfinite(*in.cursor) && amount > 0;
    const double range = in.rangeTiles * in.tileSize;
    std::vector<double> slot(restSlot);
    double cursor = 0;
    // Chaque emplacement possède la moitié des espaces qui l'entourent (aux extrémités : un demi-espace virtuel).
    auto halfBefore = [&](size_t i) { return in.gap * presence[i] / 2; };
    auto halfAfter = [&](size_t i) { return (i + 1 < n ? gapBefore[i + 1] : in.gap * presence[i]) / 2; };
    if (hasCursor) {
        const double lo = restLeft.front() - in.gap / 2;
        const double hi = restLeft.back() + restSlot.back() + in.gap / 2;
        cursor = std::clamp(*in.cursor, lo, hi);
        for (size_t i = 0; i < n; ++i) {
            if (in.items[i].separator) continue;
            double restCenter = restLeft[i] + restSlot[i] / 2;
            double full = magnifiedSize(cursor - restCenter, in.tileSize, in.largeSize, range);
            slot[i] = (in.tileSize + (full - in.tileSize) * amount) * presence[i];
        }
    }

    // Ancrage : le point sous le curseur reste sous le curseur.
    std::vector<double> left(n);
    if (!hasCursor) {
        left = restLeft;
    } else {
        size_t k = n - 1;
        for (size_t i = 0; i < n; ++i) {
            if (cursor <= restLeft[i] + restSlot[i] + halfAfter(i)) { k = i; break; }
        }
        double restStart = restLeft[k] - halfBefore(k);
        double restSpan = halfBefore(k) + restSlot[k] + halfAfter(k);
        double f = restSpan > 0 ? (cursor - restStart) / restSpan : 0.5;
        double newSpan = halfBefore(k) + slot[k] + halfAfter(k);
        left[k] = cursor - f * newSpan + halfBefore(k);
        for (size_t i = k; i-- > 0;) left[i] = left[i + 1] - gapBefore[i + 1] - slot[i];
        for (size_t i = k + 1; i < n; ++i) left[i] = left[i - 1] + slot[i - 1] + gapBefore[i];
    }

    for (size_t i = 0; i < n; ++i) {
        r.items[i].center = left[i] + slot[i] / 2;
        r.items[i].size = in.items[i].separator ? in.separatorWidth * presence[i] : slot[i];
        if (!in.items[i].separator && !in.items[i].placeholder) r.maxSize = std::max(r.maxSize, slot[i]);
    }
    r.bgStart = left.front() - in.padding;
    r.bgEnd = left.back() + slot.back() + in.padding;
    return r;
}

} // namespace md
