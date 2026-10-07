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
  4. *Effet de réduction* → *Windows* : l'animation d'origine revient ;
  5. une fenêtre ancrée sur une moitié d'écran (Snap) : elle part de sa place ancrée et y revient ;
  6. relancer le Dock avec des fenêtres déjà réduites : aucune animation au démarrage.
- **Relecture finale** : 0 critique, 3 importants, plus 1 mineur que j'ai jugé important. Tous sont corrigés avec un test :
  - au lancement du Dock (ou au redémarrage de l'Explorateur), une fenêtre déjà réduite ne rejoue plus l'animation ;
  - cliquer une deuxième miniature pendant une restauration ne laisse plus la première fenêtre réduite ;
  - une fenêtre ancrée (Snap) part de sa vraie place, pas de celle d'avant l'ancrage ;
  - une fenêtre restaurée ailleurs pendant sa restauration animée n'est plus montrée deux fois.
- **À savoir** : si tu valides « Options de performances » de Windows pendant que le Dock tourne, Windows enregistre l'animation coupée comme ta préférence. Remets-la dans ce même panneau si tu arrêtes le Dock.

### Plan 11 — Feux tricolores (sous-projet 4, fusionné dans `main`)

- **Pastilles** rouge, jaune et verte en haut à gauche de la fenêtre active : fermer, réduire (l'effet génie joue), agrandir ou restaurer. Au survol du groupe : ×, −, +. Grises quand l'action n'existe pas pour la fenêtre.
- **Comment** : la barre de menus pose un petit calque sur la barre de titre, juste au-dessus de la fenêtre, et le déplace avec elle. Un fond de la couleur de la barre de titre (mesurée à l'écran) cache l'icône de Windows sous les pastilles. Les commandes sont celles du menu système : une app qui demande « Enregistrer ? » le demande toujours.
- **Quelles fenêtres** : par défaut, celles dont Windows dessine la barre de titre. Chez Chrome, Edge, l'Explorateur à onglets ou les apps récentes, la barre de titre est à eux : des pastilles cacheraient des onglets. `trafficLights` dans `menubar.json` : `standard`, `all`, `off`.
- **Vérifié hors écran** : planche `--lights-snapshot`, tests de l'éligibilité, de la géométrie (100 % et 200 %), des clics et du rendu. Aucun calque n'a été affiché devant toi.
- **À vérifier toi-même** (après avoir relancé la barre) :
  1. ouvre la Table des caractères (`charmap`) ou `msinfo32` : les pastilles sont dans la barre de titre et suivent la fenêtre ;
  2. survol, puis clic sur chacune ; tirer la fenêtre depuis le fond à côté des pastilles, double-cliquer ce fond ;
  3. une app à barre de menus (Fichier, Édition…) : les menus restent visibles et cliquables ;
  4. Chrome, Edge, Paint ou une fenêtre lancée en administrateur : pas de pastilles ;
  5. une fenêtre agrandie, et ton deuxième écran.

### Plan 12 — Thème macOS (sous-projet 5, fusionné dans `main`)

- **Curseurs façon macOS**, dessinés par le code (aucune ressource Apple) : flèche noire bordée de blanc, attente (anneau gris qui tourne, pas de roue arc-en-ciel), démarrage d'app, quatre flèches de redimensionnement, déplacement, précision, interdit. Chaque fichier contient 32, 48, 64, 96 et 128 px : Windows prend la bonne taille selon l'échelle.
- **Fond d'écran façon Tahoe**, dessiné par le code à la résolution de chaque écran : ondes bleues, turquoise et violettes ; version nuit si Windows est en mode sombre.
- **À la demande seulement** : clic droit sur le séparateur → *Thème macOS* → *Appliquer (curseurs et fond d'écran)* ou *Rétablir le thème Windows* ; ou `MacDock.exe --theme apply|restore`.
- **Sauvegarde** : `%APPDATA%\MacDock\theme-backup.json`, écrite avant le premier changement et jamais écrasée par une deuxième application. Si elle ne peut pas être écrite, rien ne change. Elle est effacée quand tout a été rendu (un écran débranché y reste pour la prochaine fois).
- **Vérifié hors écran** : planche `--theme-snapshot` (curseurs sur fonds clair et sombre, deux fonds d'écran) ; Windows relit bien nos `.cur` et `.ani` (fichiers temporaires) ; application et rétablissement testés avec une API factice. **Je n'ai changé ni tes curseurs ni ton fond d'écran.**
- **À vérifier toi-même** (après avoir relancé le Dock) :
  1. *Thème macOS* → *Appliquer* : les curseurs et le fond changent sur tes deux écrans ;
  2. survole un bord de fenêtre, un lien, lance une app : flèches de redimensionnement et attente ;
  3. *Rétablir le thème Windows* : tes curseurs et ton fond d'avant reviennent ;
  4. après *Appliquer*, change toi-même un curseur (Propriétés de la souris) ou la taille du pointeur, puis *Rétablir* : ton choix doit rester ;
  5. ferme ta session et reviens : les curseurs du thème doivent encore être là.
- **Relecture finale** : 0 critique, 4 importants, plus 1 mineur que j'ai jugé important. Tous sont corrigés avec un test :
  - si tu changes toi-même un curseur ou un fond après l'application, la sauvegarde suit ton choix et le rétablissement ne l'écrase pas ;
  - un fond d'origine supprimé depuis ne bloque plus le rétablissement : notre fond reste sur cet écran, et c'est signalé ;
  - un écran débranché au rétablissement garde son fond d'origine en sauvegarde pour la prochaine fois ;
  - deux applications en même temps (Dock et `--theme`) passent l'une après l'autre, et nos propres fichiers ne sont jamais pris pour l'état d'origine ;
  - l'application se fait hors du fil du Dock : il ne se fige plus (en Debug, cela durait plus de 5 s, assez pour que Windhawk rende la barre des tâches).

### Plan 13 — Écran Apps (sous-projet 6, fusionné dans `main`)

- **Le bouton Apps** du Dock ouvre maintenant l'écran **Apps** de macOS Tahoe au lieu du menu Démarrer : vue plein écran en verre, champ de recherche en haut, toutes tes apps en grille (7 × 5 par page sur un écran 1080p), points de pages en bas.
- **Les apps** viennent du dossier « Apps » de Windows, celui du menu Démarrer : Win32, Store, jeux Steam… Les désinstalleurs, aides, documents et liens web sont écartés (chez toi : 334 entrées, 291 gardées). Le Dock lit la liste au démarrage puis après chaque ouverture, dans un fil à part.
- **Recherche** : en tapant, le début du nom passe en premier, puis le début d'un mot (« co » trouve « Assetto **Co**rsa »), puis le reste ; majuscules et accents ignorés.
- **Icônes** : les mêmes que celles du Dock (même plaque et même forme), chargées en arrière-plan, la page affichée d'abord.
- **Menu Démarrer** : toujours là par un clic droit sur le bouton Apps, et en repli si l'écran Apps ne peut pas s'ouvrir.
- **Vérifié hors écran** : `--apps-snapshot` avec tes vraies apps (sombre, clair avec recherche, deuxième page), tests du tri, de la recherche, de la grille, du clavier et de la lecture du dossier Apps. **Je n'ai pas ouvert l'écran Apps devant toi et je n'ai lancé aucune app.**
- **À vérifier toi-même** (après avoir relancé le Dock) :
  1. clic sur le bouton Apps : l'écran s'ouvre en fondu sur l'écran du Dock, les icônes arrivent ;
  2. tape « calc », Entrée : la Calculatrice se lance et l'écran se ferme ;
  3. molette et flèches : pages suivantes ; Échap, clic dans le vide : fermeture ;
  4. clic droit sur le bouton Apps → *Ouvrir le menu Démarrer* ;
  5. double-clic sur le bouton Apps : l'écran reste ouvert (une seule fois).
- **Relecture finale** : 0 critique, 3 importants, plus 1 mineur que j'ai jugé important. Tous sont corrigés avec un test :
  - un double-clic sur le bouton Apps ne referme plus l'écran aussitôt et n'en ouvre plus un deuxième ;
  - après la molette ou un clic sur un point de page, la sélection suit la page (les flèches et Entrée partent de la page affichée) ;
  - `--apps-snapshot` (comme `--snapshot`, `--genie-snapshot`, `--theme-snapshot`, `--capture-test`, `--theme`) sans valeur ne démarre plus un vrai Dock : erreur dans le journal ;
  - les icônes sont gardées d'une ouverture à l'autre : elles sont là tout de suite à la deuxième ouverture.

### Plan 14 — Spotlight (sous-projet 7, fusionné dans `main`)

Tu m'as écrit « je veux un windows qui ressemble complètement à macos ». J'ai donc ouvert une nouvelle série, en autonomie : 7 Spotlight, 8 Mission Control, 9 sélecteur d'apps façon Cmd+Tab, 10 affichages du volume et de la luminosité, 11 coins actifs.

- **Spotlight** : `Alt+Espace` (ou la loupe de la barre de menus) ouvre un champ en verre au tiers haut de l'écran du curseur. Les résultats apparaissent dessous, dans le même panneau : *Meilleur résultat*, *Applications* (6 au plus), *Documents* (8 au plus).
- **Calculs** : `12*(3+4)` donne `84`, `15%` donne `0,15`, résultat en français (« 1 234,5 ») ; Entrée copie le résultat.
- **Documents** : l'index de recherche de Windows, interrogé en lecture seule, dans ton profil ; fichiers cachés ou système et dossiers techniques (`.git`, `node_modules`, `AppData`) écartés. La recherche part 150 ms après ta dernière frappe et répond en 30 à 250 ms chez toi. L'emplacement s'affiche en court (« Documents › Factures »).
- **Clavier** : flèches, Entrée (lancer ou ouvrir), `Ctrl+Entrée` (montrer dans l'Explorateur), `Ctrl+V`, Échap (efface, puis ferme). Un second `Alt+Espace` ou un clic ailleurs ferme.
- **Réglage** : `spotlightHotkey` dans `settings.json` (`alt+space`, `ctrl+space`, `off`). Si le raccourci est déjà pris (PowerToys Run…), le journal le dit et la loupe reste disponible.
- **Vérifié hors écran** : `--spotlight-snapshot` (vide, « calc », « 12*(3+4) », sombre) avec tes vraies apps et tes documents. **Je n'ai pas ouvert Spotlight devant toi, je n'ai rien lancé et je n'ai pas touché au presse-papiers.**
- **À vérifier toi-même** (après avoir relancé le Dock et la barre) :
  1. `Alt+Espace` : le panneau s'ouvre, le texte tapé y va directement ;
  2. « calc » puis Entrée : la Calculatrice se lance ;
  3. « 12*(3+4) » puis Entrée, puis `Ctrl+V` dans un éditeur : `84` ;
  4. le nom d'un de tes documents : il apparaît sous *Documents* ; `Ctrl+Entrée` le montre dans l'Explorateur ;
  5. la loupe de la barre de menus ouvre et referme Spotlight.
- **Relecture finale** : 0 critique, 1 important, plus 2 mineurs que j'ai jugés importants. Tous sont corrigés avec un test :
  - un clic sur le Dock ferme Spotlight (le Dock n'active jamais de fenêtre, le clic ne comptait donc pas comme « ailleurs ») ; plus aucun menu ni aucune pile ne s'ouvre à l'intérieur d'une autre fenêtre modale ;
  - les documents restent affichés pendant que tu tapes, jusqu'aux nouveaux résultats : le panneau ne saute plus à chaque frappe ;
  - le résultat d'un calcul est copié sans espace entre les milliers (`1234,5`), donc lisible par un tableur.

### Plan 15 — Mission Control (sous-projet 8, fusionné dans `main`)

- **Mission Control** : `Ctrl+Alt+↑` écarte toutes les fenêtres visibles du bureau virtuel courant, sur chaque écran. Elles glissent de leur place réelle vers une place rangée (0,3 s), sans chevauchement, en miniatures vivantes de Windows, sur ton fond d'écran légèrement assombri.
- **Survol** : contour bleu et titre de la fenêtre dessous. **Clic** : la fenêtre revient devant, les autres retournent à leur place. **Échap**, clic dans le vide ou second appui : tout revient sans rien changer. Maj : au ralenti.
- **Raccourci** : `Win+Tab` (la Vue des tâches de Windows) ne peut pas être repris sans crochet clavier, que je m'interdis. J'ai choisi `Ctrl+Alt+↑` par défaut ; `ctrl+up` et `f3` (les touches du Mac) sont au choix dans `settings.json` (`missionControlHotkey`), car beaucoup d'apps s'en servent déjà.
- **Coins actifs** (sous-projet 11) : le Dock écoute déjà le message `MacDockMissionControl`.
- **Vérifié hors écran** : `--mission-snapshot` (3, 8 et 20 fenêtres factices, survol, sombre) et les tests du rangement (dans la zone, aucun chevauchement, proportions gardées, jamais agrandi, ordre de lecture, place pour le titre). **Je n'ai pas ouvert Mission Control devant toi et je n'ai touché à aucune fenêtre.**
- **À vérifier toi-même** (après avoir relancé le Dock) :
  1. ouvre trois ou quatre fenêtres, puis `Ctrl+Alt+↑` : elles s'écartent ;
  2. survole-en une (contour bleu, titre), clique : elle passe devant ;
  3. `Ctrl+Alt+↑` puis Échap : tout revient à sa place ;
  4. avec deux écrans : chaque écran range ses fenêtres ; cliquer sur l'autre écran ne ferme pas tout.
- **Relecture finale** : 0 critique, 2 importants :
  - le fond d'écran était gardé en mémoire en pleine résolution (jusqu'à 133 Mo pour un fond 8K, pour toute la vie du Dock) : il est maintenant mis à la taille de l'écran avant d'être gardé (corrigé avec un test) ;
  - la spec promettait un élément Mission Control dans le Dock : sur macOS il n'y en a pas par défaut, j'ai retiré cette promesse ; les coins actifs serviront d'entrée à la souris.

### Plan 16 — Sélecteur d'apps façon Cmd+Tab (sous-projet 9)

- **Alt+Tab** : une rangée d'icônes en verre au centre de l'écran, les apps ouvertes de la plus récemment utilisée à la plus ancienne, le nom de l'app choisie sous son icône. Le panneau n'apparaît qu'après 0,15 s : un `Alt+Tab` rapide bascule simplement vers l'app précédente.
- **Pendant la sélection** (Alt enfoncé) : Tab, → avancent ; Maj+Tab, ← reculent ; Q ferme l'app choisie (elle quitte la rangée) ; H la masque ; Échap annule ; un clic sur une icône la choisit. Relâcher Alt active l'app : une app masquée revient avec toutes ses fenêtres, sinon ses fenêtres non réduites passent devant (ou la première réduite est restaurée).
- **Raccourci** : `Alt+Tab` est repris par un raccourci global ordinaire (pas de crochet clavier). Si un autre outil le tient déjà, celui de Windows reste et le journal le dit. `appSwitcherHotkey: "off"` dans `settings.json` rend `Alt+Tab` à Windows.
- **Vérifié hors écran** : `--switcher-snapshot` (4 apps en clair, 25 apps en sombre : les icônes rétrécissent pour tenir dans l'écran) et les tests de la session (ordre, pas en boucle, panneau différé, relâchement rapide, Q, choix des fenêtres à activer). **Je n'ai pas appuyé sur Alt+Tab à ta place et je n'ai activé aucune fenêtre.**
- **À vérifier toi-même** (après avoir relancé le Dock) :
  1. ouvre trois apps, passe de l'une à l'autre, puis `Alt+Tab` rapide : tu reviens à l'app précédente ;
  2. garde Alt enfoncé : le panneau apparaît ; Tab plusieurs fois, puis relâche : l'app choisie passe devant ;
  3. pendant la sélection, Q sur une app sans document en cours : elle se ferme et quitte la rangée ;
  4. relâcher Alt seul après une sélection n'ouvre pas le menu de l'app au premier plan.
- **Relecture finale** : 0 critique, 3 importants, tous corrigés :
  - H (masquer) pendant la sélection était défait au relâchement d'Alt : l'app masquée reste masquée (test) ;
  - depuis le bureau, un Alt+Tab rapide sautait l'app la plus récente : il y revient maintenant (test) ;
  - un menu, une pile, Spotlight ou Mission Control ouverts pendant la sélection (clic droit sur le Dock, coin actif) terminent la sélection sans rien activer.

### Plan 17 — HUD du volume et de la luminosité (sous-projet 10)

- **Pastille de Tahoe** : en haut à droite, sous la barre de menus, un petit panneau en verre (« Volume » et le nom de ta sortie audio, ou « Luminosité », pictogramme, jauge). Il s'efface 1,5 s après le dernier changement, en 0,25 s.
- **Touches de volume** : la barre de menus les reprend (raccourcis globaux ordinaires, pas de crochet) et règle elle-même le volume par seizièmes, comme un Mac ; `Maj+Alt` donne des quarts de seizième. Le panneau de volume de Windows ne s'affiche plus. Si un autre outil tient déjà ces touches, Windows les garde et la pastille suit le volume.
- **Luminosité** : Windows prévient la barre à chaque changement (touches d'un portable, curseur des réglages rapides) ; la pastille suit. Les touches de luminosité d'un portable sont traitées par l'ordinateur lui-même : le panneau de Windows apparaît aussi, je ne peux pas l'éviter sans crochet. Un écran externe réglé par DDC/CI ne prévient pas : pas de pastille.
- **Pas de pastille** pendant qu'un menu de la barre est ouvert (son curseur est déjà sous tes yeux), ni juste après un réglage de luminosité fait dans le Centre de contrôle.
- `"hud": false` dans `menubar.json` rend les touches à Windows.
- **Vérifié hors écran** : `--hud-snapshot` (volume 50 % en clair, sourdine en sombre, luminosité 80 %) et les tests (pas du volume sur la grille, fondu, place sous la barre à toutes les échelles, avis de luminosité filtrés, réglage). **Je n'ai touché ni au volume ni à la luminosité, ni enregistré les touches pendant les essais.**
- **À vérifier toi-même** (après avoir relancé la barre de menus) :
  1. volume + et volume − : la pastille apparaît en haut à droite, la jauge avance par seizièmes, le panneau de Windows n'apparaît plus ;
  2. sourdine : haut-parleur barré, jauge vide ; volume + rend le son ;
  3. sur un portable : les touches de luminosité montrent la pastille de luminosité ;
  4. le curseur du volume dans le menu Son ne fait pas apparaître la pastille.
- **Relecture finale** : 1 critique, 3 importants, tous corrigés :
  - avec deux écrans, la capture du fond passait d'un écran à l'autre sans changer de device graphique (risque de plantage de la barre) : elle est refaite (test) ;
  - barre masquée (plein écran, masquage automatique) : la pastille se plaçait une barre trop bas (test) ;
  - la fenêtre de la pastille avalait les clics sur la droite de la barre (icône Son, horloge) : ils passent maintenant ;
  - la pastille de luminosité pouvait surgir seule (sortie de veille, passage secteur/batterie, écran rallumé) : seulement si le niveau change, et pas dans les 2 s qui suivent ces événements (test).

### Plan 18 — Coins actifs (sous-projet 11)

- **Coins actifs** : pousser le pointeur dans un coin lance l'action choisie : Mission Control, bureau, Apps, Centre de notifications, verrouillage, veille de l'écran ou économiseur. Aucun coin n'agit par défaut (un coin actif surprend, l'horloge et le logo de la barre sont tout près) : le réglage est `hotCorners` dans `settings.json`.
- **Comme sur macOS** : l'action part une fois à l'arrivée ; il faut s'éloigner de 24 pixels pour la relancer. Le coin de Mission Control le referme quand il est ouvert.
- **Garde-fous** : rien pendant un glisser (bouton enfoncé), en plein écran, pendant Alt+Tab ou un menu, ni dans un coin collé à un autre écran (le pointeur y glisserait vers l'écran voisin au lieu de s'arrêter).
- Le Dock suivait déjà le pointeur : aucun crochet de plus.
- **Vérifié** : tests des coins (un écran, deux écrans de tailles différentes, zone de 2 px), du suivi (une fois par arrivée, réarmement, blocage) et du réglage. **Je n'ai pas déplacé ton pointeur et n'ai lancé aucune action.**
- **À vérifier toi-même** (après avoir relancé le Dock) :
  1. mets `"hotCorners": {"bottomLeft": "missionControl", "bottomRight": "desktop"}` dans `settings.json` ;
  2. pointeur tout en bas à gauche : Mission Control ; tout en bas à droite : le bureau, puis (après t'en être éloigné) les fenêtres reviennent ;
  3. glisse une fenêtre jusqu'au coin : rien ne se passe.
- **Relecture finale** : 0 critique, 3 importants, tous corrigés :
  - un jeu ou une vidéo en plein écran sur un autre écran que celui du Dock ne bloquait pas le coin : le plein écran est maintenant vérifié sur l'écran du coin ;
  - j'avais mis le bureau en bas à droite par défaut : trop facile à déclencher en visant l'horloge, aucun coin n'est actif par défaut (test) ;
  - la veille de l'écran partait pendant que la main bougeait encore (l'écran se rallumait aussitôt) : elle attend une seconde, l'économiseur aussi (test).

### Plan 19 — Mineurs reportés

J'ai repris les mineurs reportés qui se voient à l'usage (ils sont retirés des listes plus bas) :
- **Spotlight** : `-2^2` vaut -4 comme sur une calculatrice ; Retour arrière efface un émoji entier ; un collage remplace les tabulations par des espaces et ne coupe pas un émoji (tests).
- **Coins actifs** : un coin contre un écran décalé d'un pixel ne compte plus (test) ; l'économiseur sans économiseur réglé le dit dans le journal.
- **Alt+Tab** : sans aucune app ouverte, relâcher Alt n'ouvre plus le menu de l'app au premier plan ; une app fermée juste avant l'affichage ne laisse plus de case vide ; les raccourcis de la sélection déjà pris sont journalisés.
- **Pastille du volume** : la couleur du texte de la barre est relevée de nouveau si la pastille a interrompu un relevé ; `"hud": false` cache toujours la pastille ; une sortie audio débranchée la cache ; plus de minuterie inutile pendant le maintien.
- **Relecture finale** : 0 critique, 3 importants, corrigés : un émoji pouvait encore être coupé à la limite de 128 caractères (collage, frappe) ; un faux coin restait une colonne plus loin avec des écrans décalés ; le fondu de la pastille pouvait être sauté (tests pour les deux premiers).

### Plan 20 — Tes retours

- **Alt+Tab ouvrait toujours le sélecteur de Windows** : Windows refuse de céder Alt+Tab à une autre app (erreur 1409). Le Dock lit maintenant le clavier avec un crochet (le même fil que celui de la souris) et ne garde que Tab avec Alt, puis Échap, flèches, Q et H pendant le sélecteur ; tout le reste passe tel quel (test).
- **Génie pixelisé** : la fenêtre était découpée en bandes de 6 px, d'où les marches sur les bords courbes. Une bande toutes les 2 px maintenant (400 au plus) (tests).
- **Lenteurs** : je t'avais fait lancer la version Debug (non optimisée). La version Release est compilée dans `build\Release`.
- **La barre des tâches sous le Dock** : c'est le mod Windhawk de MacDock qui la cache, il n'est pas encore installé (étape 2 du README).

## Mineurs reportés — plan 19
- Le relevé de la couleur du texte relancé après la pastille peut se perdre si une touche de volume ou un menu arrive dans les 0,8 s.
- Casque débranché, haut-parleurs qui prennent le relais : la pastille garde le nom du casque jusqu'au fondu.
- L'économiseur « (Aucun) » n'est peut-être pas détecté (Windows garde l'économiseur « actif »).

## Décisions prises sans toi (plan 18)
- Aucun coin actif par défaut ; à régler dans `settings.json`.
- Un coin collé à un autre écran ne compte pas (le pointeur y passe à l'écran voisin).
- Pas de « Note rapide » (pas d'équivalent sous Windows).

## Mineurs reportés — plan 18
- L'état des boutons est lu au traitement du mouvement : une fenêtre lâchée dans le coin pendant un rendu peut déclencher l'action.
- Liste des écrans gardée jusqu'au prochain changement d'affichage signalé.
- Apps lancé depuis le coin d'un autre écran s'ouvre sur l'écran du Dock.
- Un menu ouvert dans la barre de menus ne bloque pas les coins.
- Pousser le Dock vers un autre écran près d'un coin peut aussi lancer l'action.

## Décisions prises sans toi (plan 17)
- La barre de menus reprend les touches de volume (le panneau de Windows ne s'affiche plus pour elles) ; `"hud": false` les rend à Windows.
- Les touches de luminosité d'un portable restent à Windows (traitées par l'ordinateur) : son panneau et la pastille s'affichent tous les deux.
- Un clic sur la pastille va à la barre dessous ; ailleurs, il est perdu pendant l'affichage (1,75 s).

## Mineurs reportés — plan 17
- Juste après un réglage de luminosité très lent (WMI), la pastille peut apparaître à la fermeture du Centre de contrôle.
- Maj, Ctrl ou Win avec une touche de volume : panneau de Windows, pas de pastille.

## Décisions prises sans toi (plan 16)
- `Alt+Tab` remplace celui de Windows (réglable : `appSwitcherHotkey: "off"`).
- Le panneau s'affiche sur l'écran du curseur.
- Pendant la sélection, Alt+Espace n'ouvre pas Spotlight.
- Les apps dont les fenêtres sont sur un autre bureau virtuel restent dans la rangée (les choisir change de bureau, comme les Spaces de macOS).

## Mineurs reportés — plan 16
- La spec du sélecteur décrit encore un découpage de fichiers et des mesures que le plan a remplacés.


## Décisions prises sans toi (plan 15)
- Raccourci `Ctrl+Alt+↑` par défaut (`Win+Tab` impossible sans crochet clavier) ; si ton pilote Intel fait pivoter l'écran avec ce raccourci, prends `ctrl+up` ou `f3`.
- Pas de barre des bureaux virtuels (Windows ne donne pas la liste des bureaux par une API publique) : seules les fenêtres du bureau courant.
- La barre de menus se cache pendant Mission Control, comme sur macOS.
- Le fond d'écran est toujours affiché en « remplir », quel que soit ton réglage d'ajustement.

## Mineurs reportés — plan 15
- Avec deux écrans : deux contours bleus possibles, et Échap peut se perdre après un clic du bouton du milieu sur l'autre écran.
- Un clic pendant l'animation d'ouverture (0,3 s) vise la place finale ; un clic sur la place d'une fenêtre fermée referme la vue, et le rangement n'est pas refait.
- Les bordures invisibles de Windows 11 comptent dans les miniatures (contour bleu à quelques pixels du bord visible).
- Avec le masquage automatique, le Dock se montre brièvement après Mission Control.
- Un fond en couleur unie est remplacé par le fond Tahoe.

## Décisions prises sans toi (plan 14)
- La recherche de documents passe par l'index de Windows (OLE DB, lecture seule) : le dossier `search-ms:` prévu ne s'énumère pas hors de l'Explorateur.
- Syntaxe de recherche de l'Explorateur acceptée (`kind:`, `-mot`…), comme dans sa barre de recherche.
- `Alt+Espace` est pris pour tout le système (le menu système d'une fenêtre au clavier n'est plus accessible) ; `ctrl+space` ou `off` dans les réglages.
- `50+10%` vaut 50,1 (« % » divise par 100).
- Un clic dans les 14 pt d'ombre autour du panneau le ferme sans atteindre la fenêtre dessous.

## Mineurs reportés — plan 14
- Une recherche lente d'une ouverture précédente peut encore tourner (son résultat est jeté) ; une fuite rare d'un résultat à la fermeture.
- Si Windows refuse de donner le clavier au panneau, rien ne le signale.
- Un raccourci déjà pris n'est pas réessayé sans relancer le Dock.
- La spec décrit encore l'ancienne recherche ; `searchMsUrl` ne sert plus.
- Le test de recherche réelle peut échouer si l'index garde un fichier tout juste supprimé ; `FileSearcher` n'a pas de test.

## Décisions prises sans toi (plan 13)
- Pas de réorganisation à la main ni de dossiers : l'ordre est alphabétique, comme la vue Apps sans dossiers.
- Les liens web du dossier Apps sont écartés ; les liens `steam://` et autres jeux restent.
- Le texte de l'écran Apps est toujours blanc sur un verre sombre, en mode clair comme en mode sombre (comme Launchpad).
- Pas de glisser horizontal à la souris pour changer de page : molette, flèches et points.

## Mineurs reportés — plan 13
- Une icône personnalisée (`icons\<id>.png`) n'apparaît pas dans l'écran Apps (autre clé que celle du Dock).
- Alt+F4 ferme l'écran Apps par le chemin du système (quelques icônes en route perdues).
- Un changement d'écran ou d'échelle pendant que l'écran Apps est ouvert n'est pas suivi.
- Le verre de tout l'écran est recalculé à chaque clignotement du curseur et à chaque survol.
- Toute la case compte pour le clic (sur un grand écran, cliquer entre deux icônes lance une app).
- Pavé tactile : défilement page par page toutes les 0,25 s, glissement horizontal ignoré.
- Recherche : « oe » ne trouve pas « Œ », l'apostrophe droite ne trouve pas l'apostrophe typographique ; un emoji effacé laisse une demi-paire.

## Décisions prises sans toi (plan 12)
- Le thème couvre les curseurs et le fond d'écran seulement : polices, coins et ombres des fenêtres ne se règlent pas proprement sans crochet.
- Le fond suit le mode **système** de Windows (`SystemUsesLightTheme`, comme le Dock et la barre), pas celui des apps.
- `--theme apply|restore` partage le nom de l'option `--theme light|dark` de `--snapshot` : seules les valeurs `apply` et `restore` déclenchent le thème.
- Un échec à l'application ou au rétablissement s'affiche dans une boîte de message (tu as cliqué) et dans le journal.
- Un curseur qui ne peut pas être écrit n'arrête pas l'application : le reste est appliqué et l'échec signalé (la sauvegarde est déjà faite, *Rétablir* répare).
- Un écran branché après l'application garde notre fond au rétablissement ; son fond d'origine entre dans la sauvegarde à la prochaine application.

## Mineurs reportés — plan 12
- Si Windows ne donne aucun écran, seuls les curseurs changent, sans message.
- `--theme` avec une valeur inconnue, ou `--theme-snapshot` vers un dossier inexistant : pas de message d'erreur clair.
- Le type de la valeur du registre (`REG_SZ` ou `REG_EXPAND_SZ`) n'est pas conservé : tout est rendu en `REG_EXPAND_SZ`.
- Un fond en diaporama ou en couleur unie n'est pas rétabli tel quel.
- Pas de test d'un `.ani` à cinq tailles relu par Windows (seulement à une taille).

## Décisions prises sans toi (plan 11)
- Pastilles seulement sur la fenêtre active (macOS les montre grises sur les autres) : un calque par fenêtre visible demanderait de suivre l'ordre de toutes les fenêtres.
- Les boutons de Windows restent à droite.
- La couleur du fond est mesurée juste à droite des pastilles, à 4 pt du haut du cadre.
- Aucun test automatique du calque lui-même : il faudrait l'afficher devant toi.
- Les fenêtres lancées en administrateur n'ont pas de pastilles : Windows refuserait leurs commandes.
- Déplacer la fenêtre depuis le fond des pastilles passe par notre propre déplacement (sans l'ancrage Snap de Windows) ; une fenêtre agrandie ne se déplace pas depuis là.
- **Relecture finale** : 2 critiques et 3 importants, plus 1 mineur jugé important, tous corrigés : les barres de menus classiques ne sont plus recouvertes, les fenêtres en administrateur sont exclues, l'échelle suit le DPI réel de l'écran, la couleur du fond est remesurée après l'apparition de la fenêtre, le déplacement depuis le fond ne dépend plus d'un message relayé, et un double-clic sur une pastille n'envoie plus deux commandes.

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
