#include "cursor_relay.h"

namespace md {

RelayStep CursorRelay::poll(const CursorSample& s) {
    RelayStep out;
    const bool moved = !lastPolled_ || lastPolled_->x != s.cursor.x || lastPolled_->y != s.cursor.y;
    lastPolled_ = s.cursor;
    const bool behind = s.cursor.x != s.known.x || s.cursor.y != s.known.y;
    const bool silent = s.hookAgeMs >= kSilentMs;
    // Seulement un curseur qui a bougé depuis la lecture précédente : immobile, sa position connue reste (la poussée
    // contre un bord, vue par le crochet au-delà de l'écran, n'est pas écrasée par la position bornée).
    if (moved && behind && (silent || s.elevatedForeground)) {
        out.deliver = s.cursor;
        active_ = true;
        lastDelivery_ = s.nowMs;
        // Muet si longtemps sans app élevée pour l'expliquer : retiré par Windows, ou un autre crochet ne passe pas la
        // main (VMware au premier plan) ; réinstallé, il repasse en tête de la chaîne.
        if (!s.elevatedForeground && s.hookAgeMs >= kReinstallSilentMs &&
            (!lastReinstall_ || s.nowMs - *lastReinstall_ >= kReinstallEveryMs)) {
            out.reinstallHook = true;
            lastReinstall_ = s.nowMs;
        }
    } else if (active_ && ((!silent && !s.elevatedForeground) || s.nowMs - lastDelivery_ >= kIdleMs)) {
        active_ = false;   // crochet revenu, ou souris au repos : lecture lente
    }
    return out;
}

unsigned CursorRelay::intervalMs(bool elevatedForeground) const { return active_ || elevatedForeground ? 16 : 100; }

} // namespace md
