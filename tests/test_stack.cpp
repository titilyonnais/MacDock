// Piles : tri, présentation automatique, géométrie de l'éventail et de la grille (logique pure).
#include <cmath>

#include "minitest.h"
#include "../src/stack/stack_layout.h"
#include "../src/stack/stack_model.h"

namespace {
md::StackItem item(const wchar_t* name, std::uint64_t created, std::uint64_t modified, bool folder = false) {
    md::StackItem it;
    it.name = name;
    it.path = std::wstring(L"C:\\D\\") + name;
    it.created = created;
    it.modified = modified;
    it.isFolder = folder;
    return it;
}
} // namespace

TEST_CASE(stack_items_sorted_by_date) {
    auto sorted = md::sortStack({item(L"a.txt", 10, 50), item(L"b.txt", 30, 20), item(L"c.txt", 20, 40)},
                                md::StackSort::DateAdded);
    REQUIRE(sorted.size() == 3);
    CHECK(sorted[0].name == L"b.txt");   // plus récent en premier
    CHECK(sorted[1].name == L"c.txt");
    CHECK(sorted[2].name == L"a.txt");
    auto byModified = md::sortStack({item(L"a.txt", 10, 50), item(L"b.txt", 30, 20)}, md::StackSort::Modified);
    CHECK(byModified[0].name == L"a.txt");
}

TEST_CASE(stack_sort_by_name_is_case_insensitive) {
    auto sorted = md::sortStack({item(L"b.txt", 1, 1), item(L"A.txt", 2, 2), item(L"c.txt", 3, 3), item(L"x10", 4, 4),
                                 item(L"x9", 5, 5)},
                                md::StackSort::Name);
    REQUIRE(sorted.size() == 5);
    CHECK(sorted[0].name == L"A.txt");
    CHECK(sorted[1].name == L"b.txt");
    CHECK(sorted[2].name == L"c.txt");
    CHECK(sorted[3].name == L"x9");      // nombres dans l'ordre naturel, comme l'Explorateur et le Finder
    CHECK(sorted[4].name == L"x10");
    auto byKind = md::sortStack({item(L"z.txt", 1, 1), item(L"y.png", 2, 2), item(L"Dossier", 3, 3, true)},
                                md::StackSort::Kind);
    CHECK(byKind[0].name == L"Dossier");   // dossiers d'abord, puis par extension
    CHECK(byKind[1].name == L"y.png");
}

TEST_CASE(stack_view_auto_switches_at_ten) {
    CHECK(md::resolveView(md::StackView::Auto, 9) == md::StackView::Fan);
    CHECK(md::resolveView(md::StackView::Auto, 10) == md::StackView::Grid);
    CHECK(md::resolveView(md::StackView::Grid, 3) == md::StackView::Grid);
    CHECK(md::resolveView(md::StackView::Fan, 40) == md::StackView::Fan);
}

TEST_CASE(stack_fan_arc_rises_and_curves) {
    auto slots = md::fanLayout(8, 48);
    REQUIRE(slots.size() == 8);
    CHECK(slots[0].dy < -24);   // au-dessus de l'icône de la pile
    for (std::size_t i = 1; i < slots.size(); ++i) {
        CHECK(slots[i].dy < slots[i - 1].dy - 40);    // monte d'au moins une case
        CHECK(slots[i].dx > slots[i - 1].dx);         // s'incurve vers la droite
        CHECK(slots[i].angle > slots[i - 1].angle);   // et s'incline de plus en plus
    }
    CHECK(std::abs(slots[0].dx) < 4);
    CHECK(slots[7].angle < 20);
}

TEST_CASE(stack_layout_caps_items) {
    CHECK_EQ(md::fanLayout(30, 48).size(), md::kFanMaxItems);
    CHECK_EQ(md::kFanMaxItems, std::size_t(15));
    auto g = md::gridLayout(100, 48, 2000);
    CHECK_EQ(g.columns, 5);
    CHECK_EQ(g.rows, 10);   // 48 éléments au plus
    std::vector<md::StackItem> many(60, item(L"x", 1, 1));
    CHECK_EQ(md::capStack(many, md::kGridMaxItems).size(), std::size_t(48));
}

TEST_CASE(stack_grid_columns_capped_at_five) {
    CHECK_EQ(md::gridLayout(1, 48, 2000).columns, 1);
    CHECK_EQ(md::gridLayout(4, 48, 2000).columns, 4);
    CHECK_EQ(md::gridLayout(12, 48, 2000).columns, 4);
    CHECK_EQ(md::gridLayout(24, 48, 2000).columns, 5);
    auto small = md::gridLayout(48, 48, 400);   // hauteur limitée : défilement
    CHECK(small.visibleRows < small.rows);
    CHECK(small.visibleRows >= 1);
    CHECK(small.height <= 400);
    auto all = md::gridLayout(12, 48, 2000);
    CHECK_EQ(all.visibleRows, all.rows);
    CHECK(all.width > 4 * 48);
}
