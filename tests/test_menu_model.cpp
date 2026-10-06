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
