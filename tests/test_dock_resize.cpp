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

TEST_CASE(dock_resize_never_beyond_what_fits) {
    // Relecture du plan 70 : sur un Dock qui ne tient plus, la taille montait jusqu'à 128 pendant qu'il s'amincissait.
    CHECK_NEAR(md::resizedTileSize(48, 1000, 800, md::DockPosition::Bottom, 65), 65, 1e-9);
    CHECK_NEAR(md::resizedTileSize(48, 1000, 990, md::DockPosition::Bottom, 65), 58, 1e-9);
}

TEST_CASE(dock_resize_snaps_toward_the_start) {
    // Points entiers vers la taille de départ : le tremblement d'un simple clic (un pixel à 200 %) ne change rien.
    CHECK_NEAR(md::snappedTileSize(48, 48.5), 48, 1e-9);
    CHECK_NEAR(md::snappedTileSize(48, 49.4), 49, 1e-9);
    CHECK_NEAR(md::snappedTileSize(48, 47.5), 48, 1e-9);
    CHECK_NEAR(md::snappedTileSize(48, 46.4), 47, 1e-9);
    CHECK_NEAR(md::snappedTileSize(59, 60.99), 60, 1e-9);
}

TEST_CASE(dock_resize_release_ends_the_drag_first) {
    // Relecture du plan 70 : un relâchement à avaler (appui qui a fermé Spotlight) passait avant la fin du glisser de la
    // poignée, qui restait alors bloqué.
    using md::ButtonUp;
    CHECK(md::leftButtonUpAction(true, true) == ButtonUp::EndResize);
    CHECK(md::leftButtonUpAction(true, false) == ButtonUp::EndResize);
    CHECK(md::leftButtonUpAction(false, true) == ButtonUp::Swallow);
    CHECK(md::leftButtonUpAction(false, false) == ButtonUp::Pointer);
}

TEST_CASE(dock_resize_setting_from_a_full_dock) {
    // Essai réel du 10 octobre : Dock plein (réglé à 59, affiché à 58,26), glisser de 20 pt vers le bas → 38,26 enregistré.
    // Le réglage est toujours un point entier ; sans mouvement, il ne change pas (le Dock affiché à 58,26 reste à 59).
    CHECK_NEAR(md::resizedSetting(59, 58.26, 58.26), 59, 1e-9);
    CHECK_NEAR(md::resizedSetting(59, 58.26, 38.26), 38, 1e-9);
    CHECK_NEAR(md::resizedSetting(48, 48, 58), 58, 1e-9);
    CHECK_NEAR(md::resizedSetting(48, 48, 48), 48, 1e-9);
}
