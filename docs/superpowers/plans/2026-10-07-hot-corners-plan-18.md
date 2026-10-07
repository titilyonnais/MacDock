# Coins actifs — plan 18 (sous-projet 11)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** coins actifs de macOS : pousser le pointeur dans un coin lance Mission Control, le bureau, Apps, le Centre de notifications, le verrouillage, la veille de l'écran ou l'économiseur.

**Architecture:** logique pure testée (`src/interact/hot_corners`) : coins réels du bureau, suivi d'entrée et de réarmement, réglage ; le Dock la nourrit depuis `onMouse` (crochet existant) et lance l'action.

**Tech Stack:** C++20, Win32, Shell (`IShellDispatch4`), MSVC ; tests `tests/minitest.h`.

**Spec:** `docs/superpowers/specs/2026-10-07-hot-corners-design.md`

## Global Constraints

- Zone du coin : 2 px ; réarmement : 24 px ; bloqué bouton enfoncé, en plein écran, pendant une vue modale (sauf Mission Control ouvert).
- `hotCorners` : `topLeft`, `topRight`, `bottomLeft`, `bottomRight` ∈ {`off`, `missionControl`, `desktop`, `apps`, `notificationCenter`, `lockScreen`, `displaySleep`, `screenSaver`} ; défaut `bottomRight` = `desktop`, les autres `off`.
- Aucun essai ne déplace le pointeur ni ne lance d'action.

## Review Focus

1. Deux écrans de tailles ou de hauteurs différentes : seuls les coins où le pointeur bute horizontalement et verticalement comptent.
2. Glisser une fenêtre ou un fichier jusqu'au coin : rien.
3. Jeu ou vidéo en plein écran : rien.
4. Rester dans le coin : une seule action ; ressortir de quelques pixels seulement : pas de nouvelle action.
5. Réglage invalide (valeur inconnue, mauvais type) : le coin garde son défaut, le Dock démarre.

---

### Task 1: Logique pure et réglage

**Files:** Create `src/interact/hot_corners.h/.cpp` ; Modify `src/config/settings.h/.cpp` ; Test `tests/test_hot_corners.cpp`, `tests/test_config.cpp`

**Interfaces:**
- Produces : `enum class Corner { TopLeft, TopRight, BottomLeft, BottomRight };` `enum class HotCornerAction { Off, MissionControl, Desktop, Apps, NotificationCenter, LockScreen, DisplaySleep, ScreenSaver };` `std::optional<HotCornerAction> parseHotCornerAction(const std::wstring&);` `std::wstring hotCornerName(HotCornerAction);` `std::optional<Corner> cornerAt(POINT pt, const std::vector<RECT>& monitors);` `class HotCornerTracker { std::optional<Corner> update(std::optional<Corner> at, POINT pt, bool blocked); };` `Settings::hotCorners` (`std::array<HotCornerAction, 4>`, indexé par `Corner`).

- [ ] **Step 1: tests (rouges)** :

```cpp
// Coins actifs : coins réels du bureau, suivi, réglage.
#include <windows.h>

#include <vector>

#include "minitest.h"
#include "../src/interact/hot_corners.h"

using md::Corner;

TEST_CASE(hot_corners_single_screen) {
    const std::vector<RECT> mons{{0, 0, 1920, 1080}};
    CHECK(md::cornerAt({0, 0}, mons) == Corner::TopLeft);
    CHECK(md::cornerAt({1, 1}, mons) == Corner::TopLeft);   // zone de 2 px
    CHECK(!md::cornerAt({2, 0}, mons));
    CHECK(md::cornerAt({1919, 0}, mons) == Corner::TopRight);
    CHECK(md::cornerAt({0, 1079}, mons) == Corner::BottomLeft);
    CHECK(md::cornerAt({1918, 1078}, mons) == Corner::BottomRight);
    CHECK(!md::cornerAt({960, 1079}, mons));
}

TEST_CASE(hot_corners_two_screens) {
    // Écran de droite plus petit, aligné en haut : seuls comptent les coins où le pointeur bute dans les deux sens.
    const std::vector<RECT> mons{{0, 0, 1920, 1080}, {1920, 0, 1920 + 1280, 720}};
    CHECK(!md::cornerAt({1919, 0}, mons));   // haut droit du premier : l'autre écran est à côté
    CHECK(!md::cornerAt({1920, 0}, mons));   // haut gauche du second
    CHECK(!md::cornerAt({1920, 719}, mons));   // bas gauche du second : vers la gauche, le pointeur passe au premier
    CHECK(md::cornerAt({3199, 719}, mons) == Corner::BottomRight);
    CHECK(md::cornerAt({1919, 1079}, mons) == Corner::BottomRight);
    CHECK(md::cornerAt({3199, 0}, mons) == Corner::TopRight);
}

TEST_CASE(hot_corners_tracker_once_and_rearm) {
    md::HotCornerTracker t;
    CHECK(t.update(Corner::TopLeft, {0, 0}, false) == Corner::TopLeft);
    CHECK(!t.update(Corner::TopLeft, {1, 0}, false));   // toujours dedans : une seule fois
    CHECK(!t.update(std::nullopt, {10, 10}, false));    // sorti de peu : pas réarmé
    CHECK(!t.update(Corner::TopLeft, {0, 0}, false));
    CHECK(!t.update(std::nullopt, {40, 40}, false));    // loin : réarmé
    CHECK(t.update(Corner::TopLeft, {0, 0}, false) == Corner::TopLeft);
}

TEST_CASE(hot_corners_tracker_blocked) {
    md::HotCornerTracker t;
    CHECK(!t.update(Corner::BottomRight, {1919, 1079}, true));   // glisser, plein écran : rien
    CHECK(!t.update(Corner::BottomRight, {1919, 1079}, false));  // arrivé bloqué : il faut ressortir
    CHECK(!t.update(std::nullopt, {1800, 900}, false));
    CHECK(t.update(Corner::BottomRight, {1919, 1079}, false) == Corner::BottomRight);
}

TEST_CASE(hot_corners_parse) {
    CHECK(md::parseHotCornerAction(L"missionControl") == md::HotCornerAction::MissionControl);
    CHECK(md::parseHotCornerAction(L"DESKTOP") == md::HotCornerAction::Desktop);
    CHECK(md::parseHotCornerAction(L"off") == md::HotCornerAction::Off);
    CHECK(!md::parseHotCornerAction(L"launchpad"));
    for (auto a : {md::HotCornerAction::Off, md::HotCornerAction::Apps, md::HotCornerAction::ScreenSaver})
        CHECK(md::parseHotCornerAction(md::hotCornerName(a)) == a);
}
```

Dans `tests/test_config.cpp` :

```cpp
TEST_CASE(settings_hot_corners) {
    md::Settings d;
    CHECK(d.hotCorners[int(md::Corner::BottomRight)] == md::HotCornerAction::Desktop);
    CHECK(d.hotCorners[int(md::Corner::TopLeft)] == md::HotCornerAction::Off);
    auto v = md::json::parse(R"({"hotCorners": {"topLeft": "missionControl", "bottomRight": "nope", "topRight": 3}})");
    REQUIRE(v.has_value());
    auto s = md::settingsFromJson(*v);
    CHECK(s.hotCorners[int(md::Corner::TopLeft)] == md::HotCornerAction::MissionControl);
    CHECK(s.hotCorners[int(md::Corner::BottomRight)] == md::HotCornerAction::Desktop);   // inconnu : défaut
    CHECK(s.hotCorners[int(md::Corner::TopRight)] == md::HotCornerAction::Off);
    auto back = md::settingsFromJson(md::settingsToJson(s));
    CHECK(back.hotCorners == s.hotCorners);
}
```

- [ ] **Step 2:** échec de compilation. **Step 3:** implémenter (`cornerAt` : pour chaque écran, point dans la zone de 2 px d'un coin, et le point en diagonale vers l'extérieur — coin haut gauche : `(left−1, top−1)`, et aussi `(left−1, top)` et `(left, top−1)` — sur aucun écran ; suivi : `armed` vrai au départ ; dans un coin et armé et non bloqué → déclenche et désarme ; dans un coin bloqué → désarme ; hors coin → réarme si le point est à plus de 24 px (horizontalement et verticalement) du dernier coin touché ; réglage lu dans l'objet `hotCorners`, valeurs inconnues ignorées, écrit en entier).
- [ ] **Step 4:** tests verts, suite verte. **Step 5:** commit `feat(corners): logique des coins actifs et réglage`.

### Task 2: Branchements et documentation

**Files:** Modify `src/app/dock_window.h/.cpp` ; `README.md`, `docs/journal-de-nuit.md`

- [ ] **Step 1-3:** `DockApp::onMouse` : écrans lus à chaque changement d'affichage (`enumMonitors`, gardés en cache), `cornerAt`, `HotCornerTracker::update` avec `blocked` = bouton gauche ou droit enfoncé (`GetAsyncKeyState`), plein écran (`fullscreen_`), ou vue modale ouverte sauf Mission Control quand l'action est Mission Control ; action ≠ `off` → posté à la fenêtre du Dock (`WM_APP_CORNER`, wParam = action) pour ne pas agir dans le crochet ; `runHotCorner` : Mission Control (`openMissionControl`), Bureau (`IShellDispatch4::ToggleDesktop`), Apps (`openApps`), Centre de notifications (`ShellExecuteW(L"ms-actioncenter:")`), verrouillage (`LockWorkStation`), veille de l'écran (`PostMessage(hwnd_, WM_SYSCOMMAND, SC_MONITORPOWER, 2)`), économiseur (`PostMessage(hwnd_, WM_SYSCOMMAND, SC_SCREENSAVE, 0)`) ; journalisé ; suite verte.
- [ ] **Step 4:** README (Utilisation, Réglages), journal ; commits `feat(corners): coins actifs dans le Dock` puis `docs: coins actifs (plan 18)`.
