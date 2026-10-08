// Enregistrement de l'écran (⊞⇧5) : un fil copie la zone 30 fois par seconde (GDI, curseur compris ; les fenêtres
// exclues des captures n'y sont pas) et Media Foundation l'encode en H.264 dans un MP4. Aucun son, comme par défaut
// sur macOS. La vidéo fait au plus 1920 px de large (recordingSize).
#pragma once
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "../core/bgra_image.h"

namespace md {

class ScreenRecorder {
public:
    // Remplit une image BGRA (haut en premier) de w × h pixels. Par défaut : la zone de l'écran, curseur compris.
    using FrameSource = std::function<void(std::uint8_t* bgra, int w, int h)>;
    ~ScreenRecorder();
    // area : pixels de l'écran virtuel. false si l'encodeur refuse (chemin, codec) : rien n'est enregistré.
    bool start(const RECT& area, const std::wstring& path, int fps = 30, FrameSource source = nullptr);
    // Arrête et finalise le fichier ; last : dernière image (vignette). true si le fichier est complet.
    bool stop(BgraImage* last = nullptr);
    bool recording() const { return running_.load(); }
    double seconds() const;   // durée enregistrée jusqu'ici

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
    std::atomic<bool> running_{false};
};

} // namespace md
