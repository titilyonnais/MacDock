// Captures de l'Outil Capture de Windows (⊞⇧S, Impr. écran) : MacDock se montre le temps de la capture (logique pure).
#pragma once
#include <cstdint>
#include <string>

namespace md {

// Touche qui lance l'Outil Capture : Impr. écran, avec ou sans modificateurs, et ⊞⇧S. Jamais à la répétition ni au
// relâchement (wasDown : appui vu avant).
bool windowsShotKey(unsigned vk, bool down, bool repeat, bool wasDown, bool win, bool shift);

// Voile de sélection attendu : ⊞⇧S et Impr. écran seul (Windows 11 y ouvre l'outil) ; ⊞Impr. écran et Alt+Impr. écran
// copient l'écran ou la fenêtre sans voile.
bool toolShowsVeil(unsigned vk, bool win, bool alt);

// Programme de l'Outil Capture (nom de l'exécutable sans chemin, casse ignorée) : SnippingTool.exe (Windows 11),
// ScreenClippingHost.exe (Windows 10), ScreenSketch.exe (Capture d'écran et croquis).
bool isCaptureToolProcess(const std::wstring& exeName);

// Révélation tenue pour un outil de Windows, relue toutes les 250 ms : tant que l'outil est devant, puis 500 ms ; s'il
// n'a jamais été vu devant, 3 s après la dernière touche ; jamais plus de 2 min.
// Pendant une capture par l'Outil Capture de Windows (retour du 10 octobre) : son voile se pose dans la même couche que
// les fenêtres « toujours au premier plan » de MacDock, sans passer devant elles. Le Dock et la barre quittent cette
// couche le temps de la capture (sous le voile, toujours au-dessus des apps), et le Dock ignore le pointeur (au repos,
// clics traversants), comme sous un pointeur confiné ailleurs.
struct ToolSessionPlacement {
    bool topmost = true;            // fenêtres « toujours au premier plan »
    bool pointerIgnored = false;    // Dock : ni agrandissement ni clic
    bool hotCornersBlocked = false;  // coin atteint sous le voile vu bloqué : il ne part pas à la fin de la session
};
inline ToolSessionPlacement placementDuringToolSession(bool toolSession) {
    return {!toolSession, toolSession, toolSession};
}

// Message « MacDockScreenshotReveal » du Dock à la barre (wParam) : fin ; MacDock visible aux captures (⊞⇧3, ⊞⇧4,
// éditeur de l'outil : la barre garde sa couche) ; voile de l'Outil Capture devant (la barre passe aussi sous lui).
enum class ShotReveal : unsigned { Off = 0, Visible = 1, UnderVeil = 2 };
// Couche de la barre après un message : abaissée seulement sous le voile, remontée seulement si elle l'était (relecture
// du plan 69 : ⊞⇧3 la faisait repasser devant Apps et Mission Control ouverts).
struct BarLayerChange {
    bool change = false;
    bool topmost = true;
};
BarLayerChange barLayerChange(bool lowered, ShotReveal reveal);

class ToolReveal {
public:
    static constexpr std::uint64_t kMinMs = 3000, kAfterToolMs = 500, kMaxMs = 120000, kVeilWaitMs = 1500;
    // Touche, ou nouvel appui pendant la révélation ; veilExpected : toolShowsVeil.
    void start(std::uint64_t now, bool veilExpected = true);
    // false : la révélation est finie. veilInFront : la fenêtre de l'outil devant couvre son écran (voile de sélection).
    bool keep(std::uint64_t now, bool toolInFront, bool veilInFront = false);
    bool active() const { return active_; }
    // Dock et barre sous le voile, pointeur ignoré (placementDuringToolSession) : tant que le voile est devant, ou en
    // l'attendant (au plus kVeilWaitMs après la touche) ; plus après son départ, même si l'outil reste devant (éditeur),
    // ni sans voile attendu (relecture du plan 69).
    bool lowered() const { return lowered_; }

private:
    bool active_ = false, toolSeen_ = false;
    bool veilExpected_ = false, veilSeen_ = false, lowered_ = false;
    std::uint64_t startedAt_ = 0, keyAt_ = 0, toolSeenAt_ = 0;
};

} // namespace md
