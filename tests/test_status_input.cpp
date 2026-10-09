// Barre de menus : menu de saisie du clavier (plan 58), comme le menu de saisie de macOS.
#include <windows.h>

#include <string>

#include "minitest.h"
#include "../src/core/json.h"
#include "../src/menubar/bar_renderer.h"
#include "../src/menubar/menubar_settings.h"
#include "../src/menubar/status_input.h"
#include "../src/menubar/status_menus.h"

namespace {

std::wstring fakeCountry(unsigned short lang) {
    switch (lang) {
        case 0x040C: return L"FR";
        case 0x0409: return L"US";
        case 0x0809: return L"GB";
        default: return L"";
    }
}

md::StatusState twoLayouts(std::uint32_t current) {   // ce PC : français et anglais (États-Unis)
    md::StatusState s;
    s.clock = L"mer. 7 oct. 14:32";
    s.inputs = {{0x040C040C, L"Français (France)", L"FR"}, {0x04090409, L"Anglais (États-Unis)", L"US"}};
    s.input = current;
    return s;
}

const md::MenuItem* itemNamed(const md::MenuModel& m, const wchar_t* text) {
    for (const auto& it : m.items)
        if (it.text == text) return &it;
    return nullptr;
}

} // namespace

TEST_CASE(input_layout_code_from_hkl) {
    CHECK(md::layoutCode(0x040C040C, fakeCountry) == L"FR");
    CHECK(md::layoutCode(0x04090409, fakeCountry) == L"US");
    CHECK(md::layoutCode(0x040C0409, fakeCountry) == L"FR");   // langue anglaise, disposition française : on tape en AZERTY
    CHECK(md::layoutCode(0xF0020409, fakeCountry) == L"US");   // variante (Dvorak) : le pays de la langue
    CHECK(md::layoutCode(0x08090809, fakeCountry) == L"GB");
    CHECK(md::layoutCode(0, fakeCountry).empty());
    CHECK(md::layoutCode(0x04130413, fakeCountry).empty());   // pays inconnu
}

TEST_CASE(input_country_and_name_from_windows) {   // lecture seule des noms de Windows
    CHECK(md::layoutCountry(0x040C) == L"FR");
    CHECK(md::layoutCountry(0x0409) == L"US");
    CHECK(md::layoutCountry(0x0807) == L"CH");   // allemand (Suisse)
    CHECK(!md::layoutName(0x040C040C).empty());
    CHECK(md::layoutName(0x040C040C) != md::layoutName(0x04090409));
}

TEST_CASE(input_hkl_text_roundtrip) {   // l'action du menu porte la disposition en texte
    CHECK(md::hklText(0x040C040C) == L"040C040C");
    CHECK(md::hklFromText(L"040C040C") == 0x040C040Cu);
    CHECK(md::hklFromText(L"F0020409") == 0xF0020409u);
    CHECK(md::hklFromText(L"zz") == 0u);
    CHECK(md::hklFromText(L"") == 0u);
}

TEST_CASE(input_status_item_only_with_choices) {
    using K = md::StatusKind;
    auto s = twoLayouts(0x04090409);
    auto items = md::statusItems(s);
    REQUIRE(!items.empty());
    CHECK(items[0].kind == K::Input);   // à gauche des autres icônes, comme le menu de saisie de macOS
    CHECK(items[0].text == L"US");
    CHECK(md::opensMenu(K::Input));
    s.input = 0x040C040C;
    CHECK(md::statusItems(s)[0].text == L"FR");
    s.input = 0x08090809;   // absente de la liste (ajoutée depuis) : son code quand même
    CHECK(md::statusItems(s)[0].text == L"GB");
    s.inputs.pop_back();   // une seule disposition : rien à choisir, pas d'icône
    for (const auto& it : md::statusItems(s)) CHECK(it.kind != K::Input);
    s = twoLayouts(0x040C040C);
    s.settings.showInput = false;
    for (const auto& it : md::statusItems(s)) CHECK(it.kind != K::Input);
}

TEST_CASE(input_menu_lists_layouts_and_tools) {
    auto m = md::statusMenu(md::StatusKind::Input, twoLayouts(0x040C040C));
    REQUIRE(m.model.items.size() >= 6);
    CHECK(m.model.items[0].text == L"Français (France)" && m.model.items[0].checked);   // l'actuelle, cochée
    CHECK(m.model.items[1].text == L"Anglais (États-Unis)" && !m.model.items[1].checked);
    const auto& toUs = m.actions.at(m.model.items[1].id);
    CHECK(toUs.first == md::StatusAction::InputLayout && toUs.second == L"04090409");
    const md::MenuItem* emoji = itemNamed(m.model, L"Afficher les émojis et symboles");
    REQUIRE(emoji != nullptr);
    CHECK(m.actions.at(emoji->id) == md::StatusCommand(md::StatusAction::Shortcut, L"Win+."));
    const md::MenuItem* osk = itemNamed(m.model, L"Afficher le clavier visuel");
    REQUIRE(osk != nullptr);
    CHECK(m.actions.at(osk->id) == md::StatusCommand(md::StatusAction::OpenUri, L"osk.exe"));
    const md::MenuItem& last = m.model.items.back();
    CHECK(last.text == L"Ouvrir les réglages Clavier…");
    CHECK(m.actions.at(last.id) == md::StatusCommand(md::StatusAction::OpenUri, L"ms-settings:regionlanguage"));
}

TEST_CASE(menubar_settings_show_input_roundtrip) {
    md::MenuBarSettings s;
    CHECK(s.showInput);
    s.showInput = false;
    const auto back = md::menuBarSettingsFromJson(*md::json::parse(md::json::serialize(md::menuBarSettingsToJson(s))));
    CHECK(!back.showInput);
    CHECK(md::menuBarSettingsFromJson(*md::json::parse("{}")).showInput);
}

TEST_CASE(input_installed_layouts_real) {   // les dispositions de cette session (lecture seule)
    const auto layouts = md::installedLayouts();
    REQUIRE(!layouts.empty());
    for (const auto& l : layouts) {
        CHECK(l.hkl != 0);
        CHECK(!l.name.empty());
        CHECK(l.code.size() == 2);   // « FR », « US »
    }
}

TEST_CASE(bar_renderer_draws_boxed_code) {   // plan 58 : « FR » dans une boîte arrondie, comme les sources de macOS
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    {
        md::BarRenderer r;
        REQUIRE(r.initOffscreen());
        r.setFont(L"", 13);
        auto longestRun = [&](bool boxed) {   // plus longue suite horizontale de pixels allumés dans la case
            md::BarFrame f;
            f.darkText = true;
            md::BarDrawItem it;
            it.text = L"FR";
            it.boxed = boxed;
            it.x = 100;
            it.width = 30;
            f.items.push_back(it);
            const UINT w = 200, h = 24;
            std::vector<std::uint8_t> bg(size_t(w) * h * 4, 0), out;
            if (!r.renderToImage(f, bg, w, h, out)) return -1;
            int best = 0;
            for (UINT y = 0; y < h; ++y) {
                int run = 0;
                for (UINT x = 0; x < w; ++x) {
                    const bool lit = out[(size_t(y) * w + x) * 4 + 3] > 90;
                    run = lit ? run + 1 : 0;
                    best = std::max(best, run);
                    if (lit) CHECK(x >= 100 && x < 130);   // rien hors de la case
                }
            }
            return best;
        };
        CHECK(longestRun(true) >= 15);   // le bord haut ou bas de la boîte
        CHECK(longestRun(false) < 12);   // le texte seul n'a pas de long trait horizontal
    }
    CoUninitialize();
}
