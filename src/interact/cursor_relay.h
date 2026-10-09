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

    static constexpr unsigned long long kFastMs = 1000;             // lecture rapide après un mouvement

    RelayStep poll(const CursorSample& s);
    bool active() const { return active_; }
    unsigned intervalMs(unsigned long long nowMs) const;   // ~60 Hz dans la seconde qui suit un mouvement, ~10 Hz sinon
    // Silence du crochet : depuis son dernier mouvement vu, ou depuis son installation s'il est plus récent (0 : jamais).
    static unsigned long long silenceMs(unsigned long long nowMs, unsigned long long lastEventMs,
                                        unsigned long long installedMs);

private:
    std::optional<POINT> lastPolled_;
    std::optional<unsigned long long> lastMoved_;
    bool active_ = false;
    unsigned long long lastDelivery_ = 0;
    std::optional<unsigned long long> lastReinstall_;
};

// Pointeur confiné par une autre app (GetClipCursor plus petit que le bureau) dans une zone qui ne contient pas tout
// le Dock : le Dock fait comme si le pointeur était ailleurs. MacDock n'appelle jamais ClipCursor lui-même.
bool pointerConfinedAway(const RECT& clip, const RECT& desktop, const RECT& dock);

} // namespace md
