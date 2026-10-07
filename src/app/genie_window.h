// Effet génie à l'écran. Rendu principal sur le GPU (GenieSurface : une capture de la fenêtre, un maillage lisse) ;
// en attendant la capture (quelques images) ou si le GPU fait défaut, une fenêtre transparente au-dessus de tout où
// une miniature DWM par bande déforme la fenêtre. Les deux suivent la même forme : le relais ne se voit pas.
#pragma once
#include <windows.h>
#include <dwmapi.h>

#include <vector>

#include "../anim/genie.h"
#include "genie_surface.h"

namespace md {

class GenieWindow {
public:
    ~GenieWindow();
    void prepare(HINSTANCE instance);   // au lancement du Dock : fenêtres et GPU prêts avant la première réduction
    // from : la fenêtre ; toCell : sa case du Dock (pixels écran). restore : de la case vers la fenêtre.
    // false si DWM refuse la miniature (rien n'est affiché).
    bool start(HINSTANCE instance, HWND source, const RECT& from, const RECT& toCell, DockPosition edge, MinimizeEffect effect,
               bool restore, double now, bool slow);
    // Avance l'animation ; false quand elle est finie (dernière image laissée affichée jusqu'à finish()).
    bool step(double now);
    void finish();   // masque la fenêtre et retire les miniatures
    void cancel() { finish(); }
    bool running() const { return running_; }
    bool active() const { return !thumbs_.empty(); }   // affichée (en cours ou dernière image)
    HWND source() const { return source_; }
    bool onGpu() const { return stripsHidden_; }   // le rendu GPU a pris le relais
    bool restoring() const { return restore_; }

private:
    bool ensureWindow(HINSTANCE instance);
    void show(double t);
    void growStrips();   // bandes ajoutées si le GPU tarde ou fait défaut

    HWND hwnd_ = nullptr;
    std::vector<HTHUMBNAIL> thumbs_;
    HWND source_ = nullptr;
    RECT from_{}, to_{}, box_{};
    SIZE src_{};
    DockPosition edge_ = DockPosition::Bottom;
    MinimizeEffect effect_ = MinimizeEffect::Genie;
    int slices_ = 48, rows_ = 1, fullSlices_ = 48;
    bool gpuStarted_ = false;   // capture GPU lancée pour cette animation
    double elapsed_ = 0;        // secondes depuis le départ
    GenieSurface gpu_;
    bool stripsHidden_ = false;   // le GPU a pris le relais : fenêtre des bandes cachée
    bool restore_ = false, running_ = false;
    double start_ = 0, duration_ = 0;
};

// Sonde (MacDock.exe --genie-capture-probe) : un génie complet sur une fenêtre factice hors écran ; journalise le
// délai avant le rendu GPU et la régularité des images. Rien n'est visible.
bool genieLiveProbe(HINSTANCE instance);

} // namespace md
