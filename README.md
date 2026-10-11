# MacDock

Un Dock et une barre de menus façon **macOS 26 Tahoe** pour Windows 11.

- **Dock** : magnification, rebonds, infobulles, apps épinglées et ouvertes, fenêtres réduites (avec l'effet génie), Téléchargements et Corbeille. Un mod **Windhawk** cache la barre des tâches Windows tant que le Dock tourne.
- **Barre de menus** (`MacMenuBar.exe`) : transparente en haut de chaque écran, avec le menu du système, le nom de l'app active, ses menus, les icônes d'état et des autres apps, et la date et l'heure.
- **Réglages MacDock** (`MacDockSettings.exe`) : une app façon Réglages Système de macOS pour tout régler, appliqué en direct. On l'ouvre par  > « Réglages MacDock… », par le clic droit sur le Dock (« Réglages du Dock… ») ou sur la barre (« Réglages de la barre des menus… »).
- **Coup d'œil** : dans l'Explorateur ou sur le bureau, **Espace** sur un fichier sélectionné l'affiche dans une fenêtre flottante, qui s'ouvre en zoom depuis son icône.
  - **Ce qu'elle montre** :
    - les photos ;
    - les vrais aperçus des documents : PDF, Word, Excel, PowerPoint, pages web, polices (ceux de Windows et d'Office) ;
    - les vidéos et les sons, qui se lisent tout de suite (un clic met en pause) ;
    - le texte ;
    - sinon, une grande icône avec le type, la taille et la date.
  - Le bouton à deux flèches passe en plein écran.
  - Espace ou Échap ferme, Entrée ouvre, et l'aperçu suit la sélection.
- **Captures d'écran** : `⊞⇧3` pour tout l'écran, `⊞⇧4` pour une zone ou une fenêtre, comme `⌘⇧3` et `⌘⇧4`. Le fichier va sur le Bureau et une vignette flottante apparaît en bas à droite.
- **Fenêtres des autres apps** : feux tricolores à la place des boutons de Windows, sur toutes les fenêtres ; coins arrondis, barre de titre grise ; agrandir remplit l'écran entre la barre et le Dock (le refaire rend la taille d'avant) et la pastille verte passe en vrai plein écran, comme sur macOS 26 ; avec le mod `macdock-look`, police SF Pro partout, boîtes de dialogue comprises.

> État : le Dock est complet (plans 1 à 5), la barre de menus aussi : menus du système, de l'app et génériques, horloge (plan 6), vrais menus des apps et Éléments récents (plan 7), icônes d'état et Centre de contrôle (plan 8), icônes des autres apps et une barre par écran (plan 9). Les fenêtres se réduisent dans le Dock avec l'effet génie (plan 10) et ont des feux tricolores (plan 11). Voir `docs/superpowers/` et `docs/journal-de-nuit.md`.

## Installation

1. **Télécharger et lancer l'installateur** `MacDock-Setup-X.Y.Z.exe`, depuis la [dernière version publiée](https://github.com/titilyonnais/MacDock/releases/latest) :
   - installation pour ton compte, sans droits d'administrateur, dans `%LOCALAPPDATA%\Programs\MacDock` ;
   - MacDock démarre ensuite avec Windows (case décochable, réglable aussi dans l'app Réglages) ;
   - l'installateur n'est pas signé : si Windows SmartScreen affiche « Windows a protégé votre ordinateur », clique sur *Informations complémentaires*, puis *Exécuter quand même*. Le *Contrôle intelligent des applications* de Windows 11, s'il est activé, bloque les programmes non signés sans proposer de passer outre : il faut alors le désactiver (Sécurité Windows > Contrôle des applications et du navigateur) ;
   - **mises à jour automatiques** (depuis la 0.53.0) : MacDock cherche les nouvelles versions sur GitHub une minute après le démarrage, puis deux fois par jour. Une version trouvée est téléchargée et vérifiée (signature de l'auteur, puis empreinte) ; une notification propose alors de redémarrer MacDock pour l'installer, sinon elle s'installe au prochain démarrage. Réglages MacDock > À propos : *Rechercher*, *Installer*, *Rechercher automatiquement*. Une version mal signée n'est jamais installée. Seule la copie posée par l'installateur se met à jour (une copie compilée à la main, jamais). Pour passer de la 0.52.0 à la 0.53.0, lance une fois l'installateur à la main ;
   - pour désinstaller : Paramètres > Applications > MacDock ; tes réglages (`%APPDATA%\MacDock`) sont gardés.

2. **Installer le mod Windhawk** (facultatif, pour cacher la barre des tâches) :
   - Windhawk → *Créer un nouveau mod*.
   - Remplacer tout le code par le contenu de `windhawk\macdock-hide-taskbar.wh.cpp`.
   - *Compiler le mod*, puis *Quitter l'éditeur* et vérifier qu'il est activé.

   Sans Dock lancé, le mod ne cache rien. Depuis sa version 1.2, il transmet aussi à la barre de menus les icônes de la zone de notification (Discord, OneDrive, antivirus…) : si tu avais une version plus ancienne, recolle le code et recompile.

   **Coins de macOS** (facultatif) : installe dans Windhawk le mod public *Custom Window Corner Radius* (onglet *Explorer*), puis double-clic sur `windhawk\coins-macos.cmd` : il l'active avec le rayon de macOS (12 pt au lieu de 8, fenêtres ancrées arrondies aussi), ajoute `dwm.exe` à la liste d'inclusion de Windhawk si besoin et redémarre Windhawk. Windows demande l'autorisation administrateur. `coins-windows.cmd` rend les coins de Windows.

   **Police de macOS dans toutes les apps** (facultatif) : double-clic sur `windhawk\installer-macdock-look.cmd`, dans le dossier de MacDock (`%LOCALAPPDATA%\Programs\MacDock\windhawk` une fois installé), ou bouton *Installer…* dans Réglages MacDock > Mods. Windows demande l'autorisation administrateur, puis le mod est compilé et activé exactement comme par l'éditeur de Windhawk. `retirer-macdock-look.cmd` le retire. On peut aussi faire la même procédure à la main avec `windhawk\macdock-look.wh.cpp`. Il faut que SF Pro (Text et Display) soit installée ; sinon le mod ne change rien. Les polices de l'interface de Windows (Segoe UI, Segoe UI Variable, MS Shell Dlg) deviennent SF Pro Text, et SF Pro Display à partir de 20 pt, dans les apps classiques comme modernes (Explorateur, Bloc-notes, navigateurs, Electron). Les polices d'icônes ne sont jamais touchées. Les jeux, Office et les lecteurs PDF sont exclus (les documents gardent leurs polices). **Anti-triche** : une exclusion du mod n'écarte que le mod, Windhawk lui-même reste chargé. Ajoute chaque jeu en ligne à la liste globale de Windhawk (*Paramètres > Avancé > Process exclusion list*) : injecter du code dans un jeu protégé peut valoir un bannissement. Les apps déjà ouvertes changent de police à leur prochain lancement. Les boîtes de dialogue (Exécuter, Ouvrir, Enregistrer sous) gardent SF Pro depuis la version 1.3.0 ; après une mise à jour du mod, relance l'installateur.

### Compiler soi-même

1. **Compiler** (Visual Studio avec les outils C++) :
   ```powershell
   ./build.ps1 -Target all -Config Release
   ```
   Les exécutables sont dans `build\Release\` (`-OutDir` pour un autre dossier).
2. **Lancer le Dock et la barre de menus** : double-cliquer sur `build\Release\MacDockLauncher.exe`. Le lanceur démarre les deux et relance celui qui plante. *Quitter MacDock* ferme aussi la barre de menus.
3. **Démarrage automatique** (facultatif) : `build\Release\MacDockLauncher.exe --install` ; pour le retirer : `--uninstall`. `MacDockLauncher.exe --quit` arrête le Dock et la barre sans rien relancer (l'installateur s'en sert).
4. **Installateur** : `./tools/make-installer.ps1` (Inno Setup 6.7.3 : `winget install JRSoftware.InnoSetup --version 6.7.3`) écrit `build\installer\MacDock-Setup-X.Y.Z.exe` et son empreinte. Une étiquette `vX.Y.Z` envoyée sur GitHub le construit et le publie (`.github/workflows/release.yml`) ; la version vient de `src\core\version_defs.h`.
5. **Signature d'une version** : une fois l'installateur publié par GitHub, `./tools/sign-release.ps1 -Tag vX.Y.Z` vérifie d'où il vient (l'étiquette vise le même commit sur GitHub qu'ici, les fichiers ont été envoyés par le run de `release.yml`, aucun autre run récent ne vient d'un commit inconnu ici, une version vient de `main`), le télécharge, revérifie son empreinte et que `SHA256SUMS.txt` ne contient que sa ligne, signe ce fichier avec la clé « MacDock Release » de ce PC (magasin de clés de Windows, non exportable) et joint `SHA256SUMS.txt.sig`. `-DryRun` fait tous les contrôles sans rien signer. Sans cette signature, les mises à jour automatiques ignorent la version. `-CreateKey` crée la clé une seule fois et écrit sa clé publique dans `src\update\release_key.h`.

## Utilisation

- **Clic** sur une app fermée : elle se lance en rebondissant. Sur une app ouverte : elle passe au premier plan.
- **Rebond d'attention** : une app qui réclame ton attention (fenêtre qui clignote) fait rebondir son icône une fois, comme une demande « informative » sur macOS : un seul train de rebonds, même si elle continue de clignoter ; elle rebondit de nouveau si elle clignote encore après 10 s de calme, ou après être passée au premier plan. Le rebond s'arrête quand elle passe au premier plan ou que tu cliques son icône, et une app déjà au premier plan ne rebondit pas.
- **Apps lancées en administrateur** (Gestionnaire des tâches, Windhawk…) : quand l'une d'elles est au premier plan, Windows cache la souris et le clavier à MacDock. Le Dock suit quand même le curseur (il relit sa position), mais les raccourcis de MacDock (⌥⇥, ⊞⇧3/4/5, ⌥`, la touche ⌘) restent à Windows jusqu'à ce qu'une autre app passe devant.
- **VMware** (et les apps qui « prennent » la souris) : quand il a le focus, il peut garder le curseur de Windows posé là où il était, par exemple sur le Dock après un clic sur son icône ; le Dock peut alors rester agrandi tant que le curseur n'en est pas vraiment sorti. Si l'app confine le pointeur ailleurs (ClipCursor), le Dock le sait et retombe au repos ; les autres cas sont à l'étude (traces du mode diagnostic).
- **Bouton Apps** : ouvre l'écran **Apps**, comme sur macOS Tahoe : toutes les apps du menu Démarrer (Win32 et Store) en grille sur un fond de verre, triées par nom, page par page.
  - Tape pour chercher (majuscules et accents ignorés) ; Entrée lance le premier résultat ; flèches, Page précédente / suivante, Début, Fin et molette pour se déplacer ; Échap efface la recherche, puis ferme.
  - Un clic sur une app la lance ; un clic dans le vide, un clic droit ou Échap ferment sans rien lancer.
  - Clic droit sur le bouton Apps → *Ouvrir le menu Démarrer* pour le menu de Windows (il s'ouvre aussi si l'écran Apps ne peut pas s'afficher).
- **Spotlight** : `Alt+Espace` (réglable) ou la loupe de la barre de menus ouvre un champ de recherche en verre au tiers haut de l'écran du curseur, comme sur macOS. En tapant, les résultats apparaissent dessous : *Meilleur résultat*, *Applications*, *Réglages*, *Documents* (recherche de Windows dans ton profil, sans les fichiers cachés ni système).
  - Un calcul (`12*(3+4)`, `15%`, `2^10`, virgule ou point) s'affiche en meilleur résultat ; Entrée copie le résultat dans le presse-papiers.
  - Une conversion aussi : `10 km` (6,21371 mi), `100 °F en °C`, `500 Go` (465,661 Gio), `3 pieds en mètres`, `90 min` (1,5 h). Longueurs, masses, températures, volumes, surfaces, vitesses, durées, données, énergies et pressions ; symboles (`km`, `lb`, `Go`…) ou noms (`kilomètres`, `pouces`…). Sans unité d'arrivée, celle de l'autre système (milles pour des kilomètres, gibioctets pour des gigaoctets). Entrée copie le nombre.
  - *Réglages* : les pages des Paramètres de Windows (`wifi`, `bluetooth`, `fond d'écran`, `mot de passe`, `mises à jour`…) et les sections de Réglages MacDock (`dock`, `clavier`, `génie`…), avec leur tuile. Entrée ouvre la page, ou la section dans l'app Réglages MacDock. Un réglage qui porte exactement le nom tapé (`écran`, `son`) passe en meilleur résultat.
  - Flèches pour choisir, Entrée pour lancer l'app ou ouvrir le document, `Ctrl+Entrée` pour montrer le document dans l'Explorateur ; `Ctrl+V` colle.
  - Échap efface, puis ferme ; un clic ailleurs ou un second `Alt+Espace` ferme aussi.
  - Si un autre programme utilise déjà `Alt+Espace` (PowerToys Run…), c'est écrit dans le journal : choisis `ctrl+space` dans `settings.json`, ou passe par la loupe.
- **Mission Control** : `Ctrl+Alt+↑` (réglable) écarte toutes les fenêtres ouvertes du bureau courant, sur chaque écran : elles se rangent sans se chevaucher sur ton fond d'écran, en miniatures vivantes. Survole une fenêtre pour voir son titre (contour bleu), clique pour la ramener devant ; Échap, un clic dans le vide ou un second `Ctrl+Alt+↑` referment. Maj enfoncée : au ralenti. Les fenêtres réduites restent dans le Dock, comme sur macOS. Si `Ctrl+Alt+↑` fait pivoter ton écran (raccourcis d'anciens pilotes Intel), choisis `ctrl+up` ou `f3` dans `settings.json`.
- **Exposé d'une app** : `Ctrl+Alt+↓` (réglable), ou clic droit sur une app du Dock → *Afficher toutes les fenêtres*. Seules les fenêtres de cette app s'écartent, comme dans Mission Control.
- **Forcer à quitter** : `Alt+⊞+Échap` (⌥⌘⎋ sur un clavier de Mac branché au PC, réglable), ou menu  > *Forcer à quitter…*, ouvre la fenêtre de macOS 26 « Forcer des applications à quitter » : tes apps ouvertes avec leur icône, « (ne répond pas) » en rouge pour une app figée, l'app au premier plan déjà choisie. *Forcer à quitter* (ou Entrée) demande confirmation, puis arrête l'app ; pour l'Explorateur, le Finder de Windows, le bouton devient *Relancer*. Une app lancée en administrateur ne peut pas être arrêtée : la fenêtre le dit. ↑ ↓ pour choisir, Échap pour fermer.
  - Ses fenêtres réduites s'alignent en bas de l'écran, sous un trait ; un clic sur l'une d'elles la restaure.
  - Échap ou un clic dans le vide referme.
- **Sélecteur d'apps** : `Alt+Tab` montre, au milieu de l'écran, une rangée des apps ouvertes de la plus récemment utilisée à la plus ancienne, comme `Cmd+Tab`. Garde Alt enfoncé et appuie encore sur Tab (ou ←, →) pour avancer, Maj+Tab pour reculer ; relâche Alt pour passer à l'app choisie (toutes ses fenêtres non réduites passent devant). Un `Alt+Tab` rapide revient simplement à l'app précédente, sans rien afficher. Pendant la sélection : Q ferme l'app choisie, H la masque, Échap annule, un clic sur une icône la choisit ; la touche sous Échap passe aussi à la case de gauche, comme `` ⌘` `` pendant ⌘⇥. Si `Alt+Tab` est déjà pris par un autre outil, celui de Windows reste en place.
- **Fenêtre suivante de l'app** : `Alt` + la touche sous Échap (`` ` `` en QWERTY, ² en AZERTY) fait passer devant la fenêtre suivante de l'app active, comme `` ⌘` `` sur macOS ; avec Maj, la précédente. Des appuis successifs passent par toutes ses fenêtres ouvertes, dans l'ordre d'ouverture ; les fenêtres réduites sont sautées. Réglages > Clavier > « Fenêtre suivante de l'app », ou `"windowCycleHotkey": "off"` dans `settings.json`, rend la touche à Windows (pendant ⌥⇥, elle reste prise pour passer d'une case à l'autre). Limites : la touche est prise même dans une app qui s'en sert (Alt+`` ` `` des IDE JetBrains, bascule japonais / anglais d'un IME sur un clavier US) : choisis alors « Désactivée » ; elle ne marche pas au-dessus d'une app lancée en administrateur (Windows ne la montre pas à MacDock).
- **Captures d'écran**, comme sur un Mac :
  - `⊞⇧3` (Windows + Maj + 3) capture tout l'écran. Avec plusieurs écrans, chacun a son fichier.
  - `⊞⇧4` ouvre le viseur : une croix avec ses coordonnées. Tire une zone à la souris ; pendant le tirer, la largeur et la hauteur s'affichent.
    - **Espace** passe en mode fenêtre : la fenêtre survolée prend un voile bleu, et un clic la capture seule, même recouverte, avec ses coins arrondis et l'ombre de macOS sur fond transparent. Alt enfoncé au clic : sans ombre.
    - Échap ou un clic droit annule.
  - Le fichier « Capture d’écran AAAA-MM-JJ à HH.MM.SS.png » va sur ton Bureau. Plusieurs captures dans la même seconde, ou plusieurs écrans : « … (2).png », jamais d'écrasement.
  - Le Dock, les feux tricolores et les menus ouverts (barre, Dock, Centre de contrôle, Centre de notifications) apparaissent sur la capture. Spotlight, l'écran Apps et la pastille du volume aussi.
  - Avec l'**Outil Capture de Windows** (`⊞⇧S` ou Impr. écran), MacDock se montre aussi, le temps que l'outil est ouvert (2 min au plus) : Dock, piles, sélecteur d'apps, menus, feux tricolores. Sauf `⊞Impr. écran` et `Alt+Impr. écran`, qui copient l'écran sans attendre, et sous une app lancée en administrateur (MacDock ne voit pas la touche).
  - **La vignette** glisse en bas à droite et reste 5 secondes ; le survol la retient.
    - Un clic ouvre la capture.
    - Un glisser vers la droite la renvoie.
    - Un glisser ailleurs dépose le fichier dans une autre app (Explorateur, message, document).
  - Ajoute Ctrl (`⊞⌃⇧3`, `⊞⌃⇧4`) pour copier l'image dans le presse-papiers, sans fichier.
  - `⊞⇧5` ouvre la barre de capture, comme `⌘⇧5`.
    - **Modes** : capturer tout l'écran, une fenêtre ou une zone ; enregistrer tout l'écran ou une zone. Puis « Capturer » ou « Enregistrer ».
    - **Pendant l'enregistrement**, une pastille ⏹ affiche la durée en haut de l'écran ; un clic dessus, ou `⊞⇧5`, l'arrête.
    - **La vidéo** « Enregistrement de l’écran AAAA-MM-JJ à HH.MM.SS.mp4 » (H.264, 30 images par seconde, au plus 1920 px de large, sans son) va sur le Bureau, avec sa vignette.
    - Le Dock n'apparaît pas dans les vidéos.
  - `"screenshots": false` dans `settings.json` rend ces raccourcis à Windows.
- **Touche ⌘** (option, désactivée par défaut) : avec `"altAsCommand": true` dans `settings.json`, la touche Alt de gauche, sous le pouce, joue le rôle de ⌘.
  - **Raccourcis** :
    - ⌘C, ⌘V, ⌘X, ⌘Z (⌘⇧Z), ⌘A, ⌘S, ⌘W, ⌘T, ⌘N, ⌘F, ⌘P, ⌘O, ⌘R, ⌘Y et ⌘, deviennent Ctrl+… ;
    - ⌘Q ferme l'app (Alt+F4 : elle demande d'enregistrer) ;
    - ⌘← et ⌘→ vont au début et à la fin de la ligne, ⌘↑ et ⌘↓ au début et à la fin du document (Maj sélectionne).
  - **Dans l'Explorateur**, comme dans le Finder : ⌘↑ remonte au dossier parent, ⌘↓ ouvre, ⌘⌫ met à la Corbeille.
  - **Ce qui ne change pas** :
    - Alt Gr n'est jamais touché (@, #, { sur un clavier français) ;
    - avec toute autre touche, Alt garde son rôle (Alt+Tab, Alt+F4, Alt+Entrée dans un jeu) ;
    - Alt seul n'ouvre plus le menu de l'app, comme ⌘ sur Mac.
- **Sons système**, tous originaux (synthétisés par MacDock, aucun son d'Apple) :
  - un déclic d'appareil photo pour les captures ;
  - un froissement quand la Corbeille est vidée depuis le Dock ;
  - un souffle pour le nuage « poof » ;
  - un « pop » bref quand tu changes le volume au clavier.
  - `"sounds": false` (`settings.json`) et `"volumeFeedback": false` (`menubar.json`) les coupent.
- **Coins actifs** : pousse le pointeur dans un coin de l'écran pour lancer une action, comme sur macOS. Aucun coin n'agit par défaut : choisis-les dans `settings.json` (`hotCorners`, par exemple `"hotCorners": {"bottomLeft": "missionControl"}`) parmi Mission Control, bureau (un second passage rétablit les fenêtres), Apps, Centre de notifications, verrouillage, veille de l'écran, économiseur. La veille de l'écran et l'économiseur attendent une seconde, le temps que ta main s'arrête. L'action part une fois à l'arrivée dans le coin ; il faut s'en éloigner un peu pour la relancer. Rien ne se passe pendant un glisser, quand l'écran du coin est en plein écran (jeu, vidéo, présentation), ou dans un coin collé à un autre écran.
- **Clic sur une pile** (Téléchargements…) : son contenu s'ouvre comme sur macOS, en **éventail** (icônes en arc au-dessus de la pile, nom à gauche) jusqu'à 9 éléments, en **grille** de verre au-delà (molette pour défiler), ou en **liste** (menu en verre, sous-dossiers en sous-menus) si tu la choisis. Un clic ouvre l'élément ; *Ouvrir dans l'Explorateur* ouvre le dossier ; Échap ou un clic à côté referme. Dans le Dock, l'icône d'une pile montre ses derniers fichiers empilés (ou l'icône du dossier, au choix), et se met à jour en direct.
- **Clic droit** : menus en verre, comme sur macOS.
  - *App* : ses fenêtres ouvertes, *Options* (Garder dans le Dock, Ouvrir à la connexion — aussi pour les apps du Store, par un raccourci dans le dossier Démarrage —, Afficher dans l'Explorateur), Afficher toutes les fenêtres, Masquer, Quitter.
  - *Séparateur ou zone vide* : masquage automatique, agrandissement, position à l'écran (Gauche, En bas, Droite), effet de réduction, thème macOS, Réglages du Dock.
  - *Pile* : Trier par (Nom, Date d'ajout, Date de modification, Type), Afficher comme (Pile, Dossier), Présenter le contenu comme (Éventail, Grille, Liste, Automatiquement), Ouvrir dans l'Explorateur, Retirer du Dock.
  - *Corbeille* : Ouvrir, Vider la Corbeille. *Fenêtre réduite* : Restaurer, Fermer.
- **Position** : en bas, à gauche ou à droite de l'écran (clic droit sur le séparateur), changée à chaud.
- **Plusieurs écrans** : pousse le curseur contre le bord du Dock sur un autre écran (un court instant) et le Dock y passe ; il s'en souvient au prochain démarrage et revient sur l'écran principal si celui-ci est débranché.
- **Glisser une icône** : la déposer ailleurs dans le Dock la déplace ; la tirer vers le haut (« Supprimer ») puis la lâcher la retire, avec le nuage « poof ». Une app ouverte n'est que désépinglée. Échap annule.
- **Déposer des fichiers** depuis l'Explorateur :
  - un `.exe`, `.lnk` ou `.appref-ms` **entre deux icônes** : il s'épingle à cet endroit ;
  - des fichiers **sur une app** : ils s'ouvrent avec elle ;
  - **sur la Corbeille** : ils y partent (annulable) ; **sur une pile** : ils y sont déplacés.
- **Masquage automatique** (clic droit sur le séparateur) : le Dock glisse sous le bord de l'écran et revient quand le curseur touche ce bord. En plein écran (jeu, vidéo, F11), il s'efface toujours.
- **Fenêtres réduites** : miniature en direct dans le Dock, avec la petite icône de l'app dans le coin.
- **Effet génie** : une fenêtre réduite (bouton, `Win+↓`…) s'écoule dans sa miniature du Dock, comme sur macOS ; un clic sur la miniature (ou *Restaurer*) la fait ressortir à sa place. Maj enfoncée : au ralenti. Clic droit sur le séparateur → *Effet de réduction* : Génie, Échelle, ou Windows (l'animation d'origine).
  - Pour éviter deux animations l'une sur l'autre, le Dock coupe celle de Windows à la réduction et à l'agrandissement tant qu'il tourne (rien n'est écrit dans ton profil ; elle revient à l'arrêt du Dock). Avec *Windows*, rien n'est coupé.
  - Une fenêtre restaurée ailleurs que depuis le Dock (Alt+Tab, barre de menus) apparaît sans animation.
- **Corbeille** : son icône passe de vide à pleine selon son contenu.
- **Forme des icônes** : celle de macOS 26 Tahoe, un carré à coins continus dont le rayon vaut 25,9 % du côté (mesuré sur le Dock d'un vrai Tahoe ; Big Sur à Sequoia : 22,5 %). En mode « Tahoe strict », une icône qui n'a pas cette forme est posée sur une plaque, comme dans Tahoe.
- **Thème macOS** (clic droit sur le séparateur → *Thème macOS*) : *Appliquer (curseurs et fond d'écran)* remplace les curseurs de Windows par des curseurs façon macOS (flèche noire bordée de blanc, flèches de redimensionnement, anneau d'attente) et pose sur chaque écran un fond d'écran façon Tahoe, clair ou sombre selon le mode de Windows. *Rétablir le thème Windows* rend exactement les curseurs et fonds d'avant.
  - Rien ne change sans ce clic. Ce qui est remplacé est sauvegardé une seule fois dans `%APPDATA%\MacDock\theme-backup.json` (une deuxième application ne l'écrase pas) ; les fichiers du thème sont dans `%APPDATA%\MacDock\theme`.
  - Sans lancer le Dock : `MacDock.exe --theme apply` ou `MacDock.exe --theme restore`.
  - Un fond d'écran en diaporama, en couleur unie ou « Windows à la une » n'a pas de fichier : au rétablissement, notre fond reste sur cet écran (c'est noté dans le journal du Dock).
- **Quitter le Dock** : clic droit → *Quitter MacDock*, ou `MacDock.exe --quit`. La barre Windows revient immédiatement.

## Barre de menus

- **Transparente**, comme sur Tahoe : le texte est clair ou foncé selon ton fond d'écran (mesuré sous la barre, puis toutes les minutes et à chaque changement de fond).
- **À gauche** :
  - le menu du système (logo) : À propos de ce PC, Réglages système, Microsoft Store, Éléments récents (dernières apps et derniers documents, « Effacer le menu »), Forcer à quitter, Suspendre, Redémarrer, Éteindre, Verrouiller l'écran, Fermer la session. Redémarrer, Éteindre et Fermer la session demandent confirmation ;
  - le nom de l'app active en gras, avec son menu : À propos, Réglages, Masquer, Masquer les autres, Tout afficher, Quitter. Comme sur macOS, *Masquer* fait disparaître les fenêtres d'un coup, sans génie ni case au Dock ; un clic sur l'icône de l'app (ou *Tout afficher*) les réaffiche de même ;
  - **ses vrais menus** quand elle en a : barre de menus classique (Bloc-notes historique, Notepad++, 7-Zip, regedit…) ou barre de menus accessible (Bloc-notes de Windows 11, apps Qt…). Les entrées, coches, entrées grisées et sous-menus sont ceux de l'app, relus à chaque ouverture ; la commande choisie est exécutée par l'app. Un menu Fenêtre est ajouté s'il manque ;
  - sinon, des menus génériques Fichier, Édition, Présentation, Fenêtre, Aide. Ils envoient les raccourcis standard (`Ctrl+S`, `Ctrl+Z`…), affichés à droite de chaque entrée. Les apps Chromium, Electron et Firefox gardent toujours ces menus génériques. Le menu Fenêtre liste les fenêtres de l'app et range la fenêtre active comme macOS 26 : *Remplir*, *Centrer*, *Déplacer et redimensionner* (moitiés, quarts, *Organiser* avec les fenêtres suivantes, revenir à la taille précédente), avec des marges de 8 pt ;
  - sur le bureau ou dans l'Explorateur, les menus de l'Explorateur, avec **Aller** (Téléchargements, Documents, Applications, Corbeille…), comme le Finder.
- **Ouvrir un menu** : un clic sur un titre ; tant qu'un menu est ouvert, survoler un autre titre l'ouvre aussi, et les flèches ← → passent au voisin. Le clavier reste à ton app : la commande choisie lui est envoyée.
- **À droite**, comme sur macOS : le menu de saisie (« FR », « US » dans une boîte, quand il y a plusieurs dispositions de clavier), le son, le Wi-Fi (s'il y a une carte Wi-Fi), la batterie (s'il y en a une), la loupe (Spotlight du Dock ; la recherche de Windows, `Win+S`, si le Dock ne tourne pas), le Centre de contrôle, puis la date et l'heure (`mer. 7 oct. 14:32`). Les icônes sont dessinées dans la couleur du texte et suivent l'état réel (disposition du clavier, volume, sourdine, signal, charge). Un clic ouvre leur menu :
  - **Saisie** : les dispositions du clavier avec leur nom (et celui du clavier quand une langue en a deux, comme AZERTY et BÉPO), celle de l'app au premier plan cochée (un clic la lui donne ; Windows le refuse pour une app lancée en administrateur), « Afficher les émojis et symboles » (`Win+.`), « Afficher le clavier visuel », « Ouvrir les réglages Clavier… ».
  - **Son** : curseur du volume, choix de la sortie (un clic en fait la sortie par défaut), « Réglages Son… ».
  - **Wi-Fi** : interrupteur, réseaux connus (un clic connecte), autres réseaux (ouvrent les réglages), « Réglages Wi-Fi… ».
  - **Batterie** : charge, source d'alimentation, réglages.
  - **Centre de contrôle** : tuiles Wi-Fi ou Ethernet, Bluetooth (s'il y a une radio), Concentration, Recopie d'écran (`Win+K`) ; curseurs de luminosité (si l'écran se règle par WMI ou DDC/CI) et du son ; lecture en cours avec précédent, lecture/pause et suivant.
  - Curseurs, interrupteurs, tuiles et boutons agissent sans fermer le menu, qui se met à jour pendant qu'il est ouvert. Survoler une icône ou un titre passe de l'un à l'autre.
- **Centre de notifications** (clic sur la date et l'heure) :
  - la date du jour, et le calendrier du mois, avec aujourd'hui dans une pastille rouge ;
  - la lecture en cours (précédent, lecture/pause, suivant) ;
  - les liens vers les notifications de Windows, le calendrier et les réglages de date et heure.
- **Icônes des autres apps** (avec le mod Windhawk 1.2) : à gauche des icônes d'état, celles de la zone de notification de Windows, la plus récente à gauche. Un clic, un double-clic ou un clic droit leur parvient comme sur la barre des tâches (leur propre menu s'ouvre). S'il n'y a pas la place, celles de gauche s'effacent avant de toucher le logo et le nom de l'app. `"showAppIcons": false` les masque.
- **Plusieurs écrans** : une barre en haut de chaque écran, avec les mêmes menus. Celle de l'écran où tu travailles est pleine, les autres sont atténuées ; un menu s'ouvre sur la barre cliquée. Brancher ou débrancher un écran ajoute ou retire sa barre.
- **Feux tricolores** : sur chaque fenêtre à barre de titre, à la place de ses boutons réduire, agrandir et fermer, trois pastilles rouge, jaune et verte ferment, réduisent (avec l'effet génie) et agrandissent la fenêtre ; au survol, elles montrent ×, − et +. Celles de la fenêtre active sont en couleur, celles des autres fenêtres grises, et reprennent leurs couleurs au survol ; on peut fermer ou réduire une fenêtre inactive sans l'activer, comme sur macOS. Une pastille grise sur la fenêtre active n'est pas disponible (un dialogue ne se réduit pas). On peut toujours déplacer la fenêtre en tirant à côté des pastilles. Le pointeur posé sur la pastille verte ouvre, comme sur macOS 26, le menu *Déplacer et redimensionner* : moitiés (quarts avec ⌥ maintenue), *Remplir* (*Centrer* avec ⌥), la fenêtre organisée avec les suivantes, *Plein écran*. Agrandir et restaurer gardent l'animation de Windows : seule la fenêtre que le génie réduit perd la sienne, le temps de l'effet (une fenêtre masquée la retrouve à son retour).
  - Par défaut, seulement sur les fenêtres dont Windows dessine la barre de titre (Table des caractères, `msinfo32`, `dxdiag`, la plupart des outils Win32 classiques ; pas les fenêtres lancées en administrateur, qui refusent les commandes d'une app ordinaire) : chez les apps qui dessinent la leur (Chrome, Edge, l'Explorateur à onglets, les apps récentes), elles cacheraient des onglets ou des boutons. `"trafficLights": "all"` les met partout, `"off"` les retire.
  - Les boutons de Windows restent à droite (on ne peut pas les retirer sans modifier les apps).
- **Volume et luminosité** : les touches volume +, volume − et sourdine montrent en haut à droite, sous la barre, la pastille en verre de macOS Tahoe (titre, sortie audio, jauge) au lieu du panneau de Windows. Le volume avance par seizièmes, comme sur Mac ; `Maj+Alt` avec une touche de volume avance par quarts de seizième. Un changement de luminosité (touches d'un portable, curseur de Windows) montre la pastille de luminosité ; sur un portable, le panneau de Windows apparaît aussi (ces touches sont traitées par l'ordinateur lui-même). La pastille s'efface 1,5 s après le dernier changement. Si un autre outil tient déjà les touches de volume, Windows les garde et la pastille suit simplement le volume.
- **Plein écran** : la barre s'efface et revient quand le curseur touche le haut de l'écran.
- **Clic droit dans le vide de la barre** : réglages, masquage automatique, quitter la barre.
- **Logo** : par défaut celui de Windows. Pour le remplacer, mets une image `menubar-logo.png` dans `%APPDATA%\MacDock\` ; seule sa transparence compte, elle prend la couleur du texte.

## Réglages

Le plus simple : l'app **Réglages MacDock** ( > « Réglages MacDock… »).
- Barre latérale de onze sections : Général, Dock, Barre des menus, Fenêtres, Mission Control, Clavier, Captures d'écran, Sons, Police, Mods Windhawk, À propos.
- **Recherche** en haut de la barre latérale (Ctrl+F) : sans casse ni accents ; seules les sections trouvées restent, Entrée ouvre la première et les lignes trouvées sont soulignées.
- Interrupteurs, curseurs et menus appliqués tout de suite ; clair ou sombre selon Windows.
- **Raccourcis** (Spotlight, Mission Control, Fenêtres de l'app, Forcer à quitter) : clic sur le champ, puis la combinaison voulue, ⊞ compris. Échap annule, Retour arrière efface. Les combinaisons gardées par Windows (⊞L, Ctrl+Alt+Suppr, Alt+Tab…) sont refusées, et deux fonctions sur le même raccourci sont signalées en rouge.
- **Général** : ouvrir MacDock à l'ouverture de session (clé `Run` de Windows), relancer ou quitter MacDock, exporter et importer tous les réglages dans un fichier, rétablir les réglages par défaut (les apps épinglées restent).
- **Couleur d'accentuation** (Général > Apparence), comme Réglages Système > Apparence de macOS 26 : Bleu, Violet, Rose, Rouge, Orange, Jaune, Vert ou Graphite. Elle colore la sélection des menus (Dock, barre, Centre de contrôle), de Spotlight et de Forcer à quitter, les interrupteurs et curseurs de l'app, le contour de Mission Control et le bouton « Capturer » de ⊞⇧5 ; le texte dessus reste lisible (noir sur le jaune).
- **Mods Windhawk** : versions installée et livrée de chaque mod, Installer, Mettre à jour ou Retirer. Windows demande l'autorisation administrateur ; une feuille propose de redémarrer l'Explorateur.
- Les confirmations s'ouvrent dans des feuilles d'alerte, comme sur macOS.

Elle écrit les mêmes fichiers que ceux décrits ci-dessous, et seulement la clé changée : rien de ce que le Dock y a écrit n'est perdu.

Tout est dans `%APPDATA%\MacDock\`, rechargé à chaud quand tu enregistres :

| Fichier | Contenu |
|---|---|
| `settings.json` | Position (`position` : `bottom`, `left`, `right`), écran (`screen`), taille des icônes (`tileSize`), agrandissement (`magnification`, `largeSize`), masquage automatique (`autohide`), effet de réduction (`minimizeEffect` : `genie`, `scale`, `windows`), apps récentes, mode « Tahoe strict » des icônes, police, couleur d'accentuation (`accentColor` : `blue`, `purple`, `pink`, `red`, `orange`, `yellow`, `green`, `graphite`), verre Liquid Glass (`glass`), raccourci de Spotlight (`spotlightHotkey` : `alt+space`, `ctrl+space`, `off`), raccourci de Mission Control (`missionControlHotkey` : `ctrl+alt+up`, `ctrl+up`, `f3`, `off`), Exposé d'une app (`appExposeHotkey` : `ctrl+alt+down`, `ctrl+down`, `off`), Forcer à quitter (`forceQuitHotkey` : `alt+win+escape` par défaut ; ces quatre raccourcis acceptent toute combinaison réglée dans l'app Réglages, ou `off`), sélecteur d'apps (`appSwitcherHotkey` : `alt+tab` ou `off` pour garder celui de Windows), fenêtre suivante de l'app (`windowCycleHotkey` : `alt+backtick` ou `off`), captures d'écran (`screenshots` : `true` ou `false`), coins actifs (`hotCorners` : `topLeft`, `topRight`, `bottomLeft`, `bottomRight` valant `off`, `missionControl`, `desktop`, `apps`, `notificationCenter`, `lockScreen`, `displaySleep` ou `screenSaver`), épingles (pour une pile : `view` = `auto`/`fan`/`grid`/`list`, `sort` = `dateAdded`/`name`/`modified`/`kind`, `display` = `stack`/`folder`). |
| `dock-metrics.json` | Toutes les mesures visuelles et d'animation (marges, rayon, ressorts, rebonds…), bornées pour éviter les valeurs absurdes. |
| `icons\<id>.png` | Icônes personnalisées (une par app, nommée d'après son identifiant). Comme sur macOS, prévois une toile de 1024 px avec la forme à 824 px au centre, aux coins continus d'environ 213 px de rayon (25,9 % de la forme, l'arrondi de Tahoe) : l'image est utilisée telle quelle. |
| `menubar.json` | Barre de menus : masquage automatique (`autohide`), police, horloge (`clock` : `weekday`, `date`, `seconds`, `hour24`), icônes affichées (`showInput`, `showSound`, `showNetwork`, `showBattery`, `showSearch`, `showAppIcons`), pastille du volume et de la luminosité (`hud` : `false` rend les touches de volume à Windows), feux tricolores (`trafficLights` : `standard`, `all`, `off`), mesures (`metrics` : hauteur, taille du texte, marges, `statusWidth`, `statusIconSize`, ligne de base du texte `textBaseline`, écart avant l'horloge `clockGap`…). Version 2 depuis le plan 64 : un fichier plus ancien est migré au lancement de la barre (seules les mesures restées aux anciens défauts changent, par exemple `statusWidth` 30 → 36), puis réécrit. |
| `menubar-logo.png` | Logo personnalisé du menu du système (facultatif). |
| `menubar-recent.json` | Apps récentes du menu du système, écrit par la barre (les documents viennent du dossier Récents de Windows, jamais modifié). |
| `logs\` | Journaux (`logs\menubar\` pour la barre de menus). |

Un fichier invalide n'efface rien : une copie `.bak` est faite et les réglages actuels sont conservés.

## Liquid Glass

Avec `"glass": true` (par défaut), le fond du Dock et les infobulles sont en verre : ce qui se trouve derrière est flouté, réfracté sur les bords, teinté selon le thème, avec un liseré lumineux. Le Dock ne se redessine que si le contenu sous lui change vraiment.

- Pour lire l'écran sous lui, le Dock s'exclut des captures : **il n'apparaît pas dans les enregistrements ni les partages d'écran** (il se montre à ses propres captures et à l'Outil Capture de Windows, voir plus haut). Mets `"glass": false` si tu en as besoin ; le Dock passe alors en verre dépoli classique.
- Écran HDR : pris en charge (le blanc SDR de Windows est respecté).
- Si la capture est impossible (écran tourné, carte graphique sans accélération, bureau sécurisé), le Dock passe en verre dépoli et reprend tout seul.

## Sécurité

- Si le Dock plante, se fige ou est fermé, la barre des tâches Windows revient en 5 secondes au plus.
- Désactiver le mod rétablit la barre et son mode d'affichage d'origine.

## Diagnostic

- `MacDock.exe --trace-windows` : journalise le suivi des fenêtres et les performances.
- `MacDock.exe --snapshot capture.png [--hover 0] [--theme light|dark]` : rendu du Dock dans une image, sans l'afficher.
- `MacDock.exe --snapshot capture.png --wallpaper fond.png --reference mac.png --diff diff.png` : comparaison avec une capture de macOS (voir `reference/README.md`).
- `MacDock.exe --capture-test bas.png` : capture réelle du bas de l'écran, telle que le verre la voit.
- `MacDock.exe --genie-snapshot planche.png [--effect genie|scale] [--edge bottom|left|right]` : six étapes de l'effet de réduction sur une fenêtre factice, sans rien afficher.
- `MacDock.exe --apps-snapshot apps.png [--query texte] [--page n] [--theme light|dark]` : l'écran Apps avec tes apps dans une image (`--page` compte à partir de 0), sans l'afficher ni rien lancer.
- `MacDock.exe --spotlight-snapshot spot.png [--query texte] [--theme light|dark]` : le panneau Spotlight avec tes apps, les réglages et tes documents dans une image (cases de couleur à la place des icônes, sauf les tuiles des sections de Réglages MacDock), sans l'afficher ni rien lancer.
- `MacDock.exe --mission-snapshot mc.png [--count n] [--hover i] [--theme light|dark]` : Mission Control avec n fenêtres factices dans une image (`--hover` : la fenêtre survolée, à partir de 0), sans toucher à tes fenêtres.
- `MacDock.exe --switcher-snapshot sw.png [--count n] [--select i] [--theme light|dark]` : le sélecteur d'apps avec n apps factices dans une image (`--select` : l'app choisie, à partir de 0), sans rien afficher ni activer.
- `MacDock.exe --force-quit-snapshot fq.png [--sheet] [--theme light|dark]` : la fenêtre « Forcer des applications à quitter » avec des apps factices dans une image (`--sheet` : avec sa confirmation), sans rien afficher ni arrêter.
- `MacDock.exe --theme-snapshot dossier` : planche des curseurs du thème (32 et 64 px, fonds clair et sombre) et les deux fonds d'écran, sans rien appliquer.
- `Ctrl+Alt+Maj+O` : superpose `%APPDATA%\MacDock\reference\overlay.png` au Dock ; `Ctrl+Alt+Maj+Haut/Bas` règle son opacité.
- `MacMenuBar.exe --trace` : journalise l'app active, la couleur du texte et les menus ouverts.
- `MacMenuBar.exe --snapshot barre.png [--wallpaper fond.png] [--app "Nom"] [--theme light|dark] [--open 1] [--scale 1] [--time 2026-10-09T22:17]` : rendu de la barre dans une image, sans l'afficher (`--scale` : autre échelle que celle de l'écran ; `--time` : heure de l'horloge, pour comparer à une capture de macOS).
- `MacMenuBar.exe --lights-snapshot planche.png` : les feux tricolores (clair, sombre ; normal, survol, indisponible) dans une image, sans rien afficher.
- `MacMenuBar.exe --zoom-snapshot menu.png [--theme light|dark] [--option] [--zoomed]` : le menu de la pastille verte dans une image, sans rien afficher.
- `MacMenuBar.exe --hud-snapshot hud.png [--kind volume|brightness] [--level 0.5] [--muted] [--theme light|dark]` : la pastille du volume ou de la luminosité dans une image, sans rien afficher ni régler.
- `MacMenuBar.exe --quit` : ferme la barre seule.
- `MacDockLauncher.exe --check-update` : cherche une mise à jour, la télécharge et la vérifie (code de sortie 0 : à jour, 10 : prête, 1 : erreur, détaillée dans `update.json`) ; `--install-update` installe celle qui est prête. Seulement depuis la copie posée par l'installateur ; un ordre d'arrêt (`--quit`, installateur) interrompt la recherche. Pour les essais : `MACDOCK_UPDATE_DIR` (dossier de l'état et des téléchargements), `MACDOCK_UPDATE_PRERELEASE=1` (préversions comprises), `MACDOCK_UPDATE_DELAY` (secondes avant la première recherche).
- `MacDockSettings.exe --pane dock` : ouvre l'app Réglages sur une section (`general`, `dock`, `menubar`, `windows`, `desktop`, `keyboard`, `screenshots`, `sounds`, `font`, `mods`, `about`) ; `--data <dossier>` lui fait lire et écrire un autre dossier de réglages (essais) : le démarrage avec Windows y est gardé dans `startup-test.json` au lieu de la clé `Run`, et les actions qui touchent au système (quitter MacDock, installateurs, dossiers) sont seulement écrites au journal ; `MACDOCK_SETTINGS_FILE` y remplace les dialogues d'export et d'import.
- `./build.ps1 -Target tests -Run` : tests automatiques.

## Note

Aucune ressource Apple (icônes, logo, police SF Pro) n'est incluse. Si SF Pro est installé sur ta machine, le Dock et la barre l'utilisent ; sinon Inter, puis Segoe UI Variable.
