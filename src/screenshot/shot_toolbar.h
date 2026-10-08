// Barre de ⊞⇧5 (comme ⌘⇧5) : en bas au centre de l'écran du curseur, ×, capturer l'écran, une fenêtre, une zone,
// enregistrer l'écran, une zone, puis « Capturer » ou « Enregistrer ». Et la pastille ⏹ de l'enregistrement en cours.
// Fenêtres en couches, sans activation, exclues des captures ; Échap passe par le crochet clavier du Dock.
#pragma once
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <functional>
#include <string>

#include "screenshot_logic.h"

namespace md {

// Dessin Direct2D vers des pixels prémultipliés, puis UpdateLayeredWindow (fenêtre cliquable).
class LayeredPanel {
public:
    ~LayeredPanel();
    bool ensure(HINSTANCE instance, const wchar_t* cls, WNDPROC proc, void* self);
    // draw reçoit la cible (pixels w × h) ; la fenêtre est posée à topLeft.
    void paint(int w, int h, POINT topLeft, const std::function<void(ID2D1RenderTarget*, IDWriteFactory*)>& draw);
    void hide();
    HWND hwnd() const { return hwnd_; }
    bool visible() const { return hwnd_ && IsWindowVisible(hwnd_); }

private:
    HWND hwnd_ = nullptr;
    Microsoft::WRL::ComPtr<ID2D1Factory> d2d_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwrite_;
    Microsoft::WRL::ComPtr<IWICImagingFactory> wic_;
};

class ShotToolbar {
public:
    using Done = std::function<void(int item)>;   // ShotToolbarItem choisi (kToolScreen…), ou kToolClose
    bool open(HINSTANCE instance, Done done);
    void close();   // sans choix
    bool isOpen() const { return panel_.visible(); }

private:
    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    void render();
    void finish(int item);

    LayeredPanel panel_;
    ShotToolbarLayout layout_;
    double scale_ = 1;
    int mode_ = kToolRegion;   // comme macOS : une zone par défaut
    int hover_ = -1;
    POINT at_{};
    Done done_;
};

// Pastille de l'enregistrement en cours, en haut au centre : carré rouge et durée ; un clic l'arrête.
class RecordingPill {
public:
    bool show(HINSTANCE instance, HMONITOR monitor, std::function<void()> onStop);
    void update(double seconds);
    void hide();

private:
    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    LayeredPanel panel_;
    double scale_ = 1;
    POINT at_{};
    std::function<void()> onStop_;
};

} // namespace md
