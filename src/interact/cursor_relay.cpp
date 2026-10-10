#include "cursor_relay.h"

#include <algorithm>
#include <utility>

namespace md {

void CursorRelay::cursorLost(unsigned long long) {
    lost_ = true;
    lastMoved_.reset();
    active_ = false;
}

RelayStep CursorRelay::poll(const CursorSample& s) {
    RelayStep out;
    // De retour d'un bureau sécurisé : le crochet s'est tu à cause de lui, pas d'une app ; son silence repart de zéro
    // (l'appelant remet son heure d'installation), sans réinstallation pour ce tour.
    out.resumed = std::exchange(lost_, false);
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
        // main ; réinstallé, il repasse en tête de la chaîne.
        if (!out.resumed && !s.elevatedForeground && s.hookAgeMs >= kReinstallSilentMs &&
            (!lastReinstall_ || s.nowMs - *lastReinstall_ >= kReinstallEveryMs)) {
            out.reinstallHook = true;
            lastReinstall_ = s.nowMs;
        }
    } else if (active_ && ((!silent && !s.elevatedForeground) || s.nowMs - lastDelivery_ >= kIdleMs)) {
        active_ = false;   // crochet revenu, ou souris au repos : lecture lente
    }
    if (moved) lastMoved_ = s.nowMs;
    return out;
}

unsigned CursorRelay::intervalMs(unsigned long long nowMs) const {
    if (lost_) return kLostMs;
    return lastMoved_ && nowMs - *lastMoved_ < kFastMs ? 16 : 100;
}

unsigned long long CursorRelay::silenceMs(unsigned long long nowMs, unsigned long long lastEventMs,
                                          unsigned long long installedMs) {
    const unsigned long long since = std::max(lastEventMs, installedMs);
    return nowMs > since ? nowMs - since : 0;
}

bool pointerConfinedAway(const RECT& clip, const RECT& desktop, POINT cursor) {
    const bool confined = clip.left > desktop.left || clip.top > desktop.top || clip.right < desktop.right ||
                          clip.bottom < desktop.bottom;
    const bool inside = cursor.x >= clip.left && cursor.x < clip.right && cursor.y >= clip.top && cursor.y < clip.bottom;
    return confined && !inside;
}

} // namespace md
