// Spotlight : réglages proposés (plan 55), sections de Réglages MacDock et pages des Paramètres de Windows.
#include <set>
#include <string>
#include <vector>

#include "minitest.h"
#include "../src/apps/app_catalog.h"
#include "../src/core/strings.h"
#include "../src/settings/pane_catalog.h"
#include "../src/spotlight/spot_settings.h"

namespace {

std::wstring firstTitle(const wchar_t* query) {
    const auto& pages = md::settingsPages();
    const auto r = md::searchSettings(pages, query);
    return r.empty() ? std::wstring() : pages[r[0]].title;
}

} // namespace

TEST_CASE(spot_settings_table_is_valid) {
    const auto& pages = md::settingsPages();
    REQUIRE(pages.size() > 60);
    std::size_t macDock = 0;
    std::set<std::wstring> seen;
    for (const auto& p : pages) {
        CHECK(!p.title.empty());
        if (p.macDock) {
            ++macDock;
            CHECK(md::paneFromKey(md::toUtf8(p.target)).has_value());   // une clé que --pane connaît
        } else {
            CHECK(p.target.starts_with(L"ms-settings:") && p.target.size() > 12);
            CHECK(p.target.find_first_of(L" \t?&\"") == std::wstring::npos);
        }
        CHECK(seen.insert(md::foldForSearch(p.title) + (p.macDock ? L"|macdock" : L"|windows")).second);   // sans doublon
    }
    CHECK(macDock == md::paneList().size());   // toutes les sections de Réglages MacDock, en tête
    for (std::size_t i = 0; i < macDock && i < pages.size(); ++i) CHECK(pages[i].macDock);
}

TEST_CASE(spot_settings_search_finds_pages) {
    CHECK(firstTitle(L"wifi") == L"Wi-Fi");
    CHECK(firstTitle(L"Wi-Fi") == L"Wi-Fi");
    CHECK(firstTitle(L"WIFI") == L"Wi-Fi");
    CHECK(firstTitle(L"bluetooth") == L"Bluetooth");
    CHECK(firstTitle(L"fond d'écran") == L"Arrière-plan");
    CHECK(firstTitle(L"fond d’ecran") == L"Arrière-plan");   // apostrophe typographique, sans accent
    CHECK(firstTitle(L"mot de passe") == L"Options de connexion");
    CHECK(firstTitle(L"mode sombre") == L"Couleurs");
    CHECK(firstTitle(L"ecran") == L"Écran");   // le nom avant « Écran de verrouillage »
    CHECK(firstTitle(L"veilleuse") == L"Éclairage nocturne");
    CHECK(firstTitle(L"désinstaller") == L"Applications installées");
    CHECK(firstTitle(L"mises à jour") == L"Windows Update");
    CHECK(firstTitle(L"génie") == L"Fenêtres");   // mot-clé d'une section de Réglages MacDock
    CHECK(firstTitle(L"  dock ") == L"Dock");
    const auto& pages = md::settingsPages();
    const auto c = md::searchSettings(pages, L"clavier");
    REQUIRE(c.size() >= 2);
    CHECK(pages[c[0]].macDock && pages[c[0]].title == L"Clavier");   // à égalité, la section de MacDock d'abord
    CHECK(!pages[c[1]].macDock && pages[c[1]].title == L"Clavier");
    CHECK(md::searchSettings(pages, L"").empty());
    CHECK(md::searchSettings(pages, L"   ").empty());
    CHECK(md::searchSettings(pages, L"zzzz").empty());
}

TEST_CASE(spot_settings_word_starts_ranked) {   // « son » ne trouve pas « Personnalisation »
    const std::vector<md::SettingsPage> pages{
        {false, L"Personnalisation", L"ms-settings:personalization", L""},
        {false, L"Son", L"ms-settings:sound", L""},
        {false, L"Options de connexion", L"ms-settings:signinoptions", L"mot de passe"},
        {true, L"Sons", L"sounds", L""},
        {false, L"Alimentation", L"ms-settings:powersleep", L"son de veille"},
    };
    CHECK(md::searchSettings(pages, L"son") == std::vector<std::size_t>({1, 3, 4}));   // noms, puis mot-clé
    CHECK(md::searchSettings(pages, L"connexion") == std::vector<std::size_t>({2}));   // un mot du nom
    CHECK(md::searchSettings(pages, L"passe") == std::vector<std::size_t>({2}));       // un mot-clé
    CHECK(md::searchSettings(pages, L"asse").empty());                                 // pas au début d'un mot
    CHECK(md::exactSettingTitle(pages[1], L" SON "));
    CHECK(!md::exactSettingTitle(pages[3], L"son"));
}
