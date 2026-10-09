# « Déplacer et redimensionner » de macOS 26 — plan 50

> **Pour les agents :** exécution en ligne, tâche par tâche, tests d'abord. Reprendre à la première tâche non cochée.

**But :** le menu Fenêtre de macOS 26 range une fenêtre sans la glisser :
- **Remplir** et **Centrer** ;
- **Déplacer et redimensionner** : moitiés (gauche, droite, haut, bas), quarts, « Revenir à la taille précédente ».

Celui de MacDock n'envoie que Win+← et Win+→.

## Choix
- Marges de 8 pt autour des fenêtres rangées et entre elles, comme le réglage par défaut de macOS (« Les fenêtres en
  mosaïque ont des marges »).
- Géométrie sur le **cadre visible** de la fenêtre (sans ses bordures invisibles), dans la zone de travail de son
  écran (hors barre des menus et zone réservée du Dock).
- Fenêtre agrandie ou réduite : restaurée d'abord.
- « Revenir à la taille précédente » : le cadre d'avant le premier rangement ; enchaîner deux rangements ramène
  quand même au tout premier.
- Les entrées Win+← et Win+→ disparaissent, couvertes par les moitiés.

## Tâches
- [x] 1. **Géométrie** (`src/shell/window_tile.*`, pur) :
  - `TileAction`, `parseTileAction` et `tileActionName` ;
  - `tileRect(action, zone de travail, cadre actuel, marge)`.

  Tests : moitiés, quarts, Remplir, Centrer (taille gardée, puis bornée), marges entre deux moitiés.
- [x] 2. **Placement et menu** :
  - `tileWindow(fenêtre, action)` : restaurée si besoin, cadre visible calé, cadre d'avant gardé ;
  - menu Fenêtre : Remplir, Centrer, sous-menu « Déplacer et redimensionner » (`ActionKind::Tile`).

  Tests : les entrées du menu et leurs actions ; placement réel d'une fenêtre d'essai (sonde).
- [ ] 3. Essai réel, documentation, relecture, fusion.

## Essai réel
Sonde sur une fenêtre d'essai à moi, écran 4K à 200 % (marge de 16 px) : Gauche, En bas à droite, Revenir (au tout
premier cadre après deux rangements), Remplir depuis l'état agrandi (restaurée d'abord), Centrer : cadre visible
obtenu exactement égal au cadre voulu.
