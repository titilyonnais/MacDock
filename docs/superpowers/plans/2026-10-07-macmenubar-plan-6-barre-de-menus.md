# MacMenuBar — Plan 6 : la barre de menus

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** `MacMenuBar.exe`, une barre de menus transparente en haut de l'écran, façon macOS Tahoe.
- À gauche : logo et menu du système, nom de l'app active en gras et menu de l'app, puis menus génériques (ou ceux de l'Explorateur).
- À droite : l'horloge.
- Les menus s'ouvrent en verre et l'on passe d'un titre à l'autre au survol. Les actions passent par des raccourcis ou des commandes de fenêtre.
- La barre s'efface en plein écran ; elle a un masquage automatique.
- Le lanceur surveille le Dock et la barre.

**Architecture :**
- Un processus séparé. Il réutilise les modules du Dock : suivi des fenêtres, identité des apps, `AppModel`, menus en verre, capture, journal, configuration.
- Toute la logique de la barre est pure et testée : mise en page, couleur, horloge, raccourcis, classement du premier plan, menus et actions, réglages.
- La fenêtre et le rendu (DirectComposition + Direct2D) sont minces.
- `MenuWindow` gagne :
  - le texte des raccourcis ;
  - l'ouverture sous l'ancrage (`Side::Below`) ;
  - le lien avec les titres voisins (`BarLink`).

**Tech Stack :** C++ `/std:c++latest`, MSVC, Win32, Direct2D, DirectWrite, DirectComposition, Desktop Duplication.

**Spec :** `docs/superpowers/specs/2026-10-07-macmenubar-design.md` (sections 2 à 7 ; plan 6 de la section 8).

## Global Constraints
- Aucune ressource Apple. Le logo par défaut est celui de Windows (quatre carrés dessinés), remplaçable par `menubar-logo.png`.
- Messages, journaux et commentaires en français ; identifiants en anglais.
- Toute logique pure a ses tests dans `tests/`, et `build.ps1 -Target tests -Run` reste vert.
- Une seule duplication d'écran par processus : l'échantillonnage du fond ne tourne jamais pendant un menu.
- Les actions système (veille, extinction…) passent par une interface injectable. **Aucun test ne déclenche d'action système réelle.**
- Aucun essai réel qui pilote la souris : l'utilisateur est présent.
- Les fichiers de réglages de l'utilisateur sont sauvegardés et restaurés autour de chaque essai réel.

## Review Focus
1. **Le clavier doit rester à l'app.** Après un choix dans un menu, la commande va à la fenêtre qui avait le premier plan avant l'ouverture, pas à la barre ni au menu. Test : `bar_target_window_kept_from_open`. Revoir aussi l'appel à `forceForeground` avant `SendInput`.
2. **Le Dock ou le menu Démarrer au premier plan** ne remplace pas l'app affichée. Test : `foreground_ignores_shell_and_dock`.
3. **Nom d'app ou menus trop longs** pour l'écran : les menus de droite de la partie gauche disparaissent avant de toucher l'horloge ; le logo et le nom de l'app restent. Test : `bar_layout_hides_menus_before_status`.
4. **Le bureau a le premier plan** : « Fermer la fenêtre » est désactivé (Alt+F4 y ouvrirait la boîte d'arrêt). Test : `menus_desktop_close_disabled`.
5. **`menubar.json` invalide ou aux valeurs absurdes** : valeurs par défaut et bornes. Test : `menubar_settings_clamped`.

---

### Task 1 : Logique pure de la barre

**Files :**
- Create `src/menubar/bar_layout.h|.cpp`
- Create `src/menubar/bar_color.h|.cpp`
- Create `src/menubar/clock_format.h|.cpp`
- Create `src/menubar/shortcut.h|.cpp`
- Create `src/menubar/foreground_rules.h|.cpp`
- Create `src/menubar/menubar_settings.h|.cpp`
- Modify `build.ps1` : ces fichiers dans `$LogicSources`.
- Test : `tests/test_menubar.cpp`

**Interfaces :**
```cpp
// bar_layout.h — points depuis le bord gauche de la barre
struct BarLayoutInput {
    double barWidth = 0, leftMargin = 10, rightMargin = 10, minGap = 20;
    std::vector<double> leftWidths;    // logo, nom de l'app, menus (largeur de case, marges comprises)
    std::vector<double> rightWidths;   // de gauche à droite (l'horloge en dernier)
    std::size_t keepLeft = 2;          // toujours visibles (logo, nom de l'app)
};
struct BarLayout {
    std::vector<double> leftX, rightX;   // bord gauche de chaque case
    std::size_t leftVisible = 0;         // les leftVisible premières cases gauches sont affichées
};
struct BarHit { enum class Kind { None, Left, Right } kind = Kind::None; std::size_t index = 0; };
BarLayout layoutBar(const BarLayoutInput& in);
BarHit hitTestBar(const BarLayout& l, const BarLayoutInput& in, double x);

// bar_color.h
double srgbToLinear(double c);                                                   // 0..1
double stripLuminance(const std::uint8_t* bgra, int w, int h, int strideBytes);  // luminance relative moyenne
float halfToFloat(std::uint16_t h);
double stripLuminanceHalf(const std::uint16_t* rgba, int w, int h, int strideElems, double sdrWhite);   // scRGB
bool chooseDarkText(double luminance, bool currentlyDark);   // foncé au-dessus de 0,45, clair sous 0,35 (luminance linéaire)

// clock_format.h
struct ClockOptions { bool weekday = true, date = true, seconds = false, hour24 = true; };
std::wstring formatClock(const SYSTEMTIME& t, const ClockOptions& o);   // « mar. 7 oct. 14:32 »

// shortcut.h
struct Shortcut { std::vector<WORD> modifiers; WORD key = 0; };
std::optional<Shortcut> parseShortcut(std::wstring_view text);   // « Ctrl+Maj+S », « Alt+← », « Win+. », « F11 »
std::vector<INPUT> shortcutInputs(const Shortcut& s);            // appuis, puis relâchements en ordre inverse

// foreground_rules.h
enum class ForegroundKind { App, Explorer, Ignore };
ForegroundKind classifyForeground(std::wstring_view className, std::wstring_view exeName, bool ownProcess);

// menubar_settings.h
struct MenuBarMetrics { double height = 24, fontSize = 13, leftMargin = 10, titlePadding = 10, logoSize = 14,
                        highlightHeight = 22, highlightRadius = 6, statusWidth = 30, rightMargin = 10; };
struct MenuBarSettings { bool autohide = false; std::wstring font; ClockOptions clock; bool showSound = true;
                         MenuBarMetrics metrics; };
MenuBarSettings menuBarSettingsFromJson(const json::Value& v);   // valeurs bornées, défauts si absentes
json::Value menuBarSettingsToJson(const MenuBarSettings& s);     // "version": 1
```

**Tests :**
- `bar_layout_places_left_and_right`, `bar_layout_hides_menus_before_status`, `bar_hit_test_left_right_none`
- `bar_color_white_black_luminance`, `bar_color_hysteresis`, `bar_color_half_matches_srgb`
- `clock_formats_french_weekday_month`, `clock_options_seconds_12h`
- `shortcut_parses_modifiers_and_keys`, `shortcut_inputs_release_in_reverse`, `shortcut_rejects_unknown`
- `foreground_ignores_shell_and_dock`, `foreground_desktop_is_explorer`, `foreground_app`
- `menubar_settings_roundtrip`, `menubar_settings_clamped`

**Étapes :**
- [ ] Écrire les tests, constater l'échec de compilation (rouge).
- [ ] Implémenter, `build.ps1 -Target tests -Run` vert.
- [ ] Commit `feat(menubar): logique pure de la barre de menus`.

### Task 2 : Menus de la barre

**Files :**
- Create `src/menubar/app_menus.h|.cpp` (pur).
- Modify `src/popup/menu_model.h|.cpp` :
  - `MenuItem::shortcut` ;
  - `layoutMenu(m, textWidthMax, shortcutWidthMax = 0)` ;
  - `kMenuShortcutGap = 24` ;
  - `barTitleAt` et la convention de résultat « passer au titre k ».
- Modify `src/popup/menu_window.h|.cpp` :
  - `Side::Below` ;
  - `BarLink` ;
  - rendu des raccourcis à droite ;
  - passage au titre voisin (survol, flèches gauche et droite).
- Test : `tests/test_menubar.cpp`, `tests/test_menu_model.cpp`

**Interfaces :**
```cpp
// menu_model.h
struct MenuItem { /* … */ std::wstring shortcut; };   // texte affiché à droite (« Ctrl+S »)
constexpr double kMenuShortcutGap = 24;
MenuLayout layoutMenu(const MenuModel& m, double textWidthMax, double shortcutWidthMax = 0);
constexpr int kMenuSwitchBase = -1000;                 // résultat kMenuSwitchBase - k : ouvrir le titre k
constexpr int menuSwitchResult(int k) { return kMenuSwitchBase - k; }
std::optional<int> menuSwitchTarget(int result);
int barTitleAt(const std::vector<RECT>& titles, POINT pt, int current);   // titre ≠ current sous pt, sinon -1

// menu_window.h
enum class Side { Above, Right, Left, Below };
struct BarLink { std::vector<RECT> titles; int current = -1; };   // écran
static int track(const Env& env, const MenuModel& model, POINT anchor, Side side = Side::Above, const BarLink* bar = nullptr);

// app_menus.h
enum class ActionKind { None, Shortcut, CloseWindow, Minimize, Zoom, BringAllToFront, ActivateWindow, HideApp,
                        HideOthers, ShowAll, QuitApp, AboutApp, OpenUri, GoTo, Sleep, Lock, SignOut, Restart, Shutdown };
struct MenuAction { ActionKind kind = ActionKind::None; std::wstring arg; std::uint64_t window = 0; };
struct BarMenu { std::wstring title; MenuModel model; bool bold = false, logo = false; };
struct BarContext {
    std::wstring appName, userName;
    bool explorer = false, desktop = false;   // desktop : le bureau a le premier plan
    std::vector<std::pair<std::uint64_t, std::wstring>> windows;   // fenêtres de l'app (id, titre)
    std::uint64_t activeWindow = 0;
};
struct BarMenus { std::vector<BarMenu> menus; std::map<int, MenuAction> actions; };
BarMenus buildBarMenus(const BarContext& c);   // logo, app (gras), puis génériques ou Explorateur
```

**Tests :**
- `menu_layout_reserves_shortcut_width`, `menu_switch_result_roundtrip`, `bar_title_at_skips_current`
- `menus_logo_has_system_actions`, `menus_app_named_and_bold`, `menus_generic_shortcuts_parse`
- `menus_explorer_has_go_menu`, `menus_desktop_close_disabled`, `menus_window_list_checks_active`

**Étapes :**
- [ ] Rouge.
- [ ] Implémentation, suite verte.
- [ ] `MacDock.exe --menu-test` reste fonctionnel (pas de régression du Dock), puis commit `feat(menubar): menus de la barre et passage d'un titre à l'autre`.

### Task 3 : `MacMenuBar.exe`

**Files :**
- Create `src/menubar/bar_renderer.h|.cpp` : DirectComposition, Direct2D, DirectWrite, hors écran WARP.
- Create `src/menubar/backdrop_sampler.h|.cpp` : capture ponctuelle de la bande, puis luminance.
- Create `src/menubar/bar_actions.h|.cpp` :
  - exécution des `MenuAction` ;
  - `SystemActions` injectable ;
  - navigation de l'Explorateur (`IShellWindows`).
- Create `src/menubar/menubar_window.h|.cpp` :
  - barre d'application `ABE_TOP` ;
  - suivi de l'app active ;
  - minuterie de l'horloge ;
  - menus et visibilité (plein écran, masquage automatique, via `Visibility`).
- Create `src/menubar/menubar_main.cpp` : `--quit`, `--trace`, `--snapshot f.png [--wallpaper w.png] [--app Nom] [--theme light|dark]`, mutex `Local\MacMenuBar`.
- Modify `build.ps1` : cible `menubar` (`MacMenuBar.exe`), comprise dans `all`.
- Test : `tests/test_menubar.cpp`

**Interfaces :**
```cpp
// bar_actions.h
struct SystemActions {   // remplaçable dans les tests
    std::function<void()> sleep, lock, signOut, restart, shutdown;
    std::function<bool(const std::wstring& question)> confirm;
};
SystemActions realSystemActions();
// Cible d'une commande : la fenêtre au premier plan quand le menu s'est ouvert (ni la barre ni le menu).
struct BarTarget { HWND window = nullptr; std::wstring appId; };
BarTarget keepTarget(const BarTarget& current, HWND foreground, ForegroundKind kind, const std::wstring& appId);
void runAction(const MenuAction& a, const BarTarget& t, const std::vector<HWND>& appWindows, const SystemActions& sys);
```

**Tests :**
- `bar_target_window_kept_from_open` : la cible reste celle de l'app quand le premier plan passe à une interface ignorée.
- `bar_actions_system_confirmed` : redémarrer et éteindre demandent confirmation ; un refus n'appelle rien. Le test fournit un faux `SystemActions`.

**Étapes :**
- [ ] Rouge.
- [ ] Implémentation, suite verte.
- [ ] Essais :
  - `--snapshot` sur un fond clair puis sur un fond sombre : texte foncé puis clair, logo, nom en gras, menus, horloge (images relues) ;
  - lancement réel avec `--trace` (zone réservée de 24 pt, app active dans le journal), puis `--quit` ;
  - aucune souris pilotée.
- [ ] Commit `feat(menubar): MacMenuBar.exe`.

### Task 4 : Lanceur et documentation

**Files :**
- Create `src/launcher/supervisor.h|.cpp` (pur) : décision à la sortie d'un processus surveillé.
- Modify `src/launcher/launcher_main.cpp` : surveille `MacDock.exe` et `MacMenuBar.exe` (si présent) avec `WaitForMultipleObjects`. Quand le Dock s'arrête normalement, il ferme la barre.
- Modify `src/app/main.cpp` : `--quit` ferme aussi la barre.
- Modify `build.ps1` : `supervisor.cpp` dans les tests et dans le lanceur.
- Modify `README.md`, `docs/journal-de-nuit.md`.
- Test : `tests/test_crash_policy.cpp`

**Interfaces :**
```cpp
enum class ChildRole { Dock, MenuBar };
enum class ExitDecision { Relaunch, Forget, StopAll };   // Forget : ne plus surveiller ce processus
class Supervisor {
public:
    ExitDecision onExit(ChildRole role, unsigned long code, double nowSeconds);
private:
    CrashPolicy dock_, menuBar_;
};
```

**Tests :** `supervisor_dock_normal_exit_stops_all`, `supervisor_menubar_crash_relaunch_then_forget`, `supervisor_menubar_normal_exit_forgets`.

**Étapes :**
- [ ] Rouge.
- [ ] Implémentation, suite verte.
- [ ] Documentation, puis commit `feat(launcher): surveille le Dock et la barre de menus`.

---

## Fin du plan
- Relecture finale par un agent sur le modèle le plus capable, puis une passe de corrections en TDD.
- Journal.
- Fusion locale dans `main`.
