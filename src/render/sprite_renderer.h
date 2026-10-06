// Images des sprites du Dock, dessinées sur le processeur (Direct2D sur bitmap WIC) :
// icône tirée pendant un glisser (avec l'étiquette « Supprimer ») et nuage « poof ».
#pragma once
#include <windows.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>
#include <vector>

#include "../icons/icon_provider.h"

namespace md {

class SpriteRenderer {
public:
    // Icône (iconPx de côté) et, si label n'est pas vide, une capsule de texte dessous. BGRA prémultiplié w x h.
    std::vector<std::uint8_t> dragSprite(const IconProvider::Image& icon, UINT iconPx, const std::wstring& label,
                                         float scale, bool dark, const std::wstring& font, UINT& w, UINT& h);
    // Nuage « poof » à l'instant t01 (0..1) de l'animation, px x px.
    std::vector<std::uint8_t> poofFrame(double t01, UINT px);
    // Décalage du centre de l'icône depuis le coin haut gauche du sprite (pour le centrer sur le curseur).
    static float iconCenterY(UINT iconPx) { return float(iconPx) / 2; }

private:
    template <class T> using Com = Microsoft::WRL::ComPtr<T>;
    bool init();
    // Cible de dessin sur un bitmap WIC w x h vidé ; nullptr si impossible.
    Com<ID2D1RenderTarget> begin(UINT w, UINT h, Com<IWICBitmap>& bitmap);
    static std::vector<std::uint8_t> read(IWICBitmap* bitmap, UINT w, UINT h);

    Com<ID2D1Factory1> d2d_;
    Com<IWICImagingFactory> wic_;
    Com<IDWriteFactory> dwrite_;
};

} // namespace md
