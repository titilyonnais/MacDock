// Quad couvrant un rectangle de la cible (pixels), dessiné en triangle strip de 4 sommets.
cbuffer Quad : register(b0) {
    float4 rect;      // gauche, haut, droite, bas (pixels de la cible)
    float2 target;    // taille de la cible (pixels)
    float2 pad;
};

struct VSOut {
    float4 pos : SV_Position;
    float2 px : TEXCOORD0;   // position en pixels de la cible
};

VSOut main(uint id : SV_VertexID) {
    float2 t = float2(id & 1, id >> 1);
    float2 p = lerp(rect.xy, rect.zw, t);
    VSOut o;
    o.px = p;
    o.pos = float4(p.x / target.x * 2 - 1, 1 - p.y / target.y * 2, 0, 1);
    return o;
}
