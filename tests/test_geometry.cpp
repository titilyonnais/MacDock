#include <cmath>

#include "minitest.h"
#include "../src/layout/dock_geometry.h"

TEST_CASE(geometry_concentric_default) {
    md::Metrics m;
    auto g = md::dockGeometry(48, m);
    CHECK_NEAR(g.thickness, 64, 1e-9);
    CHECK_NEAR(g.iconShape, 38.625, 1e-9);
    CHECK_NEAR(g.iconRadius, 8.690625, 1e-9);
    CHECK_NEAR(g.visibleInset, 4.6875, 1e-9);
    CHECK_NEAR(g.cornerRadius, 8.690625 + 8 + 4.6875, 1e-9);
    CHECK_NEAR(g.indicatorCenter, 4, 1e-9);
    // Le point ne touche pas l'icône : au moins 5 pt entre le haut du point et le bas de la forme visible.
    CHECK(m.dockPadding + g.visibleInset - (g.indicatorCenter + m.indicatorDiameter / 2) >= 5);
}

TEST_CASE(geometry_fixed_radius_is_clamped) {
    md::Metrics m;
    m.dockCornerRadius = 30;
    CHECK_NEAR(md::dockGeometry(48, m).cornerRadius, 30, 1e-9);
    m.dockCornerRadius = 100;
    CHECK_NEAR(md::dockGeometry(48, m).cornerRadius, 32, 1e-9);
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
