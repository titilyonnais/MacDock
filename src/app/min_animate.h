// Animation de Windows à la réduction et à l'agrandissement (iMinAnimate) : coupée pendant que le Dock anime
// lui-même la réduction, rendue ensuite. Jamais écrite dans le profil : la préférence de l'utilisateur reste dans
// le registre, d'où elle est relue (un Dock relancé après un plantage ne prend pas le 0 laissé par le précédent).
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
    void apply(MinimizeEffect e);   // Génie, Échelle : coupée ; Windows : préférence de l'utilisateur
    void restore();                 // à l'arrêt : préférence rendue si on l'avait changée
    bool suppressed() const { return suppressed_; }

private:
    bool original() const;
    MinAnimateApi api_;
    bool suppressed_ = false;
};

} // namespace md
