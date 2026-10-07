// Maillage du génie : sommets en pixels de la cible, coordonnées de texture de la fenêtre capturée.
cbuffer Params : register(b0) {
    float2 target;    // taille de la cible (pixels)
    float toSdr;      // 1 : source scRGB vers une cible 8 bits sRGB
    float white;      // valeur scRGB du blanc SDR
};

struct VSIn {
    float2 px : POSITION;
    float2 uv : TEXCOORD0;
};

struct VSOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOut main(VSIn i) {
    VSOut o;
    o.pos = float4(i.px.x / target.x * 2 - 1, 1 - i.px.y / target.y * 2, 0, 1);
    o.uv = i.uv;
    return o;
}
