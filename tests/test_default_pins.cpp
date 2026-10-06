#include <windows.h>
#include <objbase.h>

#include <set>

#include "minitest.h"
#include "../src/shell/default_pins.h"

TEST_CASE(default_pins_structure) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    auto pins = md::defaultPins();
    REQUIRE(pins.size() >= 3);
    CHECK(pins[0].kind == md::PinKind::App);
    CHECK(pins[0].launch.ends_with(L"explorer.exe"));
    CHECK(pins[1].kind == md::PinKind::AppsButton);
    CHECK(pins.back().kind == md::PinKind::Stack);
    std::set<std::wstring> ids;
    for (auto& p : pins) {
        if (p.kind != md::PinKind::App) continue;
        CHECK(!p.appId.empty());
        CHECK(!p.name.empty());
        CHECK(ids.insert(p.appId).second);   // pas de doublon
        CHECK(p.appId != L"Microsoft.Windows.Explorer");   // l'Explorateur n'est épinglé qu'une fois
    }
    CoUninitialize();
}
