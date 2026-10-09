# Menu de la pastille verte (macOS 26) — plan 51

> **Pour les agents :** exécution en ligne, tâche par tâche, tests d'abord. Reprendre à la première tâche non cochée.

**But :** sur macOS 26, laisser le pointeur sur la pastille verte d'une fenêtre ouvre un menu d'icônes :
- « Déplacer et redimensionner » : moitiés gauche, droite, haut, bas ;
- « Remplir et organiser » : Remplir, puis la fenêtre organisée avec les suivantes (gauche et droite, haut et bas,
  quarts) ;
- ⌥ maintenue : les quarts, Centrer et les dispositions inversées (7 icônes sur 8 changent) ;
- « Plein écran ».

Le menu Fenêtre de macOS 26 a aussi une section « Organiser » (Gauche et droite, Droite et gauche, Haut et bas,
Bas et haut, Quarts), absente de MacDock (plan 50).

## Choix
- **Délai** : 0,7 s de survol de la pastille verte. Le délai part seulement quand le pointeur entre sur la pastille :
  un menu fermé (Échap) ne revient pas tant que le pointeur ne l'a pas quittée. Rien pendant un appui ou un glisser,
  ni si la pastille verte est indisponible.
- **Fil** : le menu s'ouvre sur le fil de la barre (verre réel, `MenuWindow::track` comme les autres menus). Le fil
  des pastilles lui poste la fenêtre et le point d'ancrage, sous la pastille. Les cadres gardés par `tileWindow`
  restent ainsi sur un seul fil.
- **Organiser**, comme macOS (ordre d'affichage) :
  - la fenêtre choisie prend la première place ;
  - les suivantes viennent de l'avant vers l'arrière, sur le même écran ;
  - admissibles : visibles, non réduites, sur ce bureau, à barre de titre, redimensionnables, ni fenêtres outils ni
    fenêtres possédées (dialogues) ;
  - une fenêtre qui refuse (UIPI, figée) laisse sa place à la suivante ;
  - moins de fenêtres que de places : les places restantes restent vides.
- **⌥** : variante choisie à l'ouverture du menu (Alt maintenue), sans bascule pendant qu'il est ouvert.
- **Plein écran** : agrandir ou restaurer, comme un clic sur la pastille verte dans MacDock (« Quitter le plein écran »
  si la fenêtre est agrandie). Le plein écran partagé (côtés gauche et droit) de macOS n'a pas d'équivalent.
- **Premier plan** : une icône choisie met la fenêtre au premier plan ; rien de choisi, le premier plan d'avant
  revient.
- **Icônes** : redessinées (écran arrondi, case de la fenêtre pleine, cases des autres fenêtres plus claires), en
  bleu au survol comme une entrée de menu. Aucune ressource Apple.

## Tâches
- [ ] 1. **Organiser** (`src/shell/window_tile.*`, menu Fenêtre) :
  - `Arrangement`, `parseArrangement`, `arrangementName` (« left-right », « right-left », « top-bottom »,
    « bottom-top », « quarters ») ;
  - `arrangementSlots(a)` : places dans l'ordre (la fenêtre choisie d'abord) ;
  - `arrangeCandidates(ordre d'affichage, fenêtre, écran)` : la fenêtre, puis les admissibles du même écran ;
  - `arrangeWindows(fenêtre, a)` : énumère, range chaque place (`tileWindow`), passe une fenêtre qui refuse ;
  - `ActionKind::Arrange` ; section « Organiser » du sous-menu « Déplacer et redimensionner ».

  Tests : noms, places, candidats (inadmissible, autre écran, fenêtre choisie absente ou inadmissible), entrées du
  menu Fenêtre, action sur une fenêtre disparue.
- [ ] 2. **Rangée d'icônes de disposition** (`src/popup/menu_model.*`, `menu_window.cpp`) :
  - `MenuRow::Layouts` (hauteur 32 pt), `LayoutBox`, `MenuTile::id` et `MenuTile::boxes` ;
  - `layoutIconLeft(k)`, `layoutIconAt(n, x)` (zone de survol jusqu'à mi-chemin de la voisine),
    `layoutsRowWidth(n)` ; `layoutMenu` assez large pour la rangée ;
  - dessin, survol icône par icône, clic : `track` renvoie l'identifiant de l'icône.

  Tests : hauteur, positions et zones des icônes, largeur du menu, ligne non sélectionnable au clavier, rendu hors
  écran (`MenuWindow::snapshot`) d'une rangée.
- [ ] 3. **Pastille verte** :
  - `buildZoomMenu(ZoomMenuContext{resizable, zoomed, option})` (`app_menus.*`, pur) : intitulés, deux rangées de
    quatre icônes, séparateur, Plein écran ; actions `Tile`, `Arrange`, `Zoom` ;
  - `zoomMenuHover(hit, pressed, dragging, enabled)` (`traffic_lights.*`, pur) ;
  - `TrafficWindow` : minuterie de survol, `setZoomMenuSink(fenêtre, message)`, message posté à la barre ;
  - `MenuBarApp::onZoomMenu` : ouvre le menu sous la pastille, exécute l'action sur la fenêtre survolée ;
  - `MacMenuBar.exe --zoom-snapshot f.png [--theme dark] [--option]` : rendu hors écran du menu.

  Tests : contenu du menu (normal, ⌥, taille fixe, agrandie), survol voulu ou non.
- [ ] 4. Essai réel, documentation, relecture, fusion.

## Revue : points d'attention
- Fenêtre fermée ou réduite pendant le délai de survol : rien ne s'ouvre.
- Pastille grise d'une fenêtre inactive : le menu s'ouvre aussi (comme macOS), l'action vise cette fenêtre.
- Deux écrans : seules les fenêtres de l'écran de la fenêtre choisie sont organisées ; le menu s'ouvre sur l'écran de
  la pastille, à son DPI.
- Menu de la barre déjà ouvert : pas de second menu.
- Fenêtre élevée (UIPI) parmi les suivantes : sautée, sans bloquer les autres places.
