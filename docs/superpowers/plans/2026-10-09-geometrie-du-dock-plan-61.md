# Géométrie du Dock de macOS 26 — plan 61

> **Pour les agents :** exécution en ligne, tâche par tâche, tests d'abord. Reprendre à la première tâche non cochée.

**But :** la suite de ta demande (« revoir toute l'interface, Dock et icônes d'abord ») : le Dock de MacDock prend les
proportions de celui de macOS 26, mesurées sur ta VM. Comparés à taille d'icône égale, les icônes de MacDock étaient
plus espacées, le Dock plus épais, les séparateurs plus courts et plus serrés.

## Mesures (VM macOS 26, 9 octobre ; pixels de la VM, forme d'une icône P = 52,2)
- Marge entre la forme et le bord du Dock : 15,97 en haut, 16,00 en bas, 16,3 en largeur → 0,306·P (icônes centrées).
- Pas entre deux icônes : 68,0 → 1,303·P. Hauteur du Dock : 84,0 → 1,609·P.
- Extrémités : le plus grand coin continu qui tient dans la hauteur (27,6 ; hauteur / 3,057 = 27,5). MacDock dessine
  déjà cette forme (son rendu plafonne le rayon à cette valeur) : rien à changer.
- Séparateur : trait d'environ 1 px, centré, long de 61,5 → 1,178·P ; il ajoute 31,1 au pas → 0,596·P.
- Point des apps ouvertes : environ 4 px, centre à 6,41 du bas du Dock → 0,123·P.
- Écart entre le Dock et le bas de l'écran : 5,29 → 0,101·P.
- La VM n'a pas d'accélération graphique : macOS y remplace le verre par un gris plat. Le verre n'est pas mesuré ici.

## Choix
- Valeurs données pour la taille 48 (celle de MacDock par défaut et la tienne), forme 38,625 pt :
  `dockPadding` 8 → 7,1 ; `iconGap` 4 → 2,3 ; `separatorMargin` 7 → 9,9 ; `separatorLengthRatio` 0,80 → 0,95 ;
  `indicatorCenterFromBottom` 4 → 4,7 ; `dockScreenMargin` 5 → 3,9. `indicatorDiameter` (4) et le rayon automatique
  restent.
- Ces mesures restent en points, comme avant : à une autre taille du Dock, les proportions s'écartent un peu. Savoir si
  macOS les garde proportionnelles demande une mesure de la VM à une autre taille (avec toi).
- `dock-metrics.json` en version 7 : une valeur encore à l'ancien défaut prend le nouveau ; une valeur choisie reste.

## Tâches
- [ ] 1. **Mesures** (`config/metrics.*`) : défauts, migration v7. Tests (défauts, migration, géométrie, point loin de
  l'icône).
- [ ] 2. **Essais réels** : MacDock recompilé et relancé ; son Dock rendu hors écran et comparé à celui de la VM à
  taille d'icône égale ; proportions relevées sur le rendu.

  Puis documentation, relecture indépendante, corrections, fusion locale.
