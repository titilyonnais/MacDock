// Spotlight : calcul, résultats, URL de recherche, raccourci, recherche de documents, rendu hors écran.
#include <windows.h>
#include <objbase.h>

#include <string>
#include <vector>

#include "minitest.h"
#include "../src/spotlight/spot_results.h"
#include "../src/spotlight/spot_calc.h"

TEST_CASE(spot_calc_evaluates) {
    auto e = [](const wchar_t* s) { return md::evaluateExpression(s); };
    CHECK(e(L"12*(3+4)") == 84.0);
    CHECK(e(L"1+2*3") == 7.0);
    CHECK(e(L" 2 ^ 3 ^ 2 ") == 512.0);     // puissance associative à droite
    CHECK(e(L"-(2)+5") == 3.0);
    CHECK(e(L"50%") == 0.5);
    CHECK(e(L"200*15%") == 30.0);
    CHECK(e(L"1,5+1") == 2.5);
    CHECK(e(L"7÷2") == 3.5);
    CHECK(e(L"3×4") == 12.0);
    CHECK(e(L"10-2-3") == 5.0);             // associatif à gauche
    CHECK(!e(L"3/0"));
    CHECK(!e(L"(("));
    CHECK(!e(L"2024"));                      // un nombre seul n'est pas un calcul
    CHECK(!e(L"-5"));
    CHECK(!e(L"calc"));
    CHECK(!e(L"2+"));
    CHECK(!e(L""));
}

TEST_CASE(spot_calc_formats_french) {
    CHECK(md::formatNumber(84) == L"84");
    CHECK(md::formatNumber(2.5) == L"2,5");
    CHECK(md::formatNumber(1234.5) == L"1\u202F234,5");
    CHECK(md::formatNumber(-1234567) == L"-1\u202F234\u202F567");
    CHECK(md::formatNumber(1.0 / 3) == L"0,3333333333");
    CHECK(md::formatNumber(0.1 + 0.2) == L"0,3");
    CHECK(md::formatNumber(1e20) == L"1e+20");
}

TEST_CASE(spot_results_sections) {
    std::vector<md::AppEntry> apps{{L"Calculatrice", L"calc!App"}, {L"Calendrier", L"cal!App"}, {L"Paint", L"paint!App"}};
    std::vector<md::SpotItem> files{{md::SpotKind::File, L"calcul.xlsx", L"C:\\Users\\x\\Documents", L"C:\\Users\\x\\Documents\\calcul.xlsx"}};
    auto r = md::spotlightResults(L"cal", apps, files);
    REQUIRE(r.size() == 3);
    CHECK(r[0].title == L"Meilleur résultat" && r[0].items.size() == 1 && r[0].items[0].title == L"Calculatrice");
    CHECK(r[0].items[0].target == L"shell:AppsFolder\\calc!App");
    CHECK(r[1].title == L"Applications" && r[1].items.size() == 1 && r[1].items[0].title == L"Calendrier");
    CHECK(r[2].title == L"Documents" && r[2].items.size() == 1);
    CHECK(md::spotCount(r) == 3);
    CHECK(md::spotAt(r, 2)->kind == md::SpotKind::File);
    CHECK(md::spotAt(r, 3) == nullptr);
    auto c = md::spotlightResults(L"12*(3+4)", apps, {});
    REQUIRE(!c.empty());
    CHECK(c[0].items[0].kind == md::SpotKind::Calc && c[0].items[0].title == L"84");
    CHECK(c[0].items[0].subtitle == L"12*(3+4) =");
    CHECK(md::spotlightResults(L"   ", apps, files).empty());
    CHECK(md::spotlightResults(L"zzz", apps, {}).empty());
    std::vector<md::AppEntry> many;
    for (int i = 0; i < 20; ++i) many.push_back({L"App " + std::to_wstring(i), L"a" + std::to_wstring(i)});
    auto m = md::spotlightResults(L"app", many, {});
    CHECK(m.size() == 2 && m[1].items.size() == 6);   // 1 meilleur + 6 applications
}

TEST_CASE(spot_search_ms_url) {
    CHECK(md::searchMsUrl(L"rapport", L"C:\\Users\\x") ==
          L"search-ms:query=rapport&crumb=location:C%3A%5CUsers%5Cx");
    CHECK(md::searchMsUrl(L"a&b=c%#\"d e", L"C:\\U") ==
          L"search-ms:query=a%26b%3Dc%25%23%22d%20e&crumb=location:C%3A%5CU");
    CHECK(md::searchMsUrl(L"école", L"C:\\U").find(L"%C3%A9cole") != std::wstring::npos);   // UTF-8 encodé
}

TEST_CASE(spot_hotkey_parse) {
    auto a = md::parseSpotlightHotkey(L"alt+space");
    REQUIRE(a.has_value());
    CHECK(a->mods == MOD_ALT && a->vk == VK_SPACE);
    auto c = md::parseSpotlightHotkey(L"Ctrl+Space");
    REQUIRE(c.has_value());
    CHECK(c->mods == MOD_CONTROL && c->vk == VK_SPACE);
    CHECK(!md::parseSpotlightHotkey(L"off"));
    CHECK(!md::parseSpotlightHotkey(L"win+space"));
    CHECK(!md::parseSpotlightHotkey(L""));
}
