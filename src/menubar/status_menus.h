// Partie droite de la barre de menus (logique pure) : icônes d'état dans l'ordre de macOS et leurs menus (son, Wi-Fi,
// batterie, Centre de contrôle). Un élément sans matériel (Wi-Fi, batterie, Bluetooth, luminosité) est masqué.
#pragma once
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "../popup/menu_model.h"
#include "menubar_settings.h"
#include "status_audio.h"
#include "status_hub.h"

namespace md {

enum class StatusKind { Sound, Network, Battery, Search, ControlCenter, Clock };

struct StatusItem {
    StatusKind kind = StatusKind::Clock;
    Glyph glyph = Glyph::None;
    float level = 1;
    bool alt = false;      // sourdine, Wi-Fi coupé, en charge
    std::wstring text;     // horloge
};

struct StatusState {
    StatusSnapshot snap;
    bool audio = false;    // une sortie audio existe
    float volume = 0;
    bool muted = false;
    std::vector<AudioOutput> outputs;
    MenuBarSettings settings;
    std::wstring clock;
};

std::vector<StatusItem> statusItems(const StatusState& s);   // de gauche à droite, horloge en dernier
bool opensMenu(StatusKind k);                                  // false : recherche (Win+S), horloge (Win+N)

enum class StatusAction {
    None,
    Volume,        // curseur (valeur)
    Output,        // arg : identifiant de la sortie
    WifiPower,     // interrupteur ou tuile
    WifiConnect,   // arg : SSID d'un réseau connu
    Bluetooth,
    Brightness,    // curseur
    Media,         // boutons de la ligne de lecture
    OpenUri,       // arg
    Shortcut,      // arg : raccourci (« Win+K »)
    BarSettings,   // ouvre menubar.json
};

using StatusCommand = std::pair<StatusAction, std::wstring>;

struct StatusMenu {
    MenuModel model;
    std::map<int, StatusCommand> actions;               // identifiant d'entrée → action
    std::map<int, std::vector<StatusCommand>> tiles;    // rangée de tuiles → action de chaque tuile
};

StatusMenu statusMenu(StatusKind kind, const StatusState& s);
// Menu ouvert, rafraîchi : les lignes (et donc les actions de chaque identifiant) restent celles de l'ouverture —
// réseaux listés, lecture en cours, curseurs —, seules les valeurs suivent now (volume, interrupteurs, tuiles…).
MenuModel refreshStatusMenu(StatusKind kind, const StatusState& opened, const StatusState& now);
// Action de repli quand c échoue (sortie audio refusée : réglages Son), sinon rien.
std::optional<StatusCommand> statusFallback(const StatusCommand& c);

} // namespace md
