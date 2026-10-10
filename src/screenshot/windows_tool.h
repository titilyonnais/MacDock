// Captures de l'Outil Capture de Windows (⊞⇧S, Impr. écran) : MacDock se montre le temps de la capture (logique pure).
#pragma once
#include <cstdint>
#include <string>

namespace md {

// Touche qui lance l'Outil Capture : Impr. écran, avec ou sans modificateurs, et ⊞⇧S. Jamais à la répétition ni au
// relâchement (wasDown : appui vu avant).
bool windowsShotKey(unsigned vk, bool down, bool repeat, bool wasDown, bool win, bool shift);

// Programme de l'Outil Capture (nom de l'exécutable sans chemin, casse ignorée) : SnippingTool.exe (Windows 11),
// ScreenClippingHost.exe (Windows 10), ScreenSketch.exe (Capture d'écran et croquis).
bool isCaptureToolProcess(const std::wstring& exeName);

// Révélation tenue pour un outil de Windows, relue toutes les 250 ms : tant que l'outil est devant, puis 500 ms ; s'il
// n'a jamais été vu devant, 3 s après la dernière touche ; jamais plus de 2 min.
class ToolReveal {
public:
    static constexpr std::uint64_t kMinMs = 3000, kAfterToolMs = 500, kMaxMs = 120000;
    void start(std::uint64_t now);                     // touche, ou nouvel appui pendant la révélation
    bool keep(std::uint64_t now, bool toolInFront);    // false : la révélation est finie
    bool active() const { return active_; }

private:
    bool active_ = false, toolSeen_ = false;
    std::uint64_t startedAt_ = 0, keyAt_ = 0, toolSeenAt_ = 0;
};

} // namespace md
