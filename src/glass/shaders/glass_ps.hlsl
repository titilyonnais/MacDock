// Verre Liquid Glass d'une forme à coins continus, calé sur des captures de macOS 27 Golden Gate (réglage moyen) :
// fond flouté et plus saturé sous un voile léger, lentille étroite le long du bord, liseré clair d'un seul pixel
// (plus vif en haut qu'en bas), fil sombre pour les menus, ombre portée. Sortie en alpha prémultiplié.
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
    float hairline;       // fil sombre au bord (menus et panneaux), 0 à 1
    float pad1;
    // v2 : plusieurs formes qui fusionnent en douceur (Liquid Glass), et lumière des reflets.
    float count;          // < 2 : forme seule (shapeRect, radius) ; 2 à 8 : formes rects/radii
    float merge;          // portée de la fusion (pixels) ; 0 : formes séparées
    float2 light;         // direction de la lumière (normalisée ; (0, 1) : d'en haut)
    float4 rects[8];
    float4 radii[2];
};
Texture2D blurTex : register(t0);
Texture2D<float> cornerLut : register(t1);   // champ de distance d'un coin de rayon 1 sur [-2, 2]²
SamplerState lin : register(s0);

struct VSOut {
    float4 pos : SV_Position;
    float2 px : TEXCOORD0;
};

static const float3 kLuma = float3(0.2126, 0.7152, 0.0722);

// Distance signée à un rectangle à coins continus (négative dedans).
float sdRect(float2 p, float4 rect, float r) {
    float2 size = rect.zw - rect.xy;
    float2 lp = p - rect.xy;
    if (r <= 1e-3) {
        float2 d = abs(lp - size / 2) - size / 2;
        return length(max(d, 0)) + min(max(d.x, d.y), 0);
    }
    float2 q = float2(min(lp.x, size.x - lp.x), min(lp.y, size.y - lp.y)) / r;
    if (all(q >= -2) && all(q <= 2)) return cornerLut.SampleLevel(lin, (q + 2) / 4, 0) * r;
    if (all(q >= 0)) return -min(q.x, q.y) * r;
    return length(max(-q, 0)) * r;
}

// Minimum adouci : deux gouttes de verre proches se rejoignent par un pont arrondi (portée k).
float smin(float a, float b, float k) {
    float h = max(k - abs(a - b), 0) / k;
    return min(a, b) - h * h * k * 0.25;
}

float sdShape(float2 p) {
    float d = 1e9;
    if (count < 1.5) {
        d = sdRect(p, shapeRect, radius);
    } else {
        const uint n = (uint)count;
        [loop] for (uint i = 0; i < 8; ++i) {
            if (i >= n) break;
            const float di = sdRect(p, rects[i], radii[i >> 2][i & 3]);
            d = i == 0 ? di : (merge > 0 ? smin(d, di, 2 * merge) : min(d, di));
        }
    }
    return d;
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

    // Lentille du bord : sur une bande étroite (bevel), le fond est lu un peu vers le centre ; aberration chromatique.
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

    // Voile : vers le blanc en clair, vers le noir en sombre ; le fond reste reconnaissable.
    col = lerp(col, dark > 0.5 ? 0.0.xxx : 1.0.xxx, tint);

    // Garde-fou de contraste : le verre reste lisible sur un fond extrême.
    float lo = dark > 0.5 ? 0.05 : 0.38, hi = dark > 0.5 ? 0.40 : 0.94;
    float l2 = dot(col, kLuma);
    if (l2 > hi) col *= hi / l2;
    else if (l2 < lo) col = lerp(col, 1.0.xxx, (lo - l2) / max(1 - l2, 1e-3));

    // Bande intérieure à peine plus claire, le long de la lentille.
    col += fresnel * strength * k * k;

    // Liseré : un fil d'un pixel (0,5 pt en 2x) tout au bord, plus vif en haut (1) que sur les côtés (0,8) et en
    // bas (0,6), comme mesuré ; le fil sombre des menus se pose au même endroit.
    float w = max(1.0, 0.5 * scale);
    float edge = saturate(1.25 - (-d) / w);
    col = lerp(col, 0.0.xxx, hairline * edge);
    // Reflet du liseré : plus vif face à la lumière (d'en haut par défaut : 1 en haut, 0,8 sur les côtés, 0,6 en bas).
    col = lerp(col, 1.0.xxx, saturate(specular * strength * (0.8 + 0.2 * dot(n, -light))) * edge);
    col = saturate(col);

    float4 glass = float4(col * cov, cov);
    return (glass + float4(0, 0, 0, sh) * (1 - cov)) * opacity;
}
