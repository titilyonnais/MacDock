// Mesures visuelles du Dock (dock-metrics.json), en points macOS.
// Valeurs de départ à calibrer contre les références de macOS Tahoe.
#pragma once
#include "../core/json.h"

namespace md {

// X-macro : (nom, défaut, minimum, maximum). Le nom sert aussi de clé JSON.
// Les bornes protègent contre une faute de frappe (division par zéro, ressort qui ne s'arrête jamais).
#define MD_METRICS_FIELDS(X)                       \
    X(iconGap, 6, 0, 64)                           \
    X(dockPadding, 8, 0, 64)                       \
    X(dockCornerRadius, 24, 0, 200)                \
    X(dockScreenMargin, 5, 0, 100)                 \
    X(separatorWidth, 1, 0.5, 10)                  \
    X(separatorMargin, 7, 0, 50)                   \
    X(separatorLengthRatio, 0.72, 0, 1)            \
    X(magnifyRangeTiles, 3.0, 0.5, 10)             \
    X(magnifyStiffness, 420, 1, 5000)              \
    X(magnifyDamping, 38, 1, 500)                  \
    X(indicatorDiameter, 4, 0, 20)                 \
    X(indicatorInset, 3, 0, 30)                    \
    X(launchBounceHeight, 0.55, 0, 3)              \
    X(launchBouncePeriod, 0.62, 0.05, 5)           \
    X(launchTimeout, 12.0, 0, 120)                 \
    X(attentionBounceHeight, 1.05, 0, 3)           \
    X(attentionBouncePeriod, 0.62, 0.05, 5)        \
    X(attentionBounceCount, 3, 0, 20)              \
    X(attentionPause, 1.2, 0, 30)                  \
    X(tooltipGap, 12, 0, 100)                      \
    X(tooltipPadX, 11, 0, 50)                      \
    X(tooltipPadY, 5, 0, 50)                       \
    X(tooltipFontSize, 13, 6, 48)                  \
    X(tooltipFadeSeconds, 0.12, 0, 2)              \
    X(bgOpacityLight, 0.32, 0, 1)                  \
    X(bgOpacityDark, 0.26, 0, 1)                   \
    X(borderOpacity, 0.55, 0, 1)                   \
    X(shadowOpacity, 0.22, 0, 1)                   \
    X(shadowBlur, 18, 0, 200)                      \
    X(iconJailInset, 0.16, 0, 0.4)

struct Metrics {
#define MD_DECLARE(name, def, lo, hi) double name = def;
    MD_METRICS_FIELDS(MD_DECLARE)
#undef MD_DECLARE
};

Metrics metricsFromJson(const json::Value& v);
json::Value metricsToJson(const Metrics& m);

} // namespace md
