// Animations de DWM coupées pour la seule fenêtre que le Dock anime lui-même, puis rendues.
#include <map>

#include "minitest.h"
#include "../src/app/transition_gate.h"

namespace {
struct FakeDwm {
    std::map<HWND, bool> disabled;   // état posé, par fenêtre
    int calls = 0;
    std::map<HWND, bool> dead;
    md::TransitionApi api() {
        return {[this](HWND h, bool off) {
                    disabled[h] = off;
                    ++calls;
                    return true;
                },
                [this](HWND h) { return !dead[h]; }};
    }
};
const HWND kA = reinterpret_cast<HWND>(0x10), kB = reinterpret_cast<HWND>(0x20);
auto idle = [](HWND) { return false; };
} // namespace

TEST_CASE(transition_gate_cuts_once_and_extends) {
    FakeDwm dwm;
    md::TransitionGate g(dwm.api());
    g.hold(kA, 1.0);
    CHECK(dwm.disabled[kA]);
    CHECK(g.held(kA));
    g.hold(kA, 2.0);   // déjà coupée : seulement retenue plus longtemps
    CHECK_EQ(dwm.calls, 1);
    g.release(1.5, idle);
    CHECK(g.held(kA));
    g.release(2.5, idle);
    CHECK(!g.held(kA));
    CHECK(!dwm.disabled[kA]);   // rendue
}

TEST_CASE(transition_gate_keeps_busy_windows) {   // réduite, ou génie en cours : la restauration ne doit pas doubler
    FakeDwm dwm;
    md::TransitionGate g(dwm.api());
    g.hold(kA, 1.0);
    g.hold(kB, 1.0);
    g.release(5.0, [](HWND h) { return h == kA; });
    CHECK(g.held(kA));
    CHECK(!g.held(kB));
    CHECK(dwm.disabled[kA]);
    CHECK(!dwm.disabled[kB]);
    CHECK(g.any());
    g.release(6.0, idle);
    CHECK(!g.any());
}

TEST_CASE(transition_gate_forgets_closed_windows) {
    FakeDwm dwm;
    md::TransitionGate g(dwm.api());
    g.hold(kA, 1.0);
    dwm.dead[kA] = true;
    g.release(0.5, idle);   // fermée : oubliée sans appel, même avant l'échéance
    CHECK(!g.held(kA));
    CHECK_EQ(dwm.calls, 1);
}

TEST_CASE(transition_gate_release_all_on_exit) {
    FakeDwm dwm;
    md::TransitionGate g(dwm.api());
    g.hold(kA, 100.0);
    g.hold(kB, 100.0);
    g.releaseAll();
    CHECK(!dwm.disabled[kA]);
    CHECK(!dwm.disabled[kB]);
    CHECK(!g.any());
}

namespace {
LRESULT CALLBACK plainProc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }
}

TEST_CASE(transition_gate_marks_held_windows_for_crash_recovery) {
    // Un Dock qui plante laisse ses fenêtres sans animations : la marque posée avec l'attribut permet au Dock suivant
    // de les leur rendre, et seulement à elles.
    WNDCLASSW wc{};
    wc.lpfnWndProc = plainProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"MacDockTestTransitions";
    RegisterClassW(&wc);
    HWND held = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr,
                                wc.hInstance, nullptr);
    HWND other = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr,
                                 wc.hInstance, nullptr);
    REQUIRE(held && other);
    const md::TransitionApi api = md::realTransitionApi();
    api.disable(held, true);
    CHECK(md::transitionsMarked(held));
    CHECK(!md::transitionsMarked(other));
    // Seulement celles de ce processus : un vrai Dock qui tourne garde les siennes.
    CHECK_EQ(md::releaseOrphanTransitions(GetCurrentProcessId()), 1);
    CHECK(!md::transitionsMarked(held));
    api.disable(held, true);
    api.disable(held, false);   // rendue normalement : plus de marque
    CHECK(!md::transitionsMarked(held));
    DestroyWindow(held);
    DestroyWindow(other);
}
