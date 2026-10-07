// Fenêtre capturée (BGRA prémultiplié, mipmaps) échantillonnée en anisotrope : nette réduite comme agrandie.
Texture2D window : register(t0);
SamplerState smp : register(s0);

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    return window.Sample(smp, uv);
}
