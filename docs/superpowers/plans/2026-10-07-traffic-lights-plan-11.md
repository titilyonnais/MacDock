# Feux tricolores (plan 11) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Trois pastilles façon macOS (fermer, réduire, zoom) en haut à gauche de la fenêtre active à barre de titre Windows.

**Architecture:** Logique pure (`traffic_lights.*` : éligibilité, géométrie, rendu CPU, couleur dominante) ; un calque `UpdateLayeredWindow` (`traffic_window.*`) dans `MacMenuBar.exe`, rattaché à la fenêtre au premier plan et suivi par `EVENT_OBJECT_LOCATIONCHANGE`.

**Tech Stack:** C++ (MSVC `/std:c++latest`), Win32 (fenêtres calques, WinEvents, GDI `GetPixel`), minitest.

**Spec:** `docs/superpowers/specs/2026-10-07-traffic-lights-design.md`

## Global Constraints

- Aucune ressource Apple : couleurs et symboles dessinés par le code.
- Aucun essai réel qui affiche le calque ou pilote la souris ou le clavier ; ne jamais lancer `MacMenuBar.exe` sans option hors écran pendant que l'utilisateur travaille.
- `trafficLights` : `standard` (défaut), `all`, `off` ; valeur inconnue → `standard`.
- Diamètre 12 pt, centres espacés de 20 pt, premier centre à 20 pt du bord gauche du cadre ; barre de titre Windows reconnue à partir de 20 px × dpi/96.
- Couleurs : rouge `#FF5F57`/`#E0443E`, jaune `#FEBC2E`/`#DEA123`, vert `#28C840`/`#1AAB29`, indisponible `#D0D0D0` (clair) / `#5A5A5A` (sombre).
- Fichiers en UTF-8, fins de ligne LF.

## Review Focus

1. App à barre de titre personnalisée (Chrome, Edge, Explorateur à onglets) : aucune pastille en mode `standard`.
2. Fenêtre agrandie : son cadre déborde de l'écran (bordures invisibles) ; les pastilles doivent rester dans la partie visible.
3. Cible fermée, réduite, cachée ou passée en plein écran : le calque disparaît, aucun pointeur ni crochet ne reste vers elle.
4. Clic sur le fond du calque : la fenêtre doit pouvoir être déplacée (pas de zone morte).
5. Écran à 200 % : tailles en points, pas de pastille coupée.

---

### Task 1: Logique des feux tricolores

**Files:**
- Create: `src/menubar/traffic_lights.h`, `src/menubar/traffic_lights.cpp` (ajouté à `LogicSources` dans `build.ps1`)
- Modify: `src/menubar/menubar_settings.h/.cpp` (`LightsMode trafficLights = LightsMode::Standard;`, JSON `trafficLights`)
- Test: `tests/test_traffic_lights.cpp`

**Interfaces:**
- Produces: `LightsMode`, `LightsWindowInfo`, `wantsLights`, `LightsLayout`, `lightsLayout`, `hitLight`, `dominantColor`, `lightCommand` (signatures de la spec §2). `LightsMode` est déclaré dans `menubar_settings.h`.

- [ ] **Step 1: tests (rouges)**

```cpp
#include "minitest.h"
#include "../src/menubar/menubar_settings.h"
#include "../src/menubar/traffic_lights.h"

namespace {
md::LightsWindowInfo classic() {
    md::LightsWindowInfo w;
    w.style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
    w.className = L"Notepad";
    w.frame = RECT{100, 100, 900, 700};
    w.client = RECT{108, 131, 892, 692};   // barre de titre de 31 px
    return w;
}
} // namespace

TEST_CASE(lights_want_classic_not_custom) {
    CHECK(md::wantsLights(classic(), md::LightsMode::Standard, 96));
    auto custom = classic();
    custom.client.top = 101;   // zone client dès le haut : barre de titre dessinée par l'app
    CHECK(!md::wantsLights(custom, md::LightsMode::Standard, 96));
    CHECK(md::wantsLights(custom, md::LightsMode::All, 96));
    CHECK(!md::wantsLights(classic(), md::LightsMode::Off, 96));
    auto hiDpi = classic();
    hiDpi.client.top = 100 + 31;   // 31 px à 200 % : moins de 20 pt
    CHECK(!md::wantsLights(hiDpi, md::LightsMode::Standard, 192));
    hiDpi.client.top = 100 + 62;
    CHECK(md::wantsLights(hiDpi, md::LightsMode::Standard, 192));
}

TEST_CASE(lights_refuse_special_windows) {
    auto tool = classic();
    tool.exStyle = WS_EX_TOOLWINDOW;
    CHECK(!md::wantsLights(tool, md::LightsMode::All, 96));
    auto noSys = classic();
    noSys.style &= ~WS_SYSMENU;
    CHECK(!md::wantsLights(noSys, md::LightsMode::All, 96));
    auto noCaption = classic();
    noCaption.style = WS_POPUP | WS_BORDER;
    CHECK(!md::wantsLights(noCaption, md::LightsMode::All, 96));
    auto shell = classic();
    shell.className = L"Shell_TrayWnd";
    CHECK(!md::wantsLights(shell, md::LightsMode::All, 96));
    auto mine = classic();
    mine.ownProcess = true;
    CHECK(!md::wantsLights(mine, md::LightsMode::All, 96));
    auto min = classic();
    min.iconic = true;
    CHECK(!md::wantsLights(min, md::LightsMode::All, 96));
}

TEST_CASE(lights_layout_points) {
    auto l = md::lightsLayout(RECT{100, 100, 900, 700}, RECT{108, 131, 892, 692}, 96);
    CHECK_NEAR(l.radius, 6.0, 1e-9);
    CHECK_EQ((l.circles[0].left + l.circles[0].right) / 2, 120L);   // 20 pt du bord
    CHECK_EQ((l.circles[1].left + l.circles[1].right) / 2, 140L);
    CHECK_EQ((l.circles[2].left + l.circles[2].right) / 2, 160L);
    CHECK_EQ((l.circles[0].top + l.circles[0].bottom) / 2, 115L);   // milieu de la barre de 31 px (arrondi)
    CHECK_EQ(l.window.left, 104L);
    CHECK_EQ(l.window.top, 100L);
    CHECK_EQ(l.window.bottom, 131L);
    CHECK_EQ(l.window.right, 174L);   // 8 pt après la dernière pastille
    auto big = md::lightsLayout(RECT{0, 0, 1600, 1200}, RECT{16, 62, 1584, 1184}, 192);
    CHECK_NEAR(big.radius, 12.0, 1e-9);
    CHECK_EQ((big.circles[0].left + big.circles[0].right) / 2, 40L);
    auto thin = md::lightsLayout(RECT{0, 0, 800, 600}, RECT{0, 0, 800, 600}, 96);   // mode « all » sans barre : 28 pt
    CHECK_EQ(thin.window.bottom - thin.window.top, 28L);
}

TEST_CASE(lights_hit_and_command) {
    auto l = md::lightsLayout(RECT{100, 100, 900, 700}, RECT{108, 131, 892, 692}, 96);
    CHECK_EQ(md::hitLight(l, POINT{120, 115}), 0);
    CHECK_EQ(md::hitLight(l, POINT{147, 115}), 1);   // cercle élargi de 2 px
    CHECK_EQ(md::hitLight(l, POINT{160, 115}), 2);
    CHECK_EQ(md::hitLight(l, POINT{130, 115}), -1);  // entre deux pastilles
    CHECK_EQ(md::hitLight(l, POINT{170, 128}), -1);
    CHECK(md::lightCommand(0, false) == SC_CLOSE);
    CHECK(md::lightCommand(1, false) == SC_MINIMIZE);
    CHECK(md::lightCommand(2, false) == SC_MAXIMIZE);
    CHECK(md::lightCommand(2, true) == SC_RESTORE);
}

TEST_CASE(lights_dominant_color) {
    std::vector<std::uint32_t> s{0xF3F3F3, 0xF2F3F4, 0x202020, 0xF3F3F3, 0x0078D4, 0xF4F3F3};
    const std::uint32_t c = md::dominantColor(s);
    CHECK(((c >> 16) & 0xFF) >= 0xF0);
    CHECK((c & 0xFF) >= 0xF0);
    CHECK_EQ(md::dominantColor({}), 0u);
}

TEST_CASE(lights_setting) {
    auto s = md::menuBarSettingsFromJson(*md::json::parse(R"({"trafficLights":"all"})"));
    CHECK(s.trafficLights == md::LightsMode::All);
    CHECK(md::menuBarSettingsFromJson(*md::json::parse(R"({"trafficLights":"x"})")).trafficLights == md::LightsMode::Standard);
    s.trafficLights = md::LightsMode::Off;
    CHECK(md::menuBarSettingsFromJson(md::menuBarSettingsToJson(s)).trafficLights == md::LightsMode::Off);
}
```

- [ ] **Step 2:** compilation en échec (`traffic_lights.h` absent).
- [ ] **Step 3:** implémentation (spec §2). Géométrie : `k = dpi / 96`, `titleTop = frame.top`, `titleH = client.top − frame.top` si ≥ `20·k`, sinon `28·k` ; `cy = titleTop + titleH / 2` (arrondi entier) ; centres `frame.left + lround((20 + 20·i)·k)` ; cercle `[c − r, c + r]` avec `r = 6·k` arrondi ; `window = {frame.left + 4, titleTop, centre(2) + lround((6 + 8)·k), titleTop + lround(titleH)}`, borné à `frame` ; `patch = window`. `hitLight` : distance au centre ≤ `r + 2`. `dominantColor` : histogramme sur `(r>>3, g>>3, b>>3)`, moyenne du groupe le plus peuplé.
- [ ] **Step 4:** `tests.exe lights` vert, suite verte.
- [ ] **Step 5:** commit `feat(menubar): logique des feux tricolores`.

### Task 2: Rendu des pastilles et planche hors écran

**Files:**
- Modify: `src/menubar/traffic_lights.h/.cpp` (`LightsState`, `renderLights`)
- Modify: `src/menubar/menubar_main.cpp` (`--lights-snapshot f.png`)
- Test: `tests/test_traffic_lights.cpp`

**Interfaces:**
- Produces: `std::vector<std::uint8_t> renderLights(const LightsLayout& l, const LightsState& s, double scale);` (BGRA prémultiplié, `l.window` en taille) ; `std::vector<std::uint8_t> lightsSheet(UINT& w, UINT& h);` (planche 2 × 3 : clair/sombre × normal/survol/indisponible, à l'échelle 2).

- [ ] **Step 1: tests (rouges)**

```cpp
TEST_CASE(lights_render_colors) {
    auto l = md::lightsLayout(RECT{100, 100, 900, 700}, RECT{108, 131, 892, 692}, 96);
    md::LightsState st;
    st.enabled[0] = st.enabled[1] = st.enabled[2] = true;
    st.patchColor = 0xF3F3F3;
    auto px = md::renderLights(l, st, 1.0);
    const int w = l.window.right - l.window.left, h = l.window.bottom - l.window.top;
    REQUIRE(px.size() == std::size_t(w * h * 4));
    auto at = [&](LONG x, LONG y) { return &px[(std::size_t(y - l.window.top) * w + (x - l.window.left)) * 4]; };
    const std::uint8_t* red = at(120, 115);
    CHECK(red[2] > 240 && red[1] < 120 && red[3] == 255);
    const std::uint8_t* yellow = at(140, 115);
    CHECK(yellow[2] > 240 && yellow[1] > 160 && yellow[0] < 80);
    const std::uint8_t* green = at(160, 115);
    CHECK(green[1] > 180 && green[2] < 80);
    const std::uint8_t* patch = at(106, 103);   // fond : couleur de la barre de titre, opaque
    CHECK(patch[3] == 255 && patch[0] == 0xF3);
    CHECK(at(l.window.right - 1, 103)[3] < at(l.window.right - 8, 103)[3]);   // fondu à droite
    st.enabled[1] = false;
    auto gray = md::renderLights(l, st, 1.0);
    const std::uint8_t* g = &gray[(std::size_t(115 - l.window.top) * w + (140 - l.window.left)) * 4];
    CHECK(std::abs(int(g[0]) - int(g[2])) < 8);   // gris
}

TEST_CASE(lights_hover_draws_symbols) {
    auto l = md::lightsLayout(RECT{100, 100, 900, 700}, RECT{108, 131, 892, 692}, 192);
    md::LightsState st;
    st.enabled[0] = st.enabled[1] = st.enabled[2] = true;
    st.patchColor = 0xF3F3F3;
    auto plain = md::renderLights(l, st, 2.0);
    st.hover = true;
    auto hover = md::renderLights(l, st, 2.0);
    CHECK(plain != hover);
    UINT w = 0, h = 0;
    auto sheet = md::lightsSheet(w, h);
    CHECK(w > 0 && h > 0 && sheet.size() == std::size_t(w) * h * 4);
}
```

- [ ] **Step 2:** compilation en échec.
- [ ] **Step 3:** rendu CPU : pour chaque pixel, couverture des cercles par 4 × 4 sous-échantillons ; bord de 0,5 pt de la couleur sombre ; symboles au survol (traits de 1,1 pt, longueur 6 pt : ×, −, +) en `#000000` à 55 % ; fond de `patchColor` opaque puis alpha linéaire de 255 à 0 sur les 8 derniers pt ; prémultiplication. `--lights-snapshot` : `lightsSheet` + `writePng` (COM initialisé), sans fenêtre ni réglage.
- [ ] **Step 4:** tests verts ; planche vérifiée à l'œil.
- [ ] **Step 5:** commit `feat(menubar): rendu des feux tricolores et planche hors écran`.

### Task 3: Calque et intégration à la barre

**Files:**
- Create: `src/menubar/traffic_window.h/.cpp`
- Modify: `src/menubar/menubar_window.h/.cpp`

**Interfaces:**
- Consumes: Task 1 et 2.
- Produces: `class TrafficWindow { bool create(HINSTANCE); void attach(HWND target, LightsMode mode); void detach(); void destroy(); HWND target() const; }`.

- [ ] **Step 1:** pas de test automatique possible sans afficher de fenêtre (règle : aucun essai réel à l'écran) ; la logique est déjà couverte par les Tasks 1 et 2. Le registre le note (Ruling).
- [ ] **Step 2:** `TrafficWindow` (spec §3) : classe `MacMenuBarLights`, `WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST` non — **pas** topmost (au-dessus de la cible seulement) ; `SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE)` ; `attach` : infos (`GetWindowLongPtrW`, `GetClassLongPtrW(GCL_STYLE)`, `DwmGetWindowAttribute(DWMWA_EXTENDED_FRAME_BOUNDS)`, `GetClientRect` + `ClientToScreen`, `GetDpiForWindow`), `wantsLights`, couleur (`GetDC(nullptr)` + `GetPixel`), `renderLights`, `UpdateLayeredWindow`, `SetWindowPos(insertAfter = GetWindow(target, GW_HWNDPREV) ou HWND_TOP, SWP_NOACTIVATE | SWP_SHOWWINDOW)` ; crochet `EVENT_OBJECT_LOCATIONCHANGE` (et `EVENT_OBJECT_HIDE`, `EVENT_OBJECT_DESTROY`, `EVENT_SYSTEM_MINIMIZESTART`) limité au processus de la cible, `OBJID_WINDOW` et `hwnd == target` seulement ; souris (survol, `TrackMouseEvent`, clic, double-clic, déplacement relayé) ; `CS_DBLCLKS`.
- [ ] **Step 3:** `MenuBarApp` : membre `TrafficWindow lights_`, créé dans `run` (pas en `--snapshot`), `onForeground` → `lights_.attach(h, settings_.trafficLights)` (aussi pour une fenêtre ignorée par la barre : `attach` décide), rechargement des réglages → réattache, arrêt → `destroy`.
- [ ] **Step 4:** `build.ps1 -Target all` sans avertissement nouveau ; suite verte.
- [ ] **Step 5:** commit `feat(menubar): feux tricolores sur la fenêtre active`.

### Task 4: Documentation

- [ ] README (barre de menus : feux tricolores ; réglage `trafficLights` ; diagnostic `--lights-snapshot`), journal (plan 11, décisions, vérifications à faire soi-même).
- [ ] Commit `docs: feux tricolores (plan 11)`.
