#include "metrics.h"

#include <cmath>

namespace md {

Metrics metricsFromJson(const json::Value& v) {
    Metrics m;
#define MD_READ(name, def)                                                   \
    if (auto* f = v.find(#name)) {                                           \
        double d = f->asNumber(m.name);                                      \
        if (std::isfinite(d)) m.name = d;                                    \
    }
    MD_METRICS_FIELDS(MD_READ)
#undef MD_READ
    return m;
}

json::Value metricsToJson(const Metrics& m) {
    json::Value v = json::Object{};
#define MD_WRITE(name, def) v.set(#name, m.name);
    MD_METRICS_FIELDS(MD_WRITE)
#undef MD_WRITE
    return v;
}

} // namespace md
