# Mission Control — plan 15 (sous-projet 8)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** une vue façon Mission Control : toutes les fenêtres visibles du bureau courant rangées sans chevauchement, en miniatures DWM vivantes, sur chaque écran.

**Architecture:** un rangement pur et testé (`src/mission/mission_layout`), une vue modale multi-écrans (`src/mission/mission_view`) qui anime des miniatures DWM au-dessus du fond d'écran, et des branchements dans le Dock (raccourci, message enregistré pour les coins actifs à venir, activation de la fenêtre choisie).

**Tech Stack:** C++20, Win32, DWM thumbnails, Direct2D/DirectComposition, `IDesktopWallpaper`, `IVirtualDesktopManager`, MSVC ; tests `tests/minitest.h`.

**Spec:** `docs/superpowers/specs/2026-10-07-mission-control-design.md`

## Global Constraints

- Mesures en points, multipliées par l'échelle de l'écran ; marges de la zone : 48 pt à gauche, à droite et en bas, 64 pt en haut ; écart entre fenêtres : 24 pt.
- Animation : 0,3 s, `easeOut` cubique ; Maj enfoncée : 5 fois plus lent.
- Raccourci `missionControlHotkey` : `ctrl+alt+up` (défaut), `ctrl+up`, `f3`, `off`.
- Aucun crochet clavier ou souris ; aucune capture d'écran.
- Aucun essai n'ouvre la vue devant l'utilisateur, n'active ni ne déplace de fenêtre.

## Review Focus

1. Fenêtre très large (21:9) ou très haute à côté de petites : rien ne sort de la zone, rien ne se chevauche.
2. Une fenêtre fermée pendant que la vue est ouverte : sa miniature disparaît sans plantage, le clic sur sa place ne fait rien.
3. Plusieurs écrans : cliquer dans la vue d'un autre écran ne ferme pas tout ; Échap marche depuis n'importe quelle vue.
4. Second appui sur le raccourci pendant l'animation d'ouverture ou de fermeture : une seule vue, pas de miniatures orphelines.
5. Fond d'écran absent (couleur unie, diaporama) : fond de repli, pas d'écran noir ni d'échec.

---

### Task 1: Rangement pur

**Files:** Create `src/mission/mission_layout.h/.cpp` ; Modify `build.ps1` (`src\mission` dans `LogicSources` et dans la cible `dock`) ; Test `tests/test_mission.cpp`

**Interfaces:**
- Produces : `struct MissionRect { double x = 0, y = 0, w = 0, h = 0; };` `constexpr double kMissionGap = 24;` `MissionRect missionArea(const MissionRect& work, double scale);` `std::vector<MissionRect> missionLayout(const std::vector<MissionRect>& windows, const MissionRect& area, double gap);` `int missionHit(const std::vector<MissionRect>& rects, double x, double y);` `MissionRect lerpRect(const MissionRect& a, const MissionRect& b, double t);` `double easeOut(double t);` `std::optional<HotkeySpec> parseMissionHotkey(const std::wstring&);` (HotkeySpec de `spot_results.h`).

- [ ] **Step 1: tests (rouges)** :

```cpp
// Mission Control : rangement, test de clic, interpolation, raccourci, rendu hors écran.
#include <windows.h>
#include <objbase.h>

#include <vector>

#include "minitest.h"
#include "../src/mission/mission_layout.h"

namespace {
bool inside(const md::MissionRect& r, const md::MissionRect& a) {
    return r.x >= a.x - 1e-6 && r.y >= a.y - 1e-6 && r.x + r.w <= a.x + a.w + 1e-6 && r.y + r.h <= a.y + a.h + 1e-6;
}
bool overlap(const md::MissionRect& a, const md::MissionRect& b) {
    return a.x < b.x + b.w - 1e-6 && b.x < a.x + a.w - 1e-6 && a.y < b.y + b.h - 1e-6 && b.y < a.y + a.h - 1e-6;
}
} // namespace

TEST_CASE(mission_layout_fits_without_overlap) {
    const md::MissionRect area{0, 0, 1920, 1000};
    for (int n : {1, 2, 3, 5, 8, 13, 30}) {
        std::vector<md::MissionRect> wins;
        for (int i = 0; i < n; ++i)
            wins.push_back({double(i * 37 % 900), double(i * 53 % 500), 600.0 + i * 40 % 500, 400.0 + i * 70 % 400});
        wins[0].w = 2560, wins[0].h = 1080;   // 21:9
        if (n > 1) wins[1].w = 500, wins[1].h = 1400;   // très haute
        auto r = md::missionLayout(wins, area, 24);
        REQUIRE(r.size() == wins.size());
        for (int i = 0; i < n; ++i) {
            CHECK(inside(r[i], area));
            CHECK_NEAR(r[i].w / r[i].h, wins[i].w / wins[i].h, 1e-6);   // proportions gardées
            CHECK(r[i].w <= wins[i].w + 1e-6);                           // jamais agrandie
            for (int j = i + 1; j < n; ++j) CHECK(!overlap(r[i], r[j]));
        }
    }
}

TEST_CASE(mission_layout_single_window_real_size_centered) {
    auto r = md::missionLayout({{50, 50, 800, 600}}, {0, 0, 1920, 1000}, 24);
    REQUIRE(r.size() == 1);
    CHECK_NEAR(r[0].w, 800, 1e-6);
    CHECK_NEAR(r[0].x + r[0].w / 2, 960, 1e-6);
    CHECK_NEAR(r[0].y + r[0].h / 2, 500, 1e-6);
}

TEST_CASE(mission_layout_reading_order) {
    // côte à côte : la gauche reste à gauche ; l'une au-dessus de l'autre (trop grandes pour une ligne) : l'ordre vertical reste
    auto r = md::missionLayout({{1000, 0, 800, 600}, {0, 0, 800, 600}}, {0, 0, 1920, 1000}, 24);
    CHECK(r[1].x < r[0].x);
    auto t = md::missionLayout({{0, 600, 1800, 500}, {0, 0, 1800, 500}}, {0, 0, 1920, 1000}, 24);
    CHECK(t[1].y < t[0].y);
}

TEST_CASE(mission_layout_uses_space) {   // 8 grandes fenêtres : elles occupent une bonne part de la zone
    std::vector<md::MissionRect> wins(8, md::MissionRect{0, 0, 1600, 900});
    auto r = md::missionLayout(wins, {0, 0, 1920, 1000}, 24);
    double sum = 0;
    for (auto& x : r) sum += x.w * x.h;
    CHECK(sum > 0.30 * 1920 * 1000);
}

TEST_CASE(mission_layout_empty_and_degenerate) {
    CHECK(md::missionLayout({}, {0, 0, 1920, 1000}, 24).empty());
    auto r = md::missionLayout({{0, 0, 0, 0}, {0, 0, 300, 200}}, {0, 0, 1920, 1000}, 24);
    REQUIRE(r.size() == 2);
    CHECK(r[0].w >= 0 && r[0].h >= 0 && r[0].w == r[0].w);   // ni négatif ni NaN
    auto tiny = md::missionLayout({{0, 0, 300, 200}}, {0, 0, 10, 10}, 24);
    CHECK(tiny[0].w <= 10 && tiny[0].h <= 10);
}

TEST_CASE(mission_area_hit_lerp_ease) {
    const md::MissionRect a = md::missionArea({100, 0, 1000, 800}, 2);
    CHECK_NEAR(a.x, 196, 1e-9);
    CHECK_NEAR(a.y, 128, 1e-9);
    CHECK_NEAR(a.w, 1000 - 192, 1e-9);
    CHECK_NEAR(a.h, 800 - 128 - 96, 1e-9);
    std::vector<md::MissionRect> rects{{0, 0, 10, 10}, {20, 0, 10, 10}};
    CHECK(md::missionHit(rects, 25, 5) == 1);
    CHECK(md::missionHit(rects, 15, 5) == -1);
    const md::MissionRect m = md::lerpRect({0, 0, 10, 10}, {10, 20, 30, 40}, 0.5);
    CHECK_NEAR(m.x, 5, 1e-9);
    CHECK_NEAR(m.h, 25, 1e-9);
    CHECK_NEAR(md::easeOut(0), 0, 1e-9);
    CHECK_NEAR(md::easeOut(1), 1, 1e-9);
    CHECK(md::easeOut(0.5) > 0.5);
    CHECK_NEAR(md::easeOut(2), 1, 1e-9);
}

TEST_CASE(mission_hotkey_parse) {
    auto a = md::parseMissionHotkey(L"ctrl+alt+up");
    REQUIRE(a.has_value());
    CHECK(a->mods == (MOD_CONTROL | MOD_ALT) && a->vk == VK_UP);
    auto b = md::parseMissionHotkey(L"Ctrl+Up");
    REQUIRE(b.has_value());
    CHECK(b->mods == MOD_CONTROL && b->vk == VK_UP);
    auto f = md::parseMissionHotkey(L"f3");
    REQUIRE(f.has_value());
    CHECK(f->mods == 0 && f->vk == VK_F3);
    CHECK(!md::parseMissionHotkey(L"off"));
    CHECK(!md::parseMissionHotkey(L"x"));
}
```

- [ ] **Step 2-4:** rouge ; `missionLayout` : tailles nulles prises comme 1 × 1 ; tri par centre vertical, `r` lignes de 1 à n réparties à parts égales (élément k → ligne `k * r / n`), chaque ligne triée par centre horizontal ; échelle commune `s = min(1, (area.w - gaps de la ligne la plus large) / somme des largeurs, (area.h - gaps verticaux) / somme des hauteurs max des lignes)` ; on garde le `r` à la plus grande échelle ; lignes centrées horizontalement, bloc centré verticalement, chaque fenêtre centrée dans la hauteur de sa ligne ; `missionArea` : marges 48/64/48/48 pt × échelle ; `easeOut(t) = 1 - (1 - clamp(t))^3` ; vert ; commit `feat(mission): rangement des fenêtres`.

### Task 2: Vue et rendu hors écran

**Files:** Create `src/mission/mission_view.h/.cpp` ; Modify `src/app/main.cpp`, `src/app/cli_args.cpp` ; Test `tests/test_mission.cpp`, `tests/test_cli_args.cpp`

**Interfaces:**
- Consumes : Task 1 ; `MenuWindow::Env`, `readPng`, `resizeBgra`, `tahoeWallpaper`, `forceForeground`.
- Produces : `class MissionView { public: struct Window { HWND hwnd = nullptr; std::wstring title; }; struct Request { std::vector<Window> windows; }; static std::optional<HWND> track(const MenuWindow::Env&, const Request&); static bool isOpen(); static void closeOpen(); };` (nullopt : fermée sans choix ou impossible) ; `BgraImage missionSnapshot(const std::vector<MissionRect>& windows, bool dark, int width, int height, int hover);`

- [ ] **Step 1: tests (rouges)** :

```cpp
TEST_CASE(mission_snapshot_draws_windows) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::vector<md::MissionRect> wins{{100, 100, 800, 600}, {900, 200, 700, 500}, {300, 500, 600, 400}};
    auto none = md::missionSnapshot({}, false, 1280, 800, -1);
    auto some = md::missionSnapshot(wins, false, 1280, 800, -1);
    auto hover = md::missionSnapshot(wins, false, 1280, 800, 0);
    REQUIRE(some.w == 1280 && some.h == 800);
    auto rects = md::missionLayout(wins, md::missionArea({0, 0, 1280, 800}, 1), md::kMissionGap);
    const md::MissionRect& r = rects[0];
    const std::size_t c = (std::size_t(r.y + r.h / 2) * 1280 + std::size_t(r.x + r.w / 2)) * 4;
    CHECK(some.px[c] != none.px[c]);
    CHECK(hover.px != some.px);
    CoUninitialize();
}
```

et dans `tests/test_cli_args.cpp` : `CHECK(md::diagnosticMissingValue(V{L"MacDock.exe", L"--mission-snapshot"}) == L"--mission-snapshot");`

- [ ] **Step 2-4:** rouge ; une vue par écran (`EnumDisplayMonitors`), fenêtre `WS_POPUP`, `WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP` sur tout l'écran ; fond : fond d'écran de l'écran (`IDesktopWallpaper` : chemin dont le rectangle d'écran correspond), lu par `readPng`, mis à l'échelle « remplir » ; sinon `tahoeWallpaper` ; voile noir 0,22 ; miniatures `DwmRegisterThumbnail` enregistrées du bas au haut de l'ordre Z (la plus haute dessus), `rcDestination` = `lerpRect(place réelle, place rangée, easeOut(t))`, mis à jour à chaque image ; survol : contour bleu `#0A84FF` de 3 pt à 4 pt du bord, pastille du titre sous la fenêtre ; clic sur une fenêtre : choix, animation de retour, fermeture, la fenêtre est renvoyée ; clic dans le vide, Échap : animation de retour, fermeture ; `WA_INACTIVE` vers une fenêtre qui n'est pas une de nos vues : fermeture immédiate ; fenêtre source fermée (`IsWindow` faux) : miniature retirée, place ignorée au clic ; `closeOpen()` lance la fermeture ; `missionSnapshot` : fond Tahoe, voile, rectangles arrondis colorés à la place des miniatures, même survol ; `MacDock.exe --mission-snapshot f.png [--count n] [--hover i] [--theme dark]` (fenêtres factices déterministes) ; vert ; commit `feat(mission): vue Mission Control`.

### Task 3: Branchements et documentation

**Files:** Modify `src/config/settings.h/.cpp`, `src/app/dock_window.h/.cpp` ; `README.md`, `docs/journal-de-nuit.md` ; Test `tests/test_config.cpp`

- [ ] **Step 1: test (rouge)** :

```cpp
TEST_CASE(settings_mission_hotkey) {
    CHECK(md::settingsFromJson(*md::json::parse("{}")).missionControlHotkey == L"ctrl+alt+up");
    CHECK(md::settingsFromJson(*md::json::parse("{\"missionControlHotkey\":\"F3\"}")).missionControlHotkey == L"f3");
    CHECK(md::settingsFromJson(*md::json::parse("{\"missionControlHotkey\":\"off\"}")).missionControlHotkey == L"off");
    CHECK(md::settingsFromJson(*md::json::parse("{\"missionControlHotkey\":\"bizarre\"}")).missionControlHotkey == L"ctrl+alt+up");
    md::Settings s;
    s.missionControlHotkey = L"ctrl+up";
    CHECK(md::settingsFromJson(md::settingsToJson(s)).missionControlHotkey == L"ctrl+up");
}
```

- [ ] **Step 2-4:** rouge ; `Settings::missionControlHotkey` ; `DockApp` : `kHotMission`, enregistrement au démarrage et à chaque changement (comme Spotlight, échec journalisé), message `RegisterWindowMessageW(L"MacDockMissionControl")` ; `openMissionControl()` : vue ouverte → `closeOpen()` ; autre fenêtre modale → rien ; sinon fenêtres des apps du Dock visibles, non réduites, non masquées par DWM (`DWMWA_CLOAKED`), sur le bureau virtuel courant ; `track` ; fenêtre renvoyée → `activateApp({h})` ; vert.
- [ ] **Step 5:** README (Utilisation, Réglages, Diagnostic), journal ; commits `feat(mission): Mission Control dans le Dock` puis `docs: Mission Control (plan 15)`.
