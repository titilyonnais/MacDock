// Relais du crochet souris (plan 62, logique pure) : le Dock suit la souris par un crochet bas niveau, qui se tait
// parfois (app en administrateur au premier plan, VMware qui avale la souris, curseur déplacé par SetCursorPos). La
// position lue par GetCursorPos prend alors le relais, par le même chemin que le crochet.
#pragma once
#include <windows.h>

#include <optional>

namespace md {

struct CursorSample {
    POINT cursor{};                       // GetCursorPos
    POINT known{};                        // position que le Dock connaît (crochet ou relais)
    unsigned long long hookAgeMs = 0;     // depuis le dernier mouvement vu par le crochet
    unsigned long long nowMs = 0;
    bool elevatedForeground = false;      // app en administrateur au premier plan : le crochet est aveugle
};

struct RelayStep {
    std::optional<POINT> deliver;   // position à livrer au Dock, comme un mouvement du crochet
    bool reinstallHook = false;     // crochet muet depuis longtemps alors que la souris bouge : le réinstaller
};

class CursorRelay {
public:
    static constexpr unsigned long long kSilentMs = 100;            // crochet muet au-delà
    static constexpr unsigned long long kIdleMs = 1000;             // relais sans mouvement : lecture lente
    static constexpr unsigned long long kReinstallSilentMs = 2000;  // muet si longtemps : réinstallation
    static constexpr unsigned long long kReinstallEveryMs = 10000;  // au plus une fois par période

    RelayStep poll(const CursorSample& s);
    bool active() const { return active_; }
    // Lecture rapide pendant le relais ou sous une app élevée (~60 Hz), lente sinon (~10 Hz).
    unsigned intervalMs(bool elevatedForeground) const;

private:
    std::optional<POINT> lastPolled_;
    bool active_ = false;
    unsigned long long lastDelivery_ = 0;
    std::optional<unsigned long long> lastReinstall_;
};

} // namespace md
