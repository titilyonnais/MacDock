// Relance de l'Explorateur de la session (bureau, barre des tâches, fenêtres de dossiers) : après l'installation d'un mod
// Windhawk (app Réglages), ou « Relancer » dans Forcer à quitter, comme le Finder de macOS.
#pragma once
#include <string>

namespace md {

// Arrête l'Explorateur de cette session seulement, sans droits élevés ; Windows le relance de lui-même en une à deux
// secondes, sinon `windowsDir`\explorer.exe est lancé. Bloque jusqu'à 5 s.
void restartExplorer(const std::wstring& windowsDir);

} // namespace md
