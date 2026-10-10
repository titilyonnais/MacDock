// Relais du crochet souris (plan 62, logique pure) : le Dock suit la souris par un crochet bas niveau, qui se tait
// parfois (app en administrateur au premier plan, curseur déplacé par SetCursorPos, crochet retiré par Windows). La
// position lue par GetCursorPos prend alors le relais, par le même chemin que le crochet. Ne couvre pas un curseur
// que l'app laisse immobile (VMware garde celui de Windows posé là où il était).
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
    bool resumed = false;           // curseur de nouveau lisible : le silence du crochet repart de zéro
};

class CursorRelay {
public:
    static constexpr unsigned long long kSilentMs = 100;            // crochet muet au-delà
    static constexpr unsigned long long kIdleMs = 1000;             // relais sans mouvement : lecture lente
    static constexpr unsigned long long kReinstallSilentMs = 2000;  // muet si longtemps : réinstallation
    static constexpr unsigned long long kReinstallEveryMs = 10000;  // au plus une fois par période

    static constexpr unsigned long long kFastMs = 1000;             // lecture rapide après un mouvement
    static constexpr unsigned kLostMs = 500;                        // curseur illisible : lecture lente

    RelayStep poll(const CursorSample& s);
    // GetCursorPos a échoué (bureau sécurisé : verrouillage, invite de l'UAC) : lecture lente jusqu'au retour.
    void cursorLost(unsigned long long nowMs);
    bool active() const { return active_; }
    // Au rythme de l'écran (setFrameMs, 16 ms par défaut) dans la seconde qui suit un mouvement, ~10 Hz sinon.
    unsigned intervalMs(unsigned long long nowMs) const;
    // Durée d'une image de l'écran du Dock (6 ms à 165 Hz), bornée de 2 à 16 ms.
    void setFrameMs(unsigned ms) { frameMs_ = ms < 2 ? 2 : (ms > 16 ? 16 : ms); }
    // Silence du crochet : depuis son dernier mouvement vu, ou depuis son installation s'il est plus récent (0 : jamais).
    static unsigned long long silenceMs(unsigned long long nowMs, unsigned long long lastEventMs,
                                        unsigned long long installedMs);

private:
    std::optional<POINT> lastPolled_;
    std::optional<unsigned long long> lastMoved_;
    bool active_ = false;
    unsigned long long lastDelivery_ = 0;
    std::optional<unsigned long long> lastReinstall_;
    bool lost_ = false;
    unsigned frameMs_ = 16;
};

// Pointeur confiné par une autre app (GetClipCursor plus petit que le bureau) dans une zone qui ne contient pas le
// curseur : il y sera ramené au prochain mouvement, le Dock fait comme s'il était ailleurs. MacDock n'appelle jamais
// ClipCursor lui-même.
bool pointerConfinedAway(const RECT& clip, const RECT& desktop, POINT cursor);

} // namespace md
