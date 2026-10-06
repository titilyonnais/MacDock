# MacDock — Plan 1 : Dock fonctionnel — Plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** remplacer la barre des tâches Windows par un Dock natif fonctionnel : apps épinglées et ouvertes, magnification fidèle, rebonds, infobulles, lancement et activation, fenêtres réduites, Téléchargements et Corbeille. La barre native est cachée par un mod Windhawk protégé par un battement de cœur, et un lanceur relance le Dock s'il plante.

**Architecture :** `MacDock.exe` (C++20/Win32) sépare la logique pure (JSON, réglages, mise en page, animations, modèle, protocole IPC, politique de relance), testée unitairement, des couches système (suivi des fenêtres, icônes, rendu DirectComposition + Direct2D, fenêtre et interactions). Le mod Windhawk se connecte au named pipe du Dock et ne cache la barre native que tant qu'il reçoit des battements de cœur.

**Tech Stack :** MSVC 14.44 (VS 2022), C++20, SDK Windows 10.0.26100, D3D11, DXGI, DirectComposition, Direct2D, DirectWrite, WIC, DWM, Shell. Windhawk (Clang 20) pour le mod.

**Spec :** `docs/superpowers/specs/2026-10-06-macos-dock-design.md`

**Hors de ce plan** (plans 2 et 3, rédigés après la livraison de celui-ci) : verre Liquid Glass avec capture et shaders, mode calibration, glisser-déposer et « poof », menus contextuels, piles en éventail, grille ou liste, miniatures DWM en direct, badges et progression relayés par le mod, masquage automatique, multi-écran, masquage en plein écran. Dans ce plan, le fond du Dock est un verre « simple » (fond translucide, liseré, ombre) dessiné en Direct2D.

## Global Constraints

- C++20, MSVC, `/W4 /permissive- /EHsc /utf-8`, `UNICODE`/`_UNICODE`, x64 uniquement.
- Aucune dépendance externe téléchargée : tout est écrit dans le dépôt ou fourni par le SDK Windows.
- Aucune ressource Apple (icônes, logos, police) n'est intégrée au dépôt.
- Toutes les mesures visuelles sont en points macOS × (DPI de l'écran / 96).
- Les réglages sont dans `%APPDATA%\MacDock\settings.json`, les mesures dans `%APPDATA%\MacDock\dock-metrics.json` et les journaux dans `%APPDATA%\MacDock\logs\`.
- Un fichier JSON invalide → valeurs par défaut et copie du fichier fautif en `.bak`.
- Le named pipe s'appelle `\\.\pipe\MacDock`, avec un battement de cœur toutes les 1 s ; la barre native réapparaît après 5 s de silence.
- Relance : abandon après 3 plantages en 60 s.
- Au repos (aucune animation, souris hors du Dock), le Dock ne redessine rien.
- Les modules logiques (`src/core`, `src/config`, `src/layout`, `src/anim`, `src/model`, `src/ipc/protocol.*`, `src/launcher/crash_policy.*`, `src/icons/squircle.*`) n'incluent pas `<windows.h>`.

## Review Focus

1. **Le Dock plante ou est tué** → la barre Windows doit réapparaître en 5 s au plus. Test : `tests/test_crash_policy.cpp` couvre la politique ; Task 9 inclut une vérification manuelle « tuer MacDock.exe dans le Gestionnaire des tâches ».
2. **`settings.json` corrompu ou tronqué** (coupure de courant pendant l'écriture) → démarrage normal avec les valeurs par défaut, fichier sauvegardé en `.bak`. Test : `config_load_invalid_json_falls_back_and_backs_up` (Task 3). L'écriture est atomique (fichier temporaire puis `MoveFileEx`).
3. **Applications dont l'identité est inhabituelle** (apps du Store hébergées par `ApplicationFrameHost.exe`, plusieurs fenêtres d'un même exe, exe disparu) → regroupement correct, pas de doublon. Test : `model_groups_windows_by_app_id` et `model_store_app_identity_is_aumid` (Task 6).
4. **Curseur hors des bornes du Dock ou Dock vide** → aucune division par zéro ni NaN dans la mise en page. Tests : `layout_empty_dock`, `layout_cursor_beyond_edges_is_clamped` (Task 4).
5. **Flux IPC corrompu ou tronqué** (message partiel, mauvais magic, longueur énorme) → le décodeur signale une erreur sans planter ni allouer des gigaoctets. Tests : `protocol_partial_frames`, `protocol_rejects_bad_magic`, `protocol_rejects_oversized_payload` (Task 7).

---

## Structure des fichiers

```
build.ps1                        construction (tests, MacDock, MacDockLauncher)
src/core/log.h|.cpp              journal fichier avec rotation (Win32 interne)
src/core/json.h|.cpp             analyseur/sérialiseur JSON minimal
src/core/strings.h|.cpp          conversions UTF-8 <-> UTF-16, minuscules
src/config/settings.h|.cpp       Settings + PinnedEntry, lecture/écriture JSON
src/config/metrics.h|.cpp        Metrics (mesures visuelles), lecture JSON
src/config/config_store.h|.cpp   chargement fichier, repli, .bak, écriture atomique (Win32)
src/layout/dock_layout.h|.cpp    magnification et positions (pur)
src/anim/spring.h|.cpp           ressort amorti (pur)
src/anim/bounce.h|.cpp           rebonds de lancement et d'attention (pur)
src/model/app_model.h|.cpp       éléments du Dock (pur)
src/ipc/protocol.h|.cpp          trames IPC (pur)
src/ipc/pipe_server.h|.cpp       serveur named pipe + battement de cœur (Win32)
src/launcher/crash_policy.h|.cpp politique de relance (pur)
src/launcher/launcher_main.cpp   MacDockLauncher.exe
src/icons/squircle.h|.cpp        masque superellipse + test « icon jail » (pur)
src/icons/icon_provider.h|.cpp   extraction et préparation des icônes (Win32/WIC)
src/tracker/app_identity.h|.cpp  identité d'app d'une fenêtre (Win32)
src/tracker/window_tracker.h|.cpp suivi des fenêtres (Win32)
src/shell/shell_actions.h|.cpp   lancer, activer, ouvrir dossier/Corbeille, menu Démarrer
src/shell/default_pins.h|.cpp    épingles par défaut (import de la barre Windows)
src/render/dock_renderer.h|.cpp  DComp + D2D + DirectWrite
src/app/dock_controller.h|.cpp   orchestration état/animations/actions
src/app/dock_window.h|.cpp       HWND, hook souris, AppBar, messages système
src/app/main.cpp                 point d'entrée MacDock.exe
windhawk/macdock-hide-taskbar.wh.cpp
tests/minitest.h  tests/test_main.cpp  tests/test_*.cpp
```

---

### Task 1 : Socle de construction, mini-framework de tests, chaînes et journal

**Files :**
- Create : `build.ps1`, `.gitignore`, `tests/minitest.h`, `tests/test_main.cpp`, `tests/test_strings.cpp`, `src/core/strings.h`, `src/core/strings.cpp`, `src/core/log.h`, `src/core/log.cpp`

**Interfaces :**
- Produces :
  - `minitest.h` : `TEST_CASE(name)`, `CHECK(expr)`, `CHECK_EQ(a,b)`, `CHECK_NEAR(a,b,eps)`, `REQUIRE(expr)` ; `int minitest::runAll(const char* filter)`.
  - `std::string md::toUtf8(std::wstring_view)` ; `std::wstring md::fromUtf8(std::string_view)` ; `std::wstring md::toLower(std::wstring_view)`.
  - `void md::log::init(const std::wstring& dir)` ; `md::log::info/warn/error(const wchar_t* fmt, ...)` (printf, rotation à 1 Mo, 3 fichiers conservés).
  - `build.ps1 [-Target tests|dock|launcher|all] [-Config Debug|Release] [-Run]` → `build\<Config>\*.exe`.

- [ ] **Step 1 : écrire `tests/minitest.h`** : registre statique de cas `{name, fn}`. `CHECK*` enregistre l'échec (fichier, ligne, expression, valeurs) sans arrêter ; `REQUIRE` lève une exception interne qui interrompt le cas. `runAll` affiche `[PASS]` ou `[FAIL]` pour chaque cas, puis un récapitulatif, et retourne le nombre d'échecs. `test_main.cpp` appelle `runAll(argc>1 ? argv[1] : nullptr)` (filtre par sous-chaîne).
- [ ] **Step 2 : écrire le test qui échoue** `tests/test_strings.cpp` :

```cpp
#include "minitest.h"
#include "../src/core/strings.h"
TEST_CASE(strings_utf8_roundtrip) {
    std::wstring w = L"Téléchargements – 日本";
    CHECK(md::fromUtf8(md::toUtf8(w)) == w);
    CHECK_EQ(md::toUtf8(L"é"), std::string("\xC3\xA9"));
}
TEST_CASE(strings_lower) { CHECK(md::toLower(L"C:\\Program Files\\APP.EXE") == L"c:\\program files\\app.exe"); }
TEST_CASE(strings_invalid_utf8_does_not_throw) { CHECK(md::fromUtf8("\xFF\xFE").size() > 0); }
```
- [ ] **Step 3 : écrire `build.ps1`** : trouve Visual Studio via `vswhere -latest -property installationPath`, puis lance `cmd /c "call vcvars64.bat && cl ..."` pour chaque cible. Options communes : `/std:c++20 /W4 /permissive- /EHsc /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /MP /nologo` ; Debug : `/Zi /Od /MDd`, Release : `/O2 /MD /DNDEBUG`. La cible `tests` compile `tests\*.cpp` et les fichiers logiques listés dans une variable `$LogicSources`. Les cibles `dock` et `launcher` ont leurs propres listes de sources et de bibliothèques. Le script échoue avec un code non nul si `cl` échoue, et `-Run` exécute `tests.exe`.
- [ ] **Step 4 : lancer et constater l'échec** : `./build.ps1 -Target tests -Run` → erreur de compilation, `strings.h` est introuvable.
- [ ] **Step 5 : implémenter `strings.cpp`** avec `MultiByteToWideChar` et `WideCharToMultiByte` : seul fichier « core » autorisé à inclure `<windows.h>`, via un include local dans le `.cpp`. Écrire aussi `towlower` par caractère et `log.cpp` : `CreateDirectoryW`, `_vsnwprintf_s`, horodatage `GetLocalTime`, mutex, rotation `log.txt` → `log.1.txt` → `log.2.txt`.
- [ ] **Step 6 : lancer** `./build.ps1 -Target tests -Run` → 3 PASS.
- [ ] **Step 7 : commit** `chore: socle de construction, minitest, chaînes et journal`.

### Task 2 : JSON minimal

**Files :** Create `src/core/json.h`, `src/core/json.cpp`, `tests/test_json.cpp`

**Interfaces :**
- Produces : `md::json::Value` (variant `nullptr_t, bool, double, std::string, Array, Object`) avec `Array = std::vector<Value>` et `Object = std::vector<std::pair<std::string, Value>>` (ordre conservé). Méthodes : `isNull/isBool/isNumber/isString/isArray/isObject`, `asBool(def)`, `asNumber(def)`, `asString(def)`, `const Value* find(std::string_view key) const`, `Value& set(std::string key, Value v)`, `push(Value)`. Fonctions `std::optional<Value> md::json::parse(std::string_view text, std::string* error = nullptr)` et `std::string md::json::serialize(const Value&, bool pretty = true)`.

- [ ] **Step 1 : tests**

```cpp
TEST_CASE(json_parse_object) {
    auto v = md::json::parse(R"({"a":1.5,"b":[true,null,"x\u00e9"],"c":{"d":-2e3}})");
    REQUIRE(v.has_value());
    CHECK_NEAR(v->find("a")->asNumber(0), 1.5, 1e-9);
    CHECK(v->find("b")->asArray()[0].asBool(false));
    CHECK(v->find("b")->asArray()[2].asString("") == "x\xC3\xA9");
    CHECK_NEAR(v->find("c")->find("d")->asNumber(0), -2000, 1e-9);
}
TEST_CASE(json_rejects_garbage) {
    std::string err;
    CHECK(!md::json::parse("{\"a\":", &err).has_value());
    CHECK(!err.empty());
    CHECK(!md::json::parse("{} trailing").has_value());
    CHECK(!md::json::parse("").has_value());
}
TEST_CASE(json_roundtrip_preserves_order_and_escapes) {
    md::json::Value o = md::json::Object{};
    o.set("z", 1.0); o.set("a", std::string("quote\" back\\ nl\n"));
    auto text = md::json::serialize(o, false);
    CHECK(text == R"({"z":1,"a":"quote\" back\\ nl\n"})");
    CHECK(md::json::parse(text).has_value());
}
TEST_CASE(json_deep_nesting_is_rejected_not_crash) {
    std::string s(100000, '[');
    CHECK(!md::json::parse(s).has_value());
}
```
- [ ] **Step 2 : constater l'échec** (compilation).
- [ ] **Step 3 : implémenter** un analyseur récursif descendant avec une profondeur maximale de 256, les échappements `\" \\ \/ \b \f \n \r \t \uXXXX` (paires de substitution → UTF-8) et les nombres via `std::from_chars`. Le sérialiseur écrit un entier sans décimales quand `v == floor(v)` et `|v| < 1e15`, sinon utilise `%.17g` ; il échappe les caractères de contrôle et indente de 2 espaces si `pretty`.
- [ ] **Step 4 : tests verts.**
- [ ] **Step 5 : commit** `feat(core): JSON minimal`.

### Task 3 : Réglages, mesures et stockage

**Files :** Create `src/config/settings.h|.cpp`, `src/config/metrics.h|.cpp`, `src/config/config_store.h|.cpp`, `tests/test_config.cpp`

**Interfaces :**
- Consumes : `md::json` (Task 2), `md::toUtf8/fromUtf8` (Task 1).
- Produces :

```cpp
namespace md {
enum class DockPosition { Bottom, Left, Right };
enum class PinKind { App, AppsButton, Stack };
struct PinnedEntry { PinKind kind = PinKind::App; std::wstring appId; std::wstring launch; std::wstring name; };
struct Settings {
    DockPosition position = DockPosition::Bottom;
    bool autohide = false, magnification = true, showRecents = true, tahoeStrictIcons = true;
    double tileSize = 48, largeSize = 128;          // bornés : tile 16..128, large tile..128
    std::wstring font;                               // vide = automatique (SF Pro > Inter > Segoe UI Variable)
    std::vector<PinnedEntry> pinned;
    bool pinnedInitialized = false;                  // false => importer les épingles par défaut
};
Settings settingsFromJson(const json::Value&);       // champs absents/invalides => défauts, bornage
json::Value settingsToJson(const Settings&);
struct Metrics {
    double iconGap = 6, dockPadding = 8, dockCornerRadius = 24, dockScreenMargin = 5;
    double separatorWidth = 1, separatorMargin = 7, separatorLengthRatio = 0.72;
    double magnifyRangeTiles = 3.0, magnifyStiffness = 420, magnifyDamping = 38;
    double indicatorDiameter = 4, indicatorInset = 3;
    double launchBounceHeight = 0.55, launchBouncePeriod = 0.62, launchTimeout = 12.0;
    double attentionBounceHeight = 1.05, attentionBouncePeriod = 0.62, attentionBounceCount = 3, attentionPause = 1.2;
    double tooltipGap = 12, tooltipPadX = 11, tooltipPadY = 5, tooltipFontSize = 13, tooltipFadeSeconds = 0.12;
    double bgOpacityLight = 0.32, bgOpacityDark = 0.26, borderOpacity = 0.55, shadowOpacity = 0.22, shadowBlur = 18;
    double iconJailInset = 0.16;
};
Metrics metricsFromJson(const json::Value&);
json::Value metricsToJson(const Metrics&);
// config_store (Win32)
std::wstring appDataDir();                            // %APPDATA%\MacDock, créé si absent
struct LoadResult { json::Value value; bool fromFile; bool wasInvalid; };
LoadResult loadJsonFile(const std::wstring& path);   // invalide => .bak + Value vide (Object)
bool saveJsonFileAtomic(const std::wstring& path, const json::Value&);
}
```

- [ ] **Step 1 : tests**

```cpp
TEST_CASE(settings_defaults_when_empty) {
    auto s = md::settingsFromJson(md::json::Object{});
    CHECK(s.position == md::DockPosition::Bottom);
    CHECK_NEAR(s.tileSize, 48, 1e-9); CHECK(s.magnification); CHECK(!s.pinnedInitialized);
}
TEST_CASE(settings_clamps_and_ignores_wrong_types) {
    auto v = md::json::parse(R"({"tileSize":500,"largeSize":10,"magnification":"yes","position":"left"})");
    auto s = md::settingsFromJson(*v);
    CHECK_NEAR(s.tileSize, 128, 1e-9); CHECK_NEAR(s.largeSize, 128, 1e-9);
    CHECK(s.magnification); CHECK(s.position == md::DockPosition::Left);
}
TEST_CASE(settings_roundtrip_with_pins) {
    md::Settings s; s.pinnedInitialized = true;
    s.pinned.push_back({md::PinKind::App, L"c:\\windows\\explorer.exe", L"C:\\Windows\\explorer.exe", L"Explorateur"});
    s.pinned.push_back({md::PinKind::Stack, L"", L"C:\\Users\\x\\Downloads", L"Téléchargements"});
    auto back = md::settingsFromJson(md::settingsToJson(s));
    CHECK_EQ(back.pinned.size(), size_t(2));
    CHECK(back.pinned[1].kind == md::PinKind::Stack);
    CHECK(back.pinned[1].name == L"Téléchargements");
}
TEST_CASE(metrics_partial_override) {
    auto m = md::metricsFromJson(*md::json::parse(R"({"dockCornerRadius":30,"unknown":1})"));
    CHECK_NEAR(m.dockCornerRadius, 30, 1e-9); CHECK_NEAR(m.iconGap, 6, 1e-9);
}
TEST_CASE(config_load_invalid_json_falls_back_and_backs_up) {
    std::wstring dir = md::testTempDir();             // helper minitest : dossier temporaire unique
    std::wstring p = dir + L"\\settings.json";
    md::testWriteFile(p, "{\"tileSize\": 4");          // tronqué
    auto r = md::loadJsonFile(p);
    CHECK(r.wasInvalid); CHECK(r.value.isObject());
    CHECK(md::testFileExists(p + L".bak"));
}
TEST_CASE(config_atomic_save_then_load) {
    std::wstring p = md::testTempDir() + L"\\m.json";
    md::json::Value o = md::json::Object{}; o.set("a", 2.0);
    CHECK(md::saveJsonFileAtomic(p, o));
    auto r = md::loadJsonFile(p);
    CHECK(r.fromFile); CHECK(!r.wasInvalid); CHECK_NEAR(r.value.find("a")->asNumber(0), 2, 1e-9);
}
```
Ajouter `tests/test_helpers.h|.cpp` avec `testTempDir()` (sous `%TEMP%\macdock-tests\<compteur>`), `testWriteFile` et `testFileExists`.
- [ ] **Step 2 : constater l'échec.**
- [ ] **Step 3 : implémenter.** Clés JSON = noms des champs. `position` vaut `"bottom"`, `"left"` ou `"right"`, et `kind` vaut `"app"`, `"apps"` ou `"stack"`. Bornage : `tileSize ∈ [16,128]`, `largeSize ∈ [tileSize,128]`. Écriture atomique : `path.tmp`, puis `MoveFileExW(MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)`. Un fichier absent donne `{Object vide, fromFile=false, wasInvalid=false}`.
- [ ] **Step 4 : tests verts.**
- [ ] **Step 5 : commit** `feat(config): réglages, mesures et stockage robuste`.

### Task 4 : Mise en page et magnification

**Files :** Create `src/layout/dock_layout.h|.cpp`, `tests/test_layout.cpp`

**Interfaces :**

```cpp
namespace md {
struct LayoutItemSpec { bool separator = false; };
struct LayoutInput {
    std::vector<LayoutItemSpec> items;
    double tileSize = 48, largeSize = 128, gap = 6, padding = 8;
    double separatorWidth = 1, separatorMargin = 7;
    double rangeTiles = 3.0;
    double amount = 0;                    // 0..1, intensité animée de la magnification
    std::optional<double> cursor;         // axe principal, relatif au centre du Dock au repos
};
struct LayoutItem { double center = 0; double size = 0; };   // size = côté de l'icône (ou largeur du séparateur)
struct LayoutResult {
    std::vector<LayoutItem> items;
    double bgStart = 0, bgEnd = 0;        // bornes du fond sur l'axe principal
    double restLength = 0;                // longueur du fond au repos
    double thickness = 0;                 // épaisseur du fond au repos = tile + 2*padding
    double maxSize = 0;                   // plus grande icône
};
double magnifiedSize(double distance, double tile, double large, double range);
LayoutResult computeLayout(const LayoutInput&);
}
```

**Algorithme :**
- `magnifiedSize(d)` : si `|d| >= range`, retourne `tile` ; sinon `tile + (large - tile) * (1 + cos(π·d/range)) / 2` (cosinus surélevé).
- Repos : largeurs `w_i` (`tile` pour les icônes, `separatorWidth + 2·separatorMargin` pour les séparateurs), espacées de `gap` et centrées sur 0. `restLength = Σw + gap·(n−1) + 2·padding`.
- Avec curseur et `amount > 0` : chaque taille vaut `tile + (magnifiedSize(|cursor − restCenter_i|, …, rangeTiles·tile) − tile)·amount` ; les séparateurs gardent leur largeur. Le curseur est borné à `[premier bord au repos, dernier bord au repos]`. On cherche l'élément `k` qui le contient au repos, en partageant chaque gap par moitié entre ses voisins, et la fraction `f` du curseur dans son intervalle. On place ensuite `k` de façon que le point situé à la fraction `f` reste sous le curseur, puis on empile les éléments vers la gauche et vers la droite avec `gap` entre eux. Le fond va du bord gauche du premier élément moins `padding` au bord droit du dernier plus `padding`.

- [ ] **Step 1 : tests**

```cpp
static md::LayoutInput five() { md::LayoutInput in; in.items.resize(5); return in; }
TEST_CASE(layout_empty_dock) {
    md::LayoutInput in; auto r = md::computeLayout(in);
    CHECK(r.items.empty()); CHECK_NEAR(r.restLength, 16, 1e-9); CHECK(std::isfinite(r.bgStart));
}
TEST_CASE(layout_rest_is_centered) {
    auto r = md::computeLayout(five());
    CHECK_NEAR(r.restLength, 5*48 + 4*6 + 16, 1e-9);
    CHECK_NEAR(r.items[2].center, 0, 1e-9);
    CHECK_NEAR(r.bgStart, -r.bgEnd, 1e-9);
    for (auto& it : r.items) CHECK_NEAR(it.size, 48, 1e-9);
}
TEST_CASE(layout_magnifiedSize_curve) {
    CHECK_NEAR(md::magnifiedSize(0, 48, 128, 144), 128, 1e-9);
    CHECK_NEAR(md::magnifiedSize(144, 48, 128, 144), 48, 1e-9);
    CHECK_NEAR(md::magnifiedSize(72, 48, 128, 144), 88, 1e-9);
}
TEST_CASE(layout_hovered_icon_reaches_large_and_stays_under_cursor) {
    auto in = five(); in.amount = 1; in.cursor = 0;
    auto r = md::computeLayout(in);
    CHECK_NEAR(r.items[2].size, 128, 1e-6);
    CHECK_NEAR(r.items[2].center, 0, 1e-6);
    CHECK_NEAR(r.items[1].size, r.items[3].size, 1e-6);
    CHECK(r.items[1].size < 128); CHECK(r.items[0].size <= r.items[1].size);
}
TEST_CASE(layout_hover_left_edge_grows_left) {
    auto in = five(); auto rest = md::computeLayout(in);
    in.amount = 1; in.cursor = rest.items[0].center;
    auto r = md::computeLayout(in);
    CHECK(r.bgStart < rest.bgStart - 30);
    CHECK(r.bgEnd - rest.bgEnd < r.items[0].size);
    CHECK_NEAR(r.items[0].center, rest.items[0].center, 1e-6);
}
TEST_CASE(layout_cursor_beyond_edges_is_clamped) {
    auto in = five(); in.amount = 1; in.cursor = 1e9;
    auto r = md::computeLayout(in);
    for (auto& it : r.items) { CHECK(std::isfinite(it.center)); CHECK(std::isfinite(it.size)); }
}
TEST_CASE(layout_amount_zero_equals_rest) {
    auto in = five(); in.cursor = 10; in.amount = 0;
    auto r = md::computeLayout(in); auto rest = md::computeLayout(five());
    for (size_t i = 0; i < 5; ++i) CHECK_NEAR(r.items[i].center, rest.items[i].center, 1e-9);
}
TEST_CASE(layout_separator_not_magnified) {
    md::LayoutInput in; in.items = {{}, {true}, {}}; in.amount = 1; in.cursor = 0;
    auto r = md::computeLayout(in);
    CHECK_NEAR(r.items[1].size, 1, 1e-9);
}
TEST_CASE(layout_no_overlap) {
    auto in = five(); in.amount = 1;
    for (double c = -200; c <= 200; c += 7) {
        in.cursor = c; auto r = md::computeLayout(in);
        for (size_t i = 1; i < 5; ++i)
            CHECK(r.items[i].center - r.items[i].size/2 >= r.items[i-1].center + r.items[i-1].size/2 + 6 - 1e-6);
    }
}
```
Pour le séparateur, la valeur `size` d'un `LayoutItem` vaut `separatorWidth`, mais l'espace qu'il occupe sur l'axe vaut `separatorWidth + 2·separatorMargin`. Le test `layout_no_overlap` ne porte que sur des icônes.
- [ ] **Step 2 : constater l'échec.** **Step 3 : implémenter.** **Step 4 : tests verts.**
- [ ] **Step 5 : commit** `feat(layout): magnification macOS ancrée sous le curseur`.

### Task 5 : Animations (ressort, rebonds)

**Files :** Create `src/anim/spring.h|.cpp`, `src/anim/bounce.h|.cpp`, `tests/test_anim.cpp`

**Interfaces :**

```cpp
namespace md {
class Spring {
public:
    explicit Spring(double stiffness = 420, double damping = 38);
    void setParams(double stiffness, double damping);
    void setTarget(double t); void snap(double v);
    bool step(double dt);                 // sous-pas fixes de 1 ms ; retourne true tant que ça bouge
    double value() const; double target() const; double velocity() const;
    bool settled() const;                 // |x-t|<1e-3 && |v|<1e-3
};
double launchBounceOffset(double elapsed, double period, double height);   // ≥ 0, parabole 4p(1-p)
double attentionBounceOffset(double elapsed, double period, double height, int count, double pause);
}
```
- [ ] **Step 1 : tests**

```cpp
TEST_CASE(spring_converges_without_overshoot_explosion) {
    md::Spring s(420, 38); s.snap(0); s.setTarget(1);
    double maxV = 0; for (int i = 0; i < 240; ++i) { s.step(1.0/120); maxV = std::max(maxV, s.value()); }
    CHECK(s.settled()); CHECK_NEAR(s.value(), 1, 1e-3); CHECK(maxV < 1.15);
}
TEST_CASE(spring_large_dt_is_stable) {
    md::Spring s; s.snap(0); s.setTarget(1); s.step(2.0);
    CHECK(std::isfinite(s.value())); CHECK_NEAR(s.value(), 1, 0.05);
}
TEST_CASE(spring_step_returns_false_when_settled) { md::Spring s; s.snap(3); s.setTarget(3); CHECK(!s.step(0.016)); }
TEST_CASE(bounce_launch_shape) {
    CHECK_NEAR(md::launchBounceOffset(0, 0.6, 10), 0, 1e-9);
    CHECK_NEAR(md::launchBounceOffset(0.3, 0.6, 10), 10, 1e-9);
    CHECK_NEAR(md::launchBounceOffset(0.6, 0.6, 10), 0, 1e-9);
    CHECK_NEAR(md::launchBounceOffset(0.9, 0.6, 10), 10, 1e-9);
}
TEST_CASE(bounce_attention_pauses) {
    // 3 rebonds de 0.6 s puis pause de 1.2 s
    CHECK(md::attentionBounceOffset(0.3, 0.6, 10, 3, 1.2) > 9);
    CHECK_NEAR(md::attentionBounceOffset(2.0, 0.6, 10, 3, 1.2), 0, 1e-9);
    CHECK(md::attentionBounceOffset(3.0 + 0.3, 0.6, 10, 3, 1.2) > 9);
}
```
- [ ] **Step 2 : constater l'échec.**
- [ ] **Step 3 : implémenter.** Le ressort utilise l'intégration d'Euler semi-implicite avec `a = −k(x−t) − c·v`, en sous-pas de 1 ms, `dt` étant borné à 0,25 s. Si `dt > 0.25`, on fait `snap(target)`.
- [ ] **Step 4 : tests verts.**
- [ ] **Step 5 : commit** `feat(anim): ressort et rebonds`.

### Task 6 : Modèle des éléments du Dock

**Files :** Create `src/model/app_model.h|.cpp`, `tests/test_model.cpp`

**Interfaces :**
- Consumes : `PinnedEntry`, `PinKind` (Task 3), `md::toLower` (Task 1).
- Produces :

```cpp
namespace md {
using WindowId = std::uint64_t;
struct AppIdentity { std::wstring appId, exePath, aumid, displayName, launch; };
enum class ItemKind { App, AppsButton, Separator, Stack, MinimizedWindow, Trash };
struct DockItem {
    ItemKind kind = ItemKind::App;
    std::wstring key;          // unique et stable : "app:<appId>", "apps", "sep:1", "stack:<path>", "win:<id>", "trash"
    std::wstring appId, name, launch;
    bool pinned = false, running = false, recent = false;
    std::vector<WindowId> windows;   // App : fenêtres ouvertes
    WindowId window = 0;             // MinimizedWindow
};
std::wstring makeAppId(const std::wstring& aumid, const std::wstring& exePath); // aumid prioritaire, sinon toLower(exePath)
class AppModel {
public:
    void loadPinned(const std::vector<PinnedEntry>&);
    std::vector<PinnedEntry> pinnedEntries() const;
    void setShowRecents(bool);
    void windowOpened(WindowId, const AppIdentity&);
    void windowClosed(WindowId);
    void windowMinimized(WindowId, bool minimized);
    void windowTitle(WindowId, const std::wstring& title);
    bool pin(const std::wstring& appId, std::size_t index);
    bool unpin(const std::wstring& key);
    bool movePinned(std::size_t from, std::size_t to);
    std::vector<DockItem> items() const;      // ordre de la spec §4.6
    std::vector<WindowId> windowsOf(const std::wstring& appId) const;
    std::optional<AppIdentity> identityOf(const std::wstring& appId) const;
    std::wstring titleOf(WindowId) const;
    std::uint64_t revision() const;           // incrémenté à chaque changement visible
};
}
```
**Ordre de `items()`** : épinglés (App et AppsButton) dans l'ordre. Si `showRecents` est faux, les apps ouvertes non épinglées suivent dans l'ordre d'ouverture. Puis `sep:1`. Si `showRecents` est vrai, les apps ouvertes non épinglées viennent à la place après `sep:1`, suivies d'au plus 3 apps fermées récemment (`recent=true`, de la plus récente à la plus ancienne), puis `sep:2`. Le `sep:2` n'est présent que si cette section n'est pas vide. Viennent ensuite les piles (épinglées `Stack`), les fenêtres réduites (`win:<id>`, par ordre de réduction) et `trash`. Une app fermée qui était épinglée n'entre pas dans les récents.

- [ ] **Step 1 : tests**

```cpp
static md::AppIdentity idOf(const wchar_t* exe, const wchar_t* aumid = L"") {
    md::AppIdentity a; a.exePath = exe; a.aumid = aumid; a.appId = md::makeAppId(aumid, exe);
    a.displayName = L"X"; a.launch = exe; return a; }
static std::vector<std::wstring> keys(const md::AppModel& m) { std::vector<std::wstring> k; for (auto& i : m.items()) k.push_back(i.key); return k; }

TEST_CASE(model_groups_windows_by_app_id) {
    md::AppModel m; m.setShowRecents(false);
    m.windowOpened(1, idOf(L"C:\\A\\a.exe")); m.windowOpened(2, idOf(L"c:\\a\\A.EXE"));
    auto it = m.items();
    CHECK_EQ(std::count_if(it.begin(), it.end(), [](auto& i){ return i.kind == md::ItemKind::App; }), 1);
    CHECK_EQ(m.windowsOf(md::makeAppId(L"", L"C:\\A\\a.exe")).size(), size_t(2));
}
TEST_CASE(model_store_app_identity_is_aumid) {
    CHECK(md::makeAppId(L"Microsoft.WindowsCalculator_8wekyb3d8bbwe!App", L"C:\\Windows\\System32\\ApplicationFrameHost.exe")
          == L"Microsoft.WindowsCalculator_8wekyb3d8bbwe!App");
}
TEST_CASE(model_order_without_recents) {
    md::AppModel m; m.setShowRecents(false);
    m.loadPinned({{md::PinKind::App, L"c:\\e.exe", L"c:\\e.exe", L"E"}, {md::PinKind::AppsButton, L"", L"", L"Apps"},
                  {md::PinKind::Stack, L"", L"C:\\D", L"D"}});
    m.windowOpened(5, idOf(L"C:\\z.exe")); m.windowOpened(6, idOf(L"C:\\e.exe"));
    m.windowMinimized(6, true);
    CHECK(keys(m) == std::vector<std::wstring>{L"app:c:\\e.exe", L"apps", L"app:c:\\z.exe", L"sep:1",
                                               L"stack:C:\\D", L"win:6", L"trash"});
}
TEST_CASE(model_recents_section) {
    md::AppModel m; m.setShowRecents(true);
    m.windowOpened(1, idOf(L"C:\\r1.exe")); m.windowClosed(1);
    m.windowOpened(2, idOf(L"C:\\r2.exe"));
    auto k = keys(m);
    CHECK(k == std::vector<std::wstring>{L"sep:1", L"app:c:\\r2.exe", L"app:c:\\r1.exe", L"sep:2", L"trash"});
    CHECK(m.items()[2].recent);
}
TEST_CASE(model_recents_capped_at_three) {
    md::AppModel m; m.setShowRecents(true);
    for (int i = 0; i < 6; ++i) { std::wstring p = L"C:\\a" + std::to_wstring(i) + L".exe";
        m.windowOpened(i + 1, idOf(p.c_str())); m.windowClosed(i + 1); }
    int recents = 0; for (auto& i : m.items()) recents += i.recent; CHECK_EQ(recents, 3);
}
TEST_CASE(model_pin_unpin_move) {
    md::AppModel m; m.setShowRecents(false);
    m.windowOpened(1, idOf(L"C:\\a.exe"));
    CHECK(m.pin(md::makeAppId(L"", L"C:\\a.exe"), 0));
    CHECK(m.items()[0].pinned);
    m.loadPinned({{md::PinKind::App, L"x", L"x", L"X"}, {md::PinKind::App, L"y", L"y", L"Y"}});
    CHECK(m.movePinned(0, 1));
    CHECK(m.pinnedEntries()[0].appId == L"y");
    CHECK(m.unpin(L"app:y")); CHECK_EQ(m.pinnedEntries().size(), size_t(1));
    CHECK(!m.unpin(L"trash"));
}
TEST_CASE(model_revision_changes_only_on_change) {
    md::AppModel m; auto r0 = m.revision(); m.windowClosed(999); CHECK_EQ(m.revision(), r0);
    m.windowOpened(1, idOf(L"C:\\a.exe")); CHECK(m.revision() != r0);
}
TEST_CASE(model_close_unknown_and_double_close_are_noops) {
    md::AppModel m; m.windowOpened(1, idOf(L"C:\\a.exe")); m.windowClosed(1); m.windowClosed(1); m.windowMinimized(42, true);
    CHECK(true);
}
```
- [ ] **Step 2 : constater l'échec.** **Step 3 : implémenter.** **Step 4 : tests verts.**
- [ ] **Step 5 : commit** `feat(model): modèle des éléments du Dock`.

### Task 7 : Protocole IPC et serveur de pipe

**Files :** Create `src/ipc/protocol.h|.cpp`, `src/ipc/pipe_server.h|.cpp`, `tests/test_protocol.cpp`

**Interfaces :**

```cpp
namespace md::ipc {
constexpr std::uint32_t kMagic = 0x4B43444D;   // "MDCK"
constexpr std::uint16_t kVersion = 1;
constexpr std::uint32_t kMaxPayload = 64 * 1024;
enum class MsgType : std::uint16_t { Heartbeat = 1, Overlay = 2, Progress = 3, Flash = 4, Goodbye = 5 };
struct Message { MsgType type{}; std::vector<std::uint8_t> payload; };
std::vector<std::uint8_t> encode(const Message&);   // en-tête 12 octets LE : magic u32, version u16, type u16, len u32
class Decoder {
public:
    void feed(const std::uint8_t* data, std::size_t n);
    std::optional<Message> next();
    bool failed() const;            // magic/version invalides ou len > kMaxPayload ; état définitif
};
struct OverlayEvent { std::uint64_t hwnd; bool hasOverlay; };
struct ProgressEvent { std::uint64_t hwnd; std::uint32_t state; std::uint64_t completed, total; };
struct FlashEvent { std::uint64_t hwnd; };
Message makeOverlay(const OverlayEvent&); std::optional<OverlayEvent> parseOverlay(const Message&);
Message makeProgress(const ProgressEvent&); std::optional<ProgressEvent> parseProgress(const Message&);
Message makeFlash(const FlashEvent&);  std::optional<FlashEvent> parseFlash(const Message&);
}
// pipe_server.h (Win32)
namespace md::ipc {
class PipeServer {    // thread dédié, un client à la fois, battement toutes les 1000 ms
public:
    using Handler = std::function<void(const Message&)>;   // appelé sur le thread du pipe
    bool start(const std::wstring& name, Handler);
    void stop();                                            // envoie Goodbye puis ferme
    bool clientConnected() const;
};
}
```
- [ ] **Step 1 : tests**

```cpp
TEST_CASE(protocol_roundtrip) {
    auto bytes = md::ipc::encode(md::ipc::makeProgress({0x1234, 2, 50, 100}));
    md::ipc::Decoder d; d.feed(bytes.data(), bytes.size());
    auto m = d.next(); REQUIRE(m.has_value());
    auto p = md::ipc::parseProgress(*m); REQUIRE(p.has_value());
    CHECK_EQ(p->hwnd, 0x1234ull); CHECK_EQ(p->completed, 50ull); CHECK(!d.next().has_value());
}
TEST_CASE(protocol_partial_frames) {
    auto a = md::ipc::encode(md::ipc::makeFlash({7})); auto b = md::ipc::encode({md::ipc::MsgType::Heartbeat, {}});
    std::vector<uint8_t> all(a); all.insert(all.end(), b.begin(), b.end());
    md::ipc::Decoder d; int got = 0;
    for (auto byte : all) { d.feed(&byte, 1); while (d.next()) ++got; }
    CHECK_EQ(got, 2); CHECK(!d.failed());
}
TEST_CASE(protocol_rejects_bad_magic) {
    std::vector<uint8_t> junk(12, 0xAB); md::ipc::Decoder d; d.feed(junk.data(), junk.size());
    CHECK(!d.next().has_value()); CHECK(d.failed());
}
TEST_CASE(protocol_rejects_oversized_payload) {
    auto f = md::ipc::encode({md::ipc::MsgType::Heartbeat, {}});
    uint32_t big = md::ipc::kMaxPayload + 1; std::memcpy(&f[8], &big, 4);
    md::ipc::Decoder d; d.feed(f.data(), f.size()); CHECK(!d.next().has_value()); CHECK(d.failed());
}
TEST_CASE(protocol_wrong_payload_size_is_rejected) {
    md::ipc::Message m{md::ipc::MsgType::Flash, {1, 2, 3}}; CHECK(!md::ipc::parseFlash(m).has_value());
}
```
- [ ] **Step 2 : constater l'échec.**
- [ ] **Step 3 : implémenter `protocol.cpp`** (petit-boutiste, `memcpy`), puis `pipe_server.cpp` : `CreateNamedPipeW(PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED, PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS, 1, 4096, 4096, 0, nullptr)`. Le thread est une boucle `ConnectNamedPipe` en overlapped ; une fois le client connecté, il fait des lectures overlapped et attend avec `WaitForMultipleObjects(lecture, arrêt, timeout 1000)`. À chaque expiration, il écrit un `Heartbeat`. En cas d'erreur, `DisconnectNamedPipe` puis nouvelle attente. Si le décodeur échoue, le client est déconnecté.
- [ ] **Step 4 : tests verts.**
- [ ] **Step 5 : commit** `feat(ipc): protocole et serveur de pipe`.

### Task 8 : Politique de relance, lanceur et démarrage automatique

**Files :** Create `src/launcher/crash_policy.h|.cpp`, `src/launcher/launcher_main.cpp`, `tests/test_crash_policy.cpp`

**Interfaces :**

```cpp
namespace md {
class CrashPolicy {
public:
    CrashPolicy(int maxCrashes = 3, double windowSeconds = 60);
    bool onCrash(double nowSeconds);   // true = relancer ; false = abandonner (3e plantage dans la fenêtre)
};
}
```
`MacDockLauncher.exe [--install|--uninstall]` : `--install` écrit `HKCU\...\Run\MacDock = "<chemin>\MacDockLauncher.exe"`, `--uninstall` supprime cette valeur. Sans argument, le lanceur crée un mutex nommé `Local\MacDockLauncher` (quitte si une instance tourne déjà), lance `MacDock.exe` du même dossier, puis attend. Code de sortie 0 → fin normale ; tout autre code ou un `TerminateProcess` → `onCrash`. En cas d'abandon : notification `Shell_NotifyIconW` (ballon « Le Dock s'est arrêté plusieurs fois. La barre des tâches Windows a été rétablie. Journal : %APPDATA%\MacDock\logs »), puis sortie.

- [ ] **Step 1 : tests**

```cpp
TEST_CASE(crash_policy_gives_up_on_third_crash_within_window) {
    md::CrashPolicy p; CHECK(p.onCrash(0)); CHECK(p.onCrash(10)); CHECK(!p.onCrash(20));
}
TEST_CASE(crash_policy_forgets_old_crashes) {
    md::CrashPolicy p; CHECK(p.onCrash(0)); CHECK(p.onCrash(10)); CHECK(p.onCrash(75)); CHECK(p.onCrash(80));
}
```
- [ ] **Step 2 : échec ; Step 3 : implémenter** (deque d'horodatages) ; **Step 4 : tests verts.**
- [ ] **Step 5 : implémenter `launcher_main.cpp`** et l'ajouter à `build.ps1` (`-Target launcher`, bibliothèques `user32 shell32 advapi32`, `/SUBSYSTEM:WINDOWS`). Vérifier que la compilation réussit.
- [ ] **Step 6 : commit** `feat(launcher): relance avec protection contre les boucles`.

### Task 9 : Mod Windhawk `macdock-hide-taskbar`

**Files :** Create `windhawk/macdock-hide-taskbar.wh.cpp`

**Comportement :**
- En-tête Windhawk : `@id macdock-hide-taskbar`, `@include explorer.exe`, `@architecture x86-64`, `@compilerOptions -luser32 -lshell32`.
- Un thread client : il tente `CreateFileW(L"\\\\.\\pipe\\MacDock", GENERIC_READ|GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr)` toutes les 1 s. Une fois connecté, il fait des lectures overlapped avec une attente de 5000 ms. Il décode les trames avec une copie minimale du format (magic, version, type, longueur), et la constante de version doit rester alignée avec `src/ipc/protocol.h`. Un `Heartbeat` donne `dockAlive=true`. Une attente expirée, une erreur, une déconnexion ou un `Goodbye` donnent `dockAlive=false`.
- Quand `dockAlive` change, on poste un message à une fenêtre cachée du mod. Dans `explorer.exe`, cette fenêtre :
  - si le Dock est vivant : enregistre l'état d'origine (`SHAppBarMessage(ABM_GETSTATE)`), passe la barre en `ABS_AUTOHIDE`, puis `ShowWindow(SW_HIDE)` sur `Shell_TrayWnd` et sur toutes les `Shell_SecondaryTrayWnd` ;
  - si le Dock est absent : restaure l'état d'origine et `ShowWindow(SW_SHOWNA)`.
- Hooks `Wh_SetFunctionHook` sur `ShowWindow` et `SetWindowPos` de `user32` : tant que `dockAlive`, toute tentative de rendre visible une fenêtre de classe `Shell_TrayWnd` ou `Shell_SecondaryTrayWnd` est neutralisée (`SW_HIDE`, ou retrait de `SWP_SHOWWINDOW`).
- `Wh_ModUninit` : arrêt du thread, restauration de l'état et réaffichage de la barre.
- Un réglage `restoreDelaySeconds` (5 par défaut).

- [ ] **Step 1 : écrire le mod.**
- [ ] **Step 2 : faire valider la compilation** en copiant le fichier dans Windhawk (« Créer un nouveau mod », coller, « Compiler »). Le mod ne cache rien tant qu'aucun Dock ne tourne.
- [ ] **Step 3 : vérifications manuelles** (après la Task 14) : le Dock tourne → barre cachée ; `taskkill /F /IM MacDock.exe` avec le lanceur arrêté → barre revenue en 5 s au plus ; mod désactivé → barre revenue immédiatement.
- [ ] **Step 4 : commit** `feat(windhawk): mod qui cache la barre des tâches sous battement de cœur`.

### Task 10 : Identité d'app et suivi des fenêtres

**Files :** Create `src/tracker/app_identity.h|.cpp`, `src/tracker/window_tracker.h|.cpp`

**Interfaces :**

```cpp
namespace md {
bool isDockEligibleWindow(HWND);          // visible, propriétaire nul ou WS_EX_APPWINDOW, pas WS_EX_TOOLWINDOW,
                                          // pas cloaké par l'app, titre non vide ou WS_EX_APPWINDOW, pas notre process
std::optional<AppIdentity> identifyWindow(HWND);   // AUMID via SHGetPropertyStoreForWindow(PKEY_AppUserModel_ID),
                                          // sinon QueryFullProcessImageNameW ; ApplicationFrameHost => CoreWindow enfant
                                          // + GetApplicationUserModelId ; nom = FileDescription de l'exe ou nom Shell
class WindowTracker {
public:
    struct Events {
        std::function<void(HWND, const AppIdentity&)> opened;
        std::function<void(HWND)> closed, activated, flashed;
        std::function<void(HWND, bool)> minimized;
        std::function<void(HWND, const std::wstring&)> titleChanged;
    };
    bool start(HWND messageWindow, Events);   // RegisterShellHookWindow + SetWinEventHook(OUTOFCONTEXT)
    void stop();
    bool handleMessage(UINT msg, WPARAM, LPARAM);   // à appeler depuis le WndProc ; true si traité
    void rescan();                                  // EnumWindows complet (démarrage, TaskbarCreated)
    HWND foreground() const;
};
}
```
Événements suivis : `EVENT_OBJECT_SHOW/HIDE/DESTROY` (`idObject==OBJID_WINDOW`, `idChild==CHILDID_SELF`), `EVENT_SYSTEM_FOREGROUND`, `EVENT_SYSTEM_MINIMIZESTART/END`, `EVENT_OBJECT_NAMECHANGE`, `EVENT_OBJECT_CLOAKED/UNCLOAKED`, plus le message ShellHook `HSHELL_FLASH`. Un ensemble `known` évite les doublons. Le suivi est réévalué à chaque `SHOW`, `HIDE` ou `UNCLOAKED` : les fenêtres qui deviennent éligibles sont « ouvertes » et celles qui ne le sont plus sont « fermées ».

- [ ] **Step 1 : implémenter.**
- [ ] **Step 2 : vérification manuelle** avec un mode de diagnostic `MacDock.exe --trace-windows` qui journalise les événements : ouvrir et fermer le Bloc-notes, la Calculatrice (Store) et deux fenêtres de l'Explorateur, réduire, restaurer → le journal montre un seul appId pour les deux Explorateurs et l'AUMID de la Calculatrice.
- [ ] **Step 3 : commit** `feat(tracker): suivi des fenêtres et identité des apps`.

### Task 11 : Icônes et squircle

**Files :** Create `src/icons/squircle.h|.cpp`, `src/icons/icon_provider.h|.cpp`, `tests/test_squircle.cpp`

**Interfaces :**

```cpp
namespace md {
bool insideSquircle(double x, double y, double size, double exponent = 5.0);  // superellipse centrée, x,y en px
double squircleCoverage(const std::uint8_t* bgra, int w, int h, int stride);  // part des pixels opaques (a>200)
                                                                              // dans le squircle / pixels du squircle
bool iconFitsSquircle(const std::uint8_t* bgra, int w, int h, int stride);    // coverage ≥ 0.88
class IconProvider {           // Win32/WIC/D2D
public:
    void setStrictTahoe(bool); void setCustomDir(std::wstring);
    // Retourne une image BGRA prémultipliée de taille px×px, déjà mise en forme (masque ou « icon jail »).
    struct Image { int size = 0; std::vector<std::uint8_t> bgra; };
    std::shared_ptr<const Image> get(const std::wstring& parsingName, int px);  // cache par (nom, px)
    void clear();
};
}
```
- [ ] **Step 1 : tests**

```cpp
TEST_CASE(squircle_center_inside_corner_outside) {
    CHECK(md::insideSquircle(50, 50, 100)); CHECK(!md::insideSquircle(1, 1, 100)); CHECK(md::insideSquircle(50, 1, 100));
}
TEST_CASE(squircle_full_square_fits) {
    std::vector<uint8_t> px(64*64*4, 255); CHECK(md::iconFitsSquircle(px.data(), 64, 64, 64*4));
}
TEST_CASE(squircle_small_circle_does_not_fit) {
    std::vector<uint8_t> px(64*64*4, 0);
    for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x)
        if ((x-32)*(x-32) + (y-32)*(y-32) < 16*16) px[(y*64+x)*4+3] = 255;
    CHECK(!md::iconFitsSquircle(px.data(), 64, 64, 64*4));
}
TEST_CASE(squircle_empty_image_is_safe) { CHECK(!md::iconFitsSquircle(nullptr, 0, 0, 0)); }
```
- [ ] **Step 2 : échec ; Step 3 : implémenter `squircle.cpp` ; Step 4 : tests verts.**
- [ ] **Step 5 : implémenter `icon_provider.cpp`.** Priorité au fichier personnalisé `<customDir>\<nom de fichier sûr de l'appId>.png`, chargé via WIC. Sinon `SHCreateItemFromParsingName` puis `IShellItemImageFactory::GetImage({256,256}, SIIGBF_ICONONLY|SIIGBF_BIGGERSIZEOK)`, avec repli sur `SIIGBF_RESIZETOFIT`. Le HBITMAP est converti en BGRA prémultiplié 32 bits ; les bitmaps sans alpha reçoivent alpha=255. Ensuite :
  - si `strictTahoe` est vrai et que l'icône ne remplit pas le squircle → fond squircle gris (`#E9E9EB` en clair) avec liseré, et l'icône réduite de `iconJailInset` de chaque côté ;
  - si l'icône remplit le squircle → on applique le masque, avec un bord anticrénelé par suréchantillonnage 4×4 ;
  - si `strictTahoe` est faux → icône d'origine sans masque.
  Redimensionnement final avec `IWICBitmapScaler` en `WICBitmapInterpolationModeHighQualityCubic`. Une icône introuvable donne une icône générique dessinée par le code : squircle gris et glyphe d'application.
- [ ] **Step 6 : commit** `feat(icons): extraction HD, squircle et icon jail`.

### Task 12 : Actions Shell et épingles par défaut

**Files :** Create `src/shell/shell_actions.h|.cpp`, `src/shell/default_pins.h|.cpp`

**Interfaces :**

```cpp
namespace md {
bool launch(const std::wstring& launch);                 // ShellExecuteExW (exe, .lnk, dossier, shell:AppsFolder\AUMID)
void activateApp(const std::vector<HWND>& windows);      // restaure les réduites, met toutes au premier plan, la plus récente active
void restoreWindow(HWND);
void minimizeAll(const std::vector<HWND>& windows);      // « Masquer »
void openRecycleBin(); void openFolder(const std::wstring& path);
void openStartMenu();                                    // SendInput Win (VK_LWIN down/up)
std::wstring downloadsFolder();                          // SHGetKnownFolderPath(FOLDERID_Downloads)
std::vector<PinnedEntry> defaultPins();                  // Explorateur, Apps, épingles de la barre Windows
                                                         // (%APPDATA%\Microsoft\Internet Explorer\Quick Launch\User Pinned\TaskBar\*.lnk
                                                         //  résolus via IShellLinkW : cible -> appId, .lnk -> launch), Téléchargements
}
```
- [ ] **Step 1 : implémenter.** `activateApp` : les fenêtres réduites sont restaurées avec `ShowWindowAsync(SW_RESTORE)`, puis `SetForegroundWindow` sur la plus récente. Le processus du Dock vient de recevoir le dernier événement d'entrée (le clic), ce qui l'autorise à changer le premier plan. Si ça échoue, repli sur `AllowSetForegroundWindow(ASFW_ANY)` combiné à un appui-relâchement simulé de la touche Alt.
- [ ] **Step 2 : vérification manuelle** en Task 14.
- [ ] **Step 3 : commit** `feat(shell): actions et épingles par défaut`.

### Task 13 : Rendu DirectComposition + Direct2D

**Files :** Create `src/render/dock_renderer.h|.cpp`

**Interfaces :**

```cpp
namespace md {
struct RenderIcon {            // en pixels de la fenêtre
    float cx, cy, size;        // centre et côté (y vers le bas)
    std::shared_ptr<const IconProvider::Image> image;
    bool indicator; bool separator; float sepLength;
    float opacity = 1;
};
struct RenderTooltip { bool visible = false; std::wstring text; float cx, bottom, opacity; };
struct RenderFrame {
    float bgLeft, bgTop, bgRight, bgBottom, cornerRadius, scale;    // scale = dpi/96
    bool dark;
    std::vector<RenderIcon> icons;
    RenderTooltip tooltip;
    DockPosition position;
};
class DockRenderer {
public:
    bool init(HWND hwnd);                       // D3D11 (BGRA), DXGI, D2D1 device, DComp target+visual+surface
    void resize(UINT w, UINT h);
    bool render(const RenderFrame&, const Metrics&, const std::wstring& fontFamily);  // false => device perdu, rappeler init
    void releaseImages();                       // vide le cache image -> ID2D1Bitmap1
};
}
```
**Contenu de chaque image :** on vide la surface (transparent) puis on dessine :
- **l'ombre** : un rectangle arrondi rendu à travers l'effet `CLSID_D2D1Shadow` (flou `shadowBlur`·scale, opacité `shadowOpacity`), décalé de 2 pt vers le bas ;
- **le fond** : rectangle arrondi rempli de blanc (clair) ou de `#1E1E1E` (sombre) à l'opacité `bgOpacity*`, avec un liseré intérieur de 1 px blanc à l'opacité `borderOpacity` et un dégradé vertical léger pour suggérer le verre ;
- **les icônes** : `DrawBitmap` en `D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC`, avec un cache `ID2D1Bitmap1` par `Image*` ;
- **les séparateurs** : ligne de 1 px, noire à 25 % en clair et blanche à 25 % en sombre ;
- **les indicateurs** : disque de `indicatorDiameter`, noir à 80 % en clair et blanc à 80 % en sombre, centré sous l'icône à `indicatorInset` du bord du fond ;
- **l'infobulle** : capsule arrondie au fond translucide et texte DirectWrite de la famille donnée, avec repli `Inter` puis `Segoe UI Variable Text`, en taille `tooltipFontSize`·scale.

Initialisation :
- `D3D11CreateDevice(D3D11_CREATE_DEVICE_BGRA_SUPPORT)` ;
- `D2D1CreateFactory` en multithread, puis `CreateDevice`, puis `CreateDeviceContext` ;
- `DCompositionCreateDevice2(d2dDevice)` ;
- `CreateTargetForHwnd(topmost=TRUE)`, `CreateVisual`, puis `CreateSurface(w, h, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED)`.

À chaque image : `BeginDraw` → `ID2D1DeviceContext` → `SetDpi(96,96)` → `EndDraw` → `Commit`. Une erreur `DXGI_ERROR_DEVICE_REMOVED`, `D2DERR_RECREATE_TARGET` ou `DCOMPOSITION_ERROR_SURFACE_BEING_RENDERED` fait retourner `false`.

- [ ] **Step 1 : implémenter.**
- [ ] **Step 2 : ajouter `-Target dock` à `build.ps1`** avec les bibliothèques `d3d11 dxgi dcomp d2d1 dwrite windowscodecs dwmapi shell32 shlwapi ole32 oleaut32 user32 gdi32 advapi32 propsys uxtheme version`, en `/SUBSYSTEM:WINDOWS`. Vérifier que la compilation réussit.
- [ ] **Step 3 : commit** `feat(render): rendu DirectComposition/Direct2D du Dock`.

### Task 14 : Contrôleur, fenêtre, interactions et intégration

**Files :** Create `src/app/dock_controller.h|.cpp`, `src/app/dock_window.h|.cpp`, `src/app/main.cpp`

**Interfaces :**

```cpp
namespace md {
class DockController {        // logique d'état de l'interface ; pas de HWND propre
public:
    void init(Settings, Metrics, AppModel*);
    void setMetrics(const Metrics&); void setSettings(const Settings&);
    void setCursor(std::optional<POINT> clientPx);      // nullopt = souris hors du Dock
    bool tick(double dt);                               // avance ressorts et rebonds ; true si animation en cours
    RenderFrame buildFrame(UINT clientW, UINT clientH, float scale, bool dark, IconProvider&);
    std::optional<std::size_t> hitTest(POINT clientPx) const;   // index de l'élément sous le point
    bool isInsideInteractiveZone(POINT clientPx) const;          // fond au repos ∪ icônes agrandies
    void startLaunchBounce(const std::wstring& appId);
    void stopLaunchBounce(const std::wstring& appId);   // première fenêtre de l'app apparue
    void setAttention(const std::wstring& appId, bool);
    double restThicknessPx(float scale) const;          // épaisseur réservée (AppBar) = thickness + dockScreenMargin
    double headroomPx(float scale) const;               // (largeSize - tileSize) + attentionBounceHeight*tile + tooltip
};
}
```
**Fenêtre** (`DockWindow`) :
- Styles : `WS_POPUP`, et `WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_LAYERED`, plus `WS_EX_TRANSPARENT` hors de la zone interactive.
- Taille : largeur de l'écran principal × (épaisseur au repos + marge + hauteur libre au-dessus), posée en bas. Les positions gauche et droite viennent dans le plan 3 ; la position est lue et le Dock reste en bas dans ce plan.
- AppBar : `SHAppBarMessage(ABM_NEW)` puis `ABM_SETPOS` pour réserver `restThicknessPx`, réappliqué sur `ABN_POSCHANGED`.
- Hook `WH_MOUSE_LL` : convertit le point en coordonnées client, appelle `setCursor`, active ou retire `WS_EX_TRANSPARENT` selon `isInsideInteractiveZone`, et lance la boucle d'animation.
- Boucle de messages : tant que `tick()` retourne vrai, on rend puis on appelle `DCompositionWaitForCompositorClock(0, nullptr, 20)` après avoir vidé les messages ; sinon `WaitMessage()`.

**Clics** (`WM_LBUTTONUP` sur l'élément touché) :
- App non ouverte → `launch` + `startLaunchBounce` ;
- App ouverte → `activateApp` ;
- AppsButton → `openStartMenu` ;
- Stack → `openFolder` ;
- MinimizedWindow → `restoreWindow` ;
- Trash → `openRecycleBin`.

**Câblage des événements :**
- `WindowTracker` → `AppModel`, et `stopLaunchBounce` à l'ouverture ;
- `flashed` → `setAttention(true)`, `activated` → `setAttention(false)`.

**Messages système :**
- `TaskbarCreated` → `rescan` + ré-enregistrement de l'AppBar ;
- `WM_DPICHANGED` / `WM_DISPLAYCHANGE` → recalcul de la taille et `resize` ;
- `WM_SETTINGCHANGE("ImmersiveColorSet")` → relecture de `AppsUseLightTheme` ;
- `FindFirstChangeNotificationW` sur `%APPDATA%\MacDock`, surveillé par un thread qui poste `WM_APP_CONFIG` → rechargement à chaud des mesures et réglages.

**Persistance :** les épingles sont enregistrées après chaque changement. Au premier lancement (`!pinnedInitialized`), `defaultPins()` est appliqué.

**`main.cpp` :**
- Déclare la sensibilité DPI `PER_MONITOR_AWARE_V2`, appelle `CoInitializeEx(APARTMENTTHREADED)` et `log::init`.
- Une instance unique est garantie par le mutex `Local\MacDock`.
- Démarre `PipeServer` sur `\\.\pipe\MacDock` ; les messages `Flash` reçus sont transmis au contrôleur, `Overlay` et `Progress` sont ignorés dans ce plan.
- Option `--trace-windows`. Sortie propre sur `WM_CLOSE` : `PipeServer::stop` (envoie `Goodbye`), `ABM_REMOVE`, code de sortie 0.
- Un gestionnaire `SetUnhandledExceptionFilter` journalise l'erreur et sort avec le code 3.

- [ ] **Step 1 : implémenter `DockController`** (`buildFrame` convertit `LayoutResult` en pixels : axe principal = x, origine au centre de l'écran ; y = bas du fond − padding − taille·0,5 − rebond).
- [ ] **Step 2 : implémenter `DockWindow` et `main.cpp`.**
- [ ] **Step 3 : compiler** `./build.ps1 -Target all -Config Release`, puis lancer `build\Release\MacDockLauncher.exe`.
- [ ] **Step 4 : vérifications manuelles :**
  - le Dock apparaît en bas, net, avec Explorateur, Apps, tes épingles, un séparateur, Téléchargements et la Corbeille ;
  - la magnification est fluide, la souris arrivant du côté gauche, du côté droit et du centre ;
  - l'infobulle affiche le nom ;
  - lancer le Bloc-notes → rebond jusqu'à l'ouverture, puis un point apparaît dessous ;
  - un clic sur une app ouverte la met au premier plan ;
  - réduire une fenêtre → miniature-icône à droite, un clic la restaure ;
  - les clics au-dessus du Dock, hors des icônes, traversent jusqu'au bureau ;
  - une fenêtre agrandie s'arrête au-dessus du Dock ;
  - modifier `dock-metrics.json` (`dockCornerRadius`) → effet immédiat ;
  - redémarrer l'Explorateur → le Dock survit ;
  - avec le mod actif : la barre est cachée, et tuer le Dock avec le lanceur arrêté la fait revenir en 5 s au plus.
- [ ] **Step 5 : commit** `feat(app): Dock fonctionnel de bout en bout`.

---

## Suite : plans 2 et 3 (rédigés après la livraison du plan 1)

- **Plan 2 – Liquid Glass et calibration :** capture Desktop Duplication avec exclusion de la fenêtre, flou séparable, shader HLSL (distance signée d'un rectangle à coins continus, réfraction du biseau, aberration chromatique, Fresnel, liseré spéculaire, teinte adaptative), repli acrylique, mode calibration (superposition d'une référence et carte de différence), collecte des références dans `reference/`, tests de rendu hors écran.
- **Plan 3 – Interactions avancées :** glisser-déposer interne et OLE, étiquette « Supprimer » et nuage « poof », menus contextuels en verre (app, séparateur, Corbeille, pile), piles en éventail, grille ou liste, miniatures DWM en direct, badges et progression relayés par le mod (hooks `ITaskbarList3`), masquage automatique, positions gauche et droite, multi-écran, masquage en plein écran, Corbeille dessinée vide ou pleine.
