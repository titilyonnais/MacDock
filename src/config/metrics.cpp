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
    // Défauts changés : ceux du plan 1 (v1) en v2, le verre Golden Gate (plus opaque, reflet plus vif) en v3. Une
    // valeur encore égale à l'ancien défaut suit le nouveau ; une valeur choisie par l'utilisateur reste.
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
    };
    const int from = jsonVersion(v);
    json::Value out = json::Object{};
    for (auto& [key, value] : v.asObject()) {
        if (key == "indicatorInset" || key == "version") continue;
        json::Value copy = value;
        for (auto& c : kChanged)
            if (from < c.before && key == c.key && copy.isNumber() && std::fabs(copy.asNumber(0) - c.oldDefault) < 1e-9)
                copy = c.newDefault;
        out.set(key, std::move(copy));
    }
    out.set("version", kMetricsVersion);
    return out;
}

} // namespace md
