# Sélecteur d'apps — plan 16 (sous-projet 9)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Alt+Tab façon Cmd+Tab de macOS : une rangée d'icônes des apps ouvertes dans l'ordre d'utilisation, Tab pour avancer, relâcher Alt pour passer à l'app.

**Architecture:** logique pure testée (`src/switcher/switcher_logic`) : historique d'activation, pas, rangement, raccourci ; un panneau non modal en verre (`src/switcher/switcher_window`) piloté par le Dock ; le Dock tient l'historique, enregistre les raccourcis, surveille le relâchement d'Alt par une minuterie et active l'app.

**Tech Stack:** C++20, Win32 (`RegisterHotKey`, `GetAsyncKeyState`, `SendInput`), Direct2D/DirectComposition, `GlassRenderer`, MSVC ; tests `tests/minitest.h`.

**Spec:** `docs/superpowers/specs/2026-10-07-app-switcher-design.md`

## Global Constraints

- Mesures en points × échelle : icône 64 pt (réduite pour tenir dans 90 % de la largeur de l'écran), case = 1,375 × icône, marge intérieure 12 pt, place du nom 28 pt, coins 18 pt.
- Affichage après 0,15 s d'Alt maintenu ; minuterie de relâchement : 15 ms.
- Raccourci `appSwitcherHotkey` : `alt+tab` (défaut) ou `off`.
- Aucun crochet clavier ou souris ; aucun essai n'enregistre Alt+Tab, n'affiche le sélecteur, n'active ni ne ferme d'app.

## Review Focus

1. Alt relâché pendant que l'enregistrement temporaire des touches est en cours ou entre deux ticks : la session se termine toujours, raccourcis temporaires désenregistrés.
2. App fermée (ou fenêtres toutes fermées) pendant que le sélecteur est ouvert : pas de plantage, l'activation d'une app sans fenêtre ne fait rien.
3. Une seule app ouverte, ou aucune : Alt+Tab ne plante pas et ne laisse pas de panneau.
4. Alt+Tab pendant un menu, une pile, Spotlight ou Mission Control : ignoré.
5. Raccourci déjà pris (ou refusé par Windows) : le Dock démarre, c'est journalisé, le sélecteur de Windows reste.

---

### Task 1: Logique pure et réglage

**Files:** Create `src/switcher/switcher_logic.h/.cpp` ; Modify `build.ps1` (`src\switcher` dans `LogicSources` et la cible `dock`), `src/config/settings.h/.cpp` ; Test `tests/test_switcher.cpp`, `tests/test_config.cpp`

**Interfaces:**
- Produces : `class AppMru { public: void touch(const std::wstring& appId); std::vector<std::wstring> order(const std::vector<std::wstring>& running) const; };` `std::size_t switcherStart(std::size_t count);` `std::size_t switcherStep(std::size_t selected, std::size_t count, int delta);` `struct SwitcherGeometry { double icon = 64, cell = 88, pad = 12, labelH = 28, width = 0, height = 0; };` `SwitcherGeometry switcherLayout(std::size_t count, double maxWidth);` `int switcherHit(const SwitcherGeometry&, std::size_t count, double x, double y);` `std::optional<HotkeySpec> parseSwitcherHotkey(const std::wstring&);` `Settings::appSwitcherHotkey`.

- [ ] **Step 1: tests (rouges)** :

```cpp
// Sélecteur d'apps : historique, pas, rangement, raccourci, rendu hors écran.
#include <windows.h>
#include <objbase.h>

#include <string>
#include <vector>

#include "minitest.h"
#include "../src/switcher/switcher_logic.h"

TEST_CASE(switcher_mru_order) {
    md::AppMru m;
    m.touch(L"a");
    m.touch(L"b");
    m.touch(L"c");
    m.touch(L"a");
    auto o = m.order({L"b", L"c", L"d", L"a"});
    REQUIRE(o.size() == 4);
    CHECK(o[0] == L"a");
    CHECK(o[1] == L"c");
    CHECK(o[2] == L"b");
    CHECK(o[3] == L"d");   // jamais activée : à la fin
    CHECK(m.order({}).empty());
    CHECK(m.order({L"z"}) == std::vector<std::wstring>{L"z"});
}

TEST_CASE(switcher_mru_bounded) {
    md::AppMru m;
    for (int i = 0; i < 500; ++i) m.touch(std::to_wstring(i));
    CHECK(m.order({L"0", L"499"}) == (std::vector<std::wstring>{L"499", L"0"}));
}

TEST_CASE(switcher_step_and_start) {
    CHECK(md::switcherStart(0) == 0);
    CHECK(md::switcherStart(1) == 0);
    CHECK(md::switcherStart(5) == 1);
    CHECK(md::switcherStep(1, 5, 1) == 2);
    CHECK(md::switcherStep(4, 5, 1) == 0);
    CHECK(md::switcherStep(0, 5, -1) == 4);
    CHECK(md::switcherStep(0, 0, 1) == 0);
    CHECK(md::switcherStep(7, 3, 0) == 2);   // sélection hors limites ramenée
}

TEST_CASE(switcher_layout_fits) {
    auto g = md::switcherLayout(4, 1800);
    CHECK_NEAR(g.icon, 64, 1e-9);
    CHECK_NEAR(g.width, 2 * g.pad + 4 * g.cell, 1e-9);
    CHECK_NEAR(g.height, 2 * g.pad + g.cell + g.labelH, 1e-9);
    auto many = md::switcherLayout(30, 1000);
    CHECK(many.width <= 1000 + 1e-6);
    CHECK(many.icon > 16 && many.icon < 64);
    CHECK(md::switcherHit(g, 4, g.pad + g.cell * 1.5, g.pad + g.cell / 2) == 1);
    CHECK(md::switcherHit(g, 4, 1, 1) == -1);
    CHECK(md::switcherHit(g, 4, g.pad + g.cell * 4.5, g.pad + g.cell / 2) == -1);
}

TEST_CASE(switcher_hotkey_parse) {
    auto a = md::parseSwitcherHotkey(L"Alt+Tab");
    REQUIRE(a.has_value());
    CHECK(a->mods == MOD_ALT && a->vk == VK_TAB);
    CHECK(!md::parseSwitcherHotkey(L"off"));
    CHECK(!md::parseSwitcherHotkey(L"ctrl+tab"));
}
```

et dans `tests/test_config.cpp` :

```cpp
TEST_CASE(settings_switcher_hotkey) {
    CHECK(md::settingsFromJson(*md::json::parse("{}")).appSwitcherHotkey == L"alt+tab");
    CHECK(md::settingsFromJson(*md::json::parse("{\"appSwitcherHotkey\":\"OFF\"}")).appSwitcherHotkey == L"off");
    CHECK(md::settingsFromJson(*md::json::parse("{\"appSwitcherHotkey\":\"bizarre\"}")).appSwitcherHotkey == L"alt+tab");
    md::Settings s;
    s.appSwitcherHotkey = L"off";
    CHECK(md::settingsFromJson(md::settingsToJson(s)).appSwitcherHotkey == L"off");
}
```

- [ ] **Step 2-4:** rouge ; `AppMru` : liste la plus récente d'abord, 64 entrées au plus ; `order` : les apps de `running` déjà vues dans l'ordre de la liste, puis les autres dans l'ordre reçu ; `switcherStep` : modulo, sélection ramenée dans [0, count) ; `switcherLayout` : `icon = min(64, (0.9 × maxWidth − 2 pad) / (1.375 × count))` (aucun plancher), `cell = 1.375 × icon`, `width = 2 pad + count × cell`, `height = 2 pad + cell + labelH` ; `switcherHit` : case sous le point (rangée seulement) ; réglage : `alt+tab` ou `off`, casse ignorée ; vert ; commit `feat(switcher): historique, rangement et réglage`.

### Task 2: Panneau et rendu hors écran

**Files:** Create `src/switcher/switcher_window.h/.cpp` ; Modify `src/app/main.cpp`, `src/app/cli_args.cpp` ; Test `tests/test_switcher.cpp`, `tests/test_cli_args.cpp`

**Interfaces:**
- Consumes : Task 1 ; `MenuWindow::Env`, `GlassRenderer`, `ScreenBackdrop`, `IconProvider::ImagePtr`.
- Produces : `class SwitcherWindow { public: struct Entry { std::wstring name; IconProvider::ImagePtr icon; }; bool show(const MenuWindow::Env&, HMONITOR, std::vector<Entry>, std::size_t selected); void select(std::size_t); void remove(std::size_t); void hide(); bool visible() const; int hitScreen(POINT) const; std::function<void(std::size_t)> onClick; ~SwitcherWindow(); };` `BgraImage switcherSnapshot(const std::vector<std::wstring>& names, std::size_t selected, bool dark, int width, int height);`

- [ ] **Step 1: tests (rouges)** :

```cpp
TEST_CASE(switcher_snapshot_draws_panel) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const std::vector<std::wstring> names{L"Un", L"Deux", L"Trois"};
    auto a = md::switcherSnapshot(names, 1, false, 1280, 800);
    auto b = md::switcherSnapshot(names, 2, false, 1280, 800);
    auto none = md::switcherSnapshot({}, 0, false, 1280, 800);
    REQUIRE(a.w == 1280 && a.h == 800);
    const std::size_t c = (std::size_t(400) * 1280 + 640) * 4;   // centre : l'icône du milieu
    CHECK(std::memcmp(&a.px[c], &none.px[c], 4) != 0);
    CHECK(a.px != b.px);   // la sélection change le dessin
    CoUninitialize();
}
```

et dans `tests/test_cli_args.cpp` : `CHECK(md::diagnosticMissingValue(V{L"MacDock.exe", L"--switcher-snapshot"}) == L"--switcher-snapshot");`

- [ ] **Step 2-4:** rouge ; fenêtre `WS_POPUP`, `WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP`, centrée sur l'écran, panneau + 24 pt d'ombre ; verre des menus sur le panneau (capture `ScreenBackdrop` de l'écran pendant l'affichage), dépoli sinon ; icônes dessinées à la taille de la case moins la marge, fond de sélection arrondi (blanc 0,18 en sombre, noir 0,10 en clair), nom de l'app sélectionnée centré sous la rangée ; `WM_MOUSEACTIVATE` → `MA_NOACTIVATE` ; clic gauche sur une icône → `onClick(i)` ; `switcherSnapshot` : fond Tahoe, panneau dépoli, cases de couleur ; `MacDock.exe --switcher-snapshot f.png [--count n] [--select i] [--theme dark]` ; vert ; commit `feat(switcher): panneau`.

### Task 3: Branchements et documentation

**Files:** Modify `src/app/dock_controller.h/.cpp` (icône d'une app au cache du Dock), `src/app/dock_window.h/.cpp` ; `README.md`, `docs/journal-de-nuit.md`

- [ ] **Step 1-3:** `DockController::appIcon(appId, icons)` : même image que le Dock (taille de l'agrandissement) ; `DockApp` : `AppMru mru_` touché à chaque activation suivie ; raccourcis `kHotSwitch` (Alt+Tab) et `kHotSwitchBack` (Alt+Maj+Tab), sans `MOD_NOREPEAT`, enregistrés au démarrage et à chaque changement du réglage (échec journalisé) ; premier appui : ignoré si `menuOpen_` ou une vue modale est ouverte ; sinon apps ouvertes dans l'ordre `mru_`, sélection `switcherStart` (ou la dernière pour Maj), touche neutre 0xE8 envoyée, raccourcis temporaires Alt+Échap, Alt+←, Alt+→, Alt+Q, Alt+H, minuterie 15 ms ; appuis suivants : pas ±1 ; minuterie : Alt relâché → fin ; 0,15 s écoulées → panneau (`pauseCapture`) ; fin : panneau caché, `resumeCapture`, raccourcis temporaires retirés, activation (app masquée : démasquée et toutes ses fenêtres ; sinon ses fenêtres non réduites, ou la première restaurée) ; Échap : fin sans activation ; Q : `WM_CLOSE` aux fenêtres de l'app choisie, retirée de la rangée ; H : masquée comme le menu du Dock ; suite verte.
- [ ] **Step 4:** README (Utilisation, Réglages, Diagnostic), journal ; commits `feat(switcher): sélecteur d'apps dans le Dock` puis `docs: sélecteur d'apps (plan 16)`.
