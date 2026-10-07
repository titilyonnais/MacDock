// Sélecteur d'apps (logique pure) : historique d'activation, pas, rangement du panneau, raccourci.
#pragma once
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "../spotlight/spot_results.h"

namespace md {

// Apps de la plus récemment activée à la plus ancienne (64 au plus).
class AppMru {
public:
    void touch(const std::wstring& appId);
    // Les apps de running déjà activées, de la plus récente à la plus ancienne, puis les autres dans l'ordre reçu.
    std::vector<std::wstring> order(const std::vector<std::wstring>& running) const;

private:
    std::vector<std::wstring> recent_;
};

std::size_t switcherStart(std::size_t count);   // la deuxième app (la précédente), ou la seule
std::size_t switcherStep(std::size_t selected, std::size_t count, int delta);   // en boucle

struct SwitcherGeometry {   // points, depuis le coin du panneau
    double icon = 64, cell = 88, pad = 12, labelH = 28, width = 0, height = 0;
};
// Rangée de count cases ; icônes réduites pour que le panneau tienne dans 90 % de maxWidth.
SwitcherGeometry switcherLayout(std::size_t count, double maxWidth);
int switcherHit(const SwitcherGeometry& g, std::size_t count, double x, double y);   // -1 : aucune case

// « alt+tab » (casse ignorée) ; nullopt pour « off » ou une valeur inconnue.
std::optional<HotkeySpec> parseSwitcherHotkey(const std::wstring& text);

} // namespace md
