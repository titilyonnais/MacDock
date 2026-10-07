// Sélecteur d'apps : historique, pas, rangement, raccourci, rendu hors écran.
#include <windows.h>
#include <objbase.h>

#include <cstring>
#include <string>
#include <vector>

#include "minitest.h"
#include "../src/switcher/switcher_window.h"
#include "../src/switcher/switcher_logic.h"

TEST_CASE(switcher_mru_order) {
    md::AppMru m;
    m.touch(L"a");
    m.touch(L"b");
    m.touch(L"c");
    m.touch(L"a");
    auto o = m.order({L"b", L"c", L"d", L"a"});
    REQUIRE(o.size() == 4);
    CHECK(o[0] == L"a");
    CHECK(o[1] == L"c");
    CHECK(o[2] == L"b");
    CHECK(o[3] == L"d");   // jamais activée : à la fin
    CHECK(m.order({}).empty());
    CHECK(m.order({L"z"}) == std::vector<std::wstring>{L"z"});
}

TEST_CASE(switcher_mru_bounded) {
    md::AppMru m;
    for (int i = 0; i < 500; ++i) m.touch(std::to_wstring(i));
    CHECK(m.order({L"0", L"499"}) == (std::vector<std::wstring>{L"499", L"0"}));
}

TEST_CASE(switcher_step_and_start) {
    CHECK(md::switcherStart(0) == 0);
    CHECK(md::switcherStart(1) == 0);
    CHECK(md::switcherStart(5) == 1);
    CHECK(md::switcherStep(1, 5, 1) == 2);
    CHECK(md::switcherStep(4, 5, 1) == 0);
    CHECK(md::switcherStep(0, 5, -1) == 4);
    CHECK(md::switcherStep(0, 0, 1) == 0);
    CHECK(md::switcherStep(7, 3, 0) == 2);   // sélection hors limites ramenée
}

TEST_CASE(switcher_layout_fits) {
    auto g = md::switcherLayout(4, 1800);
    CHECK_NEAR(g.icon, 64, 1e-9);
    CHECK_NEAR(g.width, 2 * g.pad + 4 * g.cell, 1e-9);
    CHECK_NEAR(g.height, 2 * g.pad + g.cell + g.labelH, 1e-9);
    auto many = md::switcherLayout(30, 1000);
    CHECK(many.width <= 1000 + 1e-6);
    CHECK(many.icon > 16 && many.icon < 64);
    CHECK(md::switcherHit(g, 4, g.pad + g.cell * 1.5, g.pad + g.cell / 2) == 1);
    CHECK(md::switcherHit(g, 4, 1, 1) == -1);
    CHECK(md::switcherHit(g, 4, g.pad + g.cell * 4.5, g.pad + g.cell / 2) == -1);
}

TEST_CASE(switcher_hotkey_parse) {
    auto a = md::parseSwitcherHotkey(L"Alt+Tab");
    REQUIRE(a.has_value());
    CHECK(a->mods == MOD_ALT && a->vk == VK_TAB);
    CHECK(!md::parseSwitcherHotkey(L"off"));
    CHECK(!md::parseSwitcherHotkey(L"ctrl+tab"));
}

TEST_CASE(switcher_snapshot_draws_panel) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const std::vector<std::wstring> names{L"Un", L"Deux", L"Trois"};
    auto a = md::switcherSnapshot(names, 1, false, 1280, 800);
    auto b = md::switcherSnapshot(names, 2, false, 1280, 800);
    auto none = md::switcherSnapshot({}, 0, false, 1280, 800);
    REQUIRE(a.w == 1280 && a.h == 800);
    const std::size_t c = (std::size_t(400) * 1280 + 640) * 4;   // centre : l'icône du milieu
    CHECK(std::memcmp(&a.px[c], &none.px[c], 4) != 0);
    CHECK(a.px != b.px);   // la sélection change le dessin
    CoUninitialize();
}
