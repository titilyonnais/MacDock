#include "metrics.h"

#include <algorithm>
#include <cmath>

namespace md {

Metrics metricsFromJson(const json::Value& v) {
    Metrics m;
#define MD_READ(name, def, lo, hi)                                           \
    if (auto* f = v.find(#name)) {                                           \
        double d = f->asNumber(m.name);                                      \
        if (std::isfinite(d)) m.name = std::clamp(d, double(lo), double(hi)); \
    }
    MD_METRICS_FIELDS(MD_READ)
#undef MD_READ
    return m;
}

json::Value metricsToJson(const Metrics& m) {
    json::Value v = json::Object{};
    v.set("version", kMetricsVersion);
#define MD_WRITE(name, def, lo, hi) v.set(#name, m.name);
    MD_METRICS_FIELDS(MD_WRITE)
#undef MD_WRITE
    return v;
}

bool metricsJsonComplete(const json::Value& v) {
#define MD_HAS(name, def, lo, hi) if (!v.find(#name)) return false;
    MD_METRICS_FIELDS(MD_HAS)
#undef MD_HAS
    return true;
}

int jsonVersion(const json::Value& v) {
    auto* f = v.find("version");
    double d = f ? f->asNumber(1) : 1;
    return std::isfinite(d) && d >= 1 ? int(d) : 1;
}

json::Value migrateMetricsJson(const json::Value& v) {
    if (!v.isObject() || jsonVersion(v) >= kMetricsVersion) return v;
    // Défauts changés, version par version (détail en regard de chaque groupe) : plan 1 en v2, verre Golden Gate en v3,
    // verre recalé en v4, retour à Tahoe en v5, icônes de macOS 26 en v6, proportions du Dock de macOS 26 en v7, aucun
    // contour blanc en v8, cotes du Dock mesurées à trois tailles en v9. Une valeur encore égale à l'ancien défaut suit
    // le nouveau ; une valeur choisie par l'utilisateur reste.
    struct Changed { int before; const char* key; double oldDefault; double newDefault; };
    static const Changed kChanged[] = {
        {2, "iconGap", 6, 4},
        {2, "dockCornerRadius", 24, 0},
        {2, "separatorLengthRatio", 0.72, 0.80},
        {2, "tooltipGap", 12, 10},
        {3, "glassTintLight", 0.22, 0.30},
        {3, "glassTintDark", 0.30, 0.38},
        {3, "glassSpecular", 0.55, 0.70},
        {3, "autohideShowSeconds", 0.45, 0.40},   // animations Golden Gate ~12 % plus courtes
        {3, "autohideHideSeconds", 0.45, 0.40},
        {3, "poofSeconds", 0.35, 0.31},
        // v4 : verre recalé sur des captures du Dock réel (liseré d'un pixel, voile léger, fond saturé, ombre
        // à peine visible).
        {4, "glassBevel", 9, 4},
        {4, "glassChromatic", 0.10, 0.05},
        {4, "glassFresnel", 0.18, 0.04},
        {4, "glassSpecular", 0.70, 0.75},
        {4, "glassTintLight", 0.30, 0.10},
        {4, "glassTintDark", 0.38, 0.22},
        {4, "glassSaturation", 1.15, 1.50},
        {4, "shadowOpacity", 0.22, 0.07},
        // v5 : la cible redevient macOS 26 Tahoe (plan 44) ; les durées raccourcies pour Golden Gate reprennent leur
        // valeur. Le verre de la v4, recalé sur des captures, reste en attendant celles de Tahoe.
        {5, "autohideShowSeconds", 0.40, 0.45},
        {5, "autohideHideSeconds", 0.40, 0.45},
        {5, "poofSeconds", 0.31, 0.35},
        // v6 : rayon des icônes mesuré sur le Dock de macOS 26 (plan 60) ; 0,225 était celui de Big Sur à Sequoia.
        {6, "iconCornerRatio", 0.225, 0.259},
        // v7 : proportions du Dock mesurées sur macOS 26 (plan 61), données pour la taille 48.
        {7, "dockPadding", 8, 7.1},
        {7, "iconGap", 4, 2.3},
        {7, "separatorMargin", 7, 9.9},
        {7, "separatorLengthRatio", 0.80, 0.95},
        {7, "indicatorCenterFromBottom", 4, 4.7},
        {7, "dockScreenMargin", 5, 3.9},
        // v9 : cotes du Dock de macOS 26 mesurées à 48, 64 et 71 sur la VM en plein écran (plan 70). Rembourrage, écart,
        // écart sous le Dock et point sont fixes en points (la v7 les avait ramenés à 48 en proportion) ; la longueur du
        // séparateur se rapporte à l'épaisseur du Dock, et non plus à la case.
        {9, "dockPadding", 7.1, 10},
        {9, "iconGap", 2.3, 4},
        {9, "dockScreenMargin", 3.9, 5},
        {9, "indicatorCenterFromBottom", 4.7, 6},
        {9, "separatorLengthRatio", 0.95, 0.738},
    };
    // v8 : retour de l'utilisateur du 10 octobre, le Liquid Glass de macOS 26 n'a pas de contour blanc : reflet du
    // liseré (verre GPU) et liseré clair (rendu de repli) à 0, quelle que soit la valeur d'avant.
    struct Forced { int before; const char* key; double value; };
    static const Forced kForced[] = {{8, "glassSpecular", 0}, {8, "borderOpacity", 0}};
    const int from = jsonVersion(v);
    json::Value out = json::Object{};
    for (auto& [key, value] : v.asObject()) {
        if (key == "indicatorInset" || key == "version") continue;
        if (from < 9 && key == "separatorMargin") continue;   // v9 : la marge du séparateur suit la taille
        json::Value copy = value;
        for (auto& c : kChanged) {
            if (from >= c.before || key != c.key || !copy.isNumber()) continue;
            if (std::fabs(copy.asNumber(0) - c.oldDefault) >= 1e-9) break;   // choisie par l'utilisateur
            copy = c.newDefault;   // défaut de l'époque : suit les étapes suivantes
        }
        for (auto& f : kForced)
            if (from < f.before && key == f.key && copy.isNumber()) copy = f.value;
        out.set(key, std::move(copy));
    }
    out.set("version", kMetricsVersion);
    return out;
}

} // namespace md
