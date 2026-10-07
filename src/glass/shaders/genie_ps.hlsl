// Fenêtre capturée (prémultipliée, mipmaps) échantillonnée en anisotrope : nette réduite comme agrandie.
// Bords anticrénelés sans MSAA : le maillage déborde d'un pixel, et la couverture vient de la distance (en pixels)
// au bord de la texture, tirée des dérivées de uv.
cbuffer Params : register(b0) {
    float2 target;
    float toSdr;
    float white;
};

Texture2D window : register(t0);
SamplerState smp : register(s0);

float coverage(float x) {   // x : coordonnée de texture (0..1) ; 1 à l'intérieur, rampe d'un pixel au bord
    float d = min(x, 1 - x) / max(length(float2(ddx(x), ddy(x))), 1e-6);
    return saturate(d + 0.5);
}

float3 toSrgb(float3 c) {
    c = saturate(c);
    return c <= 0.0031308 ? 12.92 * c : 1.055 * pow(c, 1 / 2.4) - 0.055;
}

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float4 c = window.Sample(smp, saturate(uv));
    if (toSdr > 0.5) {   // scRGB linéaire → sRGB, ramené au blanc SDR de l'écran
        float a = c.a;
        c.rgb = a > 0 ? toSrgb(c.rgb / a / white) * a : 0;
    }
    return c * (coverage(uv.x) * coverage(uv.y));
}
