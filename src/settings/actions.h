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
    bool restartExplorer = false;    // installation d'un mod : l'Explorateur redémarre pour le prendre
    std::wstring systemDir;          // GetSystemDirectory : PowerShell et l'Explorateur par leur chemin complet
    std::wstring windowsDir;         // GetWindowsDirectory
};

struct ActionCommand {
    std::wstring file, params;
    std::wstring verb = L"open";   // « runas » : Windows demande l'autorisation administrateur
    bool wait = false;             // attendre la fin avant la suivante (et relire l'état)
    // Puis attendre que MacDock soit vraiment arrêté (fenêtres et instances uniques disparues) : `--quit` rend la
    // main tout de suite, et le lanceur comme le Dock refusent une seconde instance.
    bool waitStopped = false;
    // Étape sans fichier : l'Explorateur de la session de l'utilisateur redémarré par l'app elle-même (jamais par un
    // processus administrateur).
    bool restartExplorer = false;
    // Code de sortie sans valeur d'erreur (recherche de mise à jour : 10 = version prête) ; l'état se relit ensuite.
    bool ignoreExitCode = false;
};

// Dans l'ordre ; vide pour une action faite dans la fenêtre, ou un mod inconnu.
std::vector<ActionCommand> actionCommands(PaneAction action, const ButtonContext& c);

// Instances uniques et fenêtres de MacDock (lanceur, Dock, barre de menus) : toutes disparues = MacDock arrêté.
const std::vector<std::wstring>& macdockMutexes();
const std::vector<std::wstring>& macdockWindows();

}  // namespace md
