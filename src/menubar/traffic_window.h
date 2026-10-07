// Feux tricolores à l'écran : un petit calque posé sur la barre de titre de la fenêtre active, juste au-dessus
// d'elle dans l'ordre d'affichage, qui la suit quand elle bouge et lui envoie ses commandes système.
#pragma once
#include <windows.h>

#include <cstdint>
#include <vector>

#include "traffic_lights.h"

namespace md {

class TrafficWindow {
public:
    ~TrafficWindow() { destroy(); }
    bool create(HINSTANCE instance);
    // Fenêtre active (nullptr : aucune). Décide (wantsLights), sinon masque le calque.
    void attach(HWND target, LightsMode mode);
    void detach();
    void destroy();
    HWND target() const { return target_; }

private:
    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    static void CALLBACK onEvent(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD);
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);
    void place(bool resample);   // relit la cible, décide, dessine si besoin, replace le calque
    void hide();
    void sample(const RECT& frame, UINT dpi);
    void paint();
    void raise();                // juste au-dessus de la cible
    void unhook();

    HWND hwnd_ = nullptr, target_ = nullptr;
    HWINEVENTHOOK hook_ = nullptr;
    LightsMode mode_ = LightsMode::Standard;
    LightsLayout layout_{};
    LightsState state_{};
    double scale_ = 1;
    bool shown_ = false, tracking_ = false, painted_ = false;
    int pressed_ = -1;
    SIZE paintedSize_{};
    static TrafficWindow* self_;
};

} // namespace md
