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
- [ ] 1. **Apps : la zone de clic est le halo** :
  - `appsItemRect(g, slot)` : icône et nom, la même zone que le halo dessiné ;
  - `appsHit` ne répond que dans cette zone ; le dessin du halo s'en sert aussi.

  Tests : centre de l'icône et nom : l'app ; entre deux icônes : -1.
- [ ] 2. **Mission Control : miniatures sur le cadre visible** (`DWMWA_EXTENDED_FRAME_BOUNDS`, repli sur
  `GetWindowRect`).

  Test : la fonction de cadre de départ, avec et sans cadre DWM.
- [ ] 3. **Mission Control : un seul contour** (survol retiré des autres écrans) **et flèches** :
  - `missionNeighbor(rects, from, dx, dy)` : la fenêtre la plus proche dans la direction ; aucune sélection : la
    première en haut à gauche ;
  - flèches : la sélection (le contour) se déplace sur l'écran de la vue ; Entrée la choisit.

  Tests : grille 2 × 2 (droite, bas, bord), départ sans sélection, fenêtres décalées.
- [ ] 4. Essai réel (MacDock lancé pour l'essai puis arrêté, fenêtres d'essai à soi seulement), documentation,
  relecture, fusion.
