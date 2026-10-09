// Conversions d'unités de Spotlight (logique pure) : « 10 km » → 6,21371 mi, « 100 °F en °C » → 37,7778 °C.
#pragma once
#include <optional>
#include <string>
#include <string_view>

namespace md {

struct UnitConversion {
    double amount = 0;      // nombre tapé
    double value = 0;       // résultat, dans l'unité d'arrivée
    std::wstring from, to;  // symboles affichés : « km », « mi »
};

// « <nombre> <unité> » (vers l'unité usuelle de l'autre système ; durées : la plus grande unité où le nombre vaut au
// moins 1) ou « <nombre> <unité> en|in|to|vers|→|-> <unité> ». Longueurs, masses, températures, volumes, surfaces,
// vitesses, durées, données, énergies, pressions. Symboles en casse exacte, noms sans casse ni accents.
// nullopt : pas une conversion (texte, nombre seul, unité inconnue, catégories différentes, nombre infini).
std::optional<UnitConversion> convertUnits(std::wstring_view query);

} // namespace md
