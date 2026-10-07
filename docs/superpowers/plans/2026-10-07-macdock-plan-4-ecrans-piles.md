# MacDock — Plan 4 : positions, écrans et piles

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** Dock à gauche ou à droite, Dock qui suit l'écran où l'on pousse le curseur, piles qui s'ouvrent en éventail ou en grille de verre, et « Ouvrir à la connexion » pour les apps empaquetées.

**Architecture :** le contrôleur garde ses calculs dans un repère « de bord » (axe principal `u`, distance au bord `v`) ; une transformation pure `EdgeFrame` convertit vers les pixels de la fenêtre et retour. Les piles sont une nouvelle fenêtre modale en verre (`StackWindow`), sur le modèle de `MenuWindow` (DComp, une capture d'écran par session). Le multi-écran réutilise le hook souris existant.

**Tech Stack :** C++ `/std:c++latest`, MSVC, Win32, D3D11, Direct2D, DirectComposition, Shell (IShellItem, IShellItemImageFactory).

**Spec :** `docs/superpowers/specs/2026-10-06-macos-dock-design.md` (sections 4.3, 4.5, 4.6, 4.7).

## Global Constraints
- Aucune ressource Apple dans le dépôt.
- Police : SF Pro si installée, sinon Inter, sinon Segoe UI Variable.
- Messages, journaux et commentaires en français ; identifiants en anglais.
- Toute nouvelle logique pure a ses tests dans `tests/` ; `build.ps1 -Target tests -Run` reste vert.
- Une seule duplication d'écran par processus (voir le Ruling de la tâche 3 du plan 3) : toute nouvelle fenêtre en verre suspend la capture du Dock le temps de sa session.
- Badges et barre de progression (spec 4.2) restent hors plan : le mod Windhawk ne relaie pas `SetOverlayIcon`/`SetProgressValue`, et le mod n'est pas installé sur la machine de test.

## Review Focus
1. **Changement de position à chaud** (bas → gauche → droite) : zone réservée sur le bon bord, aucune zone fantôme laissée sur l'ancien bord, Dock entièrement visible. Test : `edge_frame_roundtrip_*` (Task 1) et essai réel.
2. **Coordonnées d'entrée sur un Dock vertical** : clic, glisser, dépôt et menu visent le bon élément ; « Supprimer » s'active en s'éloignant du bord, pas vers le haut. Test : `controller_vertical_hit_test`, `controller_vertical_drag_remove` (Task 1).
3. **Écran débranché ou changement de résolution** pendant que le Dock est sur un écran secondaire : retour sur l'écran principal sans plantage. Test : `monitor_choice_falls_back_to_primary` (Task 2).
4. **Pile vide, dossier introuvable ou très grand** (des milliers de fichiers) : la pile s'ouvre vite, montre au plus N éléments et « Ouvrir dans l'Explorateur ». Test : `stack_layout_caps_items`, `stack_items_sorted_by_date` (Task 3).
5. **Ouverture à la connexion d'une app empaquetée** : le raccourci créé dans le dossier Démarrage lance bien l'AUMID ; décocher le supprime. Test : `startup_shortcut_name_is_stable` (Task 4).

---

### Task 1 : Dock à gauche et à droite

**Files :**
- Create `src/layout/edge_frame.h|.cpp` : transformation pure repère de bord ↔ fenêtre.
- Modify `src/app/dock_controller.*` : entrées converties par `EdgeFrame::toLocal`, sortie de `buildFrame` par `EdgeFrame::toWindow` ; « Supprimer » selon la distance au bord.
- Modify `src/render/dock_renderer.*` : `RenderTooltip` gagne un côté (`Above`, `Right`, `Left`) et un point d'ancrage ; le point indicateur est posé côté bord.
- Modify `src/app/dock_window.cpp` : taille et place de la fenêtre, bord de la barre d'application (`ABE_LEFT`, `ABE_RIGHT`), bord du masquage automatique, ancrage des menus à côté de l'icône, commandes `kCmdPosLeft|Bottom|Right` actives.
- Modify `src/app/dock_menus.cpp` : Gauche et Droite activées.
- Modify `src/popup/menu_window.*` : `track` reçoit un côté d'ouverture (au-dessus, à droite, à gauche).
- Tests : `tests/test_edge_frame.cpp`, ajouts dans `tests/test_controller.cpp`, `tests/test_dock_menus.cpp`.

**Interfaces :**
```cpp
enum class DockPosition { Bottom, Left, Right };   // existe déjà (settings.h)
struct EdgeFrame {
    DockPosition edge = DockPosition::Bottom;
    double width = 0, height = 0;   // fenêtre, px
    // Repère local : u = position sur l'axe principal (px, 0 au début de la fenêtre),
    // v = distance au bord de l'écran (px, 0 au bord, croît vers l'intérieur).
    POINT toLocal(POINT window) const;
    D2D1_POINT_2F toWindow(double u, double v) const;
    double axisLength() const;      // largeur (bas) ou hauteur (gauche/droite)
};
```
Le contrôleur calcule en local exactement comme aujourd'hui en bas (`u` = x, `v` = hauteur − y) ; `buildFrame` convertit le fond, les icônes, les points et l'infobulle.

**Tests :**
- `edge_frame_roundtrip_bottom|left|right` : `toWindow(toLocal(p)) == p`.
- `edge_frame_left_edge_is_x0` : à gauche, `v = x` ; à droite, `v = largeur − x`.
- `controller_vertical_hit_test` : Dock à gauche, le centre de chaque icône (pris dans `buildFrame`) renvoie son index.
- `controller_vertical_drag_remove` : Dock à droite, tirer une épingle de 60 pt vers la gauche active « Supprimer » ; vers le haut, non.
- `dock_menus_position_left_right_enabled`.

**Étapes :** rouge, implémentation, vert, essai réel (`position` à `left` puis `right` dans `settings.json` ; `--trace-windows` journalise la zone réservée et les positions des éléments ; capture avec `glass` à faux). Commit `feat(dock): positions gauche et droite`.

### Task 2 : Multi-écran

**Files :**
- Create `src/app/monitor_choice.h|.cpp` : règle pure.
- Modify `src/app/dock_window.cpp` : `monitor_` choisi par la règle ; déplacement de la fenêtre, de la zone réservée et de la capture ; `WM_DISPLAYCHANGE` réévalue.
- Modify `src/config/settings.*` : `screen` (identifiant `\\.\DISPLAYn` du dernier écran choisi, vide = principal).

**Comportement :** le Dock passe sur l'écran où le curseur est **poussé contre le bord du Dock** (bord bas, gauche ou droit selon la position) pendant au moins 0,35 s, et y reste. Au démarrage, il reprend l'écran enregistré s'il existe encore, sinon le principal.

**Interfaces :**
```cpp
struct MonitorInfo { std::wstring name; RECT rect; bool primary; };
// Écran visé par une poussée au bord (nullopt si le curseur n'est contre le bord d'aucun écran).
std::optional<std::size_t> pushedMonitor(const std::vector<MonitorInfo>& monitors, POINT cursor, DockPosition edge, int edgePx);
std::size_t initialMonitor(const std::vector<MonitorInfo>& monitors, const std::wstring& saved);
```
**Tests :** `monitor_push_bottom_edge`, `monitor_push_ignores_inner_edges` (bord commun à deux écrans côte à côte : seul un vrai bord d'écran compte), `monitor_choice_falls_back_to_primary`.

**Étapes :** rouge, implémentation, vert, essai réel limité (une seule machine à un écran : vérifier `initialMonitor` avec un nom inconnu et la trace « écran du Dock »). Commit `feat(dock): le Dock suit l'écran choisi`.

### Task 3 : Piles en éventail et en grille

**Files :**
- Create `src/stack/stack_model.h|.cpp` : énumération du dossier (pure côté tri et plafond), `StackItem{path, name, modified, isFolder}`.
- Create `src/stack/stack_layout.h|.cpp` : géométrie pure de l'éventail (arc) et de la grille.
- Create `src/popup/stack_window.h|.cpp` : fenêtre modale en verre (DComp + `GlassRenderer`, capture unique par session), icônes via `IShellItemImageFactory`, clic = ouverture, clic hors = fermeture, Échap.
- Modify `src/config/settings.*` : `PinnedEntry` gagne `stackView` (`fan`, `grid`, `auto`) et `stackSort` (`dateAdded`, `name`, `modified`, `kind`).
- Modify `src/app/dock_menus.cpp` : menu de pile complet (Afficher comme, Présentation, Trier par, Ouvrir dans l'Explorateur, Retirer du Dock).
- Modify `src/app/dock_window.cpp` : clic sur une pile → `StackWindow::track`.

**Comportement :** `auto` = éventail jusqu'à 9 éléments, grille au-delà (comme macOS). Éventail : jusqu'à 15 éléments empilés en arc au-dessus de l'icône, nom à gauche de chaque icône, « Ouvrir dans l'Explorateur » en bas. Grille : panneau de verre de 5 colonnes au plus, 48 éléments au plus, défilement à la molette. Tri par défaut : date d'ajout (date de création), plus récent en premier.

**Interfaces :**
```cpp
enum class StackView { Auto, Fan, Grid };
enum class StackSort { DateAdded, Name, Modified, Kind };
std::vector<StackItem> sortStack(std::vector<StackItem> items, StackSort sort);
StackView resolveView(StackView v, std::size_t count);
struct FanSlot { double dx, dy, angle; };   // points, relatif au centre de l'icône de la pile
std::vector<FanSlot> fanLayout(std::size_t count, double tile);
struct GridGeometry { int columns, rows, visibleRows; double width, height; };
GridGeometry gridLayout(std::size_t count, double tile, double maxHeight);
```
**Tests :** `stack_items_sorted_by_date`, `stack_sort_by_name_is_case_insensitive`, `stack_view_auto_switches_at_ten`, `stack_fan_arc_rises_and_curves`, `stack_layout_caps_items`, `stack_grid_columns_capped_at_five`, `dock_menus_stack_full`.

**Étapes :** rouge, implémentation, vert, essai réel (clic sur la pile Téléchargements, trace des éléments affichés, Échap). Commit `feat(dock): piles en éventail et en grille`.

### Task 4 : Ouvrir à la connexion pour les apps empaquetées

**Files :** Modify `src/shell/shell_actions.*`, `src/app/dock_window.cpp`, `src/app/dock_menus.cpp`.

**Comportement :** pour une app sans exe mais avec AUMID, « Ouvrir à la connexion » crée (ou supprime) un raccourci `MacDock - <nom>.lnk` dans `shell:startup`, qui cible `shell:AppsFolder\<AUMID>`. L'entrée du menu est active pour ces apps et cochée si le raccourci existe.

**Interfaces :**
```cpp
std::wstring startupShortcutName(const std::wstring& displayName);   // caractères interdits remplacés
bool isPackagedOpenAtLogin(const std::wstring& displayName);
bool setPackagedOpenAtLogin(const std::wstring& aumid, const std::wstring& displayName, bool on);
```
**Tests :** `startup_shortcut_name_is_stable`, `startup_shortcut_name_strips_forbidden`, `dock_menus_login_enabled_for_aumid`.

**Étapes :** rouge, implémentation, vert, essai réel (créer puis supprimer le raccourci pour une app empaquetée présente, vérifier le fichier). Commit `feat(dock): ouverture à la connexion des apps empaquetées`.

---

## Fin du plan
- Relecture finale par un agent sur le modèle le plus capable, puis une passe de corrections en TDD.
- README et journal de nuit.
- Fusion locale dans `main`.
