# Spotlight — plan 14

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Alt+Espace (ou la loupe de la barre de menus) ouvre un Spotlight en verre : apps, documents indexés et calculs.

**Architecture:** Logique pure testée (calcul, résultats, URL de recherche, raccourci) dans `src/spotlight/` ; recherche de documents par le dossier Shell `search-ms:` dans un fil à part ; panneau modal sur le modèle de `AppsWindow`, dessin partagé avec un rendu hors écran.

**Tech Stack:** C++20, MSVC, Win32, COM Shell, Direct2D/DirectWrite, D3D11, DirectComposition.

**Spec:** `docs/superpowers/specs/2026-10-07-spotlight-design.md`

## Global Constraints

- Aucune ressource Apple ; textes en français.
- Aucun essai n'ouvre le panneau devant l'utilisateur, ne pilote souris ou clavier, ne lance d'app, n'écrit dans le presse-papiers. Vérification à l'œil par `--spotlight-snapshot`.
- `src\spotlight\*.cpp` rejoint `LogicSources` et la cible `dock` dans `build.ps1`.
- Un exécutable n'est lancé que si la compilation a réussi.

## Review Focus

1. Expressions limites : `2^3^2` (associatif à droite = 512), `-(2)`, `50%`, `1,5+1`, `3/0`, `((`, `2024`, texte.
2. Recherche de documents lente ou vide : le panneau reste réactif, un résultat d'une ancienne frappe ne remplace pas celui de la nouvelle.
3. Raccourci déjà pris par une autre app : le Dock démarre quand même, c'est journalisé.
4. Deuxième Alt+Espace ou loupe pendant que le panneau est ouvert : fermeture, pas de second panneau.
5. Requête avec `&`, `=`, `%`, `#`, guillemets : l'URL `search-ms:` reste valide.

---

### Task 1: Calcul

**Files:** Create `src/spotlight/spot_calc.h/.cpp` ; Modify `build.ps1` ; Test `tests/test_spotlight.cpp`

**Interfaces:**
- Produces : `std::optional<double> evaluateExpression(std::wstring_view text)` ; `std::wstring formatNumber(double v)`.

- [ ] **Step 1: tests (rouges)**

```cpp
TEST_CASE(spot_calc_evaluates) {
    auto e = [](const wchar_t* s) { return md::evaluateExpression(s); };
    CHECK(e(L"12*(3+4)") == 84.0);
    CHECK(e(L"1+2*3") == 7.0);
    CHECK(e(L" 2 ^ 3 ^ 2 ") == 512.0);     // puissance associative à droite
    CHECK(e(L"-(2)+5") == 3.0);
    CHECK(e(L"50%") == 0.5);
    CHECK(e(L"200*15%") == 30.0);
    CHECK(e(L"1,5+1") == 2.5);
    CHECK(e(L"7÷2") == 3.5);
    CHECK(e(L"3×4") == 12.0);
    CHECK(e(L"10-2-3") == 5.0);             // associatif à gauche
    CHECK(!e(L"3/0"));
    CHECK(!e(L"(("));
    CHECK(!e(L"2024"));                      // un nombre seul n'est pas un calcul
    CHECK(!e(L"-5"));
    CHECK(!e(L"calc"));
    CHECK(!e(L"2+"));
    CHECK(!e(L""));
}

TEST_CASE(spot_calc_formats_french) {
    CHECK(md::formatNumber(84) == L"84");
    CHECK(md::formatNumber(2.5) == L"2,5");
    CHECK(md::formatNumber(1234.5) == L"1\u202F234,5");
    CHECK(md::formatNumber(-1234567) == L"-1\u202F234\u202F567");
    CHECK(md::formatNumber(1.0 / 3) == L"0,3333333333");
    CHECK(md::formatNumber(0.1 + 0.2) == L"0,3");
    CHECK(md::formatNumber(1e20) == L"1e+20");
}
```

- [ ] **Step 2-4:** rouge ; analyseur récursif (expr := terme (± terme)* ; terme := facteur (×÷*/ facteur)* ; facteur := unaire (^ facteur)? ; unaire := - unaire | primaire %* ; primaire := nombre | ( expr )) ; au moins un opérateur binaire ou `%` ; résultat fini ; format : 10 chiffres significatifs, zéros finaux retirés, notation scientifique au-delà de 1e15, séparateur des milliers U+202F ; vert ; commit `feat(spotlight): calcul`.

### Task 2: Résultats, URL de recherche et raccourci

**Files:** Create `src/spotlight/spot_results.h/.cpp` ; Test `tests/test_spotlight.cpp`

**Interfaces:**
- Consumes : `AppEntry`, `searchApps`, `launchTarget` ; `evaluateExpression`, `formatNumber`.
- Produces : `enum class SpotKind { Calc, App, File };` `struct SpotItem { SpotKind kind = SpotKind::App; std::wstring title, subtitle, target; };` `struct SpotSection { std::wstring title; std::vector<SpotItem> items; };` `std::vector<SpotSection> spotlightResults(const std::wstring& query, const std::vector<AppEntry>& apps, const std::vector<SpotItem>& files);` `std::size_t spotCount(const std::vector<SpotSection>&);` `const SpotItem* spotAt(const std::vector<SpotSection>&, std::size_t);` `std::wstring searchMsUrl(const std::wstring& query, const std::wstring& folder);` `struct HotkeySpec { UINT mods = 0; UINT vk = 0; };` `std::optional<HotkeySpec> parseSpotlightHotkey(const std::wstring&);`

- [ ] **Step 1: tests (rouges)**

```cpp
TEST_CASE(spot_results_sections) {
    std::vector<md::AppEntry> apps{{L"Calculatrice", L"calc!App"}, {L"Calendrier", L"cal!App"}, {L"Paint", L"paint!App"}};
    std::vector<md::SpotItem> files{{md::SpotKind::File, L"calcul.xlsx", L"C:\\Users\\x\\Documents", L"C:\\Users\\x\\Documents\\calcul.xlsx"}};
    auto r = md::spotlightResults(L"cal", apps, files);
    REQUIRE(r.size() == 3);
    CHECK(r[0].title == L"Meilleur résultat" && r[0].items.size() == 1 && r[0].items[0].title == L"Calculatrice");
    CHECK(r[0].items[0].target == L"shell:AppsFolder\\calc!App");
    CHECK(r[1].title == L"Applications" && r[1].items.size() == 1 && r[1].items[0].title == L"Calendrier");
    CHECK(r[2].title == L"Documents" && r[2].items.size() == 1);
    CHECK(md::spotCount(r) == 3);
    CHECK(md::spotAt(r, 2)->kind == md::SpotKind::File);
    CHECK(md::spotAt(r, 3) == nullptr);
    auto c = md::spotlightResults(L"12*(3+4)", apps, {});
    REQUIRE(!c.empty());
    CHECK(c[0].items[0].kind == md::SpotKind::Calc && c[0].items[0].title == L"84");
    CHECK(c[0].items[0].subtitle == L"12*(3+4) =");
    CHECK(md::spotlightResults(L"   ", apps, files).empty());
    CHECK(md::spotlightResults(L"zzz", apps, {}).empty());
    std::vector<md::AppEntry> many;
    for (int i = 0; i < 20; ++i) many.push_back({L"App " + std::to_wstring(i), L"a" + std::to_wstring(i)});
    auto m = md::spotlightResults(L"app", many, {});
    CHECK(m.size() == 2 && m[1].items.size() == 6);   // 1 meilleur + 6 applications
}

TEST_CASE(spot_search_ms_url) {
    CHECK(md::searchMsUrl(L"rapport", L"C:\\Users\\x") ==
          L"search-ms:query=rapport&crumb=location:C%3A%5CUsers%5Cx");
    CHECK(md::searchMsUrl(L"a&b=c%#\"d e", L"C:\\U") ==
          L"search-ms:query=a%26b%3Dc%25%23%22d%20e&crumb=location:C%3A%5CU");
    CHECK(md::searchMsUrl(L"école", L"C:\\U").find(L"%C3%A9cole") != std::wstring::npos);   // UTF-8 encodé
}

TEST_CASE(spot_hotkey_parse) {
    auto a = md::parseSpotlightHotkey(L"alt+space");
    REQUIRE(a.has_value());
    CHECK(a->mods == MOD_ALT && a->vk == VK_SPACE);
    auto c = md::parseSpotlightHotkey(L"Ctrl+Space");
    REQUIRE(c.has_value());
    CHECK(c->mods == MOD_CONTROL && c->vk == VK_SPACE);
    CHECK(!md::parseSpotlightHotkey(L"off"));
    CHECK(!md::parseSpotlightHotkey(L"win+space"));
    CHECK(!md::parseSpotlightHotkey(L""));
}
```

- [ ] **Step 2-4:** rouge ; implémentation (spec §2 « Ordre » ; titre d'un document = nom, sous-titre = dossier ; URL : caractères ASCII alphanumériques et `-_.~` gardés, le reste encodé en UTF-8 `%XX` majuscules) ; vert ; commit `feat(spotlight): résultats, URL de recherche et raccourci`.

### Task 3: Recherche de documents

**Files:** Create `src/spotlight/file_search.h/.cpp` ; Test `tests/test_spotlight.cpp`

**Interfaces:**
- Consumes : `searchMsUrl`, `SpotItem`.
- Produces : `std::vector<SpotItem> searchFiles(const std::wstring& query, const std::wstring& folder, std::size_t max);` (COM par l'appelant ; vide si requête vide) ; `class FileSearcher { public: ~FileSearcher(); void request(const std::wstring& query, HWND notify, UINT msg); static std::vector<SpotItem> take(WPARAM, LPARAM, unsigned& generation); unsigned generation() const; }` (une recherche à la fois ; la dernière demande attend la fin de la précédente ; résultat posté avec sa génération).

- [ ] **Step 1: test (rouge)** — lecture seule :

```cpp
TEST_CASE(spot_file_search_real_readonly) {   // index de Windows, lecture seule ; peut être vide
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    wchar_t profile[MAX_PATH] = {};
    GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
    const ULONGLONG t0 = GetTickCount64();
    auto r = md::searchFiles(L"desktop", profile, 5);
    CHECK(r.size() <= 5);
    for (auto& f : r) CHECK(f.kind == md::SpotKind::File && !f.target.empty() && !f.title.empty());
    CHECK(md::searchFiles(L"", profile, 5).empty());
    CHECK(GetTickCount64() - t0 < 20000);
    CoUninitialize();
}
```

- [ ] **Step 2-4:** rouge ; `SHCreateItemFromParsingName(searchMsUrl(...))`, `BindToHandler(BHID_EnumItems)`, `SIGDN_NORMALDISPLAY`, `SIGDN_FILESYSPATH` (éléments sans chemin ignorés), sous-titre = dossier parent ; `FileSearcher` : fil `std::thread`, COM propre, file d'une demande en attente ; vert ; commit `feat(spotlight): recherche de documents`.

### Task 4: Panneau et rendu hors écran

**Files:** Create `src/spotlight/spotlight_window.h/.cpp` ; Modify `src/app/main.cpp` (`--spotlight-snapshot`), `src/app/cli_args.cpp` (option avec valeur) ; Test `tests/test_spotlight.cpp`, `tests/test_cli_args.cpp`

**Interfaces:**
- Consumes : tout ce qui précède ; `MenuWindow::Env`, `GlassRenderer`, `ScreenBackdrop`, `IconProvider`, `tahoeWallpaper`.
- Produces : `class SpotlightWindow { public: struct Request { std::vector<AppEntry> apps; HMONITOR monitor = nullptr; std::wstring profile; }; struct Choice { SpotItem item; bool reveal = false; }; static std::optional<Choice> track(const MenuWindow::Env&, const Request&); static void closeOpen(); };` (nullopt : fermé sans choix ou ouverture impossible) ; `BgraImage spotlightSnapshot(const std::wstring& query, const std::vector<AppEntry>& apps, const std::vector<SpotItem>& files, bool dark, int width, int height);`

- [ ] **Step 1: tests (rouges)** :

```cpp
TEST_CASE(spot_snapshot_draws_offscreen) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::vector<md::AppEntry> apps{{L"Calculatrice", L"calc"}, {L"Calendrier", L"cal"}};
    auto empty = md::spotlightSnapshot(L"", apps, {}, false, 1280, 800);
    REQUIRE(empty.w == 1280 && empty.h == 800);
    auto withResults = md::spotlightSnapshot(L"cal", apps, {}, false, 1280, 800);
    CHECK(withResults.px != empty.px);
    const std::size_t below = (std::size_t(800 * 0.22 + 52 + 60) * 1280 + 640) * 4;   // sous le champ : liste
    CHECK(withResults.px[below] != empty.px[below]);
    CoUninitialize();
}
```

et dans `tests/test_cli_args.cpp` : `CHECK(md::diagnosticMissingValue(V{L"MacDock.exe", L"--spotlight-snapshot"}) == L"--spotlight-snapshot");`

- [ ] **Step 2-4:** rouge ; un dessin partagé (champ : loupe, texte 22 pt ou « Recherche Spotlight » grisé, curseur ; sections : titre gris 11 pt, lignes 40 pt avec icône 28 pt, titre 14 pt, sous-titre gris 11 pt à droite ou sous le titre, ligne sélectionnée sur fond bleu système `#0A84FF` et texte blanc) ; `track` : fenêtre `WS_POPUP | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP` à la taille du panneau maximal (champ + 12 lignes + titres), `SetWindowRgn` au panneau affiché, verre `GlassRenderer` (paramètres des menus), `WM_CHAR`, `WM_KEYDOWN` (Échap, Retour arrière, haut, bas, Entrée, Ctrl+Entrée), clic sur une ligne, `WA_INACTIVE` → fermeture ; recherche de documents par `FileSearcher` 150 ms après la dernière frappe (minuterie) ; icônes : apps par `IconProvider::get`, documents par `IconProvider::fileIcon`, chargées dans un fil ; `closeOpen()` pose `done` sur la session ouverte ; `spotlightSnapshot` : fond d'écran Tahoe, panneau dépoli clair ou sombre, même dessin, pas d'icônes réelles (carrés de couleur) ; vert ; commit `feat(spotlight): panneau`.

### Task 5: Branchements et documentation

**Files:** Modify `src/config/settings.h/.cpp`, `src/app/dock_window.h/.cpp`, `src/menubar/menubar_window.cpp` ; `README.md`, `docs/journal-de-nuit.md` ; Test `tests/test_settings.cpp` (ou le fichier des réglages existant)

- [ ] **Step 1: test (rouge)** :

```cpp
TEST_CASE(settings_spotlight_hotkey) {
    md::Settings s = md::settingsFromJson(*md::json::parse("{\"spotlightHotkey\":\"ctrl+space\"}"));
    CHECK(s.spotlightHotkey == L"ctrl+space");
    CHECK(md::settingsFromJson(*md::json::parse("{}")).spotlightHotkey == L"alt+space");
    CHECK(md::settingsFromJson(*md::json::parse("{\"spotlightHotkey\":\"bizarre\"}")).spotlightHotkey == L"alt+space");
    auto back = md::settingsFromJson(md::settingsToJson(s));
    CHECK(back.spotlightHotkey == L"ctrl+space");
}
```

- [ ] **Step 2-4:** rouge ; `Settings::spotlightHotkey` (`alt+space` par défaut ; valeurs acceptées : `alt+space`, `ctrl+space`, `off`) ; `DockApp` : `RegisterHotKey(hwnd_, kHotSpotlight, mods | MOD_NOREPEAT, vk)` au démarrage hors `--snapshot` et à chaque changement de réglage (échec journalisé) ; `RegisterWindowMessageW(L"MacDockSpotlight")` ; `openSpotlight()` : si ouvert → `SpotlightWindow::closeOpen()` ; sinon catalogue (`apps_.get(500)`), écran du curseur, `pauseCapture`, `track`, action (lancer, ouvrir, révéler, copier dans le presse-papiers) ; barre de menus : loupe → `PostMessageW(FindWindowW(L"MacDockWindow"), msg)` sinon Win+S ; vert.
- [ ] **Step 5:** README (Utilisation, Réglages, Diagnostic), journal ; commit `feat(spotlight): Spotlight dans le Dock et la barre` puis `docs: Spotlight (plan 14)`.
