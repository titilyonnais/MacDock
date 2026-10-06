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

    // Largeur de chaque emplacement au repos.
    std::vector<double> restSlot(n);
    double total = 0;
    for (size_t i = 0; i < n; ++i) {
        restSlot[i] = in.items[i].separator ? sepSlot : in.tileSize;
        total += restSlot[i];
    }
    if (n > 1) total += in.gap * double(n - 1);
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
        restLeft[i] = x;
        x += restSlot[i] + in.gap;
    }

    // Tailles agrandies.
    const double amount = std::clamp(std::isfinite(in.amount) ? in.amount : 0.0, 0.0, 1.0);
    const bool hasCursor = in.cursor && std::isfinite(*in.cursor) && amount > 0;
    const double range = in.rangeTiles * in.tileSize;
    std::vector<double> slot(restSlot);
    double cursor = 0;
    if (hasCursor) {
        const double lo = restLeft.front() - in.gap / 2;
        const double hi = restLeft.back() + restSlot.back() + in.gap / 2;
        cursor = std::clamp(*in.cursor, lo, hi);
        for (size_t i = 0; i < n; ++i) {
            if (in.items[i].separator) continue;
            double restCenter = restLeft[i] + restSlot[i] / 2;
            double full = magnifiedSize(cursor - restCenter, in.tileSize, in.largeSize, range);
            slot[i] = in.tileSize + (full - in.tileSize) * amount;
        }
    }

    // Ancrage : le point sous le curseur reste sous le curseur.
    std::vector<double> left(n);
    if (!hasCursor) {
        left = restLeft;
    } else {
        // Chaque emplacement possède la moitié des espaces qui l'entourent.
        size_t k = n - 1;
        for (size_t i = 0; i < n; ++i) {
            if (cursor <= restLeft[i] + restSlot[i] + in.gap / 2) { k = i; break; }
        }
        double restStart = restLeft[k] - in.gap / 2;
        double f = (cursor - restStart) / (restSlot[k] + in.gap);
        double newStart = cursor - f * (slot[k] + in.gap);
        left[k] = newStart + in.gap / 2;
        for (size_t i = k; i-- > 0;) left[i] = left[i + 1] - in.gap - slot[i];
        for (size_t i = k + 1; i < n; ++i) left[i] = left[i - 1] + slot[i - 1] + in.gap;
    }

    for (size_t i = 0; i < n; ++i) {
        r.items[i].center = left[i] + slot[i] / 2;
        r.items[i].size = in.items[i].separator ? in.separatorWidth : slot[i];
        if (!in.items[i].separator) r.maxSize = std::max(r.maxSize, slot[i]);
    }
    r.bgStart = left.front() - in.padding;
    r.bgEnd = left.back() + slot.back() + in.padding;
    return r;
}

} // namespace md
