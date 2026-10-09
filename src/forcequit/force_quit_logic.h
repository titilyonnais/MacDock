// Forcer à quitter (plan 54), logique pure : les fenêtres ouvertes regroupées en apps comme dans le Dock, l'ordre de
// la liste, les apps figées, les processus à arrêter et la sélection après une relecture de la liste.
#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace md {

// Une fenêtre éligible au Dock, ou la fenêtre figée qu'une fenêtre fantôme de Windows remplace.
struct FqWindow {
    std::wstring appId, name, launch, aumid, exePath;
    unsigned long pid = 0;   // processus de l'app (celui de la CoreWindow pour une app du Store) ; 0 : inconnu
    bool hung = false;       // ne répond pas
};

struct FqApp {
    std::wstring appId, name, launch, aumid, exePath;
    std::vector<unsigned long> pids;   // propriétaires de ses fenêtres : triés, sans doublon ni 0
    bool hung = false;                 // une de ses fenêtres ne répond pas
    bool explorer = false;             // l'Explorateur, le Finder de Windows : « Relancer »
};

bool isExplorerExe(std::wstring_view exePath);   // …\explorer.exe, casse ignorée

// Apps de la liste : fenêtres regroupées par appId (nom, cible et AUMID de la première), triées par nom dans l'ordre de
// la langue (casse et accents ignorés), puis l'Explorateur en dernier. `explorer` : l'Explorateur de la session, listé
// même sans fenêtre ouverte, comme le Finder (ses fenêtres s'y ajoutent) ; appId vide : pas d'Explorateur.
std::vector<FqApp> forceQuitApps(const std::vector<FqWindow>& windows, const FqApp& explorer);

// Processus de la session : PID, AUMID (app empaquetée seulement) et exécutable.
struct FqProcess {
    unsigned long pid = 0;
    std::wstring aumid, exePath;
};
// Processus à arrêter pour une app : les propriétaires de ses fenêtres ; sans aucun (app du Store réduite), les
// processus de même AUMID. Jamais un PID absent de `processes`, 0 ni 4, ni `self`, ni l'Explorateur,
// ApplicationFrameHost, dwm, csrss, winlogon, ni les processus de MacDock (Dock, barre, lanceur).
std::vector<unsigned long> forceQuitTargets(const FqApp& app, const std::vector<FqProcess>& processes, unsigned long self);

// Sélection après une relecture de la liste : la même app si elle est toujours là ; partie, celle qui la suivait (sinon
// celle qui la précédait) ; -1 sans sélection ou liste vide.
int selectionAfter(const std::vector<FqApp>& before, int selected, const std::vector<FqApp>& after);

} // namespace md
