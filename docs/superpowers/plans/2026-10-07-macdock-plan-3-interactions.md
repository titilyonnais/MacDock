# MacDock — Plan 3 : interactions avancées

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** donner au Dock les gestes et les retours de macOS qui lui manquent encore. Concrètement :
- glisser-déposer interne, avec « Supprimer » et nuage « poof » ;
- menus contextuels en verre au contenu complet ;
- Corbeille vide ou pleine ;
- masquage automatique et retrait en plein écran ;
- dépôt de fichiers et de raccourcis ;
- miniatures des fenêtres réduites.

**Architecture :**
- La logique reste testable : machine à états du glisser dans le contrôleur, présence animée dans la mise en page, modèle de menu et règles de dépôt purs.
- Le code Win32 est mince : fenêtre de sprite (`UpdateLayeredWindow`), fenêtre de menu en verre (DirectComposition, Direct2D, `GlassRenderer` et une seconde `BackdropCapture`), `IDropTarget` et `DwmRegisterThumbnail`.

**Tech Stack :** C++ `/std:c++latest`, MSVC, Win32, D3D11, Direct2D, DirectWrite, DirectComposition, OLE, DWM.

**Spec :** `docs/superpowers/specs/2026-10-06-macos-dock-design.md`, sections 4.3 à 4.7.

**Contexte :** plan écrit et exécuté en autonomie pendant la nuit du 7 octobre 2026, à la demande de l'utilisateur (« sans me demander quoique ce soit »). L'auteur du plan en est aussi l'exécutant. Chaque tâche donne donc ses interfaces, ses comportements et ses tests (nom et assertions), sans recopier le code d'implémentation. C'est un ruling, consigné dans le registre.

**Hors de ce plan (plan 4) :**
- positions gauche et droite (la mise en page n'a qu'un axe) ;
- multi-écran ;
- piles en éventail, en grille ou en liste ;
- badges et barre de progression (il faut des hooks `ITaskbarList3` dans le mod) ;
- « Ouvrir à la connexion » pour les apps empaquetées (AUMID).

## Global Constraints

- C++ `/std:c++latest`, MSVC, `/W4 /permissive- /EHsc /utf-8`, `UNICODE`, x64 ; SDK 10.0.26100.0.
- Aucune ressource Apple dans le dépôt. Le nuage « poof » est dessiné par le code.
- Les modules logiques n'incluent pas `<windows.h>` : `src/layout`, `src/model`, `src/anim`, `src/config`, `src/core`, `src/popup/menu_model.*` et `src/interact/drop_rules.*`. Le contrôleur, lui, l'inclut déjà.
- Toutes les durées et distances sont des mesures de `dock-metrics.json` (X-macro `metrics.h`, bornées). L'ajout de mesures ne change pas `kMetricsVersion` : les fichiers incomplets sont réécrits complets (`metricsJsonComplete`).
- Au repos, aucune image : le glisser, le nuage, le masquage et les menus n'animent que pendant leur durée.
- Messages de journal, libellés de menus et documentation en français, comme dans macOS en français.
- Un geste qui échoue (shell, OLE, DWM) ne plante pas le Dock : il est journalisé et ignoré.

## Review Focus

1. **Glisser hors du Dock puis retour** : l'icône revient à sa place si on la relâche dans le Dock ; elle n'est retirée que si on la relâche au-dessus du seuil. Une app ouverte n'est que désépinglée. Test : `controller_drag_*` (Task 1).
2. **Index du modèle et index affichés** : les séparateurs, le bouton Apps, les piles et les apps ouvertes non épinglées décalent les index. Un déplacement ou un épinglage doit viser le bon `pinned_`. Test : `controller_drag_maps_to_pinned_index` (Task 1).
3. **Menu ouvert pendant un changement d'état** (l'app se ferme, la configuration se recharge) : aucune action sur un élément disparu. Le menu travaille sur une copie de l'élément et sur des identifiants stables (clé, `appId`). Test : `menu_actions_use_item_snapshot` (Task 3).
4. **Masquage automatique et barre d'application** : un Dock masqué ne réserve aucune zone de travail. Désactiver le masquage rétablit la réservation. En plein écran, le Dock disparaît puis revient à la sortie. Tests : `autohide_*`, `fullscreen_*` (Task 5).
5. **Dépôt d'éléments inattendus** (dossier, plusieurs fichiers, fichier introuvable, zone vide) : aucun épinglage d'un fichier non exécutable, aucune action destructrice sans la Corbeille (`FOF_ALLOWUNDO`). Test : `drop_rules_*` (Task 6).

---

### Task 1 : Glisser-déposer interne, « Supprimer » et nuage « poof »

**Files :**
- Modify `src/layout/dock_layout.h|.cpp` : `LayoutItemSpec{separator, placeholder, presence}`.
- Modify `src/app/dock_controller.h|.cpp` : machine à états du glisser.
- Create `src/render/sprite_renderer.h|.cpp` : images du sprite et du nuage, D2D sur bitmap WIC.
- Create `src/app/sprite_window.h|.cpp` : fenêtre `UpdateLayeredWindow`.
- Modify `src/app/dock_window.cpp`, `src/config/metrics.h`.
- Tests : `tests/test_layout.cpp`, `tests/test_drag.cpp`, `tests/test_sprite.cpp`.

**Comportement**
- **Mise en page** : `presence` ∈ [0, 1] multiplie l'emplacement de l'élément et l'espace qui le précède. Un `placeholder` est un emplacement vide de la taille d'une case : il s'agrandit comme une icône et rien n'y est dessiné.
- **Seuil de glisser** : `dragThreshold` 4 pt. En dessous, l'appui reste un clic.
- **Éléments déplaçables**
  - apps épinglées et bouton Apps, dans la section épinglée ;
  - piles, dans la section des piles ;
  - apps ouvertes non épinglées : vers la section épinglée, ce qui les épingle.
  - La Corbeille, les séparateurs et les fenêtres réduites ne se déplacent pas.
- **Pendant le glisser**
  - l'élément d'origine se replie (`presence` → 0, ressort `dragStiffness`/`dragDamping`) ;
  - un `placeholder` s'ouvre à l'index d'insertion le plus proche du curseur, dans la section autorisée ;
  - si le curseur monte à plus de `dragRemoveDistance` (pt) au-dessus du haut du fond, le placeholder se referme et le sprite affiche « Supprimer », sauf pour une app ouverte (désépinglée, elle reste dans le Dock).
- **Relâchement**
  - **dans le Dock** : `DragOutcome::Move{fromPinned, toPinned}` ou `Pin{appId, toPinned}` ;
  - **au-dessus du seuil** : `Remove{key, poofAtScreen}`, avec nuage si l'élément quitte le Dock ;
  - **sans glisser** : `Click{index}`.
  - Les index rendus sont ceux de `AppModel::pinnedEntries()`, calculés en comptant les éléments épinglés de `items_` situés avant l'index d'insertion.
- **Sprite** : icône à la taille agrandie au moment de la prise (`scale` × taille), opacité 1, centrée sur le curseur. Sous l'icône, une étiquette capsule « Supprimer » en verre dépoli quand on est au-dessus du seuil.
- **Nuage** : 0,35 s (`poofSeconds`). Plusieurs disques blancs et gris s'écartent et s'estompent, dessinés par `SpriteRenderer::poofFrame(t, px)`.
- **Souris** : `SetCapture` à l'appui. `WM_MOUSEMOVE` et `WM_LBUTTONUP` arrivent pendant la capture, même hors du Dock. Échap annule le glisser (`cancelDrag`).
- **Mesures ajoutées** : `dragThreshold` 4, `dragRemoveDistance` 50, `dragStiffness` 400, `dragDamping` 34, `poofSeconds` 0.35.

**Interfaces (Produces)**
```cpp
// dock_layout.h
struct LayoutItemSpec { bool separator = false; bool placeholder = false; double presence = 1; };
// dock_controller.h
struct DragOutcome {
    enum class Kind { None, Click, Move, Pin, Remove } kind = Kind::None;
    std::size_t index = 0;                 // Click : index dans items()
    std::size_t fromPinned = 0, toPinned = 0;
    std::wstring key, appId;               // Remove : clé ; Pin : appId
    bool poof = false;                     // Remove : animation de nuage
};
void pointerDown(POINT clientPx);
void pointerMove(POINT clientPx);          // aussi hors du Dock (capture)
DragOutcome pointerUp(POINT clientPx);
void cancelDrag();
struct DragVisual { bool active = false; IconProvider::ImagePtr image; float sizePx = 0; bool removing = false; std::wstring key; };
DragVisual dragVisual(IconProvider& icons) const;
// sprite_renderer.h
std::vector<std::uint8_t> renderDragSprite(const IconProvider::Image& icon, UINT iconPx, const std::wstring& label,
                                           float scale, bool dark, const std::wstring& font, UINT& w, UINT& h);
std::vector<std::uint8_t> renderPoofFrame(double t01, UINT px);   // BGRA prémultiplié px × px
```

**Tests**
- `layout_presence_collapses_slot` : 3 icônes, la 2e à `presence` 0. La 3e est à `tile + gap` de la 1re, et le fond mesure `2·tile + gap + 2·padding`.
- `layout_placeholder_takes_a_tile` : un placeholder entre deux icônes donne le même fond que 3 icônes.
- `controller_click_without_drag` : appui puis relâchement à 2 pt d'écart donnent `Click` sur l'index appuyé.
- `controller_drag_reorders_pinned` : on glisse la 1re app épinglée jusqu'au centre de la 3e. Le résultat est `Move{0, 2}`.
- `controller_drag_maps_to_pinned_index` : avec le bouton Apps en 2e épingle et une app ouverte non épinglée, la cible de l'index affiché 3 donne le bon `toPinned`.
- `controller_drag_above_threshold_removes` : relâcher à `dragRemoveDistance + 10` pt au-dessus du fond donne `Remove`, avec `poof` à vrai pour une app fermée et à faux pour une app ouverte.
- `controller_drag_trash_not_draggable` : un glisser sur la Corbeille donne `None`, puis `Click` au relâchement.
- `controller_drag_unpinned_running_pins` : l'app ouverte non épinglée déposée dans la section épinglée donne `Pin{appId, k}`.
- `controller_drag_cancel_restores` : `cancelDrag` remet chaque élément à `presence` 1 à la fin de l'animation, sans placeholder.
- `sprite_label_adds_height` : le sprite avec « Supprimer » est plus haut que sans.
- `poof_fades_out` : l'alpha total de l'image à t = 0,9 est inférieur au tiers de celui à t = 0,2.

**Étapes**
- [ ] Step 1 : écrire les tests ci-dessus. **Step 2 :** constater l'échec à la compilation.
- [ ] Step 3 : implémenter la mise en page, le contrôleur, le moteur de sprite, la fenêtre de sprite et le branchement dans `DockApp` (appui, déplacement, relâchement, Échap, `savePinned` après chaque changement).
- [ ] Step 4 : tests verts, construction Release, essai réel. Lancer le Dock, simuler un glisser par `SendInput` dans un script PowerShell, puis vérifier le journal et `settings.json`.
- [ ] Step 5 : commit `feat(dock): glisser-déposer, Supprimer et nuage poof`.

### Task 2 : Menus en verre (infrastructure)

**Files :**
- Create `src/popup/menu_model.h|.cpp` : modèle pur (entrées, séparateurs, sous-menus, coches, désactivées, mise en page en points, navigation au clavier).
- Create `src/popup/menu_window.h|.cpp` : fenêtre, DirectComposition, Direct2D, verre.
- Modify `build.ps1` : `src\popup\menu_model.cpp` dans `$LogicSources`, `src\popup\*.cpp` dans la cible dock.
- Tests : `tests/test_menu_model.cpp`.

**Comportement (mesures macOS Tahoe, estimées)**
- **Panneau en verre** : rayon 12 pt, marge intérieure 5 pt.
- **Entrée** : 24 pt de haut, texte 13 pt, retrait gauche de 20 pt (place de la coche ✓), flèche › à droite pour un sous-menu.
- **Séparateur** : 1 pt de trait, 5 pt de marge verticale.
- **Survol** : capsule d'accent (`COLOR_HIGHLIGHT` ou accent Windows) de rayon 6 pt, texte blanc.
- **Désactivée** : texte à 35 % d'opacité.
- **Sous-menu** : il s'ouvre au survol après 0,2 s, à droite, ou à gauche s'il n'y a pas la place.
- **Clavier** : Haut et Bas sautent les séparateurs et les entrées désactivées ; Droite ouvre le sous-menu, Gauche le referme ; Entrée valide ; Échap ferme.
- **Fermeture** : clic à l'extérieur, perte d'activation, Échap ou choix d'une entrée.
- **Position** : au-dessus du point d'ancrage, centrée, gardée dans le moniteur.
- **Apparition** : fondu de 0,12 s.
- **API modale** : `int MenuWindow::track(const MenuModel&, POINT anchorScreen, ...)` renvoie l'identifiant choisi, ou 0. Elle tourne dans sa propre boucle de messages, comme `TrackPopupMenu`.
- **Verre** : `GlassRenderer` sur le device D3D du Dock et une `BackdropCapture` dont la région est le rectangle du menu. Le menu est exclu des captures, comme le Dock. Si `settings.glass` est faux ou si la capture est indisponible, le panneau passe en verre dépoli Direct2D.

**Interfaces (Produces)**
```cpp
struct MenuItem {
    int id = 0;                     // 0 = séparateur
    std::wstring text;
    bool checked = false, enabled = true;
    std::vector<MenuItem> submenu;
};
struct MenuModel { std::vector<MenuItem> items; };
struct MenuLayout { double width = 0, height = 0; std::vector<double> top; };   // points
MenuLayout layoutMenu(const MenuModel& m, double textWidthMax);                 // largeur selon le texte le plus long
int nextSelectable(const MenuModel& m, int from, int dir);                       // -1 si aucune
int hitTestMenu(const MenuLayout& l, const MenuModel& m, double yPoints);        // index d'entrée ou -1
```

**Tests**
- `menu_layout_heights` : 2 entrées, un séparateur, 1 entrée donnent `3·24 + 11 + 2·5` pt.
- `menu_keyboard_skips_separators_and_disabled` : partir de la 1re entrée et descendre saute un séparateur et une entrée désactivée.
- `menu_keyboard_wraps` : au-delà de la fin, retour à la première entrée sélectionnable.
- `menu_hit_test` : un point au milieu de la 3e ligne donne l'index 3 ; un point sur un séparateur donne -1.

**Étapes**
- [ ] Step 1-2 : tests rouges.
- [ ] Step 3 : modèle, puis fenêtre.
- [ ] Step 4 : tests verts. Essai réel avec une option de diagnostic `--menu-test` : un menu de démonstration s'ouvre au centre de l'écran, et le choix est journalisé. Pour vérifier, simuler Bas, Bas, Entrée par `SendInput` et lire le journal.
- [ ] Step 5 : commit `feat(popup): menus contextuels en verre`.

### Task 3 : Contenu des menus (spec 4.5)

**Files :**
- Create `src/app/dock_menus.h|.cpp` : construction pure du menu selon l'élément et l'état.
- Modify `src/app/dock_window.cpp` : `showContextMenu` passe par `MenuWindow` et exécute les actions.
- Modify `src/shell/shell_actions.h|.cpp` : `setOpenAtLogin(identity, bool)` et `openAtLogin(identity)`, via `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, valeur `MacDock:<nom>` ; `emptyRecycleBin()` avec la confirmation standard (`SHEmptyRecycleBinW` sans `SHERB_NOCONFIRMATION`) ; `minimizeAll` est désormais utilisé.
- Modify `src/app/dock_controller.h|.cpp` : `hitTestAny` reconnaît aussi le séparateur.
- Tests : `tests/test_dock_menus.cpp`.

**Contenu**
- **App**
  - Options › Garder dans le Dock (coché si épinglée), Ouvrir à la connexion (coché si active ; désactivée sans exe), Afficher dans l'Explorateur ;
  - puis la liste des fenêtres (titres, 40 caractères au plus ; un choix active la fenêtre) ;
  - puis Afficher toutes les fenêtres, Masquer, Quitter ; si l'app est fermée, Ouvrir à la place des trois.
- **Séparateur** : Activer le masquage (coché selon `autohide`), Activer l'agrandissement (coché selon `magnification`), Position à l'écran › Gauche, En bas (coché), Droite. Gauche et Droite sont désactivées jusqu'au plan 4. Puis Réglages du Dock…
- **Corbeille** : Ouvrir, Vider la Corbeille (désactivée si elle est vide).
- **Pile** : Ouvrir dans l'Explorateur, Retirer du Dock.
- **Bouton Apps** : Retirer du Dock.
- **Partout**, après un séparateur : Quitter MacDock.
- **Actions**
  - les réglages modifiés (`autohide`, `magnification`) sont écrits dans `settings.json`, qui se recharge à chaud ;
  - Masquer appelle `minimizeAll` ; les fenêtres masquées ne créent pas de miniatures, car le modèle les marque `hidden`.

**Interfaces**
```cpp
enum MenuCmd : int { kCmdOpen = 1, kCmdKeep, kCmdLogin, kCmdReveal, kCmdShowAll, kCmdHide, kCmdQuit,
                     kCmdAutohide, kCmdMagnify, kCmdPosLeft, kCmdPosBottom, kCmdPosRight, kCmdSettings,
                     kCmdTrashOpen, kCmdTrashEmpty, kCmdRemove, kCmdQuitDock, kCmdWindowBase = 1000 };
struct MenuContext { DockItem item; bool openAtLogin = false, trashFull = false; Settings settings;
                     std::vector<std::pair<WindowId, std::wstring>> windows; };
MenuModel buildDockMenu(const MenuContext& c);
```

**Tests**
- `menu_app_running_has_quit_and_windows` : une app ouverte avec 2 fenêtres a 2 entrées de fenêtre (identifiants `kCmdWindowBase + i`), puis Quitter.
- `menu_app_closed_has_open` : une app fermée a Ouvrir, et ni Quitter ni Masquer.
- `menu_separator_checks_settings` : Agrandissement est coché si `magnification` est vrai.
- `menu_trash_empty_disabled_when_empty` : Vider la Corbeille est désactivée quand `trashFull` est faux.
- `menu_actions_use_item_snapshot` : le contexte contient une copie de `DockItem` ; modifier ensuite le modèle ne change pas le menu construit.
- `controller_hit_test_separator` : un clic sur le séparateur est reconnu par `hitTestAny`.

**Étapes**
- [ ] Step 1-2 rouges, Step 3 implémentation, Step 4 tests verts et essai réel (clic droit simulé, puis capture du journal).
- [ ] Step 5 : commit `feat(dock): menus complets (Options, fenêtres, Masquer, séparateur, Corbeille)`.

### Task 4 : Corbeille vide ou pleine

**Files :**
- Modify `src/shell/shell_actions.*` : `bool recycleBinHasItems()`, via `SHQueryRecycleBinW` sur `nullptr` (tous les lecteurs).
- Modify `src/icons/icon_provider.*` : la clé `trash` sert `SIID_RECYCLER` ou `SIID_RECYCLERFULL` selon l'état.
- Modify `src/model/app_model.*` : `setTrashFull(bool)` incrémente la révision.
- Modify `src/app/dock_window.cpp` : `SHChangeNotifyRegister` sur la Corbeille (`CSIDL_BITBUCKET`), avec les événements de suppression, création, renommage et `SHCNE_UPDATEDIR`. Le message relance une requête différée de 300 ms.

**Tests :** `model_trash_full_changes_revision_and_item` (`DockItem::trashFull`), puis `icons_trash_full_differs`. Ce dernier compare le contenu des deux icônes : elles ne doivent pas être identiques.

**Étapes :** rouge, implémentation, vert, essai réel (mettre un fichier temporaire à la Corbeille, puis l'en retirer avec `IFileOperation`, et vérifier le journal `[trace] corbeille pleine/vide`). Commit `feat(dock): Corbeille vide ou pleine`.

### Task 5 : Masquage automatique et plein écran

**Files :**
- Create `src/app/visibility.h|.cpp` : logique pure du masquage.
- Modify `src/app/dock_controller.*` : décalage de masquage dans `buildFrame` (translation vers le bas) et zone interactive.
- Modify `src/app/dock_window.cpp` : barre d'application, hook souris, détection du plein écran.
- Modify `metrics.h` : `autohideDelay` 0.0, `autohideShowSeconds` 0.45, `autohideHideSeconds` 0.45, `autohideEdgePx` 2.
- Tests : `tests/test_visibility.cpp`.

**Comportement**
- `VisibilityState` : `shown` (0..1) est animé par une courbe ease-in-out sur la durée indiquée.
  - **Entrées** : `autohide`, `fullscreen`, `cursorAtEdge`, `cursorInDock`, `menuOpen`, `dragging`.
  - **Cible** : visible si `!fullscreen && (!autohide || cursorAtEdge || cursorInDock || menuOpen || dragging)`.
  - Le passage au masquage se fait 0,5 s après la sortie du curseur. C'est le comportement de macOS : le Dock reste un instant.
- **Plein écran** : la fenêtre au premier plan couvre tout le moniteur du Dock et n'est ni le bureau (`Progman`, `WorkerW`) ni la barre des tâches. Fonction pure `coversMonitor(windowRect, monitorRect)`, avec une tolérance de 1 px. La vérification est déclenchée par `EVENT_SYSTEM_FOREGROUND` et `EVENT_OBJECT_LOCATIONCHANGE` sur la fenêtre au premier plan, relayés par le tracker.
- **Barre d'application** : si `autohide` est vrai, la réservation est retirée (`ABM_REMOVE`) ; s'il redevient faux, elle est rétablie. En plein écran, le Dock ne réserve rien de plus.
- **Fenêtre** : quand le Dock est complètement masqué, elle reste en place mais n'est plus dessinée, et `WS_EX_TRANSPARENT` reste actif. Le bord est détecté par le hook souris : y ≥ bas du moniteur − `autohideEdgePx`.

**Tests**
- `autohide_shows_at_edge_then_hides_after_delay` ;
- `autohide_off_always_shown` ;
- `fullscreen_hides_even_without_autohide` ;
- `fullscreen_cover_detection_tolerance` ;
- `autohide_menu_keeps_shown`.

**Étapes :** rouge, implémentation, vert, essai réel (`autohide` vrai dans `settings.json` ; `--trace-windows` journalise la zone réservée et l'état). Commit `feat(dock): masquage automatique et retrait en plein écran`.

### Task 6 : Dépôt de fichiers et de raccourcis

**Files :**
- Create `src/interact/drop_rules.h|.cpp` : règles pures.
- Create `src/app/drop_target.h|.cpp` : `IDropTarget`, `CF_HDROP`.
- Modify `src/app/main.cpp` : `OleInitialize` au lieu de `CoInitializeEx`.
- Modify `src/app/dock_window.cpp`, `src/app/dock_controller.*` : retour visuel du survol pendant un dépôt (placeholder dans la section épinglée, ou icône ciblée assombrie à 70 %).
- Modify `src/shell/shell_actions.*` : `openWith`, `recycle`, `moveInto`.
- Tests : `tests/test_drop_rules.cpp`.

**Règles**
- Un seul fichier `.exe`, `.lnk` ou `.appref-ms`, déposé dans la section épinglée (entre deux icônes) : **épingler** à l'index d'insertion. Pour un `.lnk`, l'identité est résolue par la cible (`IShellLink`), comme les épingles par défaut.
- Des fichiers sur une app : **ouvrir avec cette app**.
  - exe : `ShellExecuteEx` sur l'exe, avec les chemins entre guillemets en paramètres ;
  - AUMID : `IApplicationActivationManager::ActivateForFile`.
- Des fichiers sur la Corbeille : **mettre à la Corbeille** (`IFileOperation` avec `FOFX_RECYCLEONDELETE`, et l'interface de progression standard).
- Des fichiers sur une pile : **déplacer dans le dossier** (`IFileOperation::MoveItem`).
- Tout le reste, y compris un dossier sur la zone épinglée, des fichiers entre deux apps ou une zone vide : **refusé** (`DROPEFFECT_NONE`).

**Interfaces**
```cpp
enum class DropAction { None, Pin, OpenWith, Recycle, MoveInto };
struct DropTargetInfo { ItemKind kind; bool betweenPinned = false; };
DropAction dropAction(const DropTargetInfo& target, const std::vector<std::wstring>& paths);
bool isPinnableFile(const std::wstring& path);   // .exe .lnk .appref-ms, insensible à la casse
```

**Tests**
- `drop_rules_pin_single_exe_between_pinned` ;
- `drop_rules_folder_not_pinned` ;
- `drop_rules_two_exes_not_pinned` ;
- `drop_rules_files_on_app_open_with` ;
- `drop_rules_on_trash_recycle` ;
- `drop_rules_on_stack_move` ;
- `drop_rules_empty_none` ;
- `drop_rules_case_insensitive`.

**Étapes :** rouge, implémentation, vert, essai réel (glisser simulé difficile : vérifier `RegisterDragDrop == S_OK` dans le journal ; vérification manuelle listée). Commit `feat(dock): dépôt de fichiers et épinglage par glisser`.

### Task 7 : Miniatures des fenêtres réduites

**Files :**
- Create `src/app/thumbnails.h|.cpp`, avec la géométrie pure `fitThumbnail`.
- Modify `src/app/dock_window.cpp` : enregistrement et mise à jour à chaque image.
- Modify `src/app/dock_controller.*` : `RenderIcon::window` et le rectangle de la case.
- Modify `src/render/dock_renderer.cpp` : la case d'une fenêtre réduite dessine une plaque, puis la miniature DWM s'affiche au-dessus.
- Tests : `tests/test_thumbnails.cpp`.

**Comportement**
- Chaque fenêtre réduite affichée reçoit un `DwmRegisterThumbnail(hwnd_ du Dock, fenêtre)`.
- À chaque image, `DwmUpdateThumbnailProperties` place la miniature dans la case :
  - la forme visible fait 0,80 de la case, centrée et ajustée au ratio de la source (`DwmQueryThumbnailSourceSize`) ;
  - opacité 255, `fSourceClientAreaOnly` à faux.
- Une petite icône de l'app (35 % de la case) est dessinée par le Dock dans le coin bas droit. Elle se trouve sous la miniature dans l'ordre DWM, donc décalée de façon à dépasser du coin. Ruling : la miniature DWM est toujours au-dessus du contenu de la fenêtre.
- Les miniatures sont retirées (`DwmUnregisterThumbnail`) quand la fenêtre est restaurée, fermée ou masquée par l'app, ou quand le Dock est masqué.

**Tests**
- `thumbnail_fit_landscape` : une source 1600×900 dans une case de 100 px donne un rectangle de 80×45, centré.
- `thumbnail_fit_portrait` ;
- `thumbnail_fit_zero_source` : rectangle vide, sans division par zéro.

**Étapes :** rouge, implémentation, vert, essai réel (réduire une fenêtre de Bloc-notes lancée par le test, vérifier `[trace] miniature enregistrée` dans le journal, puis restaurer et fermer). Commit `feat(dock): miniatures en direct des fenêtres réduites`.

---

## Fin du plan
- Relecture finale par un agent sur le modèle le plus capable, puis une passe de corrections en TDD.
- Mise à jour du README (gestes, menus, masquage, dépôt) et du journal de nuit.
- Fusion locale dans `main`.
