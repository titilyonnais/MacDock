// Rendu du génie sur le GPU : la fenêtre capturée une fois (texture à mipmaps), déformée par le maillage de
// genieMesh, bords anticrénelés dans le shader (sans MSAA : peu de mémoire même en plein écran), prémultiplié.
// Source BGRA8 ou scRGB RGBA16F (écrans HDR) ; cible de la même famille, ou 8 bits pour une source scRGB : les
// couleurs sont alors ramenées au blanc SDR de l'écran (setWhite).
#pragma once
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <vector>

#include "../anim/genie.h"
#include "../core/bgra_image.h"

namespace md {

class GenieGpu {
    template <class T> using Com = Microsoft::WRL::ComPtr<T>;

public:
    bool init(ID3D11Device* dev);
    // Copie le coin haut gauche w x h de frame (BGRA8 ou RGBA16F) dans la texture source, puis génère ses mipmaps.
    bool setSource(ID3D11DeviceContext* ctx, ID3D11Texture2D* frame, UINT w, UINT h);
    bool hasSource() const { return srv_ != nullptr; }
    bool sourceIsScRgb() const { return scRgb_; }
    void setWhite(float sdrWhite) { white_ = sdrWhite > 0 ? sdrWhite : 1; }
    void dropSource();
    // Efface dst (transparent) et y dessine mesh (pixels écran ; dst commence au point écran origin, zone w x h).
    bool draw(ID3D11DeviceContext* ctx, ID3D11Texture2D* dst, UINT w, UINT h, POINT origin, const std::vector<GenieVertex>& mesh);

private:
    Com<ID3D11Device> dev_;
    Com<ID3D11VertexShader> vs_;
    Com<ID3D11PixelShader> ps_;
    Com<ID3D11InputLayout> layout_;
    Com<ID3D11Buffer> cb_, vb_;
    UINT vbCapacity_ = 0;
    Com<ID3D11SamplerState> sampler_;
    Com<ID3D11BlendState> blend_;
    Com<ID3D11RasterizerState> raster_;
    Com<ID3D11Texture2D> src_;
    Com<ID3D11ShaderResourceView> srv_;
    bool scRgb_ = false;
    float white_ = 1;
};

// Tests et sondes : rendu sur un device WARP, lu en BGRA prémultiplié w x h (vide si impossible).
std::vector<std::uint8_t> genieRenderToBgra(const BgraImage& src, const std::vector<GenieVertex>& mesh, UINT w, UINT h,
                                            POINT origin);

} // namespace md
