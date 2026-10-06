// Mesures visuelles du Dock (dock-metrics.json), en points macOS.
// Valeurs de départ à calibrer contre les références de macOS Tahoe.
#pragma once
#include "../core/json.h"

namespace md {

// X-macro : (nom, valeur par défaut). Le nom sert aussi de clé JSON.
#define MD_METRICS_FIELDS(X)              \
    X(iconGap, 6)                         \
    X(dockPadding, 8)                     \
    X(dockCornerRadius, 24)               \
    X(dockScreenMargin, 5)                \
    X(separatorWidth, 1)                  \
    X(separatorMargin, 7)                 \
    X(separatorLengthRatio, 0.72)         \
    X(magnifyRangeTiles, 3.0)             \
    X(magnifyStiffness, 420)              \
    X(magnifyDamping, 38)                 \
    X(indicatorDiameter, 4)               \
    X(indicatorInset, 3)                  \
    X(launchBounceHeight, 0.55)           \
    X(launchBouncePeriod, 0.62)           \
    X(launchTimeout, 12.0)                \
    X(attentionBounceHeight, 1.05)        \
    X(attentionBouncePeriod, 0.62)        \
    X(attentionBounceCount, 3)            \
    X(attentionPause, 1.2)                \
    X(tooltipGap, 12)                     \
    X(tooltipPadX, 11)                    \
    X(tooltipPadY, 5)                     \
    X(tooltipFontSize, 13)                \
    X(tooltipFadeSeconds, 0.12)           \
    X(bgOpacityLight, 0.32)               \
    X(bgOpacityDark, 0.26)                \
    X(borderOpacity, 0.55)                \
    X(shadowOpacity, 0.22)                \
    X(shadowBlur, 18)                     \
    X(iconJailInset, 0.16)

struct Metrics {
#define MD_DECLARE(name, def) double name = def;
    MD_METRICS_FIELDS(MD_DECLARE)
#undef MD_DECLARE
};

Metrics metricsFromJson(const json::Value& v);
json::Value metricsToJson(const Metrics& m);

} // namespace md
