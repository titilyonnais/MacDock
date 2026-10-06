// Verre Liquid Glass d'une forme à coins continus : réfraction du biseau, aberration chromatique,
// teinte adaptative, Fresnel, liseré spéculaire et ombre portée. Sortie en alpha prémultiplié.
cbuffer Glass : register(b1) {
    float4 shapeRect;     // gauche, haut, droite, bas (pixels de la cible)
    float radius;         // rayon limité (limitedCornerRadius), pixels
    float strength;       // 1 = Dock
    float shadowOpacity;
    float bevel;          // largeur du biseau réfractif, pixels
    float refraction;     // déplacement max / biseau (positif : vers le centre)
    float chromatic;
    float fresnel;
    float specular;
    float tint;
    float saturation;
    float shadowBlur;     // pixels
    float shadowOffset;   // pixels, vers le bas
    float scale;          // pixels par point
    float dark;           // 1 = mode sombre
    float2 targetSize;    // étendue en pixels de la cible couverte par le flou (multiple de 4)
    float maxMip;         // dernier niveau de mip du flou
    float opacity;        // fondu de la forme (infobulle)
    float2 pad;
};
Texture2D blurTex : register(t0);
Texture2D<float> cornerLut : register(t1);   // champ de distance d'un coin de rayon 1 sur [-2, 2]²
SamplerState lin : register(s0);

struct VSOut {
    float4 pos : SV_Position;
    float2 px : TEXCOORD0;
};

static const float3 kLuma = float3(0.2126, 0.7152, 0.0722);

float sdShape(float2 p) {
    float2 size = shapeRect.zw - shapeRect.xy;
    float2 lp = p - shapeRect.xy;
    if (radius <= 1e-3) {
        float2 d = abs(lp - size / 2) - size / 2;
        return length(max(d, 0)) + min(max(d.x, d.y), 0);
    }
    float2 q = float2(min(lp.x, size.x - lp.x), min(lp.y, size.y - lp.y)) / radius;
    if (all(q >= -2) && all(q <= 2)) return cornerLut.SampleLevel(lin, (q + 2) / 4, 0) * radius;
    if (all(q >= 0)) return -min(q.x, q.y) * radius;
    return length(max(-q, 0)) * radius;
}

float3 sampleBlur(float2 p) { return blurTex.SampleLevel(lin, p / targetSize, 0).rgb; }

float4 main(VSOut i) : SV_Target {
    float2 p = i.px;
    float d = sdShape(p);
    float cov = saturate(0.5 - d);

    // Ombre portée (gaussienne de la distance, décalée vers le bas).
    float ds = max(sdShape(p - float2(0, shadowOffset)), 0);
    float sb = max(shadowBlur * 0.5, 0.5);
    float sh = shadowOpacity * exp(-(ds * ds) / (2 * sb * sb));
    if (cov <= 0) return float4(0, 0, 0, sh) * opacity;

    // Normale sortante (gradient de la distance).
    float2 g = float2(sdShape(p + float2(1, 0)) - sdShape(p - float2(1, 0)),
                      sdShape(p + float2(0, 1)) - sdShape(p - float2(0, 1)));
    float2 n = length(g) > 1e-4 ? normalize(g) : float2(0, -1);

    // Biseau : déplacement vers le centre, plus fort près du bord ; aberration chromatique sur le biseau.
    float t = saturate(-d / max(bevel, 0.5));
    float k = 1 - t;
    float disp = refraction * strength * bevel * k * k;
    float c = chromatic * k;
    float3 col;
    col.r = sampleBlur(p - n * disp * (1 + c)).r;
    col.g = sampleBlur(p - n * disp).g;
    col.b = sampleBlur(p - n * disp * (1 - c)).b;
    float lum = dot(col, kLuma);
    col = max(lerp(lum.xxx, col, saturation), 0);

    // Teinte adaptative selon la luminance moyenne derrière la forme.
    float2 size = shapeRect.zw - shapeRect.xy;
    float lvl = clamp(log2(max(max(size.x, size.y) / 4, 1)), 0, maxMip);
    float L = dot(blurTex.SampleLevel(lin, ((shapeRect.xy + shapeRect.zw) / 2) / targetSize, lvl).rgb, kLuma);
    if (dark > 0.5) col = lerp(col, 0.08.xxx, tint * (0.6 + 0.4 * L));
    else col = lerp(col, 1.0.xxx, tint * (0.6 + 0.4 * (1 - L)));

    // Garde-fou de contraste : le verre reste lisible sur un fond extrême.
    float lo = dark > 0.5 ? 0.06 : 0.38, hi = dark > 0.5 ? 0.42 : 0.92;
    float l2 = dot(col, kLuma);
    if (l2 > hi) col *= hi / l2;
    else if (l2 < lo) col = lerp(col, 1.0.xxx, (lo - l2) / max(1 - l2, 1e-3));

    // Fresnel et liseré spéculaire (lumière venant d'en haut à gauche, reflet plus faible en bas à droite).
    col += fresnel * strength * k * k * k;
    float rim = exp(-(-d) / (0.75 * scale));
    float2 light = normalize(float2(-1, -1));
    col += specular * strength * rim * (0.35 + 0.65 * saturate(dot(n, light)) + 0.26 * saturate(dot(n, -light)));
    col = saturate(col);

    float4 glass = float4(col * cov, cov);
    return (glass + float4(0, 0, 0, sh) * (1 - cov)) * opacity;
}
