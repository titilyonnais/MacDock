// Grille d'icône Apple : forme visible centrée dans sa case, ombre portée (pur).
#pragma once
#include <cstdint>
#include <vector>

namespace md {

// Côté de la forme visible pour une case de tilePx : max(1, lround(tilePx·shapeRatio)).
int iconShapePx(int tilePx, double shapeRatio);

// Copie content (carré shape x shape, BGRA prémultiplié) au centre d'une case tile x tile transparente.
std::vector<std::uint8_t> placeOnGrid(const std::vector<std::uint8_t>& content, int shape, int tile);

// Ombre portée : alpha flouté (gaussienne séparable σ = sigmaPx), décalé de offsetYPx, noir à opacity,
// composé SOUS l'image. opacity <= 0 : image inchangée.
void addDropShadow(std::vector<std::uint8_t>& bgra, int size, double sigmaPx, double offsetYPx, double opacity);

} // namespace md
