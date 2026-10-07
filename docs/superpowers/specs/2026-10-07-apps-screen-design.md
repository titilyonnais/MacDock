# Écran Apps — spec (sous-projet 6)

- **Date :** 7 octobre 2026
- **Spec parente :** `2026-10-06-macos-dock-design.md` (sous-projet 6 : « écran Apps façon Launchpad / Apps de Tahoe »).
- **Mode :** autonomie complète demandée par l'utilisateur (« fais la barre de menus et fais le reste ensuite, je veux tout faire »).

## 1. But

Le bouton **Apps** du Dock ouvre aujourd'hui le menu Démarrer de Windows (frappe de la touche Windows). Sur macOS Tahoe, il ouvre **Apps** : une vue plein écran en verre, avec un champ de recherche en haut et toutes les apps en grille, page par page. On remplace le menu Démarrer par cette vue.

**Critère de réussite :** un clic sur le bouton Apps ouvre, sur l'écran du Dock, une vue plein écran en verre listant toutes les apps du menu Démarrer (Win32 et Store), triées par nom ; taper filtre la liste ; un clic ou Entrée lance l'app et ferme la vue ; Échap, un clic dans le vide ou un clic droit ferment sans rien lancer.

## 2. Décisions

| Sujet | Choix | Raison |
|---|---|---|
| Source des apps | Le dossier Shell `shell:AppsFolder` (FOLDERID_AppsFolder) : exactement les apps du menu Démarrer, Win32 et Store, avec leur nom affiché et leur nom d'analyse (AUMID ou chemin) | Une seule source, celle de Windows ; le lancement passe par `shell:AppsFolder\<nom>`, déjà géré par `launch`. |
| Ce qui est écarté | Désinstalleurs, aides, documents et liens web (nom d'analyse finissant par `.chm`, `.txt`, `.pdf`, `.htm(l)`, `.url`, `.rtf`, `.ini`, `.log`, ou nom affiché commençant par « Uninstall », « Désinstaller », « Désinstallation ») ; doublons (même nom d'analyse) | Launchpad ne montre que des apps. |
| Ordre | Alphabétique, casse et accents ignorés (`CompareStringEx`, `LINGUISTIC_IGNORECASE`, `NORM_IGNORENONSPACE`) | Comme la vue Apps quand aucun dossier n'a été fait. Pas de réorganisation à la main (YAGNI). |
| Recherche | Le texte tapé filtre en direct : nom qui commence par la recherche d'abord, puis un mot du nom qui commence par elle, puis le nom qui la contient ; casse et accents ignorés ; chaque groupe dans l'ordre alphabétique | Comme Spotlight / Launchpad. |
| Grille | 7 colonnes × 5 lignes par page au plus, moins si l'écran est petit (icône de 96 pt au plus, 64 pt au moins, cases d'au moins 120 × 128 pt) ; pages horizontales avec des points en bas ; molette, flèches gauche et droite (aux bords), glisser horizontal non pris en charge | Mesures de Launchpad ; le glisser à la souris n'apporte rien sans pavé tactile. |
| Clavier | Lettres : recherche ; Retour arrière ; Échap vide la recherche, ou ferme si elle est vide ; flèches : sélection (passe à la page voisine aux bords) ; Entrée : lance la sélection (la première app si rien n'est sélectionné) ; Page précédente / suivante : page | Comme Launchpad. |
| Fond | Verre Liquid Glass du Dock sur tout l'écran, très flouté et assombri ; verre dépoli Direct2D si la capture est impossible ou si `glass` est faux | Même rendu que les piles et les menus. |
| Ouverture | Fondu et léger zoom arrière de la grille (0,2 s) ; fermeture immédiate | Animation de Launchpad, simplifiée. |
| Catalogue | Lu dans un fil à part au démarrage du Dock, puis relu après chaque ouverture de la vue (pour la suivante) ; la première ouverture attend la lecture si elle n'est pas finie | La lecture du dossier Apps prend jusqu'à une demi-seconde. |
| Icônes | `IconProvider::get` (icône d'app du Dock, avec sa plaque) dans un fil à part, page affichée d'abord | Même allure que le Dock ; une centaine d'icônes ne doit pas figer la vue. |
| Repli | Si la vue ne peut pas s'ouvrir (Direct2D, fenêtre) ou si le catalogue est vide : le menu Démarrer, comme avant (journalisé) | Le bouton doit toujours faire quelque chose. |
| Menu Démarrer | Clic droit sur le bouton Apps : « Ouvrir le menu Démarrer » au-dessus de « Retirer du Dock » | Garder un accès au menu de Windows. |

## 3. Composants

### 3.1 Catalogue — `src/apps/app_catalog.h/.cpp` (logique pure)
- `struct AppEntry { std::wstring name; std::wstring parsingName; };` ; `launchTarget(e)` = `shell:AppsFolder\` + `parsingName`.
- `bool isListedApp(const AppEntry&)` (règles du §2) ; `std::vector<AppEntry> catalogFrom(std::vector<AppEntry> raw)` : écarte, dédoublonne, trie.
- `std::vector<std::size_t> searchApps(const std::vector<AppEntry>&, const std::wstring& query)` : indices dans l'ordre d'affichage (requête vide : tous). Comparaisons par `foldForSearch` (minuscules, accents retirés via `FoldStringW(MAP_COMPOSITE)` puis suppression des marques combinantes).

### 3.2 Lecture du dossier Apps — `src/apps/apps_folder.h/.cpp`
- `std::vector<AppEntry> readAppsFolder()` : `SHGetKnownFolderItem(FOLDERID_AppsFolder)` → `BindToHandler(BHID_EnumItems)` → pour chaque `IShellItem` : `SIGDN_NORMALDISPLAY` et `SIGDN_PARSINGNAME`. COM initialisé par l'appelant.
- `class AppCatalog` : `refreshAsync()` (fil à part, résultat sous verrou), `std::vector<AppEntry> get(DWORD waitMs)`.

### 3.3 Géométrie — `src/apps/apps_layout.h/.cpp` (logique pure, en points)
- `struct AppsGeometry { int columns, rows, perPage, pages; double cellW, cellH, icon, gridLeft, gridTop, searchTop, searchW, searchH, dotsY; };`
- `AppsGeometry appsLayout(double screenW, double screenH, std::size_t count)`.
- `int appsHit(const AppsGeometry&, int page, double x, double y, std::size_t count)` → indice dans la liste filtrée, ou -1.
- `struct AppsCursor { int page = 0; int selected = -1; };` ; `AppsCursor appsKey(const AppsGeometry&, AppsCursor, UINT vk, std::size_t count)` (flèches, Page précédente/suivante, Début, Fin).

### 3.4 Vue — `src/apps/apps_window.h/.cpp`
- `AppsWindow::track(const MenuWindow::Env&, const Request&)` : fenêtre plein écran modale (comme `StackWindow`), verre, champ de recherche, grille, points de pages ; renvoie le nom d'analyse choisi (vide si fermée sans choix).
- `Request { std::vector<AppEntry> apps; HMONITOR monitor; }`.
- `BgraImage appsSnapshot(const std::vector<AppEntry>&, const std::wstring& query, int page, bool dark, …)` : même dessin hors écran (Direct2D sur bitmap WIC), fond = fond d'écran Tahoe flouté, icônes factices si demandé.

### 3.5 Branchements
- `DockApp` : `AppCatalog` ; bouton Apps → `AppsWindow::track` (capture du Dock en pause, comme les piles) → `launch(launchTarget)` ; repli sur `openStartMenu()`.
- Menu du bouton Apps : `kCmdStartMenu` « Ouvrir le menu Démarrer ».
- `MacDock.exe --apps-snapshot f.png [--query texte] [--page n] [--theme light|dark]` : rendu hors écran avec les vraies apps (lecture seule du dossier Apps), sans fenêtre.

## 4. Tests et vérifications
- Tests : `isListedApp`, `catalogFrom` (tri accentué, doublons), `searchApps` (ordre des groupes, accents, casse), `appsLayout` (grands et petits écrans, nombre de pages), `appsHit`, `appsKey` (bords de page, dernière page incomplète), menu du bouton Apps, lecture réelle du dossier Apps (non vide, noms d'analyse non vides — lecture seule).
- À l'œil : `--apps-snapshot` (clair, sombre, recherche, deuxième page).
- **Aucun essai n'ouvre la vue devant l'utilisateur** et aucun test ne lance d'app.
