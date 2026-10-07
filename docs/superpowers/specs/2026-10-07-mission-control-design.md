# Mission Control — spec (sous-projet 8)

- **Date :** 7 octobre 2026
- **Contexte :** suite de la série « un Windows qui ressemble complètement à macOS » (7 Spotlight, 8 Mission Control, 9 sélecteur d'apps, 10 affichages volume et luminosité, 11 coins actifs), en autonomie.

## 1. But

Sur macOS, Mission Control (Ctrl+↑, F3, geste à trois doigts) écarte toutes les fenêtres ouvertes du bureau courant : elles se rangent sans se chevaucher sur un fond assombri, en miniatures vivantes. Survoler une fenêtre l'entoure de bleu et affiche son titre ; un clic la ramène au premier plan et referme la vue ; Échap ou un clic dans le vide referme sans rien changer.

**Critère de réussite :** le raccourci (réglable) ou l'élément du Dock ouvre la vue sur chaque écran ; les fenêtres visibles du bureau virtuel courant y sont rangées sans chevauchement, à leur image réelle (miniatures DWM vivantes), avec une animation depuis leur place réelle ; survol = contour bleu et titre ; clic = la fenêtre passe devant et la vue se referme en ramenant les autres à leur place ; Échap, un second appui ou un clic dans le vide ferment.

## 2. Décisions

| Sujet | Choix | Raison |
|---|---|---|
| Raccourci | `missionControlHotkey` dans `settings.json` : `ctrl+alt+up` (défaut), `ctrl+up`, `f3`, `off` ; `RegisterHotKey` dans `MacDock.exe` ; un second appui ferme | Win+Tab (Vue des tâches) ne peut être pris sans crochet clavier (interdit) ; Ctrl+↑ et F3 servent dans beaucoup d'apps, donc proposés mais pas imposés. |
| Autres entrées | Message enregistré `MacDockMissionControl` (pour les coins actifs, sous-projet 11) | Même mécanisme que Spotlight. |
| Fenêtres montrées | Fenêtres suivies par le Dock (celles qui ont une icône), visibles, non réduites, sur le bureau virtuel courant (`IVirtualDesktopManager::IsWindowOnCurrentVirtualDesktop`), sur l'écran où se trouve leur centre | Comme macOS : les fenêtres réduites restent dans le Dock. |
| Plusieurs écrans | Une vue par écran ; chaque fenêtre va sur l'écran de son centre ; passer d'une vue à l'autre ne ferme pas | Comme macOS. |
| Rangement | Fonction pure : lignes de fenêtres à hauteur égale, nombre de lignes choisi pour la plus grande échelle, ordre de lecture (haut → bas, gauche → droite d'après la place réelle), jamais agrandies au-delà de 100 %, marges de 48 pt et écarts de 24 pt | Proche du rangement de macOS ; testable. |
| Rendu | Une fenêtre plein écran par écran (`WS_POPUP`, `WS_EX_TOPMOST`, `WS_EX_TOOLWINDOW`, `WS_EX_NOREDIRECTIONBITMAP`) : le fond d'écran de cet écran (lu par `IDesktopWallpaper`, fond Tahoe si aucun fichier) légèrement assombri, miniatures DWM (`DwmRegisterThumbnail`) déplacées à chaque image, contour bleu et titre dessinés par Direct2D | Miniatures vivantes ; pas de capture d'écran (elle montrerait les vraies fenêtres derrière les miniatures). |
| Animation | 0,3 s à l'ouverture (place réelle → place rangée) et à la fermeture (retour), courbe en sortie douce ; Maj : au ralenti, comme le génie | Mouvement de macOS. |
| Bureau | Pas de barre des bureaux virtuels (l'API publique ne liste pas les bureaux) | Seules les API documentées sont utilisées. |
| Vide | Aucune fenêtre : la vue s'ouvre quand même (fond seul), un clic la ferme | Comme macOS. |

## 3. Composants

- `src/mission/mission_layout.h/.cpp` (pur) : `struct MissionWindow { double x, y, w, h; }` (place réelle, pixels) ; `std::vector<RectD> missionLayout(const std::vector<MissionWindow>& windows, RectD area, double gap)` ; `int missionHit(const std::vector<RectD>& rects, double x, double y)` ; `RectD lerpRect(RectD a, RectD b, double t)` ; `double easeOut(double t)`.
- `src/mission/mission_view.h/.cpp` : `MissionView::track(env, request)` modale (plusieurs écrans), `closeOpen()`, `isOpen()` ; `BgraImage missionSnapshot(windows, dark, w, h, hover)` (rectangles colorés à la place des miniatures).
- Branchements : `Settings::missionControlHotkey`, `DockApp` (raccourci, message enregistré, activation), `MacDock.exe --mission-snapshot f.png [--theme dark] [--hover n]`.

## 4. Tests et vérifications

- Tests : rangement (dans la zone, aucun chevauchement, proportions gardées, jamais agrandi, ordre de lecture, une fenêtre, aucune, fenêtres très larges ou très hautes, 30 fenêtres), test de clic, interpolation, analyse du raccourci et réglage, rendu hors écran.
- À l'œil : `--mission-snapshot` (3, 8 et 20 fenêtres factices ; survol ; sombre).
- **Aucun essai n'ouvre la vue devant l'utilisateur, n'active ni ne déplace de fenêtre.**
