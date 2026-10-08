// Animation de Windows à la réduction et à l'agrandissement (iMinAnimate) : toujours celle de l'utilisateur. Le Dock
// coupe seulement celle de la fenêtre qu'il réduit lui-même (TransitionGate) ; agrandir, restaurer et ouvrir restent
// animés. Les Dock d'avant la coupaient partout sans l'écrire dans le profil : le 0 qu'ils ont pu laisser (plantage)
// est réparé d'après la préférence enregistrée dans le registre.
#pragma once
#include <functional>
#include <optional>

#include "../config/settings.h"

namespace md {

struct MinAnimateApi {
    std::function<std::optional<bool>()> get;     // état courant (SPI_GETANIMATION)
    std::function<bool(bool)> set;                // SPI_SETANIMATION, sans SPIF_UPDATEINIFILE
    std::function<std::optional<bool>()> stored;  // préférence enregistrée (registre MinAnimate)
};
MinAnimateApi realMinAnimateApi();

class MinAnimateGuard {
public:
    explicit MinAnimateGuard(MinAnimateApi api) : api_(std::move(api)) {}
    void apply(MinimizeEffect e);   // préférence de l'utilisateur, quel que soit l'effet
    void restore();                 // à l'arrêt : rien à rendre (gardée pour un Dock qui la changerait de nouveau)

private:
    bool original() const;
    MinAnimateApi api_;
};

} // namespace md
