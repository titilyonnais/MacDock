# Poignée des séparateurs et cotes du Dock mesurées — plan 70

> **Pour les agents :** exécution en ligne, tâche par tâche, tests d'abord. Reprendre à la première tâche non cochée.

**But :** ta demande du 10 octobre (« sur les deux barres verticales qui séparent, des flèches vers le haut et vers le
bas apparaissent et permettent d'agrandir ou de réduire le Dock ; regarde les dimensions exactes avec la VM »), et la
suite du plan 61 (« le machin mesure du Dock avec ta VM ») : le Dock prend les cotes de macOS 26 mesurées à trois tailles,
puis ses séparateurs deviennent une poignée.

**Architecture :** règles pures dans `src/layout` (géométrie, nouvelle taille au glisser), mesures par défaut dans
`dock-metrics.json` (v9), le contrôleur repère la poignée, le Dock gère curseur, glisser et enregistrement.

## Mesures (VM macOS 26 en plein écran, 1 point = 2 pixels, 10 octobre)
| Taille réglée | 48 | 64 | 96 (rétréci par macOS à ~71 pour tenir) |
|---|---|---|---|
| Épaisseur du fond | 68,0 | 84,0 | 91,0 |
| Bord de case → bord du fond | 10,0 | 10,0 | 10,1 |
| Pas entre deux icônes (écart) | 52,0 (4,0) | 68,0 (4,0) | 74,5 (3,7) |
| Centre d'icône → centre du séparateur | 37,5 | 49,5 | 54 |
| Séparateur : trait, longueur, marges haut et bas | 1 ; 50,0 ; 9,0 | 1 ; 62,0 ; 11,0 | 1 ; 67,0 ; 12,0 |
| Écart sous le Dock | 5,0 | 5,0 | 5,0 |
| Point des apps ouvertes : diamètre, centre au-dessus du bas | 4 ; 6,0 | — | 4 ; 6,0 |
| Forme d'une icône (anticrénelage compris) | 40 | 52 | 59 |

- **Fixes en points** : rembourrage 10, écart 4, écart sous le Dock 5, point de 4 centré à 6. Le plan 61 les avait
  ramenés à 48 en proportion depuis une autre taille (7,1 ; 2,3 ; 3,9 ; 4,7) : faux, le Dock de MacDock était trop fin
  (62 pt au lieu de 68) et ses icônes trop serrées.
- **Suivent la taille** : le séparateur occupe entre ses voisines une demi-case plus 3 pt (centre à centre :
  0,75·taille + 1,5 → marge de part et d'autre du trait : 0,25·taille − 3) ; sa longueur vaut 0,738 de l'épaisseur.
  Écarts entre ces règles et les trois mesures : 0,3 pt au plus.
- **Inchangés** : forme des icônes (0,8047 de la case, d'accord avec la mesure aux pixels d'anticrénelage près), bout du
  Dock (le plus grand coin continu qui tient dans l'épaisseur : 22 à 68, mesuré 22).
- **Poignée** (enregistrement de tes gestes, ce matin) : curseur macOS « flèches haut et bas » au-dessus d'un
  séparateur, sur toute l'épaisseur du Dock ; pendant le glisser, le Dock suit le pointeur jusqu'au minimum, sans
  agrandissement des icônes. Le retard de la VM (image et curseur) empêche une mesure plus fine que « un point de
  pointeur, un point de taille ».

## Choix
- `dock-metrics.json` v9 : `dockPadding` 10, `iconGap` 4, `dockScreenMargin` 5, `indicatorCenterFromBottom` 6,
  `separatorLengthRatio` 0,738 (de l'épaisseur, et non plus de la case) ; `separatorMargin` (points) remplacé par
  `separatorMarginRatio` 0,25 et `separatorMarginOffset` −3. Comme en v7 : une valeur encore à l'ancien défaut prend le
  nouveau, une valeur choisie reste.
- Poignée : zone du séparateur (trait et marges, toute l'épaisseur) ; curseur ↕ (Dock en bas) ou ↔ (Dock sur un côté),
  ceux du thème de curseurs de Windows ; la taille suit le pointeur un point pour un point en s'éloignant du bord, bornée
  à 16–128 comme le réglage ; ni agrandissement ni masquage pendant le glisser ; la fenêtre prend la hauteur de la taille
  128 pendant le glisser ; à la fin, `settings.json` (taille, et agrandissement porté au moins à la taille) puis la zone
  réservée. Pas de Maj+glisser (changer de bord) ni d'Option+glisser (tailles fixes) pour l'instant.

## Tâches
- [x] 1. Cotes mesurées : géométrie (marge et longueur du séparateur selon la taille), mesures v9 et migration. Tests :
  cotes à 48 et 64 contre la VM (épaisseur, pas, séparateur, point, écart sous le Dock), migration v9 ; tests existants
  recalés sur les nouvelles cotes (77e73f9).
- [x] 2. Poignée, règles : `resizedTileSize` (un point pour un point, selon le bord, bornée), `separatorHandleAt` du
  contrôleur, Dock au repos pendant le redimensionnement. Tests rouges, puis verts (8592536).
- [x] 3. Poignée, Dock : curseur, glisser, fenêtre, enregistrement. Essai réel, avec ton accord (souris laissée
  30 s) : curseur ↕ (celui du thème de Windows) au-dessus du séparateur ; glisser de 20 pt vers le haut : taille 59 →
  79, enregistrée ; glisser retour : 79 → 59. Dock à ta taille 59 : épaisseur 79 pt (59 + 2 × 10), pas 63 pt,
  séparateur à 46 pt du centre de l'icône voisine (règle : 45,75).
- [x] 4. Relecture indépendante (opus : à corriger, aucun critique), corrections, nouvel essai réel, fusion locale.

## Relecture indépendante (opus) et corrections
- **Relâchement avalé** (important) : un relâchement à avaler (appui qui a fermé Spotlight) passait avant la fin du
  glisser de la poignée, qui restait bloqué. Le glisser se termine en premier, un appui suivant le ferme, et un
  relâchement pris ailleurs le ferme au mouvement suivant (6485b11).
- **Glisser au-delà de ce qui tient** (important) : sur un Dock trop long pour l'écran, la taille montait jusqu'à 128
  pendant que le Dock s'amincissait. Le glisser part de la taille affichée et s'arrête à la plus grande qui tient
  (6485b11).
- **Dock rétréci** (important) : rembourrage, écart et point rétrécissaient avec les cases, contrairement à la mesure
  (à 96 rétréci à ~71, macOS garde 10, 4 et 6). Seules les cases rétrécissent (6485b11).
- **Tremblement d'un clic** (mineur regradé : il déplaçait les fenêtres agrandies) : points entiers vers la taille de
  départ (6485b11).
- **Trouvé à l'essai réel** : Dock plein (réglé 59, affiché 58,26), un glisser de 20 pt vers le bas enregistrait 38,26.
  Le réglage reste celui d'avant sans mouvement, et devient sinon un point entier (7abb004).
- **Nouvel essai réel** (souris laissée par toi) : 59 → 39 en descendant de 20 pt ; en montant de 40 pt, arrêt à 60, la
  plus grande taille qui tient avec tes éléments du moment ; ta taille 59 remise ensuite dans `settings.json`.

## Reportés
- Mineurs : zone de la poignée sans le rétrécissement (sans effet visible), clic droit et poussée d'écran pendant le
  glisser, migration v9 de valeurs choisies à la main, coût (WM_SETCURSOR, images à 128 même en rétrécissant),
  commentaires périmés ; zone réservée calculée sur la taille réglée (Dock rétréci : bande vide au-dessus).
- Test d'UI Automation instable sur ton PC : le premier appel d'un processus prend parfois 14 s (budget du test 15 s).
- Maj+glisser (changer de bord) et ⌥+glisser (tailles fixes) de macOS.
