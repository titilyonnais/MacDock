// Miniatures en direct (DWM) des fenêtres réduites, posées dans leur case du Dock.
#pragma once
#include <windows.h>
#include <dwmapi.h>

#include <cstdint>
#include <map>
#include <vector>

#include "../render/dock_renderer.h"

namespace md {

// Forme visible (ratio de la case), centrée et ajustée aux proportions de la source ; vide si impossible.
RECT fitThumbnail(int srcW, int srcH, const RECT& cell, double ratio = 0.80);

class Thumbnails {
public:
    ~Thumbnails() { clear(); }
    // Enregistre, place ou retire les miniatures selon les cases de frame (coordonnées client de dock) ;
    // marque RenderIcon::thumbnail quand la miniature couvre la case. visible = false : tout est retiré.
    void sync(HWND dock, RenderFrame& frame, bool visible);
    void clear();
    // Retire pour de bon les miniatures masquées par sync() (DwmUnregisterThumbnail : ~30 ms mesurées) ; à appeler
    // quand rien n'anime, pour ne pas figer une image.
    void flush();
    bool pending() const { return !retired_.empty(); }
    void setTrace(bool trace) { trace_ = trace; }

private:
    struct Entry {
        HTHUMBNAIL id = nullptr;
        RECT dest{};
        BYTE opacity = 0;
        SIZE size{};   // taille de la source, lue à l'inscription
    };
    std::map<std::uint64_t, Entry> entries_;
    std::vector<HTHUMBNAIL> retired_;   // masquées, à retirer au repos
    bool trace_ = false;
};

} // namespace md
