# Écran Apps — plan 13

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Le bouton Apps du Dock ouvre une vue plein écran en verre (recherche + grille paginée de toutes les apps du menu Démarrer) au lieu du menu Démarrer.

**Architecture:** Logique pure testée (catalogue, recherche, géométrie, clavier) dans `src/apps/` ; lecture du dossier Shell `shell:AppsFolder` dans un fil à part ; vue modale `AppsWindow` sur le modèle de `StackWindow` (DirectComposition + Direct2D + Liquid Glass), dont le dessin est partagé avec un rendu hors écran `--apps-snapshot`.

**Tech Stack:** C++20, MSVC, Win32, COM Shell, Direct2D/DirectWrite, D3D11, DirectComposition.

**Spec:** `docs/superpowers/specs/2026-10-07-apps-screen-design.md`

## Global Constraints

- Aucune ressource Apple ; textes en français.
- Aucun essai n'affiche la vue devant l'utilisateur, ne pilote la souris ou le clavier, ni ne lance d'app ; la vérification à l'œil passe par `--apps-snapshot`.
- `src\apps\*.cpp` rejoint `LogicSources` dans `build.ps1` (tests et Dock).
- Repli : si la vue ne peut pas s'ouvrir, ou si le catalogue est vide, le bouton ouvre le menu Démarrer comme avant.

## Review Focus

1. Recherche avec accents et majuscules (« école » trouve « École », « EDGE » trouve « Microsoft Edge ») et ordre des groupes (début du nom, début d'un mot, ailleurs).
2. Dernière page incomplète : flèches, clic dans une case vide, points de page.
3. Écran petit (1024 × 640 pt) ou très grand : la grille tient, les icônes restent entre 48 et 96 pt.
4. Catalogue pas encore lu au premier clic : on attend un peu, puis repli sur le menu Démarrer s'il est vide.
5. Une app qui disparaît entre la lecture et le clic : `launch` échoue proprement (journal), la vue est déjà fermée.

---

### Task 1: Catalogue et recherche

**Files:** Create `src/apps/app_catalog.h/.cpp` ; Modify `build.ps1` (`src\apps\*.cpp` dans `LogicSources`) ; Test `tests/test_apps.cpp`

**Interfaces:**
- Produces : `struct AppEntry { std::wstring name, parsingName; }` ; `std::wstring launchTarget(const AppEntry&)` ; `bool isListedApp(const AppEntry&)` ; `std::vector<AppEntry> catalogFrom(std::vector<AppEntry> raw)` ; `std::wstring foldForSearch(std::wstring_view)` ; `std::vector<std::size_t> searchApps(const std::vector<AppEntry>&, const std::wstring& query)`.

- [ ] **Step 1: tests (rouges)**

```cpp
TEST_CASE(apps_listed_filters_docs_and_uninstallers) {
    CHECK(md::isListedApp({L"Paint", L"Microsoft.Paint_8wekyb3d8bbwe!App"}));
    CHECK(md::isListedApp({L"Notepad++", L"C:\\Program Files\\Notepad++\\notepad++.exe"}));
    CHECK(!md::isListedApp({L"Uninstall Foo", L"C:\\Foo\\unins000.exe"}));
    CHECK(!md::isListedApp({L"Désinstaller Bar", L"C:\\Bar\\uninst.exe"}));
    CHECK(!md::isListedApp({L"Foo Help", L"C:\\Foo\\help.chm"}));
    CHECK(!md::isListedApp({L"Site web", L"C:\\Foo\\site.url"}));
    CHECK(!md::isListedApp({L"Lisez-moi", L"C:\\Foo\\README.TXT"}));
    CHECK(!md::isListedApp({L"", L"x.exe"}));
    CHECK(!md::isListedApp({L"Vide", L""}));
}

TEST_CASE(apps_catalog_sorted_deduplicated) {
    auto c = md::catalogFrom({{L"zoom", L"z"}, {L"Édition", L"e"}, {L"edge", L"m"}, {L"Calculatrice", L"c"},
                              {L"Calculatrice", L"c"}, {L"Aide", L"a.chm"}, {L"App 10", L"a10"}, {L"App 9", L"a9"}});
    REQUIRE(c.size() == 6);
    CHECK(c[0].name == L"App 9");      // chiffres comme des nombres
    CHECK(c[1].name == L"App 10");
    CHECK(c[2].name == L"Calculatrice");
    CHECK(c[3].name == L"edge");       // casse et accents ignorés : edge < Édition < zoom
    CHECK(c[4].name == L"Édition");
    CHECK(c[5].name == L"zoom");
    CHECK(md::launchTarget(c[2]) == L"shell:AppsFolder\\c");
}

TEST_CASE(apps_search_groups_and_folding) {
    std::vector<md::AppEntry> apps{{L"Calculatrice", L"1"}, {L"Microsoft Edge", L"2"}, {L"Éditeur du Registre", L"3"},
                                   {L"Paint", L"4"}, {L"Outil Capture d'écran", L"5"}, {L"Edge Dev", L"6"}};
    CHECK(md::searchApps(apps, L"").size() == 6);
    auto r = md::searchApps(apps, L"EDGE");
    REQUIRE(r.size() == 2);
    CHECK(apps[r[0]].name == L"Edge Dev");         // début du nom avant début d'un mot
    CHECK(apps[r[1]].name == L"Microsoft Edge");
    r = md::searchApps(apps, L"edit");
    REQUIRE(r.size() == 1);
    CHECK(apps[r[0]].name == L"Éditeur du Registre");   // accents ignorés
    r = md::searchApps(apps, L"cran");
    REQUIRE(r.size() == 1);
    CHECK(apps[r[0]].name == L"Outil Capture d'écran");  // ailleurs dans le nom
    r = md::searchApps(apps, L"  paint ");
    REQUIRE(r.size() == 1);                               // espaces autour ignorés
    CHECK(md::searchApps(apps, L"zzz").empty());
    CHECK(md::foldForSearch(L"ÉcOle Œuvre") == L"ecole œuvre");
}
```

- [ ] **Step 2-4:** rouge ; implémentation (spec §2 et §3.1 : extensions écartées `.chm .txt .pdf .htm .html .url .rtf .ini .log`, préfixes « uninstall », « désinstaller », « désinstallation » sur le nom replié ; tri `CompareStringEx(LOCALE_NAME_USER_DEFAULT, LINGUISTIC_IGNORECASE | NORM_IGNORENONSPACE | SORT_DIGITSASNUMBERS)` puis nom d'analyse ; `foldForSearch` : `FoldStringW(MAP_COMPOSITE)`, marques U+0300–U+036F retirées, `LCMapStringEx(LCMAP_LOWERCASE)` ; un mot commence après une espace, `-`, `(`, `.`, `_`, `'`) ; vert ; commit `feat(apps): catalogue et recherche`.

### Task 2: Géométrie et clavier

**Files:** Create `src/apps/apps_layout.h/.cpp` ; Test `tests/test_apps.cpp`

**Interfaces:**
- Produces : `struct AppsGeometry { int columns = 1, rows = 1, perPage = 1, pages = 1; double cellW = 0, cellH = 0, icon = 0, gridLeft = 0, gridTop = 0, searchTop = 0, searchW = 0, searchH = 0, dotsY = 0; };` ; `AppsGeometry appsLayout(double screenW, double screenH, std::size_t count)` ; `int appsHit(const AppsGeometry&, int page, double x, double y, std::size_t count)` ; `struct AppsCursor { int page = 0; int selected = -1; };` ; `AppsCursor appsKey(const AppsGeometry&, AppsCursor, UINT vk, std::size_t count)` ; `int pageOf(const AppsGeometry&, int index)`.

- [ ] **Step 1: tests (rouges)**

```cpp
TEST_CASE(apps_layout_large_and_small) {
    auto g = md::appsLayout(1920, 1080, 80);
    CHECK(g.columns == 7 && g.rows == 5 && g.perPage == 35 && g.pages == 3);
    CHECK(g.icon == 96);
    CHECK(g.searchTop > 0 && g.gridTop > g.searchTop + g.searchH);
    CHECK(g.gridLeft + g.columns * g.cellW <= 1920 - g.gridLeft + 0.01);
    CHECK(g.dotsY > g.gridTop + g.rows * g.cellH && g.dotsY < 1080);
    auto s = md::appsLayout(1024, 640, 80);
    CHECK(s.columns >= 4 && s.columns < 7 && s.rows >= 2 && s.rows < 5);
    CHECK(s.icon >= 48 && s.icon <= 96);
    CHECK(s.pages == int((80 + s.perPage - 1) / s.perPage));
    CHECK(md::appsLayout(1920, 1080, 0).pages == 1);
}

TEST_CASE(apps_hit_last_page) {
    auto g = md::appsLayout(1920, 1080, 37);   // page 2 : 2 apps
    const double cx = g.gridLeft + g.cellW / 2, cy = g.gridTop + g.cellH / 2;
    CHECK(md::appsHit(g, 0, cx, cy, 37) == 0);
    CHECK(md::appsHit(g, 0, cx + 2 * g.cellW, cy + g.cellH, 37) == 9);
    CHECK(md::appsHit(g, 1, cx + g.cellW, cy, 37) == 36);
    CHECK(md::appsHit(g, 1, cx + 2 * g.cellW, cy, 37) == -1);   // case vide
    CHECK(md::appsHit(g, 0, g.gridLeft - 5, cy, 37) == -1);
    CHECK(md::appsHit(g, 0, cx, g.gridTop - 5, 37) == -1);
}

TEST_CASE(apps_keys_move_across_pages) {
    auto g = md::appsLayout(1920, 1080, 37);
    md::AppsCursor c;
    c = md::appsKey(g, c, VK_RIGHT, 37);
    CHECK(c.selected == 0 && c.page == 0);          // première flèche : première app de la page
    c = md::appsKey(g, c, VK_DOWN, 37);
    CHECK(c.selected == 7);
    c = md::appsKey(g, c, VK_UP, 37);
    CHECK(c.selected == 0);
    c = md::appsKey(g, c, VK_UP, 37);
    CHECK(c.selected == 0);                          // déjà en haut
    c = md::appsKey(g, c, VK_LEFT, 37);
    CHECK(c.selected == 0);                          // déjà au début
    c = md::appsKey(g, {0, 34}, VK_RIGHT, 37);
    CHECK(c.selected == 35 && c.page == 1);          // passe à la page suivante
    c = md::appsKey(g, c, VK_DOWN, 37);
    CHECK(c.selected == 35);                         // rien en dessous
    c = md::appsKey(g, c, VK_RIGHT, 37);
    c = md::appsKey(g, c, VK_RIGHT, 37);
    CHECK(c.selected == 36);                         // dernière app
    c = md::appsKey(g, {0, -1}, VK_NEXT, 37);
    CHECK(c.page == 1 && c.selected == -1);          // page suivante sans sélection
    c = md::appsKey(g, {1, 36}, VK_PRIOR, 37);
    CHECK(c.page == 0 && c.selected == 0);           // page précédente : sa première app
    c = md::appsKey(g, {0, 5}, VK_END, 37);
    CHECK(c.page == 1 && c.selected == 36);
    c = md::appsKey(g, c, VK_HOME, 37);
    CHECK(c.page == 0 && c.selected == 0);
    CHECK(md::appsKey(g, {0, -1}, VK_RIGHT, 0).selected == -1);   // aucune app
}
```

- [ ] **Step 2-4:** rouge ; implémentation (spec §2 « Grille » : recherche à 44 pt du haut, 30 pt de haut, 280 pt de large ; grille de `searchTop + searchH + 40` à `H - 120`, marge latérale `max(80, W × 0,1)` ; colonnes = clamp(⌊largeur / 120⌋, 1, 7), lignes = clamp(⌊hauteur / 128⌋, 1, 5) ; icône = clamp(min(cellW × 0,6, cellH − 40), 48, 96) ; points à `H − 90`) ; vert ; commit `feat(apps): géométrie et clavier`.

### Task 3: Lecture du dossier Apps

**Files:** Create `src/apps/apps_folder.h/.cpp` ; Test `tests/test_apps.cpp`

**Interfaces:**
- Consumes : `AppEntry`, `catalogFrom`.
- Produces : `std::vector<AppEntry> readAppsFolder()` (brut, COM initialisé par l'appelant) ; `class AppCatalog { public: void refreshAsync(); std::vector<AppEntry> get(DWORD waitMs); ~AppCatalog(); }` (résultat déjà passé par `catalogFrom` ; un seul fil à la fois ; le destructeur attend le fil).

- [ ] **Step 1: test (rouge)** — lecture réelle, en lecture seule :

```cpp
TEST_CASE(apps_read_real_folder) {   // lit le dossier Apps de Windows sans rien lancer
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    auto raw = md::readAppsFolder();
    CHECK(raw.size() >= 5);
    for (auto& e : raw) CHECK(!e.parsingName.empty());
    md::AppCatalog cat;
    cat.refreshAsync();
    auto list = cat.get(10000);
    CHECK(!list.empty() && list.size() <= raw.size());
    CoUninitialize();
}
```

- [ ] **Step 2-4:** rouge ; `SHGetKnownFolderItem(FOLDERID_AppsFolder, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(&folder))`, `BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&items))`, `Next` un par un, `GetDisplayName(SIGDN_NORMALDISPLAY / SIGDN_PARSINGNAME)` ; fil `std::thread` avec `CoInitializeEx` propre ; vert ; commit `feat(apps): lecture du dossier Apps`.

### Task 4: Vue plein écran et rendu hors écran

**Files:** Create `src/apps/apps_window.h/.cpp` ; Modify `src/app/main.cpp` (`--apps-snapshot`), `build.ps1` si besoin ; Test `tests/test_apps.cpp`

**Interfaces:**
- Consumes : `AppEntry`, `searchApps`, `appsLayout`, `appsHit`, `appsKey`, `IconProvider`, `MenuWindow::Env`, `GlassRenderer`, `ScreenBackdrop`, `tahoeWallpaper`.
- Produces : `class AppsWindow { public: struct Request { std::vector<AppEntry> apps; HMONITOR monitor = nullptr; }; static std::optional<std::wstring> track(const MenuWindow::Env&, const Request&); };` (nullopt : la vue n'a pas pu s'ouvrir → repli ; chaîne vide : fermée sans choix ; sinon nom d'analyse) ; `BgraImage appsSnapshot(const std::vector<AppEntry>& apps, const std::wstring& query, int page, bool dark, int width, int height, bool icons)`.

- [ ] **Step 1: test (rouge)** :

```cpp
TEST_CASE(apps_snapshot_draws_offscreen) {   // Direct2D sur une bitmap : aucune fenêtre
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::vector<md::AppEntry> apps;
    for (int i = 0; i < 40; ++i) apps.push_back({L"App " + std::to_wstring(i), L"x" + std::to_wstring(i)});
    auto im = md::appsSnapshot(apps, L"", 0, true, 1280, 800, false);
    REQUIRE(im.w == 1280 && im.h == 800 && im.px.size() == 1280u * 800 * 4);
    auto q = md::appsSnapshot(apps, L"App 3", 0, false, 1280, 800, false);
    CHECK(q.px != im.px);
    CHECK(q.px[3] == 255);   // opaque : fond d'écran flouté sous la vue
    CoUninitialize();
}
```

- [ ] **Step 2-4:** rouge ; implémentation :
  - un seul dessin `drawApps(ID2D1DeviceContext*, state)` : champ de recherche (pilule en verre clair, loupe dessinée, texte ou « Rechercher » grisé, curseur clignotant), grille de la page (icône, nom sur deux lignes au plus, sélection en halo arrondi), points des pages (la page courante pleine) ; texte blanc avec ombre légère (fond sombre et flouté dans les deux modes, comme Launchpad) ;
  - `track` : fenêtre `WS_POPUP | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP` couvrant l'écran du Dock, `WDA_EXCLUDEFROMCAPTURE`, verre `GlassRenderer` sur tout l'écran (flou fort, `bevelPx = 0`, `refraction = 0`, teinte sombre 0,35) d'après `ScreenBackdrop`, sinon fond sombre opaque à 0,85 ; activée (`forceForeground`) pour recevoir le clavier ; `WM_CHAR` (caractères imprimables), `WM_KEYDOWN` (Retour arrière, Échap, Entrée, flèches, Page précédente/suivante, Début, Fin), molette (page ±1), clic gauche (case → choix ; points → page ; vide → fermeture), clic droit et perte d'activation → fermeture ; fondu + zoom 0,94 → 1 en 0,2 s ; icônes `IconProvider::get(parsingName, launchTarget, px)` dans un fil, page courante d'abord ;
  - `appsSnapshot` : D3D11 matériel (WARP sinon), Direct2D sur bitmap cible, fond = `tahoeWallpaper` flouté par l'effet Gaussien de Direct2D puis assombri, même `drawApps`, copie en mémoire ;
  - `main.cpp` : `--apps-snapshot f.png [--query texte] [--page n] [--theme light|dark]` lit le vrai dossier Apps (lecture seule), sans verrou ni Dock ;
  vert ; commit `feat(apps): vue plein écran`.

### Task 5: Branchements dans le Dock et documentation

**Files:** Modify `src/app/dock_window.h/.cpp`, `src/app/dock_menus.h/.cpp` ; `README.md`, `docs/journal-de-nuit.md` ; Test `tests/test_dock_menus.cpp`

- [ ] **Step 1: test (rouge)** :

```cpp
TEST_CASE(dock_menu_apps_button) {
    md::MenuContext c;
    c.item = md::DockItem{md::ItemKind::AppsButton};
    auto m = md::buildDockMenu(c);
    REQUIRE(m.items.size() == 3);
    CHECK(m.items[0].id == md::kCmdStartMenu && m.items[0].text == L"Ouvrir le menu Démarrer");
    CHECK(m.items[2].id == md::kCmdRemove);
}
```

- [ ] **Step 2-4:** rouge ; `kCmdStartMenu` ; `DockApp` : membre `AppCatalog apps_` (`refreshAsync` au démarrage hors `--snapshot`) ; bouton Apps → `openApps(index)` : catalogue (`get(1500)`), vide → `openStartMenu` ; sinon capture en pause, `menuOpen_`, `AppsWindow::track` ; nullopt → `openStartMenu` ; choix → `launch(launchTarget)` (journal si échec) ; puis `apps_.refreshAsync()` ; vert.
- [ ] **Step 5:** README (Utilisation, Diagnostic), journal ; commit `feat(apps): écran Apps dans le Dock` puis `docs: écran Apps (plan 13)`.
