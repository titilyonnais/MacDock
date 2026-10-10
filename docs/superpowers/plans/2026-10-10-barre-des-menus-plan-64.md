# Barre des menus de macOS 26 — plan 64

> **Pour les agents :** exécution en ligne, tâche par tâche, tests d'abord. Reprendre à la première tâche non cochée.

**But :** la suite de ta demande « revois toute l'interface » : après le Dock (plans 60 et 61), la barre des menus prend
les mesures de celle de macOS 26, relevées sur ta VM le 9 octobre.

## Mesures (VM macOS 26 en 1x contre rendu hors écran de MacDock, en points)
- Ligne de base du texte : 16,0 sur la VM, ~16,9 dans MacDock (texte trop bas de 0,9).
- Écart d'encre entre titres : 21,2 en moyenne sur la VM, 22,1 dans MacDock (la largeur de texte est arrondie à
  l'entier supérieur : +0,5 en moyenne).
- Pas entre icônes d'état (loupe → Centre de contrôle) : 36,0 sur la VM, 30 dans MacDock. Avec la troisième icône de la
  VM (recopie de l'écran, 17,85 de large) : pas de 33,7 puis 36,0, écarts d'encre de 18,9 puis 23,7 ; ni case fixe, ni
  écart fixe, ni marge fixe autour du dessin n'expliquent les deux (les symboles de macOS ont des marges propres).
- Centre de la dernière icône → début de la date : 30,9 sur la VM, 26 dans MacDock.
- Fin de l'heure → bord droit : 19,2 sur la VM, 22,0 dans MacDock.
- Horloge : « Ven. 9 oct.  22:17 » (jour avec majuscule, écart d'encre de 10,5 entre la date et l'heure) contre
  « ven. 9 oct. 23:52 » (écart de 5,5). SF Pro Text 13 pt : espace 3,66, espace fine (U+2009) 1,83, ultra-fine
  (U+200A) 0,91 ; approches du point et du chiffre ~1,84 en tout.
- Loupe : trait de 1,1 sur la VM, 1,6 dans MacDock.
- Centre de contrôle : 12,9 × 12,9, traits ~1,0 ; en haut une piste creuse au bouton plein à gauche, en bas une piste
  pleine au bouton creux à droite ; MacDock : 14 × 12, deux pistes creuses aux boutons pleins.
- Menus du Finder : « Aller » entre Présentation et Fenêtre, comme dans MacDock pour l'Explorateur (rendu
  `--app Explorateur` ; mon premier rendu, `--app Finder`, avait les menus génériques). Contenu du menu de la VM non vu
  (rien ne peut être cliqué dans la VM).
- La VM n'a pas d'accélération graphique : barre grise opaque, texte noir pur ; ni la couleur ni la transparence ne
  sont des cibles ici.

## Choix
- Texte placé par sa ligne de base (`textBaseline`, 16 pt depuis le haut de la barre) au lieu de centrer sa boîte de
  ligne.
- Largeur des titres sans arrondi à l'entier supérieur ; `titlePadding` gardé à 10.
- `statusWidth` 30 → 36 : case fixe (le modèle de MacDock), au pas mesuré entre les deux icônes standard de la VM
  (loupe, Centre de contrôle), celles que MacDock dessine aussi ; la troisième, propre à la VM, ne suit aucune règle
  simple. `clockGap` (nouveau, 3 pt) avant la case de l'horloge ; `rightMargin` 10 → 9.
- Horloge : jour avec majuscule ; entre la date et l'heure, deux espaces et une espace ultra-fine (U+200A) : écart
  calculé de 10,1 (cible 10,5 ; 0,4 de moins, peut-être l'arrondi au pixel de la VM en 1x). « Jour sans date »
  (« Dim.  9:05 ») est une extrapolation : la VM n'a montré que le format complet.
- Loupe : trait de 1,1 pt. Centre de contrôle redessiné (dessin à nous, pas le symbole d'Apple).
- Menu « Aller » : existe déjà pour l'Explorateur ; rien à ajouter sans voir celui de la VM.
- `menubar.json` en version 2 : une mesure encore à l'ancien défaut prend le nouveau ; une valeur choisie reste.

## Précautions
- `%APPDATA%\MacDock\menubar.json` (version 1, toutes les mesures aux anciens défauts) : copié dans le scratchpad avant
  tout (`menubar.json.copie-avant-plan64`, empreinte SHA-256 A736607B…BBBB79) ; les tests n'écrivent que dans des
  dossiers temporaires ; `MacMenuBar --snapshot` n'écrit pas ce fichier (empreinte identique avant et après un rendu,
  vérifiée) ; rien n'est écrit dans `%APPDATA%` avant la fusion.

## Tâches
- [x] 1. **Horloge** (`clock_format.*`) : majuscule et deux espaces. Tests.
- [x] 2. **Réglages** (`menubar_settings.*`) : nouveaux défauts, `textBaseline` et `clockGap`, migration v1 → v2. Tests.
- [x] 3. **Mise en page** (`bar_layout.*`, `menubar_window.cpp`) : écart avant l'horloge, titres sans arrondi. Tests.
- [ ] 4. **Rendu** (`bar_renderer.cpp`) : texte posé sur sa ligne de base. Test au pixel.
- [ ] 5. **Pictogrammes** (`popup/glyphs.cpp`) : loupe plus fine, Centre de contrôle redessiné. Tests au pixel.
- [ ] 6. **Menu « Aller »** : vérifier sur un rendu `--app Explorateur` (aucun ajout sans le menu de la VM).
- [ ] 7. **Essais réels** : rendu hors écran mesuré contre la VM, Release, barre relancée.

  Puis documentation, relecture indépendante, corrections, fusion locale.
