// Moteur d'animations (logique pure) : ressorts réglés comme SwiftUI (réponse, fraction d'amortissement), courbes de
// Bézier comme les fonctions de temps de Core Animation, et préréglages des mouvements de macOS.
#pragma once

namespace md {

struct SpringParams {
    double stiffness = 0, damping = 0;   // masse 1
};
// response : durée d'une oscillation non amortie (s) ; dampingFraction : 1 = critique, < 1 rebond, > 1 lent.
SpringParams springFromResponse(double response, double dampingFraction);

// Courbe de Bézier cubique de (0, 0) à (1, 1), points de contrôle (x1, y1) et (x2, y2) : progression y pour un temps x.
struct CubicBezier {
    double x1 = 0, y1 = 0, x2 = 1, y2 = 1;
    double operator()(double x) const;
};
inline constexpr CubicBezier kDefaultTiming{0.25, 0.1, 0.25, 1.0};   // « default » de Core Animation
inline constexpr CubicBezier kEaseInOut{0.42, 0.0, 0.58, 1.0};
inline constexpr CubicBezier kEaseOut{0.0, 0.0, 0.58, 1.0};
inline constexpr CubicBezier kEaseIn{0.42, 0.0, 1.0, 1.0};

enum class Motion {
    MenuOpen,       // apparition d'un menu (fondu et léger zoom)
    MenuClose,
    DockMagnify,    // agrandissement du Dock qui suit le pointeur
    WindowBounce,   // rebond (icône, pastille relâchée)
    PanelSlide,     // panneau qui glisse (vignette, Centre de notifications)
    GlassMorph,     // formes de verre qui se rejoignent ou se séparent
};
struct MotionPreset {
    double duration = 0;            // courbe : durée (s) ; 0 si ressort seul
    CubicBezier curve{};
    double response = 0, dampingFraction = 1;   // ressort
    SpringParams spring;
};
MotionPreset motionPreset(Motion m);

// Verre qui naît d'un autre (Liquid Glass) : à t = 0, une petite goutte collée au bord edgeY et fondue dans le verre
// voisin (merge = reach) ; à t = 1, la forme à sa place, séparée (merge = 0). Rectangle en pixels.
struct GlassMorph {
    double left = 0, top = 0, right = 0, bottom = 0, merge = 0;
};
GlassMorph glassEmerge(double edgeY, const GlassMorph& to, double t, double reach);

} // namespace md
