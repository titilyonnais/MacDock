# Forme des icônes de macOS 26 — plan 60

> **Pour les agents :** exécution en ligne, tâche par tâche, tests d'abord. Reprendre à la première tâche non cochée.

**But :** à ta demande (« bonnes icônes, bon arrondi, tout pareil »), les icônes de MacDock prennent la forme exacte de
macOS 26 Tahoe. MacDock suivait la grille de Big Sur à Sequoia (rayon de 22,5 % du côté) ; Tahoe arrondit davantage.

## Mesure (9 octobre, VM macOS 26 de l'utilisateur)
- **Source** : le Dock de la VM, capturé à l'écran (macOS en 1920×1080, étiré ×1,625 par VMware). Le gabarit Figma
  d'Apple n'est **pas** utilisé : sa licence réserve ces ressources aux maquettes d'apps pour les systèmes d'Apple et
  interdit de les intégrer à un logiciel.
- **Méthode** : contour au seuil de 50 % (sous-pixel), puis rayon du coin continu d'Apple (le modèle de
  `src/geom/smooth_rect.cpp`) ajusté aux moindres carrés sur les coins hauts de cinq icônes à fond uni (Apps, Safari,
  Téléphone, Rappels, Notes) : 0,2518 à 0,2539, médiane 0,2529 ; le contour colle au modèle à 0,15 px près (Notes).
- **Étalonnage** : ce premier script de mesure sous-estimait le rayon de 0,0065 à 0,007 (couverture normalisée par
  la couleur du centre de chaque ligne, moindres carrés sur le retrait horizontal), même sans étirement. Corrigé par
  l'étalonnage, puis confirmé par une mesure sans biais (`tools/measure_icon_shape.py` : contour à 50 % le long du
  gradient, ajustement en distance perpendiculaire ; auto-contrôle à ± 0,002) : médiane 0,2587 sur les cinq icônes.
  Rayon de Tahoe : **0,259 du côté** (± 0,002).
- **Pas mesuré ici** : la taille de la forme dans sa case (824/1024 gardé), le Dock lui-même (plan suivant).

## Choix
- `kIconCornerRatio` (forme par défaut) et le défaut de `iconCornerRatio` (`dock-metrics.json`) passent à 0,259.
- `dock-metrics.json` en version 6 : une valeur encore à l'ancien défaut (0,225) prend 0,259 ; une valeur choisie
  reste.
- Les rayons écrits en dur (grille d'Apps, sélecteur d'apps, tuiles de Spotlight) lisent la même constante.
- Les icônes de MacDock (`res/*.ico`, dessinées par le code) sont refaites avec la nouvelle forme.

## Tâches
- [x] 1. **Forme et mesures** (`icons/squircle.h`, `config/metrics.*`, `apps/apps_window.cpp`,
  `switcher/switcher_window.cpp`, `spotlight/spotlight_window.cpp`) : constante, défaut, migration v6, rayons en dur.
  Tests (migration, défauts, masque).
- [x] 2. **Icônes de MacDock** : `make_icons` relancé, `res/*.ico` refaits ; contrôle de leur forme.
- [x] 3. **Essais réels** : MacDock recompilé et relancé ; son Dock rendu hors écran (`--snapshot`) et mesuré par
  l'outil sans biais ; contours tracés sur une icône du Dock de la VM.

  Puis documentation, relecture indépendante, corrections, fusion locale.

## Fait
- Suite : 811 tests sur 812 (seul `uia_menus_read_and_invoke_fake_app` échoue : UI Automation bloqué par la
  fenêtre transparente de l'outil de pilotage de Claude, sans rapport avec le plan) ; tout compilé en Release.
- **Contrôles** :
  - `res/*.ico` à 256 px, mesurés par la même méthode : rapport 0,2538 (avant : 0,2207), écart attendu pour 0,259 ;
  - le `dock-metrics.json` de l'utilisateur, migré au démarrage : version 6, `iconCornerRatio` 0,259 ;
  - contours tracés sur l'icône Notes du Dock de la VM (agrandie ×6) : le nouveau suit le bord d'Apple, l'ancien
    passe à côté.
- Dock de MacDock, après l'activation de « Tahoe strict » (choix de l'utilisateur : la plaque grise, comme Tahoe) :
  les deux icônes de fond uni du rendu mesurent 0,2568 et 0,2549 avec l'outil sans biais, comme les `res/*.ico`
  (0,2576 ; l'outil donne 0,2575 pour une forme de synthèse à 0,259). Les icônes sur plaque ne se mesurent pas avec
  lui (il prend la couleur du centre, celle du logo, pour l'icône).

## Relecture indépendante
Rien de critique ; un important et six mineurs. Le relecteur a remesuré le rayon par une autre méthode : 0,2587 sur
les icônes de la VM, la valeur 0,259 tient. Corrigés, chacun avec un test vu en échec avant (814 tests verts) :
- **Important** : la tuile « = » des calculs dans Spotlight remplissait sa case de 28 pt (rayon 6,5) ; elle prend la
  forme des icônes d'apps (22,5 pt, coins continus), comme les tuiles voisines.
- **Mineurs regradés** :
  - les tuiles dessinées par MacDock (sections de Spotlight, icônes de MacDock) avaient des arcs de cercle : elles
    prennent le coin continu d'Apple (`ui::smoothRectGeometry`) et les `res/*.ico` sont refaits ;
  - l'icône de remplacement de Spotlight (rendu hors écran) prend aussi cette forme ;
  - outil de mesure sans biais versionné (`tools/measure_icon_shape.py`), paragraphe « Étalonnage » rectifié ;
  - README : le rayon à donner aux icônes personnalisées.
- Mineurs reportés : voir le journal (« Mineurs reportés — plan 60 »).
