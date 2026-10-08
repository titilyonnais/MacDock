// Moteur d'animations : ressorts réglés comme SwiftUI (réponse, amortissement), courbes de Bézier comme Core
// Animation, préréglages macOS.
#include <cmath>

#include "minitest.h"
#include "../src/anim/motion.h"
#include "../src/anim/spring.h"

TEST_CASE(motion_spring_from_response_and_damping) {
    // SwiftUI : raideur (2π / réponse)², amortissement 4π × fraction / réponse (masse 1).
    const md::SpringParams p = md::springFromResponse(0.5, 1.0);
    CHECK(std::abs(p.stiffness - std::pow(2 * 3.14159265358979 / 0.5, 2)) < 1e-6);
    CHECK(std::abs(p.damping - 4 * 3.14159265358979 * 1.0 / 0.5) < 1e-6);
    // Amortissement critique : jamais au-delà de la cible.
    md::Spring s(p.stiffness, p.damping);
    s.snap(0);
    s.setTarget(1);
    double peak = 0;
    for (int i = 0; i < 400; ++i) {
        s.step(1.0 / 120);
        peak = std::max(peak, s.value());
    }
    CHECK(peak <= 1.0 + 1e-3 && s.settled());
    // Rebond (fraction 0,5) : dépasse la cible, puis s'y pose.
    const md::SpringParams b = md::springFromResponse(0.4, 0.5);
    md::Spring r(b.stiffness, b.damping);
    r.snap(0);
    r.setTarget(1);
    double over = 0;
    for (int i = 0; i < 600; ++i) {
        r.step(1.0 / 120);
        over = std::max(over, r.value());
    }
    CHECK(over > 1.05 && r.settled());
}

TEST_CASE(motion_spring_retarget_keeps_velocity) {
    // Une animation interrompue repart de sa vitesse (pas de saut), comme les ressorts de SwiftUI.
    const md::SpringParams p = md::springFromResponse(0.35, 0.86);
    md::Spring s(p.stiffness, p.damping);
    s.snap(0);
    s.setTarget(1);
    for (int i = 0; i < 6; ++i) s.step(1.0 / 120);
    const double v = s.velocity(), x = s.value();
    s.setTarget(-1);
    CHECK(s.velocity() == v && s.value() == x);
    s.step(1.0 / 120);
    CHECK(s.value() > x);   // l'élan continue un instant avant de repartir
}

TEST_CASE(motion_cubic_bezier_like_core_animation) {
    const md::CubicBezier linear{0, 0, 1, 1};
    CHECK(std::abs(linear(0.3) - 0.3) < 1e-4);
    const md::CubicBezier ease = md::kEaseInOut;   // 0.42, 0, 0.58, 1 : symétrique
    CHECK(std::abs(ease(0.5) - 0.5) < 1e-4);
    CHECK(ease(0.1) < 0.1 && ease(0.9) > 0.9);
    CHECK(ease(0) == 0 && ease(1) == 1);
    const md::CubicBezier out = md::kEaseOut;   // 0, 0, 0.58, 1 : rapide au départ
    CHECK(out(0.25) > 0.35);
    for (double x = 0; x <= 1.0001; x += 0.05) CHECK(md::kDefaultTiming(x) >= -1e-6 && md::kDefaultTiming(x) <= 1 + 1e-6);
}

TEST_CASE(motion_presets_for_macos) {
    const md::MotionPreset menu = md::motionPreset(md::Motion::MenuOpen);
    CHECK(menu.duration > 0.1 && menu.duration < 0.4);   // apparition rapide
    const md::MotionPreset dock = md::motionPreset(md::Motion::DockMagnify);
    CHECK(dock.spring.stiffness > 0 && dock.spring.damping > 0);
    const md::MotionPreset bounce = md::motionPreset(md::Motion::WindowBounce);
    CHECK(bounce.dampingFraction < 1);   // un peu de rebond
}

TEST_CASE(motion_glass_emerges_from_an_edge) {
    const md::GlassMorph to{100, 40, 220, 72, 0};   // infobulle à sa place, au-dessus du Dock (bord à y = 90)
    const md::GlassMorph start = md::glassEmerge(90, to, 0, 24);
    CHECK(start.bottom >= 90 - 1e-9 && start.bottom - start.top < 20);   // petite goutte collée au bord
    CHECK(std::abs((start.left + start.right) / 2 - 160) < 1e-9);         // centrée sous la bulle
    CHECK(start.merge == 24);                                             // fondue dans le verre du Dock
    const md::GlassMorph end = md::glassEmerge(90, to, 1, 24);
    CHECK(end.left == 100 && end.top == 40 && end.right == 220 && end.bottom == 72 && end.merge == 0);
    const md::GlassMorph mid = md::glassEmerge(90, to, 0.5, 24);
    CHECK(mid.merge > 0 && mid.merge < 24 && mid.top < start.top && mid.top > to.top - 1e-9);
}
