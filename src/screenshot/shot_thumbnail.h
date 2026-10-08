// Vignette flottante d'une capture d'écran, en bas à droite comme sur macOS : elle glisse depuis le bord, reste
// 5 secondes (le survol la retient), puis repart. Un clic ouvre le fichier, un glisser vers la droite la renvoie,
// un glisser dans une autre direction dépose le fichier ailleurs (Explorateur, message, document).
#pragma once
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "../core/bgra_image.h"

namespace md {

class ShotThumbnail {
public:
    ~ShotThumbnail();
    // Remplace la vignette précédente. path : fichier en cours d'écriture (fileSaved dira quand il est prêt).
    void show(HINSTANCE instance, const BgraImage& shot, const std::wstring& path, HMONITOR monitor);
    void fileSaved(const std::wstring& path, bool ok);
    void close();
    bool visible() const { return state_ != State::Hidden; }
    HWND hwnd() const { return hwnd_; }

private:
    enum class State { Hidden, In, Stay, Out, Back, Held };
    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);
    bool ensureWindow(HINSTANCE instance);
    void place(double offset);   // décalage vers la droite (pixels) ; la partie hors de l'écran est coupée
    void step();
    void leave();                // départ vers la droite depuis la position actuelle
    void open();
    void dragOut();

    HWND hwnd_ = nullptr;
    HDC dc_ = nullptr;
    HBITMAP dib_ = nullptr;
    HGDIOBJ old_ = nullptr;
    int w_ = 0, h_ = 0, margin_ = 0;
    RECT area_{};    // écran (on ne déborde jamais sur l'écran voisin)
    POINT home_{};   // coin haut gauche de la fenêtre en place
    double scale_ = 1;
    State state_ = State::Hidden;
    ULONGLONG phaseStart_ = 0;
    double offset_ = 0, fromOffset_ = 0;
    double stayLeft_ = 0;        // secondes restantes (le survol les gèle)
    ULONGLONG lastTick_ = 0;
    bool hover_ = false, pressed_ = false, flicking_ = false;
    POINT pressAt_{};
    std::wstring path_;
    bool saved_ = false, openPending_ = false;
};

} // namespace md
