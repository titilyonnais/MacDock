// Verre des fenêtres modales (piles…) : l'écran entier est capturé une seule fois par session (une seconde
// duplication échouerait), chaque fenêtre en copie sa portion, puis la passe Liquid Glass remplit une texture
// que Direct2D pose sous le contenu.
#pragma once
#include <windows.h>
#include <d2d1_3.h>
#include <d3d11.h>
#include <wrl/client.h>

#include "../glass/backdrop_capture.h"

namespace md {

struct WindowBackdrop {   // portion de l'écran capturé située sous une fenêtre
    Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    bool valid = false, scRgb = false;
    float white = 1;
};

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
