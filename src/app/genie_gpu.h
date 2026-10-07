// Rendu du génie sur le GPU : la fenêtre capturée une fois (texture à mipmaps), déformée par le maillage de
// genieMesh dans une cible MSAA 4×, résolue dans la texture de destination (prémultiplié). Formats : ceux de la
// capture et de la destination, BGRA 8 bits ou scRGB RGBA16F (écrans HDR : les couleurs passent telles quelles).
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
    void dropSource();
    // Dessine mesh (pixels écran) dans dst (w x h, même famille de format, coin écran origin), sur un fond transparent.
    bool draw(ID3D11DeviceContext* ctx, ID3D11Texture2D* dst, UINT w, UINT h, POINT origin, const std::vector<GenieVertex>& mesh);

private:
    bool ensureTarget(UINT w, UINT h, DXGI_FORMAT format);

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
    Com<ID3D11Texture2D> msaa_;
    Com<ID3D11RenderTargetView> msaaRtv_;
    UINT tw_ = 0, th_ = 0, samples_ = 1;
    DXGI_FORMAT tf_ = DXGI_FORMAT_UNKNOWN;
};

// Tests et sondes : rendu sur un device WARP, lu en BGRA prémultiplié w x h (vide si impossible).
std::vector<std::uint8_t> genieRenderToBgra(const BgraImage& src, const std::vector<GenieVertex>& mesh, UINT w, UINT h,
                                            POINT origin);

} // namespace md
