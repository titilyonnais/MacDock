// Modèle des menus contextuels en verre : mise en page, navigation au clavier, sélection à la souris.
#include "minitest.h"
#include "../src/popup/menu_model.h"

namespace {
md::MenuModel sample() {
    md::MenuModel m;
    m.items.push_back({1, L"Ouvrir"});
    m.items.push_back({2, L"Afficher dans l'Explorateur"});
    m.items.push_back({});                       // séparateur
    md::MenuItem off{3, L"Masquer"};
    off.enabled = false;
    m.items.push_back(off);
    m.items.push_back({4, L"Quitter"});
    return m;
}
} // namespace

TEST_CASE(menu_layout_heights) {
    md::MenuModel m;
    m.items = {{1, L"A"}, {2, L"B"}, {}, {3, L"C"}};
    auto l = md::layoutMenu(m, 100);
    CHECK_NEAR(l.height, 3 * md::kMenuItemHeight + md::kMenuSeparatorHeight + 2 * md::kMenuPadding, 1e-9);
    CHECK_NEAR(l.top[0], md::kMenuPadding, 1e-9);
    CHECK_NEAR(l.top[3], md::kMenuPadding + 2 * md::kMenuItemHeight + md::kMenuSeparatorHeight, 1e-9);
    CHECK(l.width > 100);
}

TEST_CASE(menu_keyboard_skips_separators_and_disabled) {
    auto m = sample();
    CHECK_EQ(md::nextSelectable(m, -1, +1), 0);
    CHECK_EQ(md::nextSelectable(m, 0, +1), 1);
    CHECK_EQ(md::nextSelectable(m, 1, +1), 4);   // saute le séparateur et « Masquer » désactivé
    CHECK_EQ(md::nextSelectable(m, 4, -1), 1);
}

TEST_CASE(menu_keyboard_wraps) {
    auto m = sample();
    CHECK_EQ(md::nextSelectable(m, 4, +1), 0);
    CHECK_EQ(md::nextSelectable(m, 0, -1), 4);
    CHECK_EQ(md::nextSelectable(m, -1, -1), 4);
    md::MenuModel empty;
    CHECK_EQ(md::nextSelectable(empty, -1, +1), -1);
}

TEST_CASE(menu_hit_test) {
    auto m = sample();
    auto l = md::layoutMenu(m, 120);
    CHECK_EQ(md::hitTestMenu(l, m, l.top[4] + md::kMenuItemHeight / 2), 4);
    CHECK_EQ(md::hitTestMenu(l, m, l.top[2] + md::kMenuSeparatorHeight / 2), -1);   // séparateur
    CHECK_EQ(md::hitTestMenu(l, m, l.top[3] + 3), -1);                              // désactivée
    CHECK_EQ(md::hitTestMenu(l, m, -5), -1);
    CHECK_EQ(md::hitTestMenu(l, m, l.height + 5), -1);
}

TEST_CASE(menu_layout_reserves_icon_space) {
    md::MenuModel plain{{{1, L"Un"}, {2, L"Deux"}}};
    md::MenuModel withIcon = plain;
    auto img = std::make_shared<md::IconProvider::Image>();
    img->size = 16;
    img->bgra.assign(16 * 16 * 4, 255);
    withIcon.items[1].icon = img;
    auto a = md::layoutMenu(plain, 200), b = md::layoutMenu(withIcon, 200);
    CHECK_NEAR(a.iconSpace, 0, 1e-9);
    CHECK(b.iconSpace >= md::kMenuIconSize);
    CHECK_NEAR(b.width, a.width + b.iconSpace, 1e-9);
}

TEST_CASE(menu_layout_reserves_shortcut_width) {
    md::MenuModel m;
    m.items.push_back({1, L"Enregistrer"});
    CHECK_NEAR(md::layoutMenu(m, 150).width, 199, 1e-9);            // 2×5 + 11 (sans coche) + 150 + 28
    CHECK_NEAR(md::layoutMenu(m, 150, 40).width, 263, 1e-9);        // + 24 d'écart + 40 de raccourci
    CHECK_NEAR(md::layoutMenu(m, 100, 0).width, md::kMenuMinWidth, 1e-9);
}

TEST_CASE(menu_switch_result_roundtrip) {
    for (int k : {0, 1, 7}) CHECK(md::menuSwitchTarget(md::menuSwitchResult(k)) == std::optional<int>(k));
    CHECK(!md::menuSwitchTarget(0).has_value());     // rien choisi
    CHECK(!md::menuSwitchTarget(42).has_value());    // une entrée
    CHECK(!md::menuSwitchTarget(-1).has_value());
}

TEST_CASE(bar_title_at_skips_current) {
    std::vector<RECT> titles{{0, 0, 30, 24}, {30, 0, 100, 24}, {100, 0, 160, 24}};
    CHECK_EQ(md::barTitleAt(titles, POINT{50, 10}, 0), 1);
    CHECK_EQ(md::barTitleAt(titles, POINT{50, 10}, 1), -1);   // titre déjà ouvert
    CHECK_EQ(md::barTitleAt(titles, POINT{120, 23}, 1), 2);
    CHECK_EQ(md::barTitleAt(titles, POINT{120, 24}, 1), -1);  // sous la barre
    CHECK_EQ(md::barTitleAt(titles, POINT{200, 10}, 1), -1);
}

TEST_CASE(menu_layout_text_close_to_edge_without_checks) {
    // macOS 27 (menu Édition) : sans coche, le texte commence à ~15 pt du bord ; la colonne de coche n'est réservée
    // que dans un menu qui en a.
    md::MenuModel plain;
    plain.items = {md::MenuItem{1, L"Couper"}, md::MenuItem{2, L"Copier"}};
    md::MenuModel checks = plain;
    checks.items[1].checked = true;
    const auto a = md::layoutMenu(plain, 200), b = md::layoutMenu(checks, 200);
    CHECK_NEAR(md::kMenuPadding + a.textLeft, 16, 1.0);
    CHECK_NEAR(b.textLeft, md::kMenuTextLeft, 1e-9);
    CHECK(a.width < b.width);
}
