// Génie sur le GPU, à l'écran : fenêtre transparente au-dessus de tout (clics traversants, exclue des captures),
// chaîne d'échange DirectComposition de la taille de l'animation, capture de la fenêtre par WindowCapture et rendu
// du maillage par GenieGpu. Tant que la capture n'est pas arrivée, rien n'est affiché (le rendu par bandes assure
// l'animation) ; en cas d'échec, tout se replie sur les bandes.
#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <vector>

#include "../anim/genie.h"
#include "genie_gpu.h"
#include "window_capture.h"

namespace md {

class GenieSurface {
    template <class T> using Com = Microsoft::WRL::ComPtr<T>;

public:
    ~GenieSurface();
    // Device, fenêtre, DirectComposition et relais de capture, une fois (au lancement du Dock : pas d'attente au
    // premier génie). false : GPU ou capture indisponibles, les bandes seules serviront.
    bool prepare(HINSTANCE instance);
    // Capture lancée d'avance (réduction annoncée, case survolée) : begin() la reprend pour la même source, et le
    // génie démarre sans attendre la première image. cool() : annonce sans suite.
    // box (réduction annoncée) : chaîne d'échange posée d'avance à cette taille et fenêtre affichée, transparente.
    bool warm(HINSTANCE instance, HWND source, const RECT* box = nullptr);
    // Tant que l'annonce tient : prend les images de la capture et trace l'instant de départ à blanc (non présenté).
    void pump(const std::vector<GenieVertex>& mesh);
    void cool();
    HWND warmSource() const { return warmSource_; }
    // Animation de source dans box (pixels écran) : capture lancée (ou reprise), chaîne d'échange à la taille de box,
    // fenêtre cachée.
    bool begin(HINSTANCE instance, HWND source, const RECT& box);
    // Dessine mesh si la capture est arrivée (affiche la fenêtre à la première image). false : pas (encore) d'image GPU.
    bool frame(const std::vector<GenieVertex>& mesh);
    int framesShown() const { return frames_; }
    bool ready() const { return dev_ != nullptr; }   // prepare() a réussi
    void end();   // cache la fenêtre, arrête la capture, libère la texture

private:
    void reset();   // périphérique perdu : tout sera recréé au prochain prepare()
    bool ensureSwap(const RECT& box);   // chaîne d'échange à la taille de box, fenêtre placée (pas affichée)

    HWND hwnd_ = nullptr;
    Com<ID3D11Device> dev_;
    Com<ID3D11DeviceContext> ctx_;
    Com<IDCompositionDevice> dcomp_;
    Com<IDCompositionTarget> target_;
    Com<IDCompositionVisual> visual_;
    Com<IDXGISwapChain1> swap_;
    UINT sw_ = 0, sh_ = 0;
    GenieGpu gpu_;
    WindowCapture capture_;
    RECT box_{};
    bool capturing_ = false, failed_ = false;
    HWND warmSource_ = nullptr;   // capture lancée d'avance pour cette fenêtre
    bool shown_ = false;          // fenêtre affichée (première image, ou transparente d'avance)
    bool warmDrawn_ = false;      // tracé à blanc fait pendant l'annonce
    int frames_ = 0;
};

} // namespace md
