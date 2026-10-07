// Effet génie à l'écran : une fenêtre transparente au-dessus de tout, où une miniature DWM par bande déforme
// la fenêtre réduite (ou restaurée) entre sa place et sa case du Dock.
#pragma once
#include <windows.h>
#include <dwmapi.h>

#include <vector>

#include "../anim/genie.h"

namespace md {

class GenieWindow {
public:
    ~GenieWindow();
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
    bool restoring() const { return restore_; }

private:
    bool ensureWindow(HINSTANCE instance);
    void show(double t);

    HWND hwnd_ = nullptr;
    std::vector<HTHUMBNAIL> thumbs_;
    HWND source_ = nullptr;
    RECT from_{}, to_{}, box_{};
    SIZE src_{};
    DockPosition edge_ = DockPosition::Bottom;
    MinimizeEffect effect_ = MinimizeEffect::Genie;
    int slices_ = 48;
    bool restore_ = false, running_ = false;
    double start_ = 0, duration_ = 0;
};

} // namespace md
