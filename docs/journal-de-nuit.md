# Journal de nuit — 7 octobre 2026

Travail en autonomie, de 00 h 38 à 8 h, à ta demande (« prends des initiatives, fais grossir le projet tout seul, sans me demander »). Ce fichier est mis à jour au fil de la nuit : c'est le premier à lire au réveil.

## Journée du 7 octobre : la barre de menus, puis le reste

À ta demande (« fais la barre de menus et fais le reste ensuite, je veux tout faire »), j'enchaîne les sous-projets 2 à 6 en autonomie :
- pour chacun, j'écris une spec et un plan, je les exécute, un agent fait la relecture finale, puis je fusionne dans `main` en local ;
- tes quatre choix sont dans la spec de la barre de menus (`docs/superpowers/specs/2026-10-07-macmenubar-design.md`).

### Plan 6 — Barre de menus (`MacMenuBar.exe`, fusionné dans `main`)

- **Barre transparente** en haut de l'écran (24 pt réservés). Le texte est clair ou foncé selon ton fond d'écran, mesuré sous la barre.
- **Contenu de la barre** :
  - le logo Windows, avec le menu du système (veille, redémarrage et extinction avec confirmation, verrouillage, session) ;
  - le **nom de l'app active** en gras et son menu ;
  - des menus Fichier, Édition, Présentation, Fenêtre, Aide, qui envoient les raccourcis standard à l'app ;
  - pour le bureau et l'Explorateur, un menu **Aller** comme le Finder ;
  - la date et l'heure à droite.
- **Menus en verre** comme ceux du Dock, avec les raccourcis affichés à droite. Une fois un menu ouvert, survoler un autre titre l'ouvre.
- **Plein écran** : la barre s'efface et revient au bord haut.
- **Clic droit dans le vide** : réglages, masquage automatique, quitter.
- **Le lanceur** démarre maintenant le Dock et la barre ; *Quitter MacDock* arrête les deux.
- **Essais réels** :
  - images hors écran sur fonds clair, sombre et sur un fond de Windows ;
  - lancement réel : zone réservée, app active reconnue (« Claude »), fond mesuré (texte clair) ;
  - le lanceur démarre et arrête les deux processus ;
  - tes réglages ont été restaurés.
- **Limite** : les apps sans vraie barre de menus (la plupart des apps modernes) reçoivent des menus génériques. Une commande n'agit que si l'app connaît le raccourci. Les vrais menus des apps Win32 et UI Automation arrivent au plan 7.
- **Relecture finale** : 5 problèmes importants, plus 3 que j'ai jugés importants (1 du lot, 2 classés mineurs par le relecteur). Tous sont corrigés :
  - la barre suit maintenant le bureau et les dialogues ;
  - le menu ouvert ne lit plus de mémoire libérée quand l'heure ou l'app change ;
  - le clavier revient à ton app quand tu fermes un menu sans choisir ;
  - aucune frappe n'est envoyée si ton app n'a pas pu repasser au premier plan ;
  - une app figée ne bloque plus la barre ;
  - la barre se relance si le pilote graphique est réinitialisé ;
  - « Suspendre » obtient le privilège nécessaire.
- **Essai réel sans souris ni clavier** : `tests\real\menubar_unlisted_foreground.ps1` prouve que la barre suit une fenêtre non éligible au Dock.

### Plan 7 — Vrais menus et Éléments récents (fusionné dans `main`)

- **Vrais menus des apps Win32** (barre de menus classique : Bloc-notes historique, Notepad++, 7-Zip, regedit…) :
  - leurs titres et leurs entrées remplacent les menus génériques ;
  - à chaque ouverture, l'app prépare son menu (coches, entrées grisées, fichiers récents) et la barre le relit ;
  - la commande choisie est envoyée à l'app (`WM_COMMAND`) ;
  - pendant un dialogue, les menus restent affichés mais grisés.
- **Vrais menus par UI Automation**, pour les apps sans barre classique mais avec une barre accessible (Bloc-notes de Windows 11, apps Qt) :
  - lus sur un fil à part : une app lente ne fige pas la barre plus de 2,5 s ;
  - les apps Chromium, Electron, Firefox et celles à navigateur intégré (WebView2) ne sont jamais interrogées.
- **Menu Fenêtre** ajouté avant l'Aide quand l'app n'en a pas.
- **Éléments récents** dans le menu du système : les 10 dernières apps et les 10 derniers documents, avec leurs icônes, et « Effacer le menu ». Le dossier Récents de Windows n'est jamais modifié.
- **Relecture finale** : 6 problèmes importants, tous corrigés avec un test :
  - la barre suit une app qui change ses menus (document ouvert) ;
  - une lecture UIA abandonnée n'agit plus en retard dans l'app ;
  - les apps WebView2 (nouvel Outlook, Teams) ne sont plus parcourues ;
  - l'attente ne bloque plus UI Automation ;
  - les entrées dessinées par l'app gardent leur texte ;
  - les icônes des documents ne lisent plus le disque à chaque mise en page.
- **À vérifier toi-même** : le chemin UIA (déplier le menu de l'app, lire, replier, invoquer) est testé sur une app factice, pas sur une vraie. Aucune app concernée n'était ouverte, et l'essai réel aurait affiché des menus pendant que tu travaillais. Ouvre le Bloc-notes de Windows 11 : la barre doit montrer Fichier, Modifier, Affichage, et leurs entrées.

### Plan 8 — Icônes d'état et Centre de contrôle (fusionné dans `main`)

- **Partie droite** comme sur macOS : son, Wi-Fi, batterie, recherche, Centre de contrôle, horloge. Un élément sans matériel est masqué (sur ton poste : ni Wi-Fi, ni batterie, ni Bluetooth, ni luminosité réglable, donc son, recherche, Centre de contrôle et horloge).
- **Menus enrichis** en verre : intitulés, curseurs, interrupteurs, tuiles bleues quand elles sont actives, lecture en cours avec ses boutons. Ils agissent sans se fermer et se mettent à jour toutes les 500 ms.
- **Sources** :
  - Core Audio pour le son, avec notification (l'icône suit la touche volume) ;
  - WlanAPI pour le Wi-Fi, `Windows.Devices.Radios` pour le Bluetooth ;
  - la lecture en cours de Windows (navigateur, Spotify…) ;
  - WMI, puis DDC/CI, pour la luminosité, relue toutes les 30 s ou à l'ouverture d'un menu.
  
  Tout ce qui est lent tourne sur un fil à part ; la barre ne l'attend jamais.
- **Vérifié à l'œil, hors écran** : la barre (`--snapshot`) et les quatre menus (images du test `status_menus_render_offscreen` avec `MACDOCK_DUMP`). Rien n'a été affiché devant toi, et aucun réglage (volume, sortie, Wi-Fi…) n'a été touché.
- **Relecture finale** : 6 problèmes importants, tous corrigés avec un test :
  - un menu ouvert garde ses lignes. Avant, l'interrupteur Wi-Fi pouvait mentir, un clic pouvait viser le mauvais réseau, et le menu se figeait dès qu'une ligne apparaissait ;
  - l'icône du son suit les changements tout de suite ;
  - WMI et DDC/CI ne sont plus interrogés toutes les 2 s ;
  - si Windows refuse une sortie audio, les réglages Son s'ouvrent.
- **À vérifier toi-même** :
  1. le clic sur l'icône du son, puis le curseur et le choix d'une sortie ;
  2. le Centre de contrôle, avec ta vidéo en cours (titre, pause, suivant) ;
  3. sur un portable : le Wi-Fi, la batterie et le Bluetooth (absents ici).

### Plan 9 — Icônes des autres apps et une barre par écran (fusionné dans `main`)

- **Icônes de la zone de notification** dans la barre, à gauche des icônes d'état :
  - le mod Windhawk (version 1.2) écoute ce que les apps envoient à la barre des tâches (`Shell_NotifyIcon`), dessine chaque icône et la transmet à la barre par le pipe `\\.\pipe\MacMenuBar` ;
  - la barre relaie les clics à l'app au format qu'elle attend (version 4 ou ancienne) : clic, double-clic, clic droit pour son menu ;
  - une app fermée sans retirer son icône est oubliée (contrôle toutes les 5 s et au clic) ;
  - sans le mod, rien ne change : pas d'icônes d'apps.
- **Une barre par écran** : la même barre en haut de chaque écran, pleine sur l'écran actif (celui de la fenêtre au premier plan), atténuée ailleurs. Chaque barre a sa zone réservée, sa couleur de texte, son masquage et son plein écran.
- **Vérifié hors écran** : `--snapshot` (capsule, icônes d'état). Le mod est contrôlé avec le compilateur de Windhawk et testé dans `tests.exe` avec un faux Windhawk ; il n'a pas été installé et aucun crochet n'a été posé sur ton poste.
- **Relecture finale** : 1 problème critique, 2 importants, et 3 mineurs que j'ai jugés importants. Tous sont corrigés :
  - brancher ou débrancher un écran faisait planter la barre ;
  - un changement d'écran arrivé pendant la reconstruction des barres la relançait par-dessus elle-même ;
  - en masquage automatique, une barre recréée restait affichée ;
  - trop d'icônes d'apps recouvraient le logo et le nom de l'app ;
  - le double-clic n'arrivait pas aux icônes d'apps ;
  - avec deux écrans l'un au-dessus de l'autre, le bord haut de l'écran du bas faisait apparaître la barre de l'écran du haut.
- **À vérifier toi-même** :
  1. installe le mod 1.2 (Windhawk → ton mod → recoller le code → Compiler) : les icônes de tes apps (Discord, OneDrive…) doivent apparaître dans la barre ;
  2. un clic gauche, un double-clic et un clic droit sur ces icônes ;
  3. tes deux écrans : une barre sur chacun, l'autre atténuée, un menu qui s'ouvre sur la barre cliquée.

### Plan 10 — Effet génie (sous-projet 3, fusionné dans `main`)

- **Réduction** : la fenêtre s'écoule dans sa miniature du Dock, comme sur macOS (le bas se resserre vers la case, puis tout glisse dedans). L'effet **Échelle** de macOS est proposé aussi.
- **Restauration depuis le Dock** : clic sur la miniature ou *Restaurer* : le chemin inverse, puis la fenêtre reprend sa place.
- **Comment** : le Dock pose au-dessus de tout une fenêtre transparente qui laisse passer les clics, et y découpe la fenêtre réduite en 16 à 128 bandes (des miniatures DWM, comme celles du Dock). Pas d'injection, pas de capture d'écran.
- **Choix** : clic droit sur le séparateur → *Effet de réduction* (Génie, Échelle, Windows), ou `minimizeEffect` dans `settings.json`. Maj enfoncée au moment de réduire : ralenti × 8.
- **Animation de Windows** : coupée à la réduction et à l'agrandissement pendant que le Dock tourne (sinon elle se superpose), sans rien écrire dans ton profil. Elle revient à l'arrêt du Dock ; après un plantage, au prochain démarrage du Dock ou à ta prochaine session.
- **Vérifié hors écran** : planches `--genie-snapshot` (Dock en bas, à gauche, à droite ; génie et échelle). Je n'ai réduit aucune de tes fenêtres et je n'ai pas touché au réglage d'animation de Windows (les tests utilisent des fonctions factices).
- **À vérifier toi-même** (après avoir relancé le Dock) :
  1. réduire une fenêtre : elle doit s'écouler dans sa miniature, sans la petite animation de Windows ;
  2. cliquer sur sa miniature : elle ressort et reprend sa place ;
  3. une fenêtre agrandie, puis le Dock à gauche ;
  4. *Effet de réduction* → *Windows* : l'animation d'origine revient.

## Décisions prises sans toi (plan 10)
- L'agrandissement n'est plus animé tant que l'effet Génie ou Échelle est actif : Windows règle les deux par le même interrupteur.
- Seule la restauration depuis le Dock est animée : ailleurs, la fenêtre est déjà affichée quand le Dock l'apprend.
- L'ouverture et la fermeture des fenêtres gardent les animations de Windows 11, déjà proches de macOS.
- Dock masqué : la fenêtre va vers la case où elle serait si le Dock était visible.
- Une seule animation à la fois : une nouvelle réduction termine net la précédente.
- La restauration part de la case affichée (agrandie sous le curseur) ; la réduction, de la case au repos.

## Décisions prises sans toi (plan 9)
- Les icônes d'apps passent par le pipe de la barre ; c'est le mod qui se connecte, et il renvoie tout à chaque reconnexion.
- Une fenêtre de contrôle cachée garde le nom de classe `MacMenuBarWindow` (le lanceur et `--quit` la trouvent) ; les barres ont leur propre classe.
- Les autres barres sont à 60 % d'opacité, sans capsule : seul l'écran du menu ouvert la montre.
- Trop d'icônes d'apps : celles de gauche (les plus récentes) s'effacent d'abord, comme sur macOS où les icônes qui touchent les menus disparaissent.
- Le double-clic envoie d'abord le clic simple, puis le double-clic, comme Windows.
- Le masquage d'une barre recréée n'a pas de test automatique : la barre elle-même n'est pas dans `tests.exe`.
- Laissés en l'état après la relecture :
  - les barres de deux écrans ne se renvoient pas leurs changements de zone réservée en boucle ;
  - la barre n'écoute pas `TaskbarCreated` : Explorer garde ses zones réservées ;
  - quand Explorer redémarre, Windhawk recharge le mod avec lui, et donc son crochet ;
  - une fenêtre d'app recyclée est oubliée en 5 s au plus ;
  - après la mesure du fond, la dernière couleur du texte est gardée ;
  - au-delà de 200 %, les icônes de 32 px sont agrandies ;
  - les icônes rangées sous le chevron de Windows sont montrées comme les autres.

## Mineurs reportés — plan 9
- Au déchargement du mod, le crochet n'attend que les appels déjà comptés.
- Le mod se reconnecte au pipe sans délai croissant.
- La minuterie de mise en page des icônes est relancée à chaque rafale.
- Chaque changement d'icône refait toute la mise en page.
- Les apps à l'ancien format (version 3) ne reçoivent ni `NIN_SELECT` ni `WM_CONTEXTMENU`.
- Après `TaskbarCreated`, la version d'une icône reste inconnue jusqu'à ce que l'app la redonne.
- `TaskbarCreated`, diffusé par le mod, fait refaire au Dock sa zone réservée.
- Le pipe n'est pas restreint à ta session.
- `barWidthPoints` n'est pas utilisé hors des tests.

## Décisions prises sans toi (plan 8)
- Les tuiles Wi-Fi et Bluetooth ne basculent pas d'avance : elles prennent l'état du relevé suivant, une seconde au plus après le clic.
- Les titres des tuiles sont « Concentration » et « Recopie d'écran », et le Centre de contrôle fait 340 pt de large. Avec les titres d'origine, le texte était tronqué.
- Aux flèches du clavier, la recherche et l'horloge sont sautées : elles n'ont pas de menu.
- La structure d'un menu ouvert ne change pas. Une lecture apparue pendant qu'il est ouvert s'affichera à la prochaine ouverture.
- `--snapshot` ne lit ni les radios, ni la lecture en cours, ni la luminosité.

## Mineurs reportés — plan 8
- Le bouton lecture/pause peut clignoter un instant après un clic.
- Au démarrage, la partie droite peut rester incomplète pendant 2 s.
- La luminosité avance par paliers quand on glisse le curseur.
- Un clic sur le réseau déjà connecté relance la connexion.
- Les menus d'état s'ouvrent depuis le bord gauche de l'icône ; la spec demande un alignement à droite.
- Pendant un glisser, le pictogramme du curseur ne suit qu'au rafraîchissement suivant.
- La logique qui saute la recherche et l'horloge n'a pas de test automatique.

## Décisions prises sans toi (plan 7)
- Les apps récentes sont dans `menubar-recent.json` et non dans `menubar.json` : la barre réécrirait sinon ton fichier de réglages à chaque changement d'app.
- Documents récents : seuls les raccourcis dont le nom porte une extension (« rapport.docx ») ; les dossiers récents sont écartés.
- Pendant la lecture d'un menu UIA, la barre attend sans traiter les autres événements, 2,5 s au plus : pas de changement d'état sous le menu qui s'ouvre.
- Un sous-menu UIA trop long à lire reste dans la liste ; le choisir le déplie dans l'app.

## Mineurs reportés — plan 7
- Pas de retour visuel pendant la lecture d'un menu UIA lent (jusqu'à 2,5 s).
- Une app UIA encore en chargement peut garder les menus génériques jusqu'au prochain changement d'app.
- Des titres UIA arrivés pendant qu'un menu est ouvert peuvent décaler ce menu par rapport au titre surligné, un instant.
- Après la préparation d'un menu Win32, `WM_UNINITMENUPOPUP` n'est pas envoyé.
- Les menus Win32 « par position » (`WM_MENUCOMMAND`) ne réagissent pas.
- Un sous-menu UIA en cours de fermeture pourrait être pris pour le suivant (non vérifié).
- Des résultats UIA en attente ne sont pas libérés à l'arrêt.
- Les entrées radio WinUI n'affichent pas leur coche.
- Pendant un dialogue d'une app UIA, la barre revient aux menus génériques.
- Un avertissement de compilation (paramètre inutilisé).

## Décisions prises sans toi (plan 6)
- Processus séparé (`MacMenuBar.exe`), surveillé par le même lanceur : un plantage de l'un n'emporte pas l'autre, et chacun a sa propre capture d'écran pour le verre.
- Logo par défaut : celui de Windows (quatre carrés), car pas de pomme. Remplaçable par `menubar-logo.png`.
- Couleur du texte : foncé au-dessus d'une luminance de 0,45, clair sous 0,35. La barre n'est exclue des captures d'écran que le temps de mesurer le fond : elle apparaît dans tes captures.
- « Fermer la fenêtre » envoie `WM_CLOSE` (jamais Alt+F4, qui ouvrirait la boîte d'arrêt sur le bureau).
- L'Explorateur ne se quitte pas (comme le Finder) ; son menu propose « Vider la Corbeille… » (avec la confirmation de Windows).
- Redémarrer, Éteindre et Fermer la session demandent confirmation ; Suspendre et Verrouiller, non (comme macOS).
- Un Dock qui plante en boucle n'est plus relancé, mais la barre continue.
- Sur les machines en veille moderne (S0), « Suspendre » n'a pas d'effet : c'est une limite de Windows.

## Mineurs reportés — plan 6
- Un nom d'app très long n'est pas tronqué : il peut recouvrir l'horloge.
- Le nom d'utilisateur est relu à chaque changement d'app (lent sur un domaine d'entreprise).
- Après un plantage de la barre, Windows peut garder la bande de 24 pt réservée jusqu'à la relance.
- « Vider la Corbeille » fige la barre le temps de la suppression.
- Une autre barre d'application déjà en haut de l'écran n'est pas prise en compte.

## Ce qui est fait (nuit)

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
