# MacMenuBar — Plan 8 : icônes d'état et Centre de contrôle

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** la partie droite de la barre gagne :
- le son, le réseau, la batterie, la recherche et le Centre de contrôle ;
- pour chacun, un menu de verre avec des lignes enrichies (curseurs, interrupteurs, tuiles, lecture en cours) qui agissent en direct.

**Architecture :**
- **Lignes enrichies.** `MenuItem` gagne un genre de ligne (intitulé, curseur, interrupteur, tuiles, média). `MenuWindow::track` reçoit des rappels `Live` : le menu reste ouvert pendant ces interactions et se rafraîchit toutes les 500 ms.
- **Pictogrammes.** Dessinés en Direct2D (`glyphs`), partagés par la barre et les menus.
- **Sources d'état** (`status_*`) :
  - Core Audio sur le fil de la barre ;
  - WlanAPI, C++/WinRT (radios, lecture en cours) et luminosité (WMI, DDC/CI) sur un fil de travail, `StatusHub`, qui poste des instantanés.
- **Menus d'état.** `status_menus` (pur) construit les icônes et les menus depuis un instantané.

**Tech Stack :** C++ `/std:c++latest`, Direct2D, Core Audio, WlanAPI, C++/WinRT (`windowsapp.lib`), WMI, `dxva2`.

**Spec :** `docs/superpowers/specs/2026-10-07-macmenubar-design.md`, sections 4.5 et 4.9.

## Global Constraints
- Aucune ressource Apple : pictogrammes dessinés par le code. Messages et commentaires en français, identifiants en anglais.
- Les tests ne changent aucun réglage de l'utilisateur. Volume, sortie audio, Wi-Fi, Bluetooth et luminosité sont seulement lus.
- Aucun essai qui pilote la souris ou le clavier.
- La barre n'attend jamais une source lente : WinRT, WlanAPI et DDC/CI passent par le fil de travail, et les appels WinRT bloquants (`.get()`) n'ont jamais lieu sur le fil STA de la barre.
- Une source indisponible masque son élément ; rien ne plante. Ce poste n'a ni Wi-Fi, ni Bluetooth, ni batterie.

## Review Focus
1. **Matériel absent** (pas de Wi-Fi, de batterie, de Bluetooth, de luminosité réglable) : éléments et tuiles masqués, pas de menu vide. Tests : `status_menus_hide_missing_hardware`, `status_power_from_system`.
2. **Curseur glissé vite ou au-delà des bords** : valeur bornée à [0, 1], volume appliqué en direct sans saccade. Tests : `menu_rows_slider_value_clamped` et revue de `MenuWindow` (glisser).
3. **Valeur changée ailleurs pendant que le menu est ouvert** (touche volume, morceau suivant) : le menu se met à jour sans se refermer. Test : `menu_rows_refresh_updates_model` (rappel `refresh`), plus revue.
4. **Sortie audio débranchée, session média disparue entre deux instantanés** : pas d'accès à un objet libéré, action sans effet. Test : `status_audio_reads_default_output` (lecture réelle), plus revue des identifiants.
5. **Survol entre les icônes d'état et les titres de gauche** pendant qu'un menu est ouvert : bascule comme sur macOS. Test : `status_menus_order_and_hit` (ordre et zones).

---

### Task 1 : Lignes enrichies des menus et pictogrammes

**Files :**
- Modify `src/popup/menu_model.h|.cpp` : `MenuRow`, `MenuTile`, champs de `MenuItem`, `MenuModel::width`, hauteurs, aides pures.
- Create `src/popup/glyphs.h|.cpp` : `Glyph`, `drawGlyph`.
- Modify `src/popup/menu_window.h|.cpp` : `MenuWindow::Live`, dessin des lignes, glisser, rafraîchissement.
- Modify `build.ps1` : `glyphs.cpp`.
- Test : `tests/test_menu_rows.cpp`

**Interfaces :**
```cpp
enum class Glyph { None, Speaker, Sun, Wifi, Ethernet, Bluetooth, Moon, ScreenMirror, Battery, Search, ControlCenter,
                   Play, Pause, Previous, Next };
enum class MenuRow { Normal, Header, Slider, Toggle, Tiles, Media };
struct MenuTile { std::wstring title, subtitle; Glyph glyph = Glyph::None; bool on = false, enabled = true; };
// MenuItem : MenuRow row; double value (curseur, 0..1) ; bool on (interrupteur) ; Glyph glyph ; double level (pictogramme) ;
//            std::vector<MenuTile> tiles ; std::wstring subtitle (média) ; bool playing.
// MenuModel : double width = 0 (0 = selon le texte).
double menuRowHeight(MenuRow r);
double sliderValueAt(double rowWidth, double x);              // x depuis le bord gauche de la ligne, borné [0, 1]
int tileAt(std::size_t tiles, double rowWidth, double x);     // -1 hors tuile
int mediaButtonAt(double rowWidth, double x);                 // 0 précédent, 1 lecture/pause, 2 suivant, -1
void drawGlyph(ID2D1DeviceContext* dc, Glyph g, D2D1_RECT_F box, ID2D1Brush* ink, float level = 1);
struct MenuWindow::Live {
    std::function<void(int id, double value)> slider;
    std::function<void(int id, bool on)> toggle;
    std::function<void(int id, int tile)> tile;
    std::function<void(int id, int button)> media;
    std::function<bool(MenuModel&)> refresh;   // toutes les 500 ms ; true si le modèle a changé
};
```

**Tests :**
- `menu_rows_layout_heights_and_width`
- `menu_rows_slider_value_clamped`
- `menu_rows_tile_and_media_hit`
- `menu_rows_header_not_selectable`
- `menu_rows_refresh_updates_model` (fonction pure `applyRefresh` utilisée par `MenuWindow`)
- `glyphs_draw_every_glyph` (rendu hors écran WIC, pixels non vides)

**Étapes :** rouge, implémentation, vert, puis commit `feat(menus): lignes enrichies et pictogrammes`.

### Task 2 : Sources d'état

**Files :**
- Create :
  - `src/menubar/status_audio.h|.cpp` (Core Audio, `IPolicyConfig`)
  - `src/menubar/status_power.h|.cpp`
  - `src/menubar/status_network.h|.cpp` (WlanAPI, `GetAdaptersAddresses`)
  - `src/menubar/status_winrt.h|.cpp` (radios, lecture en cours)
  - `src/menubar/status_brightness.h|.cpp` (WMI, DDC/CI)
  - `src/menubar/status_hub.h|.cpp` (fil de travail, instantanés)
- Modify `build.ps1` : sources et bibliothèques (`wlanapi`, `iphlpapi`, `windowsapp`, `wbemuuid`, `dxva2`).
- Test : `tests/test_status.cpp`

**Interfaces :**
```cpp
struct AudioOutput { std::wstring id, name; bool isDefault = false; };
class AudioStatus { public: bool init(); bool ready() const; float volume(); bool muted(); void setVolume(float);
                    void setMuted(bool); std::vector<AudioOutput> outputs(); bool setDefault(const std::wstring& id); };
struct BatteryInfo { bool present = false, charging = false, onAC = true; int percent = -1; };
BatteryInfo batteryFrom(const SYSTEM_POWER_STATUS& s);
struct WifiNetwork { std::wstring ssid; int quality = 0; bool secured = false, known = false, connected = false; };
struct NetworkInfo { bool wifiInterface = false, wifiOn = false, ethernet = false; std::wstring ssid; int quality = 0;
                     std::vector<WifiNetwork> networks; };
int wifiBars(int quality);                                   // 0..3
std::vector<WifiNetwork> sortNetworks(std::vector<WifiNetwork> list);   // connecté, connus, signal ; SSID unique
NetworkInfo readNetwork(); bool connectWifi(const std::wstring& ssid);
struct RadioInfo { bool wifiPresent = false, wifiOn = false, btPresent = false, btOn = false; };
struct MediaInfo { bool present = false, playing = false; std::wstring title, artist; };
RadioInfo readRadios(); bool setRadio(bool bluetooth, bool on); MediaInfo readMedia(); bool mediaCommand(int button);
std::optional<double> readBrightness(); bool setBrightness(double v);
struct StatusSnapshot { NetworkInfo network; RadioInfo radios; MediaInfo media; std::optional<double> brightness; BatteryInfo battery; };
class StatusHub { public: bool start(HWND notify, UINT msg); void stop(); void post(std::function<void()> job);
                  // msg : lParam = StatusSnapshot* (à libérer) ; relevé toutes les 2 s et après chaque action
};
```

**Tests :**
- `status_power_from_system`
- `status_wifi_bars_and_sort`
- `status_audio_reads_default_output` : lecture réelle, aucun changement.
- `status_network_reads_without_crash` : lecture réelle ; ce poste n'a pas de Wi-Fi.
- `status_winrt_reads_on_worker` : lecture réelle des radios et du média, sur un fil MTA.
- `status_hub_posts_snapshots`

**Étapes :** rouge, implémentation, vert, puis commit `feat(menubar): sources d'état`.

### Task 3 : Icônes d'état et Centre de contrôle dans la barre

**Files :**
- Create `src/menubar/status_menus.h|.cpp` (pur) : éléments de droite, menus du son, du Wi-Fi, de la batterie et du Centre de contrôle, actions.
- Modify `src/menubar/menubar_settings.*` : `showNetwork`, `showBattery`, `showSearch`.
- Modify `src/menubar/bar_renderer.*` : `BarDrawItem::glyph` et `level`.
- Modify `src/menubar/menubar_window.*` :
  - partie droite ;
  - ouverture des menus d'état avec `Live` ;
  - `BarLink` sur tous les éléments ;
  - `StatusHub`, Core Audio ;
  - `--snapshot` montre les icônes.
- Test : `tests/test_status.cpp`

**Interfaces :**
```cpp
enum class StatusKind { Sound, Network, Battery, Search, ControlCenter, Clock };
struct StatusItem { StatusKind kind; Glyph glyph; float level = 1; std::wstring text; };   // text : horloge
struct StatusState { StatusSnapshot snap; bool audio = false; float volume = 0; bool muted = false;
                     std::vector<AudioOutput> outputs; MenuBarSettings settings; std::wstring clock; };
std::vector<StatusItem> statusItems(const StatusState& s);   // de gauche à droite, horloge en dernier
enum class StatusAction { None, Volume, Output, WifiPower, WifiConnect, Bluetooth, Brightness, Media, OpenUri, Shortcut };
struct StatusMenu { MenuModel model; std::map<int, std::pair<StatusAction, std::wstring>> actions; };
StatusMenu statusMenu(StatusKind kind, const StatusState& s);
```

**Tests :**
- `status_menus_hide_missing_hardware`
- `status_menus_order_and_hit`
- `status_menus_sound_lists_outputs`
- `status_menus_control_center_rows`
- `menubar_settings_status_roundtrip`

**Étapes :** rouge, implémentation, vert, puis commit `feat(menubar): icônes d'état et Centre de contrôle`.

---

## Fin du plan
- Relecture finale par un agent sur le modèle le plus capable, puis une passe de corrections en TDD.
- README et journal.
- Fusion locale dans `main`.
