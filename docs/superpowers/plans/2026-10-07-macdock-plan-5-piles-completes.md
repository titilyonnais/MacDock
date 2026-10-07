# MacDock — Plan 5 : piles complètes

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** terminer les piles de la spec : l'icône d'une pile montre ses derniers éléments empilés (« Afficher comme : Pile »), ou le dossier (« Dossier ») ; son contenu peut aussi se présenter en « Liste » (menu en verre avec icônes, sous-dossiers en sous-menus).

**Architecture :** la géométrie des couches de l'icône « Pile » est pure (`stack_icon`) ; `IconProvider` compose les images des derniers éléments (rotation, échelle, ombre) en une icône de case. Le Dock surveille chaque dossier de pile (`SHChangeNotifyRegister`, comme la Corbeille) et transmet au modèle l'aperçu (chemins des 3 premiers éléments selon le tri). La « Liste » réutilise `MenuWindow`, dont les entrées gagnent une icône facultative.

**Tech Stack :** C++ `/std:c++latest`, MSVC, Win32, Direct2D, DirectComposition, Shell.

**Spec :** `docs/superpowers/specs/2026-10-06-macos-dock-design.md` (sections 4.3 « Pile → éventail, grille ou liste » et 4.5 « Afficher comme (Pile, Dossier), Présentation (Éventail, Grille, Liste, Automatique) »).

## Global Constraints
- Aucune ressource Apple dans le dépôt.
- Messages, journaux et commentaires en français ; identifiants en anglais.
- Toute nouvelle logique pure a ses tests dans `tests/` ; `build.ps1 -Target tests -Run` reste vert.
- Une seule duplication d'écran par processus : la Liste (MenuWindow) suspend la capture du Dock comme les menus.
- Les réglages de l'utilisateur sont sauvegardés et restaurés autour de chaque essai réel.

## Review Focus
1. **Dossier de pile qui change sans cesse** (téléchargement en cours) : l'icône suit sans rafale de recompositions ni blocage du Dock. Test : délai de regroupement (comme la Corbeille) et `stack_preview_takes_first_three`.
2. **Dossier vide ou introuvable** : la pile affichée en « Pile » retombe sur l'icône du dossier. Test : `stack_preview_takes_first_three` (cas vide).
3. **Vignettes non carrées ou sans alpha** dans l'icône composée : pas de débordement de la case. Test : `icons_compose_stack_stays_in_cell`.
4. **Liste d'un dossier avec des sous-dossiers très remplis** : sous-menus plafonnés, pas de récursion profonde. Test : `stack_list_menu_caps_and_nests_one_level`.
5. **Changement de tri ou d'affichage à chaud** : l'icône se met à jour immédiatement. Test : `model_stack_preview_changes_revision`.

---

### Task 1 : Afficher comme Pile ou Dossier

**Files :**
- Create `src/stack/stack_icon.h|.cpp` : couches de l'icône « Pile » (pure).
- Modify `src/config/settings.*` : `PinnedEntry::stackDisplay` (`StackDisplay::Stack` par défaut, `Folder`), clé JSON `display` (`stack` / `folder`).
- Modify `src/model/app_model.*` : `DockItem::stackDisplay`, `DockItem::stackPreview` ; `setStackPreview(key, paths)` ; `setStackOptions` accepte l'affichage.
- Modify `src/icons/icon_provider.*` : `composeStack(key, paths, px)`.
- Modify `src/app/dock_controller.cpp` : icône d'une pile = `composeStack` si affichage Pile et aperçu non vide, sinon icône du dossier.
- Modify `src/app/dock_menus.*` : sous-menu « Afficher comme » (Pile, Dossier) ; commandes `kCmdDisplayStack`, `kCmdDisplayFolder`.
- Modify `src/app/dock_window.*` : surveillance des dossiers de pile, regroupement 400 ms, `refreshStacks()`.

**Interfaces :**
```cpp
enum class StackDisplay { Stack, Folder };
struct StackLayer { double dx, dy, angle, scale; };            // fraction de la case, degrés
std::vector<StackLayer> stackIconLayers(std::size_t count);     // du dessous vers le dessus, 3 au plus
std::vector<std::wstring> stackPreview(const std::vector<StackItem>& sorted);   // 3 premiers chemins
bool AppModel::setStackPreview(const std::wstring& key, std::vector<std::wstring> paths);
IconProvider::ImagePtr IconProvider::composeStack(const std::wstring& key, const std::vector<std::wstring>& paths, int px);
```
**Tests :** `stack_icon_layers_capped_at_three`, `stack_icon_top_layer_is_upright`, `stack_preview_takes_first_three`, `model_stack_preview_changes_revision`, `icons_compose_stack_stays_in_cell`, `settings_stack_display_roundtrip`, `dock_menus_stack_display_checked`.

**Étapes :** rouge, implémentation, vert, essai réel (pile Téléchargements en Pile puis en Dossier, trace de l'aperçu ; création puis suppression d'un fichier de test dans un dossier temporaire épinglé comme pile → l'aperçu suit). Commit `feat(dock): pile affichée comme pile ou dossier`.

### Task 2 : Présentation en liste

**Files :**
- Modify `src/config/settings.*`, `src/stack/stack_model.*` : `StackView::List` (clé `list`) ; « Automatiquement » ne choisit jamais la liste (comme macOS).
- Modify `src/popup/menu_model.h|.cpp`, `src/popup/menu_window.cpp` : `MenuItem::icon` (image facultative) ; place réservée à gauche du texte quand une entrée du menu a une icône.
- Create dans `src/stack/stack_model.*` : construction pure du menu de liste.
- Modify `src/app/dock_menus.cpp` : entrée « Liste » dans « Présenter le contenu comme ».
- Modify `src/app/dock_window.cpp` : `openStack` → `MenuWindow::track` pour la liste ; résultat → ouverture du chemin.

**Interfaces :**
```cpp
constexpr int kStackListBase = 5000;   // identifiants des entrées de la liste
// Entrées : éléments (dossiers → sous-menu de leur contenu, un seul niveau, kGridMaxItems au plus),
// séparateur, « Ouvrir dans l'Explorateur ». paths[id - kStackListBase] = chemin à ouvrir.
MenuModel stackListMenu(const std::wstring& folder, const std::vector<StackItem>& items,
                        const std::function<std::vector<StackItem>(const std::wstring&)>& listSub,
                        std::vector<std::wstring>& paths);
```
**Tests :** `stack_list_menu_caps_and_nests_one_level`, `stack_list_menu_ends_with_open`, `stack_view_auto_never_list`, `menu_layout_reserves_icon_space`, `dock_menus_stack_full` (mis à jour).

**Étapes :** rouge, implémentation, vert, essai réel (Téléchargements en Liste : trace des entrées, survol d'un sous-dossier, Échap). Commit `feat(dock): pile présentée en liste`.

---

## Fin du plan
- Relecture finale par un agent sur le modèle le plus capable, puis une passe de corrections en TDD.
- README et journal de nuit.
- Fusion locale dans `main`.
