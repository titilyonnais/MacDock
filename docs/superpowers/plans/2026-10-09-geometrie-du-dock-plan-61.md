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
- La VM tourne en 1x (barre des menus de 24 px) : avec une forme de 52,2 pt, son Dock était réglé vers 64–65 pt, pas
  à 48 (relecture).

## Choix
- Valeurs données pour la taille 48 (celle de MacDock par défaut et la tienne), forme 38,625 pt :
  `dockPadding` 8 → 7,1 ; `iconGap` 4 → 2,3 ; `separatorMargin` 7 → 9,9 ; `separatorLengthRatio` 0,80 → 0,95 ;
  `indicatorCenterFromBottom` 4 → 4,7 ; `dockScreenMargin` 5 → 3,9. `indicatorDiameter` (4) et le rayon automatique
  restent.
- Les valeurs à 48 supposent que macOS garde ces proportions d'une taille à l'autre (ramenées de ~65 pt à 48, × 0,74).
  Si macOS garde au contraire certaines distances fixes en points, elles sont fausses d'un facteur 1,35 (écart sous le
  Dock 5,3 pt au lieu de 3,9, centre du point 6,4 au lieu de 4,7). Mesure décisive, avec toi, dans la VM :
  `defaults read com.apple.dock tilesize`, puis `defaults write com.apple.dock tilesize -int 48; killall Dock`, capture.
- Ces mesures restent en points, comme avant : aux tailles extrêmes, les proportions s'écartent nettement (à 16, le
  Dock est 46 % plus épais par rapport aux icônes qu'à 48 ; à 128, le séparateur s'arrête à 10 pt des bords du verre au
  lieu de 22). La mesure ci-dessus dira s'il faut passer marges et écarts en proportions de la case.
- `dock-metrics.json` en version 7 : une valeur encore à l'ancien défaut prend le nouveau ; une valeur choisie reste.

## Tâches
- [x] 1. **Mesures** (`config/metrics.*`) : défauts, migration v7. Tests (défauts, migration, géométrie, point loin de
  l'icône).
- [x] 2. **Essais réels** : MacDock recompilé et relancé ; son Dock rendu hors écran et comparé à celui de la VM à
  taille d'icône égale ; proportions relevées sur le rendu.

  Puis documentation, relecture indépendante, corrections, fusion locale.

## Fait
- Suite : 815 tests verts ; tout compilé en Release, MacDock relancé.
- `dock-metrics.json` de l'utilisateur migré au démarrage : version 7, les six valeurs nouvelles.
- **Rendu hors écran** (`--snapshot`, taille 48, échelle 2) : pas de 100,5 px (attendu 100,6), soit 1,301·P contre
  1,303·P sur la VM ; comparaison côte à côte à taille d'icône égale : espacement, épaisseur et séparateurs pareils. Le
  point, lui, n'a pas été converti : il est resté à 4 pt alors que, ramené comme le reste, il ferait ~3 pt (sur la
  planche, ~6 px pour la VM, 8 pour MacDock) ; il reste ainsi jusqu'à la mesure à la taille 48. Le verre diffère (la
  VM l'affiche en gris plat, faute d'accélération graphique).

## Relecture
Verdict « avec corrections » (agent indépendant, opus).
- **Important 1, corrigé** (cd4b2e1) : quand le Dock rétrécit pour tenir sur son axe, le séparateur sortait du verre,
  et la marge sous les icônes, le point et les extrémités restaient à la taille réglée. Test
  `controller_dock_shrinks_to_fit_axis` rouge (séparateur de 45,6 px pour 23,5 attendus, icônes décalées de 3,4 px,
  point à 4,7 au lieu de 2,4), puis vert.
- **Important 2** : « points pareils » retiré ; le diamètre reste à 4 pt en attendant la mesure à 48.
- **Important 3** : hypothèse de proportionnalité écrite plus haut ; la mesure décisive attend ta VM.
- **Mineurs** : commentaires périmés de la migration et du rayon corrigés ; « s'écartent un peu » précisé. Reporté :
  le point n'est à 5 pt de la forme de l'icône qu'à partir de la taille 47 (3,5 pt à 32, 2,0 à 16) ; à trancher avec la
  mesure à 48.
