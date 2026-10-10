// Poignée des séparateurs du Dock (plan 70) : la taille des icônes suit le pointeur, comme sur macOS 26.
#include "minitest.h"
#include "../src/layout/dock_resize.h"

TEST_CASE(dock_resize_follows_the_pointer_one_for_one) {
    using md::DockPosition;
    // Dock en bas : en montant de 10 points, les icônes grandissent de 10 points.
    CHECK_NEAR(md::resizedTileSize(48, 1000, 990, DockPosition::Bottom), 58, 1e-9);
    CHECK_NEAR(md::resizedTileSize(48, 1000, 1030, DockPosition::Bottom), 18, 1e-9);
    CHECK_NEAR(md::resizedTileSize(48, 1000, 1000, DockPosition::Bottom), 48, 1e-9);
    // Bornée comme le réglage (16 à 128).
    CHECK_NEAR(md::resizedTileSize(48, 1000, 1100, DockPosition::Bottom), 16, 1e-9);
    CHECK_NEAR(md::resizedTileSize(48, 1000, 800, DockPosition::Bottom), 128, 1e-9);
    // Sur un côté : en s'éloignant du bord de l'écran.
    CHECK_NEAR(md::resizedTileSize(48, 100, 110, DockPosition::Left), 58, 1e-9);
    CHECK_NEAR(md::resizedTileSize(48, 100, 110, DockPosition::Right), 38, 1e-9);
    CHECK_NEAR(md::resizedTileSize(48, 100, 90, DockPosition::Right), 58, 1e-9);
}
