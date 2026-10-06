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

int jsonVersion(const json::Value& v) {
    auto* f = v.find("version");
    double d = f ? f->asNumber(1) : 1;
    return std::isfinite(d) && d >= 1 ? int(d) : 1;
}

json::Value migrateMetricsJson(const json::Value& v) {
    if (!v.isObject() || jsonVersion(v) >= kMetricsVersion) return v;
    // Défauts du plan 1 (v1) qui ont changé en v2.
    struct Changed { const char* key; double oldDefault; double newDefault; };
    static const Changed kChanged[] = {
        {"iconGap", 6, 4},
        {"dockCornerRadius", 24, 0},
        {"separatorLengthRatio", 0.72, 0.80},
        {"tooltipGap", 12, 10},
    };
    json::Value out = json::Object{};
    for (auto& [key, value] : v.asObject()) {
        if (key == "indicatorInset" || key == "version") continue;
        json::Value copy = value;
        for (auto& c : kChanged)
            if (key == c.key && value.isNumber() && std::fabs(value.asNumber(0) - c.oldDefault) < 1e-9)
                copy = c.newDefault;
        out.set(key, std::move(copy));
    }
    out.set("version", kMetricsVersion);
    return out;
}

} // namespace md
