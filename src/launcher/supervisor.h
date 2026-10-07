// Décision du lanceur à la sortie d'un processus surveillé (logique pure) : chaque processus a sa propre
// politique de relance ; l'arrêt normal du Dock arrête tout.
#pragma once
#include "crash_policy.h"

namespace md {

enum class ChildRole { Dock, MenuBar };
enum class ExitDecision {
    Relaunch,   // plantage : relancer ce processus
    Forget,     // ne plus le surveiller (arrêt normal de la barre, ou trop de plantages)
    StopAll,    // « Quitter MacDock » : fermer la barre aussi et quitter le lanceur
};

class Supervisor {
public:
    ExitDecision onExit(ChildRole role, unsigned long code, double nowSeconds);

private:
    CrashPolicy dock_, menuBar_;
};

} // namespace md
