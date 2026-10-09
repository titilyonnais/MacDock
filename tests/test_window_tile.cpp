// « Déplacer et redimensionner » de macOS 26 (plan 50) : géométrie des fenêtres rangées, marges comprises.
#include <windows.h>

#include "minitest.h"
#include "../src/shell/window_tile.h"

namespace {
bool same(const RECT& a, const RECT& b) { return EqualRect(&a, &b) != FALSE; }
// Zone de travail d'un écran 1920 × 1080 sous une barre des menus de 48 px et au-dessus d'un Dock de 48 px.
constexpr RECT kWork{0, 48, 1920, 1032};
constexpr RECT kWindow{100, 100, 900, 700};
} // namespace

TEST_CASE(tile_halves_and_quarters_with_margins) {
    using md::TileAction;
    // Marges de 8 px : bord de la zone, puis entre les deux moitiés (956 → 964).
    CHECK(same(md::tileRect(TileAction::Left, kWork, kWindow, 8), RECT{8, 56, 956, 1024}));
    CHECK(same(md::tileRect(TileAction::Right, kWork, kWindow, 8), RECT{964, 56, 1912, 1024}));
    CHECK(same(md::tileRect(TileAction::Top, kWork, kWindow, 8), RECT{8, 56, 1912, 536}));
    CHECK(same(md::tileRect(TileAction::Bottom, kWork, kWindow, 8), RECT{8, 544, 1912, 1024}));
    CHECK(same(md::tileRect(TileAction::TopLeft, kWork, kWindow, 8), RECT{8, 56, 956, 536}));
    CHECK(same(md::tileRect(TileAction::TopRight, kWork, kWindow, 8), RECT{964, 56, 1912, 536}));
    CHECK(same(md::tileRect(TileAction::BottomLeft, kWork, kWindow, 8), RECT{8, 544, 956, 1024}));
    CHECK(same(md::tileRect(TileAction::BottomRight, kWork, kWindow, 8), RECT{964, 544, 1912, 1024}));
    CHECK(same(md::tileRect(TileAction::Fill, kWork, kWindow, 8), RECT{8, 56, 1912, 1024}));
    CHECK(same(md::tileRect(TileAction::Left, kWork, kWindow, 0), RECT{0, 48, 960, 1032}));   // sans marges
}

TEST_CASE(tile_center_keeps_size) {
    using md::TileAction;
    CHECK(same(md::tileRect(TileAction::Center, kWork, kWindow, 8), RECT{560, 240, 1360, 840}));   // 800 × 600 centrée
    // Plus grande que la zone : bornée, comme Remplir.
    CHECK(same(md::tileRect(TileAction::Center, kWork, RECT{0, 0, 3000, 2000}, 8), RECT{8, 56, 1912, 1024}));
    // Taille précédente : le placement la tient de son côté (la géométrie rend le cadre actuel).
    CHECK(same(md::tileRect(TileAction::Previous, kWork, kWindow, 8), kWindow));
}

TEST_CASE(tile_action_names) {
    for (auto a : {md::TileAction::Left, md::TileAction::Right, md::TileAction::Top, md::TileAction::Bottom,
                   md::TileAction::TopLeft, md::TileAction::TopRight, md::TileAction::BottomLeft,
                   md::TileAction::BottomRight, md::TileAction::Fill, md::TileAction::Center, md::TileAction::Previous})
        CHECK(md::parseTileAction(md::tileActionName(a)) == a);
    CHECK(!md::parseTileAction(L"diagonale").has_value());
}

TEST_CASE(tile_anchor_when_app_refuses_size) {
    // Relecture du plan 50 : une app dont la largeur minimale dépasse la moitié (Electron à 150 %) déborde de sa case ;
    // elle est recalée contre le bord visé, sans déborder de l'écran.
    using md::TileAction;
    const RECT right = md::tileRect(TileAction::Right, kWork, kWindow, 8);   // 964..1912
    const RECT got{964, 56, 2064, 1024};                                       // 1100 de large au lieu de 948
    CHECK(same(md::anchorTile(TileAction::Right, right, got), RECT{812, 56, 1912, 1024}));
    const RECT left = md::tileRect(TileAction::Left, kWork, kWindow, 8);
    CHECK(same(md::anchorTile(TileAction::Left, left, RECT{8, 56, 1108, 1024}), RECT{8, 56, 1108, 1024}));
    const RECT br = md::tileRect(TileAction::BottomRight, kWork, kWindow, 8);   // 964..1912 × 544..1024
    CHECK(same(md::anchorTile(TileAction::BottomRight, br, RECT{964, 544, 2064, 1124}), RECT{812, 444, 1912, 1024}));
}
