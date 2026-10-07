# MacMenuBar — Plan 7 : vrais menus et Éléments récents

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** la barre montre les vrais menus de l'app active quand elle en a :
- barre de menus Win32 (`HMENU`) ;
- barre de menus exposée par UI Automation.

Leurs commandes s'exécutent dans l'app. Le menu du système gagne « Éléments récents » (applications et documents).

**Architecture :**
- **Modèle commun.** Un modèle pur d'entrées lues (`RawMenuItem`) est rempli par deux lecteurs :
  - `win32_menu` : `HMENU`, synchrone et rapide ;
  - `uia_menu` : UI Automation, sur un fil de travail, avec des délais courts.
- **Construction des menus.** `buildBarMenus` reçoit les titres et les entrées lues et produit les menus de verre et leurs actions : `MenuCommand` (`WM_COMMAND`) ou `UiaInvoke` (chemin dans l'arbre UIA).
- **Éléments récents.** Une liste d'apps récentes (pure, gardée dans `menubar.json`) et les documents du dossier Récents de Windows.

**Tech Stack :** C++ `/std:c++latest`, Win32 (menus), UI Automation (COM), Shell.

**Spec :** `docs/superpowers/specs/2026-10-07-macmenubar-design.md`, sections 4.4 et 4.8.

## Global Constraints
- Aucune ressource Apple ; messages et commentaires en français, identifiants en anglais.
- Toute logique pure a ses tests, et `build.ps1 -Target tests -Run` reste vert.
- La barre ne se bloque jamais sur une app figée :
  - `SendMessageTimeout` (200 ms, `SMTO_ABORTIFHUNG`) pour les messages de menu ;
  - délais UI Automation de 1 s pour la connexion et de 1,5 s pour la transaction ;
  - UI Automation sur un fil de travail.
- Les fenêtres Chromium, Electron et Firefox ne sont jamais interrogées par UI Automation.
- Aucun essai qui pilote la souris ou le clavier ; aucune app de l'utilisateur n'est ouverte ni modifiée par un test. Les tests créent leurs propres fenêtres, hors écran.
- Le dossier Récents de Windows n'est jamais modifié.

## Review Focus
1. **App figée ou lente** pendant la lecture d'un menu. La barre reste réactive : délais, fil de travail. Tests : `uia_menus_titles_from_win32_window` (délais posés) et revue de `refreshWin32Popup`.
2. **Entrées owner-draw, séparateurs, sous-menus imbriqués et `&&`** dans les menus Win32. Rendu fidèle, rien d'illisible. Tests : `win32_menu_reads_nested_and_states`, `menu_label_parses_mnemonic_and_shortcut`.
3. **Menu de l'app qui change pendant que la barre l'affiche** (fichiers récents, entrée grisée). Il est relu à chaque ouverture. Test : `win32_menu_reads_nested_and_states` (relecture après modification).
4. **Chromium et Firefox** : jamais interrogés. Test : `uia_probe_skips_chromium_firefox`.
5. **Liste des apps récentes** : pas de doublon, 10 au plus, la plus récente en tête, survit au redémarrage. Tests : `recent_apps_mru_dedupes_and_caps`, `menubar_settings_recent_roundtrip`.

---

### Task 1 : Menus Win32

**Files :**
- Create `src/menubar/win32_menu.h|.cpp`
- Modify `src/menubar/app_menus.h|.cpp` :
  - `BarContext::source`, `realTitles`, `realItems`, `menuOwner` ;
  - `ActionKind::MenuCommand`, `MenuAction::command` ;
  - menu Fenêtre ajouté s'il manque.
- Modify `src/menubar/bar_actions.cpp` : `MenuCommand` (premier plan, puis `PostMessage(WM_COMMAND)`).
- Modify `src/menubar/menubar_window.*` : titres lus au changement d'app ; sous-menu relu à l'ouverture.
- Test : `tests/test_menubar.cpp`

**Interfaces :**
```cpp
struct RawMenuItem {
    std::wstring text, shortcut;
    UINT id = 0;                 // Win32 : identifiant de WM_COMMAND
    bool separator = false, enabled = true, checked = false;
    std::vector<RawMenuItem> children;
};
struct MenuLabel { std::wstring text, shortcut; };
MenuLabel parseMenuLabel(std::wstring_view raw);   // « &Enregistrer\tCtrl+S » → « Enregistrer », « Ctrl+S »
std::vector<RawMenuItem> readWin32Menu(HMENU menu, int maxDepth = 4);
std::vector<std::wstring> win32MenuTitles(HMENU bar);
void refreshWin32Popup(HWND owner, HMENU bar, int index);   // WM_INITMENU + WM_INITMENUPOPUP, 200 ms au plus chacun

enum class MenuSource { Generic, Win32, Uia };
// BarContext : MenuSource source; std::vector<std::wstring> realTitles;
//              std::vector<std::vector<RawMenuItem>> realItems; std::uint64_t menuOwner;
// MenuAction : int command (Win32 : identifiant) ; std::vector<int> path (UIA : titre, entrée[, sous-entrée]).
```

**Tests :**
- `menu_label_parses_mnemonic_and_shortcut`
- `win32_menu_reads_nested_and_states` (menu construit dans le test : sous-menus, séparateur, entrée grisée, coche, owner-draw omise, relecture après modification)
- `win32_menu_titles`
- `menus_real_win32_replace_generic`
- `menus_real_adds_window_menu_once`

**Étapes :** rouge, implémentation, vert, puis commit `feat(menubar): vrais menus Win32`.

### Task 2 : Menus UI Automation

**Files :**
- Create `src/menubar/uia_menu.h|.cpp` :
  - `shouldProbeUia` (pur) ;
  - `UiaMenus` (lecteur) ;
  - `UiaWorker` (fil de travail, requêtes, résultats postés).
- Modify `src/menubar/app_menus.cpp` : actions `UiaInvoke` (chemin et nom).
- Modify `src/menubar/menubar_window.*` :
  - titres demandés au fil quand l'app active n'a pas de `HMENU` ;
  - entrées lues à l'ouverture, avec une attente bornée et la boucle de messages active ;
  - invocation.
- Modify `build.ps1` : `uia_menu.cpp` dans les tests et la barre.
- Test : `tests/test_menubar.cpp`

**Interfaces :**
```cpp
bool shouldProbeUia(std::wstring_view className);   // false : Chrome_WidgetWin_*, MozillaWindowClass, fenêtres système
class UiaMenus {                                      // un seul fil : celui qui l'a initialisé
public:
    bool init();                                      // délais : connexion 1 s, transaction 1,5 s
    std::vector<std::wstring> titles(HWND window);    // barre de menus hors barre système ; vide sinon
    std::vector<RawMenuItem> items(HWND window, int title);   // déplie, lit les entrées, replie
    bool invoke(HWND window, const std::vector<int>& path, const std::wstring& name);
};
```

**Tests :**
- `uia_probe_skips_chromium_firefox`
- `uia_menus_titles_from_win32_window` : fenêtre de test avec `HMENU`, sur un fil qui traite ses messages, hors écran ; ses titres sont lus par UI Automation.
- `menus_real_uia_actions_carry_path`

**Étapes :** rouge, implémentation, vert, puis commit `feat(menubar): menus lus par UI Automation`.

### Task 3 : Éléments récents

**Files :**
- Create `src/menubar/recent_items.h|.cpp` : MRU des apps (pur) et documents du dossier Récents.
- Modify `src/menubar/menubar_settings.*` : `recentApps`, `recentClearedAt`.
- Modify `src/menubar/app_menus.*` :
  - sous-menu « Éléments récents » ;
  - `ActionKind::ClearRecent`.
- Modify `src/menubar/menubar_window.cpp` :
  - app ajoutée à la liste à chaque changement d'app ;
  - icônes des documents ;
  - Effacer.
- Test : `tests/test_menubar.cpp`

**Interfaces :**
```cpp
struct RecentEntry { std::wstring name, target; };
void pushRecent(std::vector<RecentEntry>& list, RecentEntry e, std::size_t max = 10);   // en tête, sans doublon
std::vector<RecentEntry> recentDocuments(const std::wstring& folder, std::size_t max, std::uint64_t clearedAt);
```

**Tests :**
- `recent_apps_mru_dedupes_and_caps`
- `recent_documents_sorted_and_cleared` (dossier temporaire de raccourcis factices)
- `menubar_settings_recent_roundtrip`
- `menus_logo_recent_items`

**Étapes :** rouge, implémentation, vert, puis commit `feat(menubar): éléments récents`.

---

## Fin du plan
- Relecture finale par un agent sur le modèle le plus capable, puis une passe de corrections en TDD.
- README et journal.
- Fusion locale dans `main`.
