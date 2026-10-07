// Écran Apps : catalogue, recherche, géométrie, clavier, lecture du dossier Apps, rendu hors écran.
#include <windows.h>
#include <objbase.h>

#include <string>
#include <vector>

#include "minitest.h"
#include "../src/apps/app_catalog.h"

TEST_CASE(apps_listed_filters_docs_and_uninstallers) {
    CHECK(md::isListedApp({L"Paint", L"Microsoft.Paint_8wekyb3d8bbwe!App"}));
    CHECK(md::isListedApp({L"Notepad++", L"C:\\Program Files\\Notepad++\\notepad++.exe"}));
    CHECK(!md::isListedApp({L"Uninstall Foo", L"C:\\Foo\\unins000.exe"}));
    CHECK(!md::isListedApp({L"Désinstaller Bar", L"C:\\Bar\\uninst.exe"}));
    CHECK(!md::isListedApp({L"Foo Help", L"C:\\Foo\\help.chm"}));
    CHECK(!md::isListedApp({L"Site web", L"C:\\Foo\\site.url"}));
    CHECK(!md::isListedApp({L"Lisez-moi", L"C:\\Foo\\README.TXT"}));
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
