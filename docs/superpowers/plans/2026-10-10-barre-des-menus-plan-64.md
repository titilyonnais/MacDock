# Barre des menus de macOS 26 — plan 64

> **Pour les agents :** exécution en ligne, tâche par tâche, tests d'abord. Reprendre à la première tâche non cochée.

**But :** la suite de ta demande « revois toute l'interface » : après le Dock (plans 60 et 61), la barre des menus prend
les mesures de celle de macOS 26, relevées sur ta VM le 9 octobre.

## Mesures (VM macOS 26 en 1x contre rendu hors écran de MacDock, en points)
- Ligne de base du texte : 16,0 sur la VM, 16,47 dans MacDock (texte trop bas de 0,45 ; « ~16,9 » dans une première
  mesure qui mélangeait les méthodes, voir « Décisions en cours de route »).
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
- [x] 4. **Rendu** (`bar_renderer.cpp`) : texte posé sur sa ligne de base. Test au pixel.
- [x] 5. **Pictogrammes** (`popup/glyphs.cpp`) : loupe plus fine, Centre de contrôle redessiné. Tests au pixel.
- [x] 6. **Menu « Aller »** : vérifier sur un rendu `--app Explorateur` (aucun ajout sans le menu de la VM).
- [x] 7. **Essais réels** : rendu hors écran mesuré contre la VM, Release, barre relancée.

  Puis documentation, relecture indépendante, corrections, fusion locale.

## Décisions en cours de route
- **Mesures refaites, même méthode des deux côtés** (`baseline_measure.py`, `edge_gaps.py` dans le scratchpad) : bords à
  50 % de l'encre, bruit du fond de la VM retiré ; le flou de l'étirement de VMware ne déplace pas ce point. Ligne de
  base : VM 16,00 à 16,09 ; MacDock 16,47 avant (et non 16,9 : l'ancienne mesure mélangeait les méthodes). Écart
  d'encre date → heure : VM 10,12 (10,46 dans les premières mesures, seuil différent) ; MacDock 5,62 avec une espace,
  donc ≈ 10,19 avec deux espaces et une ultra-fine (tâche 1 gardée).
- **`textBaseline` suit le milieu de la barre** : une autre hauteur garde le texte au même écart de son milieu, comme
  le logo et les icônes, centrés eux aussi. Si c'était faux : rien ne change à la hauteur par défaut.
- **« FR » garde son centrage** : ses majuscules sont déjà au milieu de la boîte à 0,5 px près (SF Pro, Segoe UI,
  Inter Tight, Arial, Georgia, mesuré). Le poser sur la ligne de base de la barre le mettrait trop bas dans sa boîte.
  Test gardé comme garde-fou (vert avant comme après).
- **Icônes d'état : cotes ajustées sur la capture, plus de « 1,1 » ni de « ~1,0 »** (`fit_icons.py`,
  `fit_cc_global.py`) : sur la VM, le nom de l'app, les titres et la pomme sont d'un noir plein, mais les icônes
  d'état, la date et l'heure sont à 85 % de noir ; les premières épaisseurs, normalisées sur le noir de « Finder » et
  sans les queues du flou, étaient trop faibles. Ajustement d'un dessin paramétré, rendu en 1x puis étiré x 1,625 comme
  VMware, comparé pixel à pixel (écart moyen 3,4 et 3,7 niveaux de gris, sans erreur systématique) :
  - loupe : anneau de 4,50 de rayon, trait 1,32 ; manche à 45,6°, jusqu'à 9,16 du centre, trait 1,90 ; boîte
    12,5 x 12,7, haut à 6,36 (un peu plus bas que le milieu de la barre) ;
  - Centre de contrôle : 13,19 x 13,30 ; piste du haut de 6,48, contour de 1,27, bouton plein de 5,09 x 2,98 à 0,48 du
    contour ; écart de 1,02 ; piste du bas de 5,80, pleine, bouton creux de 5,47 x 3,38 bordé de 1,21.
  Notre rendu, passé par le même chemin et ajusté de même, retombe sur ces cotes à 0,15 pt près (le plus souvent
  moins de 0,09). Dessins à nous (champs de distance et formes Direct2D), aucun symbole d'Apple.
- **Menu « Aller »** : présent pour l'Explorateur, entre Présentation et Fenêtre, comme sur la VM (rendu
  `--app Explorateur` comparé à la capture). Rien d'ajouté.
- **Rendus de mesure reproductibles** : `MacMenuBar --snapshot` prend `--scale` (vrai rendu 1x, comme la VM) et
  `--time 2026-10-09T22:17` (l'horloge de la capture). Notre rendu 1x, étiré x 1,625 comme VMware, se compare tel quel
  à la capture.
- **Horloge à chiffres à chasse fixe** : sur la VM, « 22:17 » fait 35 pt et son « 1 » est centré dans une case large
  comme un « 0 » : macOS dessine l'horloge en chiffres tabulaires (elle ne bouge pas d'une minute à l'autre). MacDock
  aussi désormais (fonction OpenType tnum). Du coup l'espace ultra-fine de la tâche 1 était de trop : deux espaces
  simples donnent 10,34 pt (1x) et 10,70 (2x) entre la date et l'heure, contre 10,35 sur la VM.
- **`clockGap` 1,5 et `rightMargin` 8,4** (au lieu de 3 et 9) : mesurés sur ces rendus, icône → date 23,88 (1x) et
  24,10 (2x) contre 23,99 ; marge droite 19,27 et 19,76 contre 19,71.
- **Pas de « trak » dans ce plan** (contrordre du chef d'orchestre, 04 h 40) : le texte de MacDock est 1 à 1,6 pt plus
  large par mot que celui de la VM. Cause trouvée : CoreText applique la table AAT « trak » de SF Pro (lue dans la
  police installée : −12/2048 em à 13 pt, soit −0,076 pt par caractère ; +0,151 pt à 9,4 pt), DirectWrite l'ignore.
  L'appliquer à la seule barre la rendrait plus serrée que les menus ouverts juste dessous : plan à part pour tout le
  texte de MacDock (barre, menus, infobulle du Dock, Spotlight, Réglages). Ébauche testée gardée hors du dépôt.

## Tableau final (une seule méthode)
Bords à 50 % de l'encre, encre normalisée côté par côté (85 % de noir à droite sur la VM comme dans MacDock), fond pris
sur les lignes du haut de la barre ; rendus de MacDock passés par le chemin de la VM (`simulate_vm.py` : rendu 1x, étiré
x 1,625 en bilinéaire). « Avant » : rendu du 10 octobre à 3 h 23 (horloge « sam. 10 oct. 3:23 ») ; « après » :
`--time 2026-10-09T22:17`, la même horloge que la capture. Points.

| Cote | VM macOS 26 | Avant | Après 1x | Après 2x |
|---|---|---|---|---|
| Ligne de base du texte | 16,02 | 16,51 | 16,03 | 16,05 |
| Écart d'encre entre titres (moyenne) | 22,30 | 22,50 | 21,40 | 21,70 |
| Pas entre les deux icônes (centres) | 36,43 | 29,74 | 36,06 | 36,11 |
| Icône → début de la date (encre) | 23,83 | 19,35 | 23,32 | 23,84 |
| Centre de l'icône → début de la date | 30,30 | 26,43 | 29,78 | 30,34 |
| Date → heure (encre) | 10,06 | 6,12 | 9,70 | 10,23 |
| Fin de l'heure → bord droit | 19,59 | 21,26 | 19,11 | 19,54 |
| Largeur de la loupe (50 %) | 12,07 | 12,81 | 12,13 | 12,26 |
| Largeur du Centre de contrôle (50 %) | 12,95 | 14,16 | 12,94 | 13,01 |

Dessins (ajustement du même modèle, VM contre « après ») : loupe à 0,06 pt près (anneau 1,32 / 1,36, manche 1,90 / 1,90) ;
Centre de contrôle à 0,15 pt près (contour 1,27 / 1,24, pistes 6,48 et 5,80 / 6,33 et 5,79).

Écarts restants, expliqués :
- **À l'échelle 1**, le texte est calé au pixel entier : jusqu'à 0,5 pt de décalage (icône → date, marge droite). À
  l'échelle 2 (l'écran de l'utilisateur), tout est à 0,3 pt près, sauf les deux lignes suivantes.
- **Écart entre titres** : 21,7 contre 22,3. Mesuré proprement, macOS met ~10,4 pt de marge de chaque côté d'un titre
  (MacDock : 10). L'ancien arrondi à l'entier supérieur masquait l'écart (22,50) ; la marge sera recalée avec le plan
  « trak », qui revoit la largeur de tout le texte en même temps.
- **Pas des icônes** : 36,1 contre 36,4 : MacDock garde des cases fixes de 36, macOS donne à chaque icône sa largeur.

## Fait
- Sept commits de tâches (aad32ba à 11e60cb) et un de corrections (9620eb3) sur `feat/barre-vm-64`.
- Suite : 857 cas, un seul échec, dû à l'environnement (`uia_menus_read_and_invoke_fake_app` : le Gestionnaire des
  tâches, en administrateur, tient le premier plan).
- `menubar.json` de l'utilisateur intact jusqu'à la fusion (empreinte A736607B… vérifiée après chaque rendu).

## Relecture indépendante (opus)
Verdict « avec corrections », aucun critique. Corrigés, rouge puis vert :
- **Important** : la ligne de base ignorait la taille du texte (un texte de 20 pt dans une barre de 34 remontait de
  6 px) ; l'écart au milieu suit maintenant `fontSize` (`bar_renderer_text_sits_on_its_baseline`, deux cas ajoutés).
- **Mineur regradé important** : un titre à saut de ligne se dessinait sous la barre ; sa première ligne se pose sur la
  ligne de base (`bar_renderer_multiline_title_keeps_first_line`).
- Commentaire périmé d'un test, documentation (README, journal, feuille de route) : faits.

## Mineurs reportés
- Manche de la loupe : son bout arrondi déborde de 0,25 pt dans le verre de l'anneau.
- Horloge : mise en page refaite seulement quand la longueur du texte change (au changement de jour, jusqu'à 1,5 pt de
  débord de sa case jusqu'au prochain changement d'app).
- `--scale` sans borne, virgule décimale non lue, `--time` invalide ignoré sans avertissement.
- Écart avant l'horloge : zone morte au clic gauche.
- Échec de la réécriture du fichier migré non noté dans le journal.

## Limites
- Une seule capture de la VM, à l'échelle 1 et sans accélération graphique (barre grise opaque) : couleurs et
  transparence ne sont pas des cibles. Côté gauche (pomme, nom de l'app, titres) en noir plein sur la VM, côté droit à
  85 % : MacDock dessine tout à 85 %, noté sans être changé.
- Icônes mesurées : loupe et Centre de contrôle. Son, saisie (« FR »), Wi-Fi et batterie n'apparaissent pas sur la VM :
  non mesurés.
- Largeur des mots : 1 à 1,6 pt de trop par mot (table « trak », plan à venir).
