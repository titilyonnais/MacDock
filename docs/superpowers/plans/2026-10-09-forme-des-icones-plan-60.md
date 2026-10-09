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
- **Étalonnage** : la même chaîne (forme rendue à 52 px, étirée ×1,625, mesurée) sous-estime le rayon de 0,0065 à
  0,007, quel que soit l'étirement (bilinéaire, bicubique, Lanczos). Rayon de Tahoe : **0,259 du côté** (± 0,002).
- **Pas mesuré ici** : la taille de la forme dans sa case (824/1024 gardé), le Dock lui-même (plan suivant).

## Choix
- `kIconCornerRatio` (forme par défaut) et le défaut de `iconCornerRatio` (`dock-metrics.json`) passent à 0,259.
- `dock-metrics.json` en version 6 : une valeur encore à l'ancien défaut (0,225) prend 0,259 ; une valeur choisie
  reste.
- Les rayons écrits en dur (grille d'Apps, sélecteur d'apps, tuiles de Spotlight) lisent la même constante.
- Les icônes de MacDock (`res/*.ico`, dessinées par le code) sont refaites avec la nouvelle forme.

## Tâches
- [ ] 1. **Forme et mesures** (`icons/squircle.h`, `config/metrics.*`, `apps/apps_window.cpp`,
  `switcher/switcher_window.cpp`, `spotlight/spotlight_window.cpp`) : constante, défaut, migration v6, rayons en dur.
  Tests (migration, défauts, masque).
- [ ] 2. **Icônes de MacDock** : `make_icons` relancé, `res/*.ico` refaits ; contrôle de leur forme.
- [ ] 3. **Essais réels** : MacDock recompilé et relancé ; son Dock capturé (sa seule fenêtre) et mesuré par la même
  méthode (rendu direct, sans étirement : attendu ≈ 0,257) ; comparaison côte à côte avec le Dock de la VM.

  Puis documentation, relecture indépendante, corrections, fusion locale.
