// Actions des boutons de l'app Réglages (logique pure) : la ou les commandes à lancer pour chacune. La fenêtre les
// exécute (ShellExecuteEx) ; exporter, importer et rétablir se font dans la fenêtre (dialogues, feuille d'alerte).
#pragma once
#include <string>
#include <vector>

#include "panes.h"

namespace md {

struct ButtonContext {
    std::wstring exeDir;    // dossier de MacDockSettings.exe (MacDock.exe et le lanceur sont à côté)
    std::wstring dataDir;   // dossier des réglages (journaux dans logs\)
    std::wstring arg;       // identifiant du mod…
    bool restartExplorer = false;   // installation d'un mod : l'Explorateur redémarre pour le prendre
};

struct ActionCommand {
    std::wstring file, params;
    std::wstring verb = L"open";   // « runas » : Windows demande l'autorisation administrateur
    bool wait = false;             // attendre la fin avant la suivante (et relire l'état)
};

// Dans l'ordre ; vide pour une action faite dans la fenêtre, ou un mod inconnu.
std::vector<ActionCommand> actionCommands(PaneAction action, const ButtonContext& c);

}  // namespace md
