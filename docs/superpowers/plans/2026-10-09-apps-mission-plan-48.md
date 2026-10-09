# Apps et Mission Control : zones justes, miniatures fidèles, clavier — plan 48

> **Pour les agents :** exécution en ligne, tâche par tâche, tests d'abord. Reprendre à la première tâche non cochée.

**But :** défauts relevés par l'agent de propositions du 9 octobre, vérifiés dans le code.

## Constat
- **Apps** : `appsHit` (src/apps/apps_layout.cpp) prend toute la case. Sur un écran 4K, une case fait environ
  219 × 169 pt pour un halo de survol de 120 × 146 : un clic entre deux icônes lance une app. Sur macOS, un clic dans
  le vide ferme la vue.
- **Mission Control** : `Session::open` cale chaque miniature sur `GetWindowRect`, bordures invisibles comprises. Or la
  source d'une miniature DWM est le cadre visible : mesuré sur une fenêtre d'essai, 778 × 489 contre 800 × 500. L'image
  est étirée de 2 à 3 % et le contour bleu décalé d'environ 11 px.
- **Mission Control, deux écrans** : chaque écran garde son survol ; passer de l'un à l'autre laisse deux contours.
- **Mission Control, clavier** : Entrée choisit la fenêtre survolée à la souris seulement ; les flèches ne font rien.
  Sur macOS, elles déplacent la sélection, et Entrée la choisit.

## Tâches
- [x] 1. **Apps : la zone de clic est le halo** :
  - `appsItemRect(g, slot)` : icône et nom, la même zone que le halo dessiné ;
  - `appsHit` ne répond que dans cette zone ; le dessin du halo s'en sert aussi.

  Tests : centre de l'icône et nom : l'app ; entre deux icônes : -1.
- [x] 2. **Mission Control : miniatures sur le cadre visible**.

  Décision : `genieVisibleRect(rectangle Windows, taille de la miniature DWM)`, déjà utilisée et testée pour le génie
  (`test_genie`), au lieu de `DWMWA_EXTENDED_FRAME_BOUNDS` : sur la fenêtre d'essai mesurée, elle donne exactement le
  cadre visible, (311, 300)-(1089, 789). Câblage seul, sans nouveau test unitaire.
- [x] 3. **Mission Control : un seul contour** (survol retiré des autres écrans) **et flèches** :
  - `missionNeighbor(rects, from, dx, dy)` : la fenêtre la plus proche dans la direction ; aucune sélection : la
    première en haut à gauche ;
  - flèches : la sélection (le contour) se déplace sur l'écran de la vue ; Entrée la choisit.

  Tests : grille 2 × 2 (droite, bas, bord), départ sans sélection, fenêtres décalées.
- [x] 4. Essai réel (MacDock lancé pour l'essai puis arrêté, fenêtres d'essai à soi seulement), documentation,
  relecture, fusion.

## Essai réel
MacDock en diagnostic, lancé puis arrêté ; deux fenêtres d'essai à moi (A à gauche, B à droite), A au premier plan :
Exposé de l'app (Ctrl+Alt+↓) ouvert, puis →, →, Entrée : B choisie et passée au premier plan, Exposé fermé.

## Relecture
Rien de critique. Corrigés :
- un nom long dépassait du halo et n'y était plus cliquable : il est mis en page dans le halo (deux lignes, puis « … ») ;
  instantané vérifié (« AMD Software: Adrenalin Edition » sur deux lignes sous l'icône) ;
- plusieurs écrans : Entrée et les flèches suivent la sélection globale ;
- fenêtre agrandie : bordures invisibles des quatre côtés (mesuré : (-13, -13)-(3853, 2173) pour un cadre de
  (0, 0)-(3840, 2160)), miniature calée 13 px trop haut par l'estimation : le cadre de DWM fait foi quand il a la taille
  de la miniature (`thumbnailFrame`) ;
- clic dans le vide pendant l'ouverture de l'écran Apps : ne ferme plus ; flèche tapée pendant l'ouverture de Mission
  Control : appliquée à la fin.
