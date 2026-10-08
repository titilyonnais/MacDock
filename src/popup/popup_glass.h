// Verre des fenêtres modales (piles…) : l'écran entier est capturé une seule fois par session (une seconde
// duplication échouerait), chaque fenêtre en copie sa portion, puis la passe Liquid Glass remplit une texture
// que Direct2D pose sous le contenu.
#pragma once
#include <windows.h>
#include <d2d1_3.h>
#include <d3d11.h>
#include <wrl/client.h>

#include "../glass/backdrop_capture.h"
#include "../glass/glass_renderer.h"

namespace md {

struct Metrics;

// Matériaux Golden Gate (captures de macOS 27) : menus presque opaques au fil sombre d'un pixel, panneaux
// (Spotlight, volume, sélecteur d'apps) plus vitrés. Ombre, capture et HDR restent à régler par l'appelant.
enum class PopupMaterial { Menu, Panel };
GlassParams popupGlassParams(const Metrics& m, bool dark, float scale, PopupMaterial kind);
// Ombre des menus et panneaux : nette, indépendante de celle du Dock (presque nulle sur macOS 27).
float popupShadowOpacity(bool dark);

struct WindowBackdrop {   // portion de l'écran capturé située sous une fenêtre
    Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    bool valid = false, scRgb = false;
    float white = 1;
};

// La ressource a été créée sur dev (chaque barre de menus a son propre device : une texture ne passe pas de l'un à
// l'autre).
bool onDevice(ID3D11DeviceChild* resource, ID3D11Device* dev);

class ScreenBackdrop {
public:
    // Capture tout l'écran mon (monitorRect) ; notifyMsg est posté à notify à chaque image.
    void start(HWND notify, UINT notifyMsg, HMONITOR mon, const RECT& monitorRect);
    void stop() { capture_.stop(); }
    BackdropCapture::Status status() const { return capture_.status(); }
    bool failed() const {
        return status() == BackdropCapture::Status::Unavailable || status() == BackdropCapture::Status::Failed;
    }
    // À la notification : copie la dernière image ; false si rien de neuf.
    bool take(ID3D11Device* dev);
    // Copie dans out la portion sous windowRc (écran) ; out est (re)créé à la taille de la fenêtre.
    bool copyTo(ID3D11Device* dev, const RECT& windowRc, WindowBackdrop& out) const;

private:
    BackdropCapture capture_;
    RECT rc_{};
    Microsoft::WRL::ComPtr<ID3D11Texture2D> screen_;
    bool scRgb_ = false;
    float white_ = 1;
};

struct GlassTarget {   // cible de la passe de verre, lisible par Direct2D
    Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap;
    bool ensure(ID3D11Device* dev, ID2D1DeviceContext* dc, UINT w, UINT h);
};

} // namespace md
