// Ligne de commande de MacDock.exe (logique pure).
#pragma once
#include <string>
#include <vector>

namespace md {

// Option de diagnostic qui attend une valeur (--snapshot f.png, --theme dark…) mais n'en a pas, ou écrite
// « --option=valeur » : renvoie l'argument fautif (vide si tout va bien). Le Dock ne doit alors pas démarrer.
std::wstring diagnosticMissingValue(const std::vector<std::wstring>& argv);

} // namespace md
