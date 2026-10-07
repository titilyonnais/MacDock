// Spotlight : calcul, résultats, URL de recherche, raccourci, recherche de documents, rendu hors écran.
#include <windows.h>
#include <objbase.h>

#include <string>
#include <vector>

#include "minitest.h"
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
