#include <cmath>

#include "minitest.h"
#include "../src/layout/dock_geometry.h"
#include "../src/layout/dock_layout.h"

TEST_CASE(geometry_concentric_default) {
    md::Metrics m;
    auto g = md::dockGeometry(48, m);
    CHECK_NEAR(g.thickness, 48 + 2 * 10.0, 1e-9);   // rembourrage de macOS 26, mesuré à trois tailles (plan 70)
    CHECK_NEAR(g.iconShape, 38.625, 1e-9);
    CHECK_NEAR(g.iconRadius, 38.625 * 0.259, 1e-9);   // rayon des icônes de macOS 26 (plan 60)
    CHECK_NEAR(g.visibleInset, 4.6875, 1e-9);
    // Concentrique (borné à la demi-épaisseur) ; le rendu le ramène ensuite au plus grand coin continu qui tient dans la
    // hauteur (épaisseur / 3,057), la forme des extrémités du Dock de macOS 26 (plan 61). Au-delà de 117 pt, le rayon
    // concentrique passe un peu sous ce plafond (46,28 contre 46,51 pt à 128) : écart invisible.
    CHECK_NEAR(g.cornerRadius, std::min(38.625 * 0.259 + 10 + 4.6875, (48 + 2 * 10.0) / 2), 1e-9);
    CHECK_NEAR(g.indicatorCenter, 6, 1e-9);
    // Le point ne touche pas l'icône : au moins 5 pt entre le haut du point et le bas de la forme visible.
    CHECK(m.dockPadding + g.visibleInset - (g.indicatorCenter + m.indicatorDiameter / 2) >= 5);
}

TEST_CASE(geometry_fixed_radius_is_clamped) {
    md::Metrics m;
    m.dockCornerRadius = 30;
    CHECK_NEAR(md::dockGeometry(48, m).cornerRadius, 30, 1e-9);
    m.dockCornerRadius = 100;
    CHECK_NEAR(md::dockGeometry(48, m).cornerRadius, (48 + 2 * m.dockPadding) / 2, 1e-9);   // demi-épaisseur
}

TEST_CASE(geometry_extreme_tile_sizes) {
    md::Metrics m;
    for (double t : {16.0, 128.0, 0.0, -5.0, double(NAN)}) {
        auto g = md::dockGeometry(t, m);
        CHECK(std::isfinite(g.cornerRadius));
        CHECK(g.cornerRadius >= 0);
        CHECK(g.cornerRadius <= g.thickness / 2 + 1e-9);
    }
}

namespace {
// Pas entre deux icônes et distance du centre d'une icône à celui du séparateur qui la suit, à la taille t, avec les
// mesures par défaut, comme le contrôleur remplit la mise en page.
struct Spacing {
    double pitch = 0, toSeparator = 0;
};
Spacing spacingAt(double t) {
    md::Metrics m;
    const md::DockGeometry g = md::dockGeometry(t, m);
    md::LayoutInput in;
    in.items = {{false}, {false}, {true}, {false}};
    in.tileSize = in.largeSize = t;
    in.gap = m.iconGap;
    in.padding = m.dockPadding;
    in.separatorWidth = m.separatorWidth;
    in.separatorMargin = g.separatorMargin;
    const md::LayoutResult r = md::computeLayout(in);
    return {r.items[1].center - r.items[0].center, r.items[2].center - r.items[1].center};
}
} // namespace

TEST_CASE(geometry_matches_macos_26_at_48_and_64) {
    // Dock de macOS 26 mesuré sur la VM en plein écran (plan 70) : rembourrage, écart, écart sous le Dock et point fixes
    // en points ; place et longueur du séparateur selon la taille.
    md::Metrics m;
    const md::DockGeometry g48 = md::dockGeometry(48, m), g64 = md::dockGeometry(64, m);
    CHECK_NEAR(g48.thickness, 68, 1e-9);
    CHECK_NEAR(g64.thickness, 84, 1e-9);
    CHECK_NEAR(g48.separatorLength, 50, 0.3);
    CHECK_NEAR(g64.separatorLength, 62, 0.3);
    CHECK_NEAR(g48.indicatorCenter, 6, 1e-9);
    CHECK_NEAR(m.indicatorDiameter, 4, 1e-9);
    CHECK_NEAR(m.dockScreenMargin, 5, 1e-9);
    const Spacing s48 = spacingAt(48), s64 = spacingAt(64);
    CHECK_NEAR(s48.pitch, 52, 1e-9);
    CHECK_NEAR(s64.pitch, 68, 1e-9);
    CHECK_NEAR(s48.toSeparator, 37.5, 1e-9);
    CHECK_NEAR(s64.toSeparator, 49.5, 1e-9);
    CHECK_NEAR(spacingAt(70.8).toSeparator, 54, 0.7);   // Dock rétréci par macOS (taille 96 sur un écran trop étroit)
    CHECK(md::dockGeometry(16, m).separatorMargin >= 0);   // jamais négative aux petites tailles
}
