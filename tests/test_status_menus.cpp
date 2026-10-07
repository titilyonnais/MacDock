// Barre de menus : icônes d'état (partie droite) et leurs menus, dont le Centre de contrôle.
#include <windows.h>
#include <objbase.h>

#include <cmath>

#include "minitest.h"
#include "../src/calib/png_io.h"
#include "../src/core/json.h"
#include "../src/menubar/bar_renderer.h"
#include "../src/menubar/menubar_settings.h"
#include "../src/menubar/status_menus.h"
#include "../src/popup/menu_window.h"

namespace {

md::StatusState bare() {   // ce poste : ni Wi-Fi, ni Bluetooth, ni batterie, ni luminosité réglable
    md::StatusState s;
    s.snap.network.ethernet = true;
    s.clock = L"mer. 7 oct. 14:32";
    return s;
}

md::StatusState full() {
    md::StatusState s;
    s.audio = true;
    s.volume = 0.5f;
    s.outputs = {{L"{a}", L"Haut-parleurs", false}, {L"{b}", L"Focusrite USB Audio", true}};
    s.snap.network.wifiInterface = s.snap.network.wifiOn = true;
    s.snap.network.ssid = L"Maison";
    s.snap.network.quality = 80;
    s.snap.network.networks = {{L"Maison", 80, true, true, true}, {L"Bureau", 60, true, true, false},
                               {L"Café", 90, false, false, false}};
    s.snap.radios = {true, true, true, false};
    s.snap.media = {true, true, L"Titre", L"Artiste"};
    s.snap.brightness = 0.7;
    s.snap.battery = {true, true, true, 87};
    s.clock = L"mer. 7 oct. 14:32";
    return s;
}

std::vector<md::StatusKind> kinds(const std::vector<md::StatusItem>& items) {
    std::vector<md::StatusKind> out;
    for (const auto& i : items) out.push_back(i.kind);
    return out;
}

const md::MenuItem* findText(const md::MenuModel& m, const std::wstring& text) {
    for (const auto& it : m.items)
        if (it.text == text) return &it;
    return nullptr;
}

const md::MenuItem* findRow(const md::MenuModel& m, md::MenuRow row, md::Glyph glyph = md::Glyph::None) {
    for (const auto& it : m.items)
        if (it.row == row && (glyph == md::Glyph::None || it.glyph == glyph)) return &it;
    return nullptr;
}

} // namespace

TEST_CASE(status_menus_hide_missing_hardware) {
    auto s = bare();
    using K = md::StatusKind;
    CHECK(kinds(md::statusItems(s)) == (std::vector<K>{K::Search, K::ControlCenter, K::Clock}));
    auto cc = md::statusMenu(K::ControlCenter, s);
    const md::MenuItem* tiles = findRow(cc.model, md::MenuRow::Tiles);
    REQUIRE(tiles != nullptr);
    CHECK(tiles->tiles[0].title == L"Réseau");      // Ethernet, pas de Wi-Fi
    CHECK(tiles->tiles[0].subtitle == L"Ethernet");
    CHECK(tiles->tiles[0].on);
    for (const auto& it : cc.model.items)
        for (const auto& t : it.tiles) CHECK(t.title != L"Bluetooth");
    CHECK(findRow(cc.model, md::MenuRow::Slider) == nullptr);   // ni son ni luminosité
    CHECK(findRow(cc.model, md::MenuRow::Media) == nullptr);
    CHECK(!cc.model.items.back().text.empty());   // les réglages restent
}

TEST_CASE(status_menus_order_and_hit) {
    auto s = full();
    using K = md::StatusKind;
    auto items = md::statusItems(s);
    CHECK(kinds(items) == (std::vector<K>{K::Sound, K::Network, K::Battery, K::Search, K::ControlCenter, K::Clock}));
    CHECK(items[0].glyph == md::Glyph::Speaker);
    CHECK_NEAR(items[0].level, 0.5, 1e-6);
    CHECK(items[1].glyph == md::Glyph::Wifi);
    CHECK_NEAR(items[1].level, 1.0, 1e-6);   // 80 % : trois arcs
    CHECK(items[2].alt);                     // en charge
    CHECK(items[5].text == s.clock);
    s.muted = true;
    s.snap.network.wifiOn = false;
    items = md::statusItems(s);
    CHECK(items[0].alt);
    CHECK(items[1].alt);
    s.settings.showSound = false;
    s.settings.showBattery = false;
    s.settings.showSearch = false;
    s.settings.showNetwork = false;
    CHECK(kinds(md::statusItems(s)) == (std::vector<K>{K::ControlCenter, K::Clock}));
    CHECK(md::opensMenu(K::Sound));
    CHECK(!md::opensMenu(K::Search));   // Win+S
    CHECK(!md::opensMenu(K::Clock));    // centre de notifications
}

TEST_CASE(status_menus_sound_lists_outputs) {
    auto m = md::statusMenu(md::StatusKind::Sound, full());
    const md::MenuItem* slider = findRow(m.model, md::MenuRow::Slider, md::Glyph::Speaker);
    REQUIRE(slider != nullptr);
    CHECK_NEAR(slider->value, 0.5, 1e-6);
    CHECK(m.actions.at(slider->id).first == md::StatusAction::Volume);
    const md::MenuItem* focus = findText(m.model, L"Focusrite USB Audio");
    REQUIRE(focus != nullptr);
    CHECK(focus->checked);
    CHECK(!findText(m.model, L"Haut-parleurs")->checked);
    CHECK(m.actions.at(findText(m.model, L"Haut-parleurs")->id) == std::make_pair(md::StatusAction::Output, std::wstring(L"{a}")));
    CHECK(m.model.items.back().text == L"Réglages Son…");
    CHECK(m.actions.at(m.model.items.back().id).first == md::StatusAction::OpenUri);
    CHECK(m.model.width > 0);
}

TEST_CASE(status_menus_wifi_networks) {
    auto m = md::statusMenu(md::StatusKind::Network, full());
    const md::MenuItem* toggle = findRow(m.model, md::MenuRow::Toggle);
    REQUIRE(toggle != nullptr);
    CHECK(toggle->on);
    CHECK(m.actions.at(toggle->id).first == md::StatusAction::WifiPower);
    REQUIRE(findText(m.model, L"Maison") != nullptr);
    CHECK(findText(m.model, L"Maison")->checked);
    CHECK(m.actions.at(findText(m.model, L"Bureau")->id) == std::make_pair(md::StatusAction::WifiConnect, std::wstring(L"Bureau")));
    CHECK(m.actions.at(findText(m.model, L"Café")->id).first == md::StatusAction::OpenUri);   // inconnu : réglages
    auto off = full();
    off.snap.network.wifiOn = false;
    m = md::statusMenu(md::StatusKind::Network, off);
    CHECK(findText(m.model, L"Bureau") == nullptr);   // radio coupée : pas de liste
}

TEST_CASE(status_menus_control_center_rows) {
    auto m = md::statusMenu(md::StatusKind::ControlCenter, full());
    std::vector<const md::MenuItem*> tileRows;
    for (const auto& it : m.model.items)
        if (it.row == md::MenuRow::Tiles) tileRows.push_back(&it);
    REQUIRE(tileRows.size() == 2);
    CHECK(tileRows[0]->tiles[0].title == L"Wi-Fi");
    CHECK(tileRows[0]->tiles[0].subtitle == L"Maison");
    CHECK(tileRows[0]->tiles[1].title == L"Bluetooth");
    CHECK(!tileRows[0]->tiles[1].on);
    CHECK(m.tiles.at(tileRows[0]->id)[1].first == md::StatusAction::Bluetooth);
    CHECK(tileRows[1]->tiles[0].glyph == md::Glyph::Moon);
    CHECK(m.tiles.at(tileRows[1]->id)[1].first == md::StatusAction::Shortcut);   // recopie : Win+K
    const md::MenuItem* screen = findRow(m.model, md::MenuRow::Slider, md::Glyph::Sun);
    REQUIRE(screen != nullptr);
    CHECK_NEAR(screen->value, 0.7, 1e-6);
    CHECK(m.actions.at(screen->id).first == md::StatusAction::Brightness);
    REQUIRE(findRow(m.model, md::MenuRow::Slider, md::Glyph::Speaker) != nullptr);
    const md::MenuItem* media = findRow(m.model, md::MenuRow::Media);
    REQUIRE(media != nullptr);
    CHECK(media->text == L"Titre");
    CHECK(media->playing);
    CHECK(m.actions.at(media->id).first == md::StatusAction::Media);
    CHECK(m.actions.at(m.model.items.back().id).first == md::StatusAction::BarSettings);
}

TEST_CASE(menubar_settings_status_roundtrip) {
    md::MenuBarSettings s;
    CHECK(s.showNetwork);
    CHECK(s.showBattery);
    CHECK(s.showSearch);
    s.showNetwork = false;
    s.showSearch = false;
    s.metrics.statusIconSize = 18;
    auto back = md::menuBarSettingsFromJson(*md::json::parse(md::json::serialize(md::menuBarSettingsToJson(s))));
    CHECK(!back.showNetwork);
    CHECK(back.showBattery);
    CHECK(!back.showSearch);
    CHECK_NEAR(back.metrics.statusIconSize, 18, 1e-9);
}

TEST_CASE(bar_renderer_draws_status_glyph) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    {
        md::BarRenderer r;
        REQUIRE(r.initOffscreen());
        r.setFont(L"", 13);
        md::BarFrame f;
        f.darkText = true;
        md::BarDrawItem icon;
        icon.glyph = md::Glyph::ControlCenter;
        icon.x = 100;
        icon.width = 30;
        f.items.push_back(icon);
        const UINT w = 200, h = 24;
        std::vector<std::uint8_t> bg(size_t(w) * h * 4, 0), out;
        REQUIRE(r.renderToImage(f, bg, w, h, out));
        int inside = 0, outside = 0;
        for (UINT y = 0; y < h; ++y)
            for (UINT x = 0; x < w; ++x) {
                const bool lit = out[(size_t(y) * w + x) * 4 + 3] > 60;
                (x >= 100 && x < 130 ? inside : outside) += lit;
            }
        CHECK(inside > 20);
        CHECK_EQ(outside, 0);
    }
    CoUninitialize();
}

TEST_CASE(status_menus_render_offscreen) {   // chaque menu d'état se dessine (MACDOCK_DUMP=dossier : images)
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    {
        md::MenuWindow::Env env;
        env.scale = 2;
        env.dark = true;
        wchar_t dump[MAX_PATH] = {};
        const bool save = GetEnvironmentVariableW(L"MACDOCK_DUMP", dump, MAX_PATH) != 0;
        using K = md::StatusKind;
        const std::pair<K, const wchar_t*> kinds[] = {
            {K::Sound, L"son"}, {K::Network, L"wifi"}, {K::Battery, L"batterie"}, {K::ControlCenter, L"centre"}};
        for (const auto& [kind, name] : kinds) {
            auto m = md::statusMenu(kind, full());
            std::vector<std::uint8_t> px;
            UINT w = 0, h = 0;
            REQUIRE(md::MenuWindow::snapshot(env, m.model, px, w, h));
            CHECK_EQ(w, UINT(std::lround(m.model.width * 2)));
            CHECK(px[(size_t(h / 2) * w + w / 2) * 4 + 3] > 200);
            if (save) md::writePng(std::wstring(dump) + L"\\status-" + name + L".png", px.data(), w, h);
        }
        auto cc = md::statusMenu(K::ControlCenter, bare());
        std::vector<std::uint8_t> px;
        UINT w = 0, h = 0;
        CHECK(md::MenuWindow::snapshot(env, cc.model, px, w, h));
        if (save) md::writePng(std::wstring(dump) + L"\\status-centre-ce-poste.png", px.data(), w, h);
    }
    CoUninitialize();
}
