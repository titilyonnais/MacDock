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
- [x] 1. **Organiser** (`src/shell/window_tile.*`, menu Fenêtre) :
  - `Arrangement`, `parseArrangement`, `arrangementName` (« left-right », « right-left », « top-bottom »,
    « bottom-top », « quarters ») ;
  - `arrangementSlots(a)` : places dans l'ordre (la fenêtre choisie d'abord) ;
  - `arrangeCandidates(ordre d'affichage, fenêtre, écran)` : la fenêtre, puis les admissibles du même écran ;
  - `arrangeWindows(fenêtre, a)` : énumère, range chaque place (`tileWindow`), passe une fenêtre qui refuse ;
  - `ActionKind::Arrange` ; section « Organiser » du sous-menu « Déplacer et redimensionner ».

  Tests : noms, places, candidats (inadmissible, autre écran, fenêtre choisie absente ou inadmissible), entrées du
  menu Fenêtre, action sur une fenêtre disparue.
- [x] 2. **Rangée d'icônes de disposition** (`src/popup/menu_model.*`, `menu_window.cpp`) :
  - `MenuRow::Layouts` (hauteur 32 pt), `LayoutBox`, `MenuTile::id` et `MenuTile::boxes` ;
  - `layoutIconLeft(k)`, `layoutIconAt(n, x)` (zone de survol jusqu'à mi-chemin de la voisine),
    `layoutsRowWidth(n)` ; `layoutMenu` assez large pour la rangée ;
  - dessin, survol icône par icône, clic : `track` renvoie l'identifiant de l'icône.

  Tests : hauteur, positions et zones des icônes, largeur du menu, ligne non sélectionnable au clavier, rendu hors
  écran (`MenuWindow::snapshot`) d'une rangée.
- [x] 3. **Pastille verte** :
  - `buildZoomMenu(ZoomMenuContext{resizable, zoomed, option})` (`app_menus.*`, pur) : intitulés, deux rangées de
    quatre icônes, séparateur, Plein écran ; actions `Tile`, `Arrange`, `Zoom` ;
  - `zoomMenuHover(hit, pressed, dragging, enabled)` (`traffic_lights.*`, pur) ;
  - `TrafficWindow` : minuterie de survol, `setZoomMenuSink(fenêtre, message)`, message posté à la barre ;
  - `MenuBarApp::onZoomMenu` : ouvre le menu sous la pastille, exécute l'action sur la fenêtre survolée ;
  - `MacMenuBar.exe --zoom-snapshot f.png [--theme dark] [--option]` : rendu hors écran du menu.

  Tests : contenu du menu (normal, ⌥, taille fixe, agrandie), survol voulu ou non.
- [x] 4. Essai réel, documentation, relecture, fusion.

## Revue : points d'attention
- Fenêtre fermée ou réduite pendant le délai de survol : rien ne s'ouvre.
- Pastille grise d'une fenêtre inactive : le menu s'ouvre aussi (comme macOS), l'action vise cette fenêtre.
- Deux écrans : seules les fenêtres de l'écran de la fenêtre choisie sont organisées ; le menu s'ouvre sur l'écran de
  la pastille, à son DPI.
- Menu de la barre déjà ouvert : pas de second menu.
- Fenêtre élevée (UIPI) parmi les suivantes : sautée, sans bloquer les autres places.

## Essai réel
Sonde `tests/real/zoom_menu_probe.cpp`, sur deux fenêtres d'essai à moi, MacMenuBar en diagnostic, écran 4K à 200 % :
- **ouverture** : rien avant 0,4 s, menu au bout de 0,8 s sous la pastille, étendu vers la gauche ;
- **« Gauche »** : cadre exact, la fenêtre passe au premier plan ;
- **Échap** : rien ne bouge, premier plan rendu ; le pointeur resté sur la pastille ne rouvre pas le menu ;
- **« Gauche et droite »** : A et B exactes, B juste sous A, devant une fenêtre outil qui la recouvrait ;
- **« Plein écran »** : A reçoit `WM_SYSCOMMAND SC_MAXIMIZE`, comme au clic sur la pastille ;
- **passage à une autre fenêtre** pendant le menu (comme Alt+Tab) : le premier plan y reste.

Le vrai menu a été vérifié sur une capture en thème sombre (verre réel, icône survolée en bleu), puis la capture a
été effacée. Les trois derniers essais échouaient sur la version d'avant les corrections.

## Relecture
Un défaut critique, déjà trouvé en réel et corrigé de la même façon : la fenêtre du menu, marge d'ombre comprise,
recouvre la pastille. Le calque voyait le pointeur partir puis revenir, et le menu se rouvrait en boucle. La barre
prévient maintenant le fil des pastilles à la fermeture.

Corrigés aussi :
- **premier plan** : un menu fermé parce qu'on est passé ailleurs (Alt+Tab, ⊞) ne le rend plus à l'app d'avant
  (`MenuClose`, aussi pour les menus de la barre, qui refermaient le menu Démarrer) ;
- **Organiser** :
  - fenêtres « toujours au-dessus » après les autres ;
  - fenêtre qui ne répond pas en 100 ms sautée ;
  - chaque fenêtre rangée passe juste sous la précédente ;
- **demande en retard** ignorée (pointeur parti, clic en cours, barre occupée) ;
- **Plein écran et Zoom** : `WM_SYSCOMMAND`, comme un clic sur la pastille.

Mineurs reportés :
- **clavier** : le menu ouvert au simple survol prend le clavier, et l'app active perd ses listes déroulantes ouvertes
  (macOS ne prend rien) ;
- **survol** : les pastilles perdent le leur tant que le menu les recouvre ;
- **⌥** : lue à l'ouverture seulement ;
- **écrans** : pas de « Déplacer vers » un autre écran ;
- **bas de l'écran** : pas d'ouverture au-dessus de la pastille quand la place manque en dessous ;
- **Organiser** :
  - une fenêtre suivante élevée et agrandie serait seulement restaurée ;
  - les fenêtres principales possédées (applis VCL) sont écartées ;
- **surlignage** : une icône reste en bleu si le pointeur quitte vite le menu.
