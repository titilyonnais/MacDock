// Réduction par 4 (moyenne 4x4 par 4 lectures bilinéaires) ; un arrière-plan scRGB (HDR) est ramené en sRGB.
cbuffer Down : register(b1) {
    float2 srcSize;     // taille de la source (pixels)
    float scRgb;        // 1 : source RGBA16F linéaire (scRGB)
    float sdrWhite;     // valeur scRGB du blanc SDR
};
Texture2D src : register(t0);
SamplerState lin : register(s0);

struct VSOut {
    float4 pos : SV_Position;
    float2 px : TEXCOORD0;
};

float3 toSrgb(float3 c) {
    c = saturate(c);
    return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1.0 / 2.4) - 0.055;
}

float4 main(VSOut i) : SV_Target {
    float2 center = i.px * 4;   // centre du bloc 4x4 dans la source
    float3 c = 0;
    c += src.SampleLevel(lin, (center + float2(-1, -1)) / srcSize, 0).rgb;
    c += src.SampleLevel(lin, (center + float2(1, -1)) / srcSize, 0).rgb;
    c += src.SampleLevel(lin, (center + float2(-1, 1)) / srcSize, 0).rgb;
    c += src.SampleLevel(lin, (center + float2(1, 1)) / srcSize, 0).rgb;
    c *= 0.25;
    if (scRgb > 0.5) c = toSrgb(c / max(sdrWhite, 1e-3));
    return float4(c, 1);
}
