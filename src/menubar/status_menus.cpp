#include "status_menus.h"

#include <algorithm>

namespace md {
namespace {

class Builder {
public:
    explicit Builder(StatusMenu& out, double width) : out_(out) { out_.model.width = width; }

    MenuItem& push(MenuItem it) {
        out_.model.items.push_back(std::move(it));
        return out_.model.items.back();
    }
    MenuItem& item(std::wstring text, StatusCommand action = {}, bool enabled = true) {
        MenuItem it;
        it.id = next_++;
        it.text = std::move(text);
        it.enabled = enabled;
        if (action.first != StatusAction::None) out_.actions[it.id] = std::move(action);
        return push(std::move(it));
    }
    void header(std::wstring text) {
        MenuItem it;
        it.row = MenuRow::Header;
        it.text = std::move(text);
        push(std::move(it));
    }
    void separator() { push({}); }
    MenuItem& slider(Glyph glyph, double value, StatusAction action, bool alt = false) {
        MenuItem it;
        it.id = next_++;
        it.row = MenuRow::Slider;
        it.glyph = glyph;
        it.value = std::clamp(value, 0.0, 1.0);
        it.level = float(it.value);
        it.on = alt;
        out_.actions[it.id] = {action, {}};
        return push(std::move(it));
    }
    void tiles(std::vector<std::pair<MenuTile, StatusCommand>> list) {
        for (std::size_t i = 0; i < list.size(); i += 2) {   // deux tuiles par rangée
            MenuItem it;
            it.id = next_++;
            it.row = MenuRow::Tiles;
            for (std::size_t k = i; k < std::min(i + 2, list.size()); ++k) {
                it.tiles.push_back(list[k].first);
                out_.tiles[it.id].push_back(list[k].second);
            }
            push(std::move(it));
        }
    }

private:
    StatusMenu& out_;
    int next_ = 1;
};

float wifiLevel(const NetworkInfo& n) { return float(wifiBars(n.ssid.empty() ? 0 : n.quality)) / 3.0f; }

void soundMenu(Builder& b, const StatusState& s) {
    b.header(L"Son");
    b.slider(Glyph::Speaker, s.muted ? 0 : s.volume, StatusAction::Volume, s.muted);
    if (!s.outputs.empty()) {
        b.separator();
        b.header(L"Sortie");
        for (const auto& o : s.outputs) b.item(o.name, {StatusAction::Output, o.id}).checked = o.isDefault;
    }
    b.separator();
    b.item(L"Réglages Son…", {StatusAction::OpenUri, L"ms-settings:sound"});
}

void wifiMenu(Builder& b, const StatusState& s) {
    const NetworkInfo& n = s.snap.network;
    MenuItem& t = b.item(L"Wi-Fi", {StatusAction::WifiPower, {}});
    t.row = MenuRow::Toggle;
    t.on = n.wifiOn;
    if (n.wifiOn) {
        bool knownHeader = false, otherHeader = false;
        for (const auto& w : n.networks) {
            if (w.known || w.connected) {
                if (!knownHeader) {
                    b.separator();
                    b.header(L"Réseaux connus");
                    knownHeader = true;
                }
                b.item(w.ssid, {StatusAction::WifiConnect, w.ssid}).checked = w.connected;
            }
        }
        for (const auto& w : n.networks) {
            if (w.known || w.connected) continue;
            if (!otherHeader) {
                b.separator();
                b.header(L"Autres réseaux");
                otherHeader = true;
            }
            b.item(w.ssid, {StatusAction::OpenUri, L"ms-availablenetworks:"});
        }
    }
    b.separator();
    b.item(L"Réglages Wi-Fi…", {StatusAction::OpenUri, L"ms-settings:network-wifi"});
}

void batteryMenu(Builder& b, const StatusState& s) {
    const BatteryInfo& bat = s.snap.battery;
    b.header(L"Batterie");
    b.item(bat.percent >= 0 ? std::to_wstring(bat.percent) + L" %" : L"Charge inconnue", {}, false);
    b.item(bat.charging ? L"En charge" : bat.onAC ? L"Source d'alimentation : secteur" : L"Source d'alimentation : batterie",
           {}, false);
    b.separator();
    b.item(L"Réglages de la batterie…", {StatusAction::OpenUri, L"ms-settings:batterysaver"});
}

void controlCenterMenu(Builder& b, const StatusState& s) {
    const NetworkInfo& n = s.snap.network;
    const RadioInfo& r = s.snap.radios;
    std::vector<std::pair<MenuTile, StatusCommand>> tiles;
    if (n.wifiInterface) {
        const std::wstring state = !n.wifiOn ? L"Désactivé" : n.ssid.empty() ? L"Non connecté" : n.ssid;
        tiles.push_back({{L"Wi-Fi", state, Glyph::Wifi, n.wifiOn}, {StatusAction::WifiPower, {}}});
    } else {
        tiles.push_back({{L"Réseau", n.ethernet ? L"Ethernet" : L"Déconnecté", Glyph::Ethernet, n.ethernet},
                         {StatusAction::OpenUri, L"ms-settings:network"}});
    }
    if (r.btPresent)
        tiles.push_back({{L"Bluetooth", r.btOn ? L"Activé" : L"Désactivé", Glyph::Bluetooth, r.btOn}, {StatusAction::Bluetooth, {}}});
    tiles.push_back({{L"Concentration", L"Ne pas déranger", Glyph::Moon, false},
                     {StatusAction::OpenUri, L"ms-settings:notifications"}});
    tiles.push_back({{L"Recopie d'écran", L"Projeter", Glyph::ScreenMirror, false}, {StatusAction::Shortcut, L"Win+K"}});
    b.tiles(std::move(tiles));
    if (s.snap.brightness) {
        b.header(L"Écran");
        b.slider(Glyph::Sun, *s.snap.brightness, StatusAction::Brightness);
    }
    if (s.audio) {
        b.header(L"Son");
        b.slider(Glyph::Speaker, s.muted ? 0 : s.volume, StatusAction::Volume, s.muted);
    }
    if (s.snap.media.present) {
        MenuItem& m = b.item(s.snap.media.title.empty() ? L"Lecture en cours" : s.snap.media.title, {StatusAction::Media, {}});
        m.row = MenuRow::Media;
        m.subtitle = s.snap.media.artist;
        m.playing = s.snap.media.playing;
    }
    b.separator();
    b.item(L"Réglages de la barre des menus…", {StatusAction::BarSettings, {}});
}

} // namespace

std::vector<StatusItem> statusItems(const StatusState& s) {
    std::vector<StatusItem> out;
    const MenuBarSettings& set = s.settings;
    if (set.showSound && s.audio) out.push_back({StatusKind::Sound, Glyph::Speaker, s.muted ? 0 : s.volume, s.muted, {}});
    const NetworkInfo& n = s.snap.network;
    if (set.showNetwork && n.wifiInterface) out.push_back({StatusKind::Network, Glyph::Wifi, wifiLevel(n), !n.wifiOn, {}});
    const BatteryInfo& bat = s.snap.battery;
    if (set.showBattery && bat.present)
        out.push_back({StatusKind::Battery, Glyph::Battery, bat.percent >= 0 ? float(bat.percent) / 100.0f : 1.0f, bat.charging, {}});
    if (set.showSearch) out.push_back({StatusKind::Search, Glyph::Search, 1, false, {}});
    out.push_back({StatusKind::ControlCenter, Glyph::ControlCenter, 1, false, {}});
    out.push_back({StatusKind::Clock, Glyph::None, 1, false, s.clock});
    return out;
}

bool opensMenu(StatusKind k) { return k != StatusKind::Search && k != StatusKind::Clock; }

StatusMenu statusMenu(StatusKind kind, const StatusState& s) {
    StatusMenu out;
    switch (kind) {
        case StatusKind::Sound: {
            Builder b(out, 280);
            soundMenu(b, s);
            break;
        }
        case StatusKind::Network: {
            Builder b(out, 280);
            wifiMenu(b, s);
            break;
        }
        case StatusKind::Battery: {
            Builder b(out, 260);
            batteryMenu(b, s);
            break;
        }
        case StatusKind::ControlCenter: {
            Builder b(out, 340);
            controlCenterMenu(b, s);
            break;
        }
        case StatusKind::Search:
        case StatusKind::Clock: break;
    }
    return out;
}

} // namespace md
