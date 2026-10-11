// Remplir au lieu d'agrandir (plan 72) : fenêtres de premier niveau de l'app au premier plan qui passent à l'état
// agrandi (glissées contre le haut de l'écran, double-clic sur la barre de titre, ⊞↑, bouton agrandir). Crochet
// EVENT_OBJECT_LOCATIONCHANGE de ce seul processus, comme les pastilles : jamais tous les déplacements du système.
#pragma once
#include <windows.h>

#include "../shell/window_fill.h"

namespace md {

class MaximizeWatch {
public:
    ~MaximizeWatch() { stop(); }
    // `message` posté à `sink`, wParam la fenêtre, à chaque passage à l'état agrandi.
    void setSink(HWND sink, UINT message) {
        sink_ = sink;
        message_ = message;
    }
    // Suit ce processus (0 : plus rien). L'état de ses fenêtres est relu : une fenêtre déjà agrandie le reste.
    void follow(DWORD pid);
    void stop();

private:
    static void CALLBACK onEvent(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD);
    static BOOL CALLBACK seedOne(HWND h, LPARAM self);
    static MaximizeWatch* self_;   // fil de la barre : un seul suivi
    HWINEVENTHOOK hook_ = nullptr;
    DWORD pid_ = 0;
    HWND sink_ = nullptr;
    UINT message_ = 0;
    ZoomTracker zoom_;
};

} // namespace md
