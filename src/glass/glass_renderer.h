// Verre Liquid Glass (D3D11) : réduction et flou de l'arrière-plan, puis composition des formes de verre et de leurs ombres.
#pragma once
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <span>

namespace md {

struct GlassShape {                 // pixels de la cible
    float left = 0, top = 0, right = 0, bottom = 0;
    float radius = 0;               // limitedCornerRadius(...)
    float strength = 1;             // 1 = Dock ; tooltipGlassStrength pour l'infobulle
    float shadowOpacity = 0;        // 0 = pas d'ombre
    float opacity = 1;              // fondu de la forme
};

struct GlassParams {
    float scale = 1;
    bool dark = false;
    float blurSigmaPx = 20, bevelPx = 18, refraction = 0.6f, chromatic = 0.1f, fresnel = 0.18f, specular = 0.55f;
    float tint = 0.10f, saturation = 1.5f;
    float hairline = 0;             // fil sombre au bord (menus et panneaux)
    float shadowBlurPx = 36, shadowOffsetPx = 4;
    // v2 : formes qui fusionnent en douceur quand elles se rapprochent (pixels ; 0 = chacune seule, au plus 8 formes),
    // et direction de la lumière des reflets (vers où elle va ; (0, 1) = d'en haut, le rendu calé sur macOS).
    float merge = 0;
    float lightX = 0, lightY = 1;
    bool backdropIsScRgb = false;
    float sdrWhiteScale = 1;        // HDR : valeur scRGB du blanc SDR
};

class GlassRenderer {
public:
    bool init(ID3D11Device* dev);
    // backdrop : arrière-plan de la cible (w x h, BGRA8 ou RGBA16F). target : BGRA8 prémultiplié w x h, vidé puis
    // rempli du verre et des ombres (transparent ailleurs).
    bool render(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* backdrop, UINT w, UINT h,
                ID3D11RenderTargetView* target, std::span<const GlassShape> shapes, const GlassParams& p);

private:
    template <class T> using Com = Microsoft::WRL::ComPtr<T>;
    struct Target {
        Com<ID3D11Texture2D> tex;
        Com<ID3D11RenderTargetView> rtv;
        Com<ID3D11ShaderResourceView> srv;
        UINT w = 0, h = 0, mips = 1;
    };
    bool ensureTargets(UINT w, UINT h);
    bool makeTarget(Target& t, UINT w, UINT h, bool mips);
    void drawQuad(ID3D11DeviceContext* ctx, float l, float t, float r, float b, UINT tw, UINT th);
    template <class T> void setConstants(ID3D11DeviceContext* ctx, ID3D11Buffer* buf, const T& data);

    Com<ID3D11Device> dev_;
    Com<ID3D11VertexShader> vs_;
    Com<ID3D11PixelShader> downPs_, blurPs_, glassPs_;
    Com<ID3D11Buffer> quadCb_, downCb_, blurCb_, glassCb_;
    Com<ID3D11SamplerState> linear_;
    Com<ID3D11BlendState> premulBlend_, noBlend_;
    Com<ID3D11RasterizerState> raster_;
    Com<ID3D11ShaderResourceView> lutSrv_;
    Target down_, blurA_, blurB_;
};

} // namespace md
