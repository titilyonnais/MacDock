# HUD du volume et de la luminosité — plan 17 (sous-projet 10)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** la pastille de Tahoe en haut à droite pour le volume (touches reprises, pas de 1/16) et la luminosité (avis de Windows).

**Architecture:** logique pure testée (`src/hud/hud_logic`) : pas du volume, fondu, place, garde de la luminosité ; une fenêtre non activée en verre (`src/hud/hud_window`) pilotée par la barre de menus ; `MenuBarApp` enregistre les touches de volume, écoute Core Audio et l'avis de luminosité, et anime le fondu.

**Tech Stack:** C++20, Win32 (`RegisterHotKey`, `RegisterPowerSettingNotification`), Core Audio (`AudioStatus`), Direct2D/DirectComposition, `GlassRenderer`, MSVC ; tests `tests/minitest.h`.

**Spec:** `docs/superpowers/specs/2026-10-07-volume-hud-design.md`

## Global Constraints

- Pastille 280 × 64 pt, coins 20 pt, 12 pt du bord droit, 8 pt sous la barre ; titre gras 13 pt ; jauge en capsule de 6 pt ; pictogramme 16 pt.
- Visible 1,5 s après le dernier changement, fondu 0,25 s ; minuterie de 16 ms pendant l'affichage seulement.
- Pas du volume 1/16, fin 1/64 (Maj+Alt) ; réglage `hud` (`menubar.json`, `true` par défaut).
- Aucun crochet ; aucun essai n'enregistre les touches de volume, ne change le volume ou la luminosité, ni n'affiche la pastille devant l'utilisateur.

## Review Focus

1. Touches de volume déjà prises par un autre outil : la barre démarre, c'est journalisé, la pastille suit les changements venus d'ailleurs, sans doublon quand la barre règle elle-même le volume.
2. Pas de sortie audio (ou sortie débranchée pendant l'affichage) : aucune touche ne plante, la pastille ne montre pas de valeur fausse.
3. Curseur de volume ou de luminosité glissé dans un menu de la barre : pas de pastille par-dessus le menu.
4. Changement d'écran ou de DPI pendant l'affichage : la pastille ne reste pas à une place fausse, pas de fuite.
5. `hud` passé à `false` à chaud : touches rendues à Windows, pastille cachée.

---

### Task 1: Logique pure et réglage

**Files:** Create `src/hud/hud_logic.h/.cpp` ; Modify `build.ps1` (`src\hud\*.cpp` dans `LogicSources` et la cible `menubar`), `src/menubar/menubar_settings.h/.cpp` ; Test `tests/test_hud.cpp`, `tests/test_menubar.cpp`

**Interfaces:**
- Produces : `enum class HudKind { Volume, Brightness };` `struct HudContent { HudKind kind = HudKind::Volume; float level = 0; bool muted = false; std::wstring detail; };` `float volumeStep(float v, int dir, bool fine);` `class HudFade { static constexpr double kHold = 1.5, kFade = 0.25; void show(double now); float opacity(double now) const; bool visible(double now) const; void reset(); };` `struct HudPlace { int x, y, w, h; };` `HudPlace hudPlace(const RECT& monitor, int barBottom, float scale);` `class BrightnessGate { bool accept(double now, bool menuOpen); void noteOwnChange(double now); };` `MenuBarSettings::hud`.

- [ ] **Step 1: tests (rouges)** :

```cpp
// HUD du volume et de la luminosité : pas, fondu, place, garde, rendu hors écran.
#include <windows.h>

#include "minitest.h"
#include "../src/hud/hud_logic.h"

TEST_CASE(hud_volume_step_grid) {
    CHECK_NEAR(md::volumeStep(0.5f, 1, false), 0.5625, 1e-6);
    CHECK_NEAR(md::volumeStep(0.3f, 1, false), 0.3125, 1e-6);    // aligné sur la grille
    CHECK_NEAR(md::volumeStep(0.3f, -1, false), 0.25, 1e-6);
    CHECK_NEAR(md::volumeStep(0.25f, -1, false), 0.1875, 1e-6);
    CHECK_NEAR(md::volumeStep(0.49999f, 1, false), 0.5625, 1e-6);   // presque sur la grille : compté dessus
    CHECK_NEAR(md::volumeStep(0.5f, 1, true), 0.515625, 1e-6);
    CHECK_NEAR(md::volumeStep(1.0f, 1, false), 1.0, 1e-6);
    CHECK_NEAR(md::volumeStep(0.0f, -1, false), 0.0, 1e-6);
    CHECK_NEAR(md::volumeStep(-1.0f, 1, false), 0.0625, 1e-6);   // valeur inconnue : bornée d'abord
}

TEST_CASE(hud_fade_hold_and_revive) {
    md::HudFade f;
    CHECK(!f.visible(0));
    f.show(10);
    CHECK_NEAR(f.opacity(11.4), 1.0, 1e-6);
    CHECK_NEAR(f.opacity(11.625), 0.5, 1e-3);
    CHECK(!f.visible(11.76));
    f.show(11.6);   // ravivée pendant le fondu
    CHECK_NEAR(f.opacity(12.0), 1.0, 1e-6);
    f.reset();
    CHECK(!f.visible(12.0));
}

TEST_CASE(hud_place_top_right_under_bar) {
    const RECT mon{0, 0, 1920, 1080};
    auto p = md::hudPlace(mon, 24, 1.0f);
    CHECK(p.w == 280);
    CHECK(p.h == 64);
    CHECK(p.x == 1920 - 12 - 280);
    CHECK(p.y == 24 + 8);
    const RECT second{1920, 0, 1920 + 2560, 1440};
    p = md::hudPlace(second, 36, 1.5f);
    CHECK(p.w == 420);
    CHECK(p.x == 1920 + 2560 - 18 - 420);
    CHECK(p.y == 36 + 12);
}

TEST_CASE(hud_brightness_gate) {
    md::BrightnessGate g;
    CHECK(!g.accept(0, false));   // premier avis : la valeur courante, envoyée à l'enregistrement
    CHECK(g.accept(5, false));
    CHECK(!g.accept(6, true));    // menu ouvert : le curseur est sous les yeux
    g.noteOwnChange(10);
    CHECK(!g.accept(10.5, false));
    CHECK(g.accept(11.1, false));
}
```

Dans `tests/test_menubar.cpp` :

```cpp
TEST_CASE(menubar_settings_hud) {
    CHECK(md::MenuBarSettings{}.hud);
    auto v = md::json::parse(R"({"hud": false})");
    REQUIRE(v.has_value());
    CHECK(!md::menuBarSettingsFromJson(*v).hud);
    md::MenuBarSettings s;
    s.hud = false;
    CHECK(!md::menuBarSettingsFromJson(md::menuBarSettingsToJson(s)).hud);
}
```

- [ ] **Step 2:** `build.ps1 -Target tests` → échec de compilation (`hud_logic.h` absent).
- [ ] **Step 3:** implémenter `hud_logic` (grille : `g = fine ? 1/64 : 1/16` ; v borné à [0,1] ; montée `(floor(v/g + 1e-3) + 1)·g`, descente `(ceil(v/g − 1e-3) − 1)·g`, bornées ; fondu linéaire ; place `w = lround(280·s)`, `h = lround(64·s)`, `x = right − lround(12·s) − w`, `y = barBottom + lround(8·s)` ; garde : premier avis refusé, refus si menu ouvert ou moins d'1 s après `noteOwnChange`) et `hud` dans les réglages (lu par `readBool`, écrit).
- [ ] **Step 4:** tests verts (`tests.exe hud`, `tests.exe menubar`), suite verte.
- [ ] **Step 5:** commit `feat(hud): logique du HUD et réglage`.

### Task 2: Pastille et rendu hors écran

**Files:** Create `src/hud/hud_window.h/.cpp` ; Modify `src/menubar/menubar_main.cpp` (`--hud-snapshot`) ; Test `tests/test_hud.cpp`

**Interfaces:**
- Consumes : Task 1.
- Produces : `class HudWindow { bool show(const MenuWindow::Env&, HMONITOR, const HudPlace&, const HudContent&); void update(const HudContent&); void setOpacity(float); void hide(); bool visible() const; };` `BgraImage hudSnapshot(const HudContent&, bool dark, int w, int h);`

- [ ] **Step 1: test (rouge)** :

```cpp
TEST_CASE(hud_snapshot_draws_panel) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    md::HudContent c;
    c.level = 0.5f;
    c.detail = L"Haut-parleurs";
    auto img = md::hudSnapshot(c, true, 800, 300);
    REQUIRE(img.width == 800);
    REQUIRE(img.pixels.size() == std::size_t(800) * 300 * 4);
    // Le panneau (en haut à droite) diffère du fond Tahoe autour.
    auto px = [&](int x, int y) { return img.pixels[(std::size_t(y) * 800 + x) * 4 + 1]; };
    CHECK(px(800 - 12 - 140, 24 + 8 + 32) != px(100, 250));
}
```

- [ ] **Step 2:** échec (symbole absent). **Step 3:** `HudWindow` sur le modèle de `SwitcherWindow` (DComp + D2D sur `env.device`, `ScreenBackdrop` le temps de l'affichage, verre de repli, `WS_EX_TRANSPARENT` en plus), opacité du visuel DComp pour le fondu ; dessin partagé `paintHud` (titre « Volume »/« Luminosité », détail à droite en gris, pictogramme `drawGlyph(Speaker|Sun, level, muted)`, jauge) ; `hudSnapshot` : fond Tahoe flou, panneau à `hudPlace` (barre de 24 pt). `--hud-snapshot f.png [--kind volume|brightness] [--level x] [--muted] [--theme dark]` dans `menubar_main` (aucune fenêtre).
- [ ] **Step 4:** tests verts ; rendus à l'œil : volume 50 % clair, sourdine sombre, luminosité 80 %.
- [ ] **Step 5:** commit `feat(hud): pastille et rendu hors écran`.

### Task 3: Branchements et documentation

**Files:** Modify `src/menubar/menubar_window.h/.cpp` ; `README.md`, `docs/journal-de-nuit.md`

- [ ] **Step 1-3:** `MenuBarApp` : `registerVolumeKeys()` au démarrage et à chaque rechargement du réglage (`VK_VOLUME_UP/DOWN/MUTE` sans modificateur et avec `MOD_SHIFT|MOD_ALT`, sur `ctl_` ; échec journalisé, `volumeKeys_` faux) ; `WM_HOTKEY` : sourdine basculée ou `volumeStep` puis `setVolume`, pastille ; `WM_APP_VOLUME` (wParam 0) : pastille seulement si les touches ne sont pas reprises, `hud` vrai et aucun menu ouvert ; `RegisterPowerSettingNotification(ctl_, GUID_VIDEO_CURRENT_MONITOR_BRIGHTNESS)` → `WM_POWERBROADCAST`/`PBT_POWERSETTINGCHANGE` → `BrightnessGate` ; `noteOwnChange` au curseur de luminosité du menu ; `showHud(content)` : écran du curseur parmi les barres (barre absente : haut de l'écran), `HudFade::show`, minuterie `kHudTimer` 16 ms → opacité, fin → `hide` et arrêt de la minuterie ; écrans refaits → pastille cachée ; arrêt : touches et avis désenregistrés ; le relevé du fond de la barre est sauté pendant l'affichage (une seule duplication par processus) ; suite verte.
- [ ] **Step 4:** README (Utilisation, Réglages, Diagnostic), journal ; commits `feat(hud): pastille du volume et de la luminosité dans la barre` puis `docs: HUD du volume et de la luminosité (plan 17)`.
