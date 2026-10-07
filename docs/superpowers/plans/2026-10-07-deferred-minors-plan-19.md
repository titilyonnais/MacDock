# Mineurs reportés — plan 19

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** corriger les mineurs reportés des plans 14 à 18 qui se voient à l'usage courant.

**Architecture:** petites corrections locales ; ce qui est calculable passe par une fonction pure testée (`spot_calc`, `spot_results`, `hot_corners`), le reste est de la liaison Win32 vérifiée par compilation.

**Tech Stack:** C++20, Win32, MSVC ; tests `tests/minitest.h`.

**Spec:** `docs/journal-de-nuit.md`, sections « Mineurs reportés » des plans 14 à 18 (chaque ligne décrit le défaut et ce qu'on attend).

## Global Constraints

- Aucun changement de comportement hors des défauts listés ; aucun essai ne lance d'app, n'affiche de fenêtre, ne règle le volume ou ne déplace le pointeur.

## Review Focus

1. `-2^2`, `2^-1`, `-(2)^2`, `--2` : priorités comme une calculatrice (moins unaire après la puissance).
2. Émoji ou caractère hors du plan de base (paire de substitution) : jamais coupé par Retour arrière ni par la limite de 128 caractères du collage.
3. Écrans décalés d'un pixel, ou décalés verticalement : un coin n'est compté que si le pointeur y bute vraiment.
4. Pastille du HUD affichée puis cachée : la couleur du texte de la barre est relevée de nouveau si un relevé a été interrompu.
5. Alt+Tab sans aucune app : rien ne s'ouvre, ni panneau ni menu de l'app au premier plan.

---

### Task 1: Corrections calculables (tests)

**Files:** Modify `src/spotlight/spot_calc.cpp`, `src/spotlight/spot_results.h/.cpp`, `src/spotlight/spotlight_window.cpp`, `src/interact/hot_corners.cpp` ; Test `tests/test_spotlight.cpp` (ou le fichier des tests de Spotlight), `tests/test_hot_corners.cpp`

**Interfaces:**
- Produces : `void spotEraseLast(std::wstring& query);` (dernier caractère, paire de substitution entière) ; `std::wstring spotPasteLine(const std::wstring& clip);` (première ligne, tabulations en espaces, 128 unités au plus sans couper une paire).

- [ ] **Step 1: tests (rouges)** :

```cpp
TEST_CASE(spot_calc_unary_minus_after_power) {
    CHECK(*md::evaluateExpression(L"-2^2") == -4);
    CHECK(*md::evaluateExpression(L"2^-1") == 0.5);
    CHECK(*md::evaluateExpression(L"(-2)^2") == 4);
    CHECK(*md::evaluateExpression(L"--2") == 2);
}

TEST_CASE(spot_erase_last_keeps_surrogates) {
    std::wstring q = L"a\U0001F600";
    md::spotEraseLast(q);
    CHECK(q == L"a");
    md::spotEraseLast(q);
    CHECK(q.empty());
    md::spotEraseLast(q);
    CHECK(q.empty());
}

TEST_CASE(spot_paste_line) {
    CHECK(md::spotPasteLine(L"a\tb\r\nc") == L"a b");
    const std::wstring longText = std::wstring(127, L'x') + L"\U0001F600";
    CHECK(md::spotPasteLine(longText).size() == 127);   // la paire ne tient pas : écartée entière
}
```

Dans `tests/test_hot_corners.cpp` :

```cpp
TEST_CASE(hot_corners_offset_screens) {
    // Second écran décalé d'un pixel vers le bas : en y = 1, le pointeur passe à droite sur lui.
    const std::vector<RECT> mons{{0, 0, 1920, 1080}, {1920, 1, 3840, 1081}};
    CHECK(!md::cornerAt({1919, 1}, mons));
    CHECK(md::cornerAt({1919, 0}, mons) == Corner::TopRight);   // en y = 0, rien à droite : il bute
}

TEST_CASE(hot_corners_rearm_along_edge) {
    md::HotCornerTracker t;
    CHECK(t.update(Corner::BottomLeft, {0, 1079}, false) == Corner::BottomLeft);
    CHECK(!t.update(std::nullopt, {30, 1079}, false));   // glissé le long du bord : réarmé
    CHECK(t.update(Corner::BottomLeft, {0, 1079}, false) == Corner::BottomLeft);
}
```

- [ ] **Step 2:** échecs (fonctions absentes, `-2^2` = 4, coin décalé accepté). **Step 3:** moins unaire qui lit une puissance (`-` → `-power()`), `spotEraseLast` et `spotPasteLine` utilisés par Spotlight, coin : les voisins du point lui-même aussi hors écran. **Step 4:** verts, suite verte. **Step 5:** commit `fix: calcul, saisie de Spotlight et coins décalés`.

### Task 2: Corrections de liaison

**Files:** Modify `src/app/dock_window.cpp`, `src/menubar/menubar_window.h/.cpp` ; `docs/journal-de-nuit.md`

- [ ] **Step 1:** Alt+Tab : touche neutre envoyée avant de tester s'il y a des apps ; au panneau, les apps sorties du modèle sont retirées de la session ; raccourcis temporaires refusés journalisés (une fois par session).
- [ ] **Step 2:** HUD : relevé du fond interrompu par la pastille relancé quand elle se cache ; `hud` à `false` cache toujours la pastille ; `WM_HOTKEY` d'identifiant inconnu ignoré ; minuterie du fondu armée seulement à la fin du maintien ; sortie par défaut disparue pendant l'affichage → pastille cachée.
- [ ] **Step 3:** coins : économiseur sans économiseur réglé → journalisé.
- [ ] **Step 4:** suite verte ; journal (mineurs corrigés retirés des listes « reportés ») ; commits `fix: Alt+Tab, HUD et coins (mineurs reportés)` puis `docs: mineurs corrigés (plan 19)`.
