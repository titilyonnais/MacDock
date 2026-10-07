# Journal de nuit — 7 octobre 2026

Travail en autonomie, de 00 h 38 à 8 h, à ta demande (« prends des initiatives, fais grossir le projet tout seul, sans me demander »). Ce fichier est mis à jour au fil de la nuit : c'est le premier à lire au réveil.

## Ce qui est fait

### Plan 5 — Piles complètes (fusionné dans `main`)

- **Icône « Pile »** : dans le Dock, la pile Téléchargements montre ses 3 derniers fichiers empilés, ceux du dessous légèrement inclinés, comme sur macOS. Elle se met à jour en direct quand un fichier arrive ou disparaît. Clic droit › *Afficher comme* › *Dossier* pour revenir à l'icône du dossier.
- **Présentation en liste** : clic droit › *Présenter le contenu comme* › *Liste*. La pile s'ouvre alors en menu de verre avec les icônes des fichiers ; les sous-dossiers s'ouvrent en sous-menus. La liste se limite à ce qui tient à l'écran.
- Essais réels : rendu de l'icône composée vérifié sur image (`--snapshot`) ; dossier temporaire épinglé : l'aperçu passe de 0 à 1 fichier puis revient à 0 ; liste ouverte avec un sous-menu. Tes réglages ont été restaurés à chaque fois.
- Relecture finale : 3 problèmes importants, corrigés. Un fichier remplacé sous le même nom (capture réenregistrée) met maintenant l'icône à jour ; chaque sous-dossier de la liste se termine par « Ouvrir dans l'Explorateur » ; les icônes de la liste visible passent avant celles des sous-menus. Une rafale d'avis (téléchargement) met l'icône à jour au moins toutes les 2 s.

### Plan 4 — Positions, écrans et piles (fusionné dans `main`)

- **Dock à gauche ou à droite** (clic droit sur le séparateur › Position à l'écran), changé à chaud. La zone réservée suit le bord ; les menus, les infobulles et la grille des piles s'ouvrent à côté du Dock.
- **Trop d'éléments pour l'écran** (Dock vertical sur un portable…) : tout le Dock rétrécit pour tenir, comme sur macOS.
- **Plusieurs écrans** : pousse le curseur contre le bord du Dock sur un autre écran (≈ 0,35 s de mouvement contre le bord) et le Dock y passe. Il s'en souvient (`"screen"` dans `settings.json`) et retombe sur l'écran principal si celui-ci est débranché.
- **Piles** : un clic sur Téléchargements ouvre son contenu comme sur macOS.
  - *Éventail* : icônes en arc au-dessus de la pile, nom à gauche. Il ne dépasse jamais le haut de l'écran ; en haut, « Ouvrir dans l'Explorateur » ou « N de plus dans l'Explorateur ».
  - *Grille* : panneau Liquid Glass avec le titre, molette pour défiler, « Ouvrir dans l'Explorateur » en bas.
  - *Automatiquement* : éventail jusqu'à 9 éléments, grille au-delà.
  - Menu de la pile : Trier par (Nom, Date d'ajout, Date de modification, Type), Présenter le contenu comme.
  - Les vignettes se chargent en arrière-plan : la souris ne saccade jamais.
- **Ouvrir à la connexion** marche aussi pour les apps du Store (raccourci `MacDock - <nom>.lnk` dans ton dossier Démarrage).

**Relecture finale** par un agent indépendant : 1 critique (Dock vertical qui débordait de l'écran), 3 importants (changement d'écran déclenché par une simple attente au bord, éventail trop haut, vignettes qui bloquaient la souris), tous corrigés ; 4 mineurs corrigés au passage, 6 notés plus bas.

**Essais réels** : Dock à gauche (fenêtre 0–819 px) et à droite (3021–3840 px), repli d'écran, éventail et grille sur ton dossier Téléchargements (67 éléments), raccourci de la Calculatrice créé puis supprimé. Tes réglages ont été restaurés après chaque essai ; la Calculatrice et la fenêtre de l'Explorateur ouvertes par les essais ont été refermées.

### Plan 3 — Interactions (fusionné dans `main`)

- **Glisser-déposer interne** : réorganiser les épingles, tirer une icône vers le haut (« Supprimer ») puis la lâcher dans un nuage « poof ». Une app ouverte n'est que désépinglée.
- **Menus en verre** (clic droit), avec sous-menus, clavier et fermeture au clic extérieur (le clic est absorbé, comme sur macOS) :
  - *app* : ses fenêtres, Options › (Garder dans le Dock, Ouvrir à la connexion, Afficher dans l'Explorateur), Afficher toutes les fenêtres, Masquer, Quitter ;
  - *séparateur ou zone vide* : masquage automatique, agrandissement, position (Gauche/Droite arrivent au plan 4), Réglages ;
  - *Corbeille* : Ouvrir, Vider. *Pile* : Ouvrir, Retirer. *Fenêtre réduite* : Restaurer, Fermer.
- **Corbeille vide ou pleine** : l'icône suit son contenu.
- **Masquage automatique** : le Dock glisse sous le bord et revient quand le curseur touche le bas (0,5 s de répit au départ du curseur). Pas de zone réservée dans ce mode. **En plein écran** (jeu, vidéo, F11), le Dock s'efface toujours.
- **Dépôt de fichiers depuis l'Explorateur** : un `.exe` ou un raccourci entre deux icônes s'épingle ; des fichiers sur une app s'ouvrent avec elle ; sur la Corbeille ils y partent (annulable) ; sur une pile ils y sont déplacés.
- **Fenêtres réduites** : miniature en direct (DWM) dans le Dock, petite icône de l'app dans le coin.
- **Icône pressée assombrie**, comme sur macOS.

**Relecture finale** par un agent indépendant : 0 critique, 4 importants + 1 mineur reclassé, tous corrigés (un clic qui tremble n'épingle plus une app ; le dépôt respecte le protocole du Shell et la source ne supprime jamais l'original ; les commandes de menu visent le bon élément même si le Dock change pendant le menu ; options désactivées pour les apps du Store). 9 mineurs notés plus bas.

**Ta machine n'a pas été abîmée par les essais** : tes réglages ont été sauvegardés et restaurés à chaque essai, les fichiers d'essai envoyés à la Corbeille ont été restaurés puis supprimés de `%TEMP%`, ta Corbeille (72 éléments) n'a jamais été vidée.

### Plan 2 — Géométrie fidèle et Liquid Glass (fusionné dans `main`)

**Tes retours, corrigés :**
- rayon du fond concentrique avec celui des icônes (≈ 21,4 pt pour des icônes de 48 pt) ;
- point indicateur détaché de l'icône, centré à 4 pt du bas du fond ;
- magnification ramenée à 80 pt au lieu de 128. Tes fichiers de `%APPDATA%\MacDock` ont été migrés et tes réglages personnels conservés.

**Formes Apple :** coins continus (« squircle ») au lieu d'arcs de cercle, et grille d'icônes officielle (forme à 824/1024 de la case, ombre portée).

**Liquid Glass en direct :**
- capture de ce qui est sous le Dock, flou, réfraction sur les bords, aberration chromatique légère, reflet de Fresnel, liseré lumineux, teinte adaptative, ombre ;
- HDR pris en charge (ton écran est en HDR, c'est vérifié) ;
- au repos, le Dock ne redessine rien : mesuré à 0,16 % d'un cœur, 0 image.

**Outils de calibration :**
- `--snapshot … --reference mac.png --diff diff.png` ;
- superposition d'une capture de macOS avec Ctrl+Alt+Maj+O ;
- `--capture-test`.

**Relecture finale** par un agent indépendant : 0 point critique, 5 importants corrigés, 2 mineurs reclassés et corrigés, 10 mineurs notés pour plus tard (voir plus bas).

### À savoir
- **Le Dock n'apparaît plus sur les captures d'écran** quand le verre est actif : c'est le prix de la lecture de l'écran sous lui. Mets `"glass": false` dans `settings.json` si tu en as besoin.
- Les icônes de ton bureau ont pu se déplacer : le Dock réserve sa hauteur comme zone de travail, comme avant.

### ⚠ Le mod Windhawk n'est pas installé
`macdock-hide-taskbar` n'apparaît pas dans Windhawk : la barre des tâches Windows reste visible et le Dock se pose juste au-dessus d'elle. Pour le rendu final, installe le mod depuis `windhawk\macdock-hide-taskbar.wh.cpp` (Windhawk → Créer un mod → coller → Compiler).

## Vérifications à faire toi-même — plan 4
1. L'aspect de l'éventail (arc, noms) et de la grille en verre : je ne vois pas l'écran.
2. Un second écran : pousse le curseur contre le bas de l'autre écran.
3. Dock à gauche puis à droite : rien ne doit rester réservé sur l'ancien bord.
4. Calculatrice (ou une autre app du Store) cochée « Ouvrir à la connexion », puis une reconnexion.

## Vérifications à faire toi-même — plan 3
1. Miniatures des fenêtres réduites : je n'ai pas pu les voir (sur ton écran HDR, les captures d'écran sortent noires).
2. Icône de la Corbeille vide : ta Corbeille étant pleine, seule l'icône pleine a été vue.
3. Glisser un fichier depuis l'Explorateur vers une pile (Téléchargements), puis depuis un autre gestionnaire de fichiers.
4. Plein écran d'une vidéo YouTube et F11 dans ton navigateur.
5. Aspect du nuage « poof » et de l'étiquette « Supprimer ».

## Vérifications à faire toi-même — plan 2 (je n'ai pas d'écran)
1. Le verre sur ton fond d'écran : flou, réfraction sur les bords, liseré.
2. Une fenêtre déplacée sous le Dock : le verre suit.
3. Une invite UAC : le Dock passe en verre dépoli, puis reprend.
4. Un changement de résolution.
5. HDR activé, puis désactivé.
6. Mode clair et mode sombre.
7. `"glass": false` dans `settings.json`.
8. Ctrl+Alt+Maj+O avec une capture de Tahoe dans `%APPDATA%\MacDock\reference\overlay.png`.

## Décisions prises sans toi (plan 4)
- Dock à gauche ou à droite : les piles s'ouvrent toujours en grille, à côté (macOS ne fait l'éventail qu'avec un Dock en bas).
- « Ouvrir dans l'Explorateur » en haut de l'éventail (comme le Finder) et en bas de la grille.
- « Afficher comme (Pile, Dossier) » et la présentation « Liste » sont reportées au plan 5 : pas d'entrée de menu sans effet.
- Infobulle d'un Dock vertical posée à côté de l'icône (la fenêtre est plus large de 240 pt pour elle).
- Le test de « Ouvrir à la connexion » écrit dans un dossier temporaire, jamais dans ton dossier Démarrage.

## Décisions prises sans toi (plan 5)
- Icônes de la liste tirées de la liste d'icônes système (rapide, jamais de vignette), 400 au plus.
- La liste se limite à ce qui tient à l'écran (le menu ne défile pas) ; le reste s'ouvre par « Ouvrir dans l'Explorateur ».
- Rafale d'avis d'un dossier regroupée 400 ms, avec au plus 2 s d'attente.
- Essai réel de la liste après le plafond de hauteur non refait : il pilote la souris et tu étais là.

## Mineurs reportés — plan 5
- Couches inclinées de l'icône de pile un peu coupées aux coins et sans lissage de bord.
- Cache des icônes de fichiers jamais purgé (borné par les piles et les dossiers parcourus).

## Mineurs reportés — plan 4
- Raccourci de démarrage nommé d'après le nom affiché (deux apps homonymes, ou un renommage, se gênent).
- Énumération du dossier d'une pile synchrone (un partage réseau hors ligne figerait le Dock).
- Dock vertical : « Éventail » peut apparaître coché alors que la grille s'ouvre.
- Tri « Type » par extension, pas par description du type.
- Dock rétréci : la zone réservée garde l'épaisseur normale.
- Code du verre encore dupliqué entre les menus et les piles.

## Décisions prises sans toi (plan 2)
Chaque décision est notée avec son coût si elle est fausse. La liste complète figure dans le message de fin de plan.
- Test de réfraction avec des bandes horizontales (au bord haut, la réfraction est verticale).
- Plancher de luminosité du verre clair à 0,38, pour garder le point lisible sur fond noir.
- En HDR, Windows signale tout l'écran comme modifié à chaque image. Le Dock compare donc le contenu réel (image réduite au quart) avant de se redessiner ; sans cela, il tournait à 165 images/s.
- La première image de la capture peut être noire : elle est ignorée.
- Aucune capture de Tahoe sur cette machine : les valeurs par défaut sont gardées.

## Décisions prises sans toi (plan 3)
- Une seule capture d'écran possible par processus : pendant un menu, celle du Dock est suspendue et le menu capture tout l'écran (≈ 66 Mo de mémoire graphique le temps du menu).
- Clic droit dans le vide du Dock = menu du séparateur (macOS n'affiche rien).
- Plein écran vérifié au changement d'app au premier plan et chaque seconde (pour F11 ou une vidéo).
- Une fenêtre maximisée avec barre de titre ne compte pas comme plein écran (sinon, barre Windows masquée, le Dock ne reviendrait jamais).
- Badges et barre de progression reportés : le mod ne relaie pas ces informations.

## Mineurs reportés — plan 3
- Le Dock ne s'anime pas pendant qu'un menu ou une boîte de dialogue Windows est ouvert.
- Un rechargement de `settings.json` pendant un menu fait passer le Dock en verre dépoli quelques secondes.
- Appui mémorisé par index (un changement du Dock pile pendant l'appui pourrait tirer la mauvaise icône).
- Clés Run très longues ignorées par « Ouvrir à la connexion ».
- Tirer la première icône laisse 4 pt de fond en trop.
- Interrogation de la Corbeille synchrone (un disque en veille peut figer le Dock un instant).
- Miniature DWM en échec réessayée à chaque image.
- Dépôt d'un dossier nommé `x.exe` accepté comme épingle.
- Détails : emoji coupé dans un titre long, racine de lecteur mal citée, Échap lu au mouvement suivant, menu de plus de 40 fenêtres sans défilement.

## Mineurs reportés — plan 2
- Région perdue si le Dock bouge pendant une copie.
- Une image grise possible au redimensionnement.
- Séparateur peu contrasté en clair sur fond noir.
- Migration `largeSize` si les icônes font plus de 80 pt.
- `indicatorInset` supprimé sans message.
- Flou décalé de 3 px au plus.
- Flou tronqué si `glassBlur` est très grand.
- `pending_` bloqué si la file de messages est pleine.
- `--snapshot` réécrit tes fichiers de réglages.

## Vérifications à faire toi-même — plan 5
1. L'icône de Téléchargements dans le vrai Dock (lance `build\Debug\MacDock.exe` ou ta version installée).
2. Un téléchargement en cours : l'icône suit sans scintiller.
3. Pile en liste : survol d'un sous-dossier, clic sur un fichier.

## Suite
Le sous-projet 1 (le Dock) couvre maintenant toute la spec, sauf les badges et la barre de progression (le mod Windhawk ne les relaie pas, et il n'est pas installé). Le prochain grand morceau serait le sous-projet 2 : la barre de menus.
