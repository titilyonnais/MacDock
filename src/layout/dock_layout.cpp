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

    // Loupe continue, comme sous macOS : chaque point du Dock au repos est étiré d'un facteur 1 + a·(1 + cos)/2 selon
    // sa distance au curseur, et sa place est l'intégrale de ce facteur depuis le curseur. Icônes, espaces et
    // séparateurs sont traversés de la même façon : le point sous le curseur y reste, et tant que la loupe est à
    // l'intérieur du Dock, ses bords ne bougent pas (l'ancrage case par case faisait glisser tout le Dock à chaque
    // séparateur). La taille d'une icône est sa largeur étirée : jamais de chevauchement.
    const double amount = std::clamp(std::isfinite(in.amount) ? in.amount : 0.0, 0.0, 1.0);
    const bool hasCursor = in.cursor && std::isfinite(*in.cursor) && amount > 0;
    const double range = std::max(0.0, in.rangeTiles * in.tileSize);
    double cursor = 0, a = 0;
    if (hasCursor) {
        const double lo = restLeft.front() - in.gap / 2;
        const double hi = restLeft.back() + restSlot.back() + in.gap / 2;
        cursor = std::clamp(*in.cursor, lo, hi);
        if (in.tileSize > 0 && range > 0) {
            // Moyenne du cosinus surélevé sur une case centrée : l'icône sous le curseur atteint largeSize, ni plus
            // ni moins.
            const double h = std::min(in.tileSize / 2, range), arc = std::numbers::pi * h / range;
            const double mean = (1 + std::sin(arc) / arc) / 2;
            a = std::max(-0.99, (in.largeSize / in.tileSize - 1) * amount / mean);   // étirement toujours positif
        }
    }
    auto place = [&](double u) {
        if (!hasCursor) return u;
        const double d = u - cursor, e = std::clamp(d, -range, range);
        return cursor + d + (a != 0 ? a / 2 * (e + range / std::numbers::pi * std::sin(std::numbers::pi * e / range)) : 0);
    };
    std::vector<double> left(n), slot(n);
    for (size_t i = 0; i < n; ++i) {
        left[i] = place(restLeft[i]);
        slot[i] = place(restLeft[i] + restSlot[i]) - left[i];
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
