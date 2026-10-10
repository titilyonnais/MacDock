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

TEST_CASE(spot_settings_window_cycle_keywords) {   // plan 59 : « fenêtre suivante » mène à Réglages > Clavier
    const auto& pages = md::settingsPages();
    const auto r = md::searchSettings(pages, L"fenêtre suivante");
    REQUIRE(!r.empty());
    CHECK(pages[r[0]].macDock && pages[r[0]].title == L"Clavier");
}

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
    CHECK(firstTitle(L"accentuation") == L"Général");   // relecture du plan 57 : Général > Apparence
    CHECK(firstTitle(L"apparence") == L"Général");
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

TEST_CASE(spot_settings_spaces_hyphens_and_plurals) {   // mineurs du plan 55
    CHECK(firstTitle(L"wi fi") == L"Wi-Fi");
    CHECK(firstTitle(L"wifi") == L"Wi-Fi");
    CHECK(firstTitle(L"wi-fi") == L"Wi-Fi");
    CHECK(firstTitle(L"haut parleurs") == L"Son");   // mot-clé « haut-parleurs »
    CHECK(firstTitle(L"hautparleurs") == L"Son");
    CHECK(firstTitle(L"capture d'écran") == L"Captures d'écran");
    CHECK(firstTitle(L"capture d’écran") == L"Captures d'écran");   // apostrophe courbe
    // Gardes : un mot court n'est pas pris pour un pluriel ; un mot inconnu ne trouve toujours rien.
    const auto& pages = md::settingsPages();
    for (std::size_t i : md::searchSettings(pages, L"pas")) CHECK(pages[i].title != std::wstring(L"Pavé tactile"));
    // Le dernier mot tapé reste tel quel (relecture) : « tous » n'est pas « tou », « docs » n'est pas « doc(k) ».
    CHECK(firstTitle(L"tous").empty());
    CHECK(firstTitle(L"docs").empty());
    for (std::size_t i : md::searchSettings(pages, L"mods")) CHECK(pages[i].title != std::wstring(L"Mode Avion"));
    CHECK(md::searchSettings(pages, L"zorglub").empty());
    // Nom exact aux espaces et pluriels près : la section passe devant une app qui ne fait que contenir ces mots.
    for (const md::SettingsPage& p : pages) {
        if (p.title == std::wstring(L"Captures d'écran")) {
            CHECK(!md::exactSettingTitle(p, L"capture d'écran"));
            CHECK(md::exactSettingTitle(p, L"capture d'écran", true));   // second recours, aux pluriels près
        }
        if (p.title == std::wstring(L"Wi-Fi")) CHECK(md::exactSettingTitle(p, L"wi fi"));
    }
}

TEST_CASE(spot_settings_hyphens_optional) {   // « wifi » pour « Wi-Fi », « email » pour « E-mail et comptes »
    const std::vector<md::SettingsPage> pages{
        {false, L"Wi-Fi", L"ms-settings:network-wifi", L""},
        {false, L"E-mail et comptes", L"ms-settings:emailandaccounts", L""},
    };
    CHECK(md::searchSettings(pages, L"wifi") == std::vector<std::size_t>({0}));
    CHECK(md::searchSettings(pages, L"wi-fi") == std::vector<std::size_t>({0}));
    CHECK(md::searchSettings(pages, L"email") == std::vector<std::size_t>({1}));
    CHECK(md::exactSettingTitle(pages[0], L"WiFi"));
    CHECK(!md::exactSettingTitle(pages[1], L"email"));
    CHECK(md::searchSettings(pages, L"--").empty());   // que des traits d'union : rien, pas toutes les pages
    CHECK(!md::exactSettingTitle(pages[0], L"-"));
}
