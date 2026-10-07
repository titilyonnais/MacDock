// Fenêtre flottante qui affiche une image BGRA prémultipliée au-dessus de tout (UpdateLayeredWindow) :
// icône tirée pendant un glisser, nuage « poof ». Transparente aux clics, sans activation, exclue des captures.
#pragma once
#include <windows.h>

#include <cstdint>
#include <vector>

namespace md {

class SpriteWindow {
public:
    ~SpriteWindow();
    bool create(HINSTANCE instance);
    // Affiche l'image avec son coin haut gauche en topLeft (écran).
    void show(const std::vector<std::uint8_t>& bgra, UINT w, UINT h, POINT topLeft);
    void move(POINT topLeft);   // déplace sans redessiner
    void hide();
    bool visible() const { return visible_; }

private:
    bool ensureSurface(UINT w, UINT h);

    HWND hwnd_ = nullptr;
    HDC dc_ = nullptr;
    HBITMAP dib_ = nullptr;
    HGDIOBJ old_ = nullptr;
    void* bits_ = nullptr;
    UINT w_ = 0, h_ = 0;
    bool visible_ = false;
};

} // namespace md
