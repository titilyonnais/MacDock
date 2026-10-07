// Choix de l'écran du Dock : poussée contre le bord, écran enregistré (logique pure).
#include "minitest.h"
#include "../src/app/monitor_choice.h"

namespace {
// Deux écrans côte à côte : A (principal) 0..1920, B 1920..3840, même hauteur.
std::vector<md::MonitorInfo> sideBySide() {
    return {{L"\\\\.\\DISPLAY1", RECT{0, 0, 1920, 1080}, true},
            {L"\\\\.\\DISPLAY2", RECT{1920, 0, 3840, 1080}, false}};
}
} // namespace

TEST_CASE(monitor_push_bottom_edge) {
    auto m = sideBySide();
    auto hit = md::pushedMonitor(m, POINT{2500, 1079}, md::DockPosition::Bottom, 2);
    CHECK(hit == std::optional<std::size_t>(1));
    CHECK(!md::pushedMonitor(m, POINT{2500, 900}, md::DockPosition::Bottom, 2).has_value());
}

TEST_CASE(monitor_push_ignores_inner_edges) {
    auto m = sideBySide();
    // Bord droit de A = bord gauche de B : le curseur peut passer, ce n'est pas un bord d'écran.
    CHECK(!md::pushedMonitor(m, POINT{1919, 500}, md::DockPosition::Right, 2).has_value());
    CHECK(!md::pushedMonitor(m, POINT{1920, 500}, md::DockPosition::Left, 2).has_value());
    // Vrais bords extérieurs.
    auto right = md::pushedMonitor(m, POINT{3839, 500}, md::DockPosition::Right, 2);
    CHECK(right == std::optional<std::size_t>(1));
    auto left = md::pushedMonitor(m, POINT{0, 500}, md::DockPosition::Left, 2);
    CHECK(left == std::optional<std::size_t>(0));
    // Écrans empilés : le bas de celui du haut n'est pas un bord.
    std::vector<md::MonitorInfo> stacked{{L"\\\\.\\DISPLAY1", RECT{0, 0, 1920, 1080}, true},
                                         {L"\\\\.\\DISPLAY2", RECT{0, 1080, 1920, 2160}, false}};
    CHECK(!md::pushedMonitor(stacked, POINT{800, 1079}, md::DockPosition::Bottom, 2).has_value());
    auto low = md::pushedMonitor(stacked, POINT{800, 2159}, md::DockPosition::Bottom, 2);
    CHECK(low == std::optional<std::size_t>(1));
}

TEST_CASE(monitor_choice_falls_back_to_primary) {
    std::vector<md::MonitorInfo> m{{L"\\\\.\\DISPLAY2", RECT{-1920, 0, 0, 1080}, false},
                                   {L"\\\\.\\DISPLAY1", RECT{0, 0, 1920, 1080}, true}};
    CHECK_EQ(md::initialMonitor(m, L""), std::size_t(1));
    CHECK_EQ(md::initialMonitor(m, L"\\\\.\\DISPLAY9"), std::size_t(1));
    CHECK_EQ(md::initialMonitor(m, L"\\\\.\\display2"), std::size_t(0));   // casse ignorée
    CHECK_EQ(md::initialMonitor({}, L""), std::size_t(0));
}

TEST_CASE(monitor_push_requires_motion) {
    md::ScreenPush push;
    const std::wstring b = L"B";
    // Souris posée au bord puis immobile : aucun événement, pas de changement d'écran.
    CHECK(push.update(b, 0.0).empty());
    CHECK(push.update(b, 0.6).empty());   // après un silence, la poussée repart de zéro
    // Poussée continue (événements toutes les 20 ms) : confirmée après 0,35 s, une seule fois.
    std::wstring got;
    double t = 0.62;
    for (; t < 1.2 && got.empty(); t += 0.02) got = push.update(b, t);
    CHECK(got == b);
    CHECK(t > 0.6 + 0.35);
    CHECK(push.update(b, t + 0.02).empty());
    // Changer d'écran visé, ou quitter le bord, remet à zéro.
    md::ScreenPush other;
    for (double u = 0; u < 0.3; u += 0.02) other.update(b, u);
    CHECK(other.update(L"C", 0.32).empty());
    CHECK(other.update(L"", 0.34).empty());
    CHECK(other.update(b, 0.36).empty());
}
