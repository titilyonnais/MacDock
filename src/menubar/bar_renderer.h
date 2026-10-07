// Rendu de la barre de menus transparente : textes, logo, capsule du titre ouvert. DirectComposition + Direct2D
// à l'écran ; cible WIC pour le rendu hors écran (--snapshot). Le même code de dessin sert aux deux.
#pragma once
#include <windows.h>
#include <d2d1_3.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dwrite_3.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>
#include <vector>

#include "menubar_settings.h"

namespace md {

// Erreurs de rendu qui exigent de recréer le device (pilote mis à jour, réinitialisation du GPU, veille).
bool isDeviceLost(HRESULT hr);

struct BarDrawItem {      // pixels de la barre
    std::wstring text;
    bool bold = false, logo = false;
    float x = 0, width = 0;
    bool highlighted = false;   // menu ouvert : capsule derrière le titre
};

struct BarFrame {
    float scale = 1;
    bool darkText = false;   // texte foncé (fond clair) ; sinon clair avec ombre
    std::vector<BarDrawItem> items;
    MenuBarMetrics metrics;
};

struct LogoImage {           // logo de l'utilisateur (menubar-logo.png) : seul l'alpha compte
    UINT w = 0, h = 0;
    std::vector<std::uint8_t> bgra;
};

class BarRenderer {
    template <class T> using Com = Microsoft::WRL::ComPtr<T>;

public:
    bool init(HWND hwnd);    // device matériel (aussi celui des menus en verre) + DirectComposition
    void reset();            // libère device et composition (avant un nouvel init après une perte du device)
    bool initOffscreen();    // sans fenêtre ni device D3D (cible WIC)
    void resize(UINT w, UINT h);
    ID3D11Device* device() const { return d3d_.Get(); }
    // Police (famille voulue, vide = automatique) et taille en pixels ; renvoie la famille réellement utilisée.
    std::wstring setFont(const std::wstring& wanted, float sizePx);
    float measure(const std::wstring& text, bool bold);   // largeur en pixels
    void setLogo(LogoImage logo) { logo_ = std::move(logo); logoBitmapStale_ = true; }
    bool render(const BarFrame& f);
    HRESULT lastError() const { return lastError_; }   // échec du dernier render (isDeviceLost : recréer)
    // Hors écran : la barre dessinée sur background (BGRA prémultiplié w x h) ; out reçoit l'image BGRA.
    bool renderToImage(const BarFrame& f, const std::vector<std::uint8_t>& background, UINT w, UINT h,
                       std::vector<std::uint8_t>& out);

private:
    bool createFactories();
    void draw(ID2D1RenderTarget* rt, const BarFrame& f, float height);
    Com<IDWriteTextLayout> layoutOf(const std::wstring& text, bool bold);

    Com<ID3D11Device> d3d_;
    Com<ID2D1Factory3> d2d_;
    Com<ID2D1Device2> d2dDevice_;
    Com<ID2D1DeviceContext2> dc_;
    Com<IDCompositionDesktopDevice> dcomp_;
    Com<IDCompositionTarget> target_;
    Com<IDCompositionVisual2> visual_;
    Com<IDCompositionSurface> surface_;
    Com<IDWriteFactory3> dwrite_;
    Com<IDWriteTextFormat> regular_, bold_;
    Com<IWICImagingFactory> wic_;
    std::wstring family_;
    float sizePx_ = 13;
    UINT width_ = 1, height_ = 1;
    LogoImage logo_;
    Com<ID2D1Bitmap> logoBitmap_;
    ID2D1RenderTarget* logoOwner_ = nullptr;   // cible pour laquelle logoBitmap_ a été créé
    bool logoBitmapStale_ = true;
    HRESULT lastError_ = S_OK;
};

} // namespace md
