// Comparaison d'images BGRA (calibration contre des captures de macOS) (pur).
#pragma once
#include <cstdint>
#include <vector>

namespace md {

struct DiffStats {
    double meanAbs = 0;         // écart absolu moyen par canal (B, G, R)
    int maxAbs = 0;             // plus grand écart sur un canal
    double fractionAbove = 0;   // part des pixels dont l'écart max dépasse le seuil
    bool sameSize = false;      // false si une entrée est vide
};

// Écart par canal (B, G, R), alpha ignoré.
DiffStats diffImages(const std::uint8_t* a, const std::uint8_t* b, int w, int h, int threshold);

// Carte de différence : gris = identique, rouge = a plus clair, bleu = b plus clair ; intensité ∝ écart.
std::vector<std::uint8_t> diffHeatmap(const std::uint8_t* a, const std::uint8_t* b, int w, int h);

} // namespace md
