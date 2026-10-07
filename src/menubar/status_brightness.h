// Luminosité de l'écran principal : WMI (écran intégré d'un portable), sinon DDC/CI (écran externe). Lent (DDC/CI :
// des dizaines de millisecondes) : fil de travail (StatusHub), COM initialisé.
#pragma once
#include <optional>

namespace md {

std::optional<double> readBrightness();   // 0..1 ; rien si aucun moyen de la régler
bool setBrightness(double v);             // borné à [0, 1]

} // namespace md
