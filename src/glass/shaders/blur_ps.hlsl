// Flou gaussien séparable (une direction par passe), jusqu'à ±30 texels.
cbuffer Blur : register(b1) {
    float2 dir;        // (1,0) horizontal ou (0,1) vertical
    float2 texSize;    // taille de la source (texels)
    float sigma;       // écart-type (texels)
    float radius;      // demi-largeur du noyau (≤ 30)
    float2 pad;
};
Texture2D src : register(t0);
SamplerState lin : register(s0);

struct VSOut {
    float4 pos : SV_Position;
    float2 px : TEXCOORD0;
};

float4 main(VSOut i) : SV_Target {
    float s = max(sigma, 1e-3);
    float4 acc = 0;
    float wsum = 0;
    [loop] for (int k = -30; k <= 30; ++k) {
        if (abs(k) > radius) continue;
        float w = exp(-(k * k) / (2 * s * s));
        acc += w * src.SampleLevel(lin, (i.px + dir * k) / texSize, 0);
        wsum += w;
    }
    return acc / wsum;
}
