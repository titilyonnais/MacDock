// Écran Apps : catalogue, recherche, géométrie, clavier, lecture du dossier Apps, rendu hors écran.
#include <windows.h>
#include <objbase.h>

#include <memory>
#include <string>
#include <vector>

#include "minitest.h"
#include "../src/apps/app_catalog.h"
#include "../src/apps/apps_folder.h"
#include "../src/apps/apps_icon_cache.h"
#include "../src/apps/apps_window.h"
#include "../src/apps/apps_layout.h"

TEST_CASE(apps_listed_filters_docs_and_uninstallers) {
    CHECK(md::isListedApp({L"Paint", L"Microsoft.Paint_8wekyb3d8bbwe!App"}));
    CHECK(md::isListedApp({L"Notepad++", L"C:\\Program Files\\Notepad++\\notepad++.exe"}));
    CHECK(!md::isListedApp({L"Uninstall Foo", L"C:\\Foo\\unins000.exe"}));
    CHECK(!md::isListedApp({L"Désinstaller Bar", L"C:\\Bar\\uninst.exe"}));
    CHECK(!md::isListedApp({L"Foo Help", L"C:\\Foo\\help.chm"}));
    CHECK(!md::isListedApp({L"Site web", L"C:\\Foo\\site.url"}));
    CHECK(!md::isListedApp({L"Lisez-moi", L"C:\\Foo\\README.TXT"}));
    CHECK(!md::isListedApp({L"Epson Connect Site", L"https://www.epsonconnect.com/?p=0"}));   // lien web
    CHECK(!md::isListedApp({L"Forum", L"HTTP://example.com/forum"}));
    CHECK(md::isListedApp({L"Counter-Strike 2", L"steam://rungameid/730"}));                // jeu : gardé
    CHECK(!md::isListedApp({L"", L"x.exe"}));
    CHECK(!md::isListedApp({L"Vide", L""}));
}

TEST_CASE(apps_catalog_sorted_deduplicated) {
    auto c = md::catalogFrom({{L"zoom", L"z"}, {L"Édition", L"e"}, {L"edge", L"m"}, {L"Calculatrice", L"c"},
                              {L"Calculatrice", L"c"}, {L"Aide", L"a.chm"}, {L"App 10", L"a10"}, {L"App 9", L"a9"}});
    REQUIRE(c.size() == 6);
    CHECK(c[0].name == L"App 9");      // chiffres comme des nombres
    CHECK(c[1].name == L"App 10");
    CHECK(c[2].name == L"Calculatrice");
    CHECK(c[3].name == L"edge");       // casse et accents ignorés : edge < Édition < zoom
    CHECK(c[4].name == L"Édition");
    CHECK(c[5].name == L"zoom");
    CHECK(md::launchTarget(c[2]) == L"shell:AppsFolder\\c");
}

TEST_CASE(apps_search_groups_and_folding) {
    std::vector<md::AppEntry> apps{{L"Calculatrice", L"1"}, {L"Microsoft Edge", L"2"}, {L"Éditeur du Registre", L"3"},
                                   {L"Paint", L"4"}, {L"Outil Capture d'écran", L"5"}, {L"Edge Dev", L"6"}};
    CHECK(md::searchApps(apps, L"").size() == 6);
    auto r = md::searchApps(apps, L"EDGE");
    REQUIRE(r.size() == 2);
    CHECK(apps[r[0]].name == L"Edge Dev");         // début du nom avant début d'un mot
    CHECK(apps[r[1]].name == L"Microsoft Edge");
    r = md::searchApps(apps, L"edit");
    REQUIRE(r.size() == 1);
    CHECK(apps[r[0]].name == L"Éditeur du Registre");   // accents ignorés
    r = md::searchApps(apps, L"cran");
    REQUIRE(r.size() == 1);
    CHECK(apps[r[0]].name == L"Outil Capture d'écran");  // ailleurs dans le nom
    r = md::searchApps(apps, L"  paint ");
    REQUIRE(r.size() == 1);                               // espaces autour ignorés
    CHECK(md::searchApps(apps, L"zzz").empty());
    CHECK(md::foldForSearch(L"ÉcOle Œuvre") == L"ecole œuvre");
}

TEST_CASE(apps_layout_large_and_small) {
    auto g = md::appsLayout(1920, 1080, 80);
    CHECK(g.columns == 7 && g.rows == 5 && g.perPage == 35 && g.pages == 3);
    CHECK(g.icon == 96);
    CHECK(g.searchTop > 0 && g.gridTop > g.searchTop + g.searchH);
    CHECK(g.gridLeft + g.columns * g.cellW <= 1920 - g.gridLeft + 0.01);
    CHECK(g.dotsY > g.gridTop + g.rows * g.cellH && g.dotsY < 1080);
    auto s = md::appsLayout(1024, 640, 80);
    CHECK(s.columns >= 4 && s.columns < 7 && s.rows >= 2 && s.rows < 5);
    CHECK(s.icon >= 48 && s.icon <= 96);
    CHECK(s.pages == int((80 + s.perPage - 1) / s.perPage));
    CHECK(md::appsLayout(1920, 1080, 0).pages == 1);
}

TEST_CASE(apps_hit_last_page) {
    auto g = md::appsLayout(1920, 1080, 37);   // page 2 : 2 apps
    const double cx = g.gridLeft + g.cellW / 2, cy = g.gridTop + g.cellH / 2;
    CHECK(md::appsHit(g, 0, cx, cy, 37) == 0);
    CHECK(md::appsHit(g, 0, cx + 2 * g.cellW, cy + g.cellH, 37) == 9);
    CHECK(md::appsHit(g, 1, cx + g.cellW, cy, 37) == 36);
    CHECK(md::appsHit(g, 1, cx + 2 * g.cellW, cy, 37) == -1);   // case vide
    CHECK(md::appsHit(g, 0, g.gridLeft - 5, cy, 37) == -1);
    CHECK(md::appsHit(g, 0, cx, g.gridTop - 5, 37) == -1);
}

TEST_CASE(apps_keys_move_across_pages) {
    auto g = md::appsLayout(1920, 1080, 37);
    md::AppsCursor c;
    c = md::appsKey(g, c, VK_RIGHT, 37);
    CHECK(c.selected == 0 && c.page == 0);          // première flèche : première app de la page
    c = md::appsKey(g, c, VK_DOWN, 37);
    CHECK(c.selected == 7);
    c = md::appsKey(g, c, VK_UP, 37);
    CHECK(c.selected == 0);
    c = md::appsKey(g, c, VK_UP, 37);
    CHECK(c.selected == 0);                          // déjà en haut
    c = md::appsKey(g, c, VK_LEFT, 37);
    CHECK(c.selected == 0);                          // déjà au début
    c = md::appsKey(g, {0, 34}, VK_RIGHT, 37);
    CHECK(c.selected == 35 && c.page == 1);          // passe à la page suivante
    c = md::appsKey(g, c, VK_DOWN, 37);
    CHECK(c.selected == 35);                         // rien en dessous
    c = md::appsKey(g, c, VK_RIGHT, 37);
    c = md::appsKey(g, c, VK_RIGHT, 37);
    CHECK(c.selected == 36);                         // dernière app
    c = md::appsKey(g, {0, -1}, VK_NEXT, 37);
    CHECK(c.page == 1 && c.selected == -1);          // page suivante sans sélection
    c = md::appsKey(g, {1, 36}, VK_PRIOR, 37);
    CHECK(c.page == 0 && c.selected == 0);           // page précédente : sa première app
    c = md::appsKey(g, {0, 5}, VK_END, 37);
    CHECK(c.page == 1 && c.selected == 36);
    c = md::appsKey(g, c, VK_HOME, 37);
    CHECK(c.page == 0 && c.selected == 0);
    CHECK(md::appsKey(g, {0, -1}, VK_RIGHT, 0).selected == -1);   // aucune app
}

TEST_CASE(apps_read_real_folder) {   // lit le dossier Apps de Windows sans rien lancer
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    auto raw = md::readAppsFolder();
    CHECK(raw.size() >= 5);
    for (auto& e : raw) CHECK(!e.parsingName.empty());
    md::AppCatalog cat;
    cat.refreshAsync();
    auto list = cat.get(10000);
    CHECK(!list.empty() && list.size() <= raw.size());
    CoUninitialize();
}

TEST_CASE(apps_snapshot_draws_offscreen) {   // Direct2D sur une bitmap : aucune fenêtre
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::vector<md::AppEntry> apps;
    for (int i = 0; i < 40; ++i) apps.push_back({L"App " + std::to_wstring(i), L"x" + std::to_wstring(i)});
    auto im = md::appsSnapshot(apps, L"", 0, true, 1280, 800, false);
    REQUIRE(im.w == 1280 && im.h == 800 && im.px.size() == 1280u * 800 * 4);
    auto q = md::appsSnapshot(apps, L"App 3", 0, false, 1280, 800, false);
    CHECK(q.px != im.px);
    CHECK(q.px[3] == 255);   // opaque : fond d'écran flouté sous la vue
    CoUninitialize();
}

TEST_CASE(apps_go_to_page_moves_selection) {   // relecture finale, important 2 : molette et points de page
    auto g = md::appsLayout(1920, 1080, 37);
    auto c = md::appsGoToPage(g, {0, 0}, 1, 37);
    CHECK(c.page == 1 && c.selected == 35);          // la sélection suit la page
    c = md::appsGoToPage(g, {0, -1}, 1, 37);
    CHECK(c.page == 1 && c.selected == -1);          // sans sélection : aucune
    c = md::appsGoToPage(g, {1, 36}, 0, 37);
    CHECK(c.page == 0 && c.selected == 0);
    c = md::appsGoToPage(g, {0, 3}, 9, 37);
    CHECK(c.page == 1 && c.selected == 35);          // page bornée
    c = md::appsKey(g, md::appsGoToPage(g, {0, 0}, 1, 37), VK_RIGHT, 37);
    CHECK(c.page == 1 && c.selected == 36);          // la flèche part de la page affichée
}

TEST_CASE(apps_press_gate_needs_press_in_view) {   // relecture finale, important 1 : double-clic sur le bouton Apps
    md::PressGate gate;
    CHECK(!gate.release());                           // relâchement d'un clic commencé ailleurs (le Dock) : ignoré
    gate.press();
    CHECK(gate.release());
    CHECK(!gate.release());
}

TEST_CASE(apps_icon_cache_keeps_icons_between_openings) {   // relecture finale : icônes déjà là à la réouverture
    md::AppsIconCache cache;
    cache.setStyle(L"clair");
    auto img = std::make_shared<md::IconProvider::Image>();
    img->size = 4;
    CHECK(cache.find(L"a", 96) == nullptr);
    cache.put(L"a", 96, img);
    CHECK(cache.find(L"a", 96) == img);
    CHECK(cache.find(L"a", 64) == nullptr);          // autre taille : autre image
    cache.setStyle(L"clair");
    CHECK(cache.find(L"a", 96) == img);              // même style : gardée
    cache.setStyle(L"sombre");
    CHECK(cache.find(L"a", 96) == nullptr);          // style changé : vidé
}
