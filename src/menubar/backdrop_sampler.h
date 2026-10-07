// Luminance de la bande d'écran sous la barre (Desktop Duplication, ponctuelle) : la barre est exclue de la
// capture le temps de l'échantillon, puis la capture s'arrête. Une seule duplication par processus : jamais
// pendant un menu en verre.
#pragma once
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <optional>

#include "../glass/backdrop_capture.h"

namespace md {

class BackdropSampler {
public:
    // Démarre un échantillon de strip (écran) ; notifyMsg est posté à notify quand une image arrive.
    bool start(HWND notify, UINT notifyMsg, HMONITOR monitor, const RECT& strip);
    // À la notification : luminance de la bande, ou nullopt (pas encore d'image utilisable). La capture
    // s'arrête dès qu'une luminance est obtenue.
    std::optional<double> take(ID3D11Device* dev);
    void stop();
    bool running() const { return running_; }
    bool failed() const;
    // Une image entièrement noire a été vue : à l'expiration du délai, la bande est réellement noire.
    bool sawBlack() const { return sawBlack_; }

private:
    BackdropCapture capture_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> frame_, staging_;
    bool scRgb_ = false;
    bool running_ = false;
    bool sawBlack_ = false;
};

} // namespace md
