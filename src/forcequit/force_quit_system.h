// Forcer à quitter (plan 54), côté Windows : fenêtres ouvertes (fenêtres fantômes comprises), processus de la session
// et leur AUMID, l'Explorateur de la session, arrêt des processus.
#pragma once
#include <windows.h>

#include <functional>
#include <string>
#include <vector>

#include "force_quit_logic.h"

namespace md {

// Fenêtres éligibles au Dock des autres processus, figées ou non ; une fenêtre fantôme (« Ghost », posée par Windows sur
// une fenêtre qui ne répond plus) compte pour la fenêtre figée qu'elle remplace, jamais pour elle-même.
std::vector<FqWindow> forceQuitWindows();

// L'Explorateur de la session (le processus du bureau et ceux de ses fenêtres) ; appId vide s'il ne tourne pas.
FqApp sessionExplorer();

// appId, dans la liste, de l'app d'une fenêtre (celle du premier plan à l'ouverture, choisie d'abord) : sa fenêtre
// principale, la fenêtre figée derrière une fenêtre fantôme, l'Explorateur pour le bureau et ses fenêtres ; vide sinon.
std::wstring forceQuitAppIdOf(HWND window);

// Processus de la session, avec leur exécutable et, s'ils sont empaquetés, leur AUMID.
std::vector<FqProcess> sessionProcesses();

// Arrête ces processus et attend leur fin (1 s au plus en tout). Chacun est vérifié une fois ouvert : un PID repris par
// un autre programme que `exePath` (l'app est partie entre-temps) n'est jamais arrêté. Un processus déjà parti n'est
// pas une erreur ; 0, 4 et ce processus ne sont jamais touchés. false si Windows refuse (app lancée en
// administrateur…) : `error` dit pourquoi.
bool terminateProcesses(const std::vector<FqProcess>& targets, std::wstring* error);

// Force l'app `appId` à quitter telle qu'elle est maintenant : fenêtres et processus relus (jamais les PID d'une lecture
// ancienne, qui ont pu être repris) ; partie entre-temps : rien à faire, true. `allow` (essais) peut refuser les
// processus visés.
bool forceQuitNow(const std::wstring& appId, std::wstring* error,
                  const std::function<bool(const std::vector<FqProcess>&)>& allow = {});

} // namespace md
