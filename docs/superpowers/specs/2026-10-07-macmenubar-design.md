# MacMenuBar — Spécification de conception (sous-projet 2 : la barre de menus)

- **Date :** 2026-10-07
- **Statut :** validée par délégation. L'utilisateur a demandé « fais la barre de menus et fais le reste ensuite, je veux tout faire » et a choisi de tout faire en autonomie. Les quatre choix ci-dessous viennent de lui ; tout le reste est tranché ici et signalé dans le journal.
- **Référence visuelle :** barre de menus de macOS Tahoe 26 (transparente, menus en Liquid Glass)
- **Spec parente :** `2026-10-06-macos-dock-design.md` (découpage en sous-projets, unités, police, règles sur les ressources Apple)

## 1. Objectif

Donner à Windows 11 une barre de menus en haut de l'écran, au comportement et à l'aspect de celle de macOS Tahoe :

- à gauche, le menu du système, le nom de l'app active en gras, puis les menus de cette app ;
- à droite, les icônes d'état, le Centre de contrôle et la date et l'heure.

**Critère de réussite :** au quotidien, on peut passer par la barre pour les commandes des apps (Fichier, Édition…), le système (veille, verrouillage, extinction) et les réglages rapides (son, Wi-Fi, luminosité). Elle ne gêne jamais : elle s'efface en plein écran et ne vole pas le clavier à l'app active. Si elle plante, le lanceur la relance.

### 1.1 Choix de l'utilisateur (2026-10-07)

| Question | Réponse |
|---|---|
| Validation des sous-projets 2 à 6 | Tout en autonomie : spec, plan, code, relecture par un agent, fusion locale |
| Menus de l'app active | Les vrais menus quand l'app en a (Win32 ou UI Automation), sinon des menus génériques branchés sur les raccourcis standard |
| Apparence | Tahoe transparente : aucun fond, texte clair ou foncé selon le fond d'écran, menus déroulants en verre |
| Icônes d'état | Icônes système d'abord. Celles des autres apps viennent par le mod Windhawk, actives quand il est installé |

### 1.2 Limites assumées
- **Aucune ressource Apple** : pas de logo Apple ni de symboles SF.
  - Le menu du système porte par défaut le logo Windows, dessiné en quatre carrés.
  - L'utilisateur peut le remplacer par son image : `%APPDATA%\MacDock\menubar-logo.png`, teinte appliquée selon la couleur du texte.
  - Les icônes d'état sont dessinées par le code (géométrie Direct2D).
- **Fidélité des menus d'app.** Une app sans menus lisibles (la plupart des apps UWP, Electron ou Chromium) reçoit des menus génériques. Une commande générique n'agit que si l'app gère le raccourci correspondant.
- **Mesures estimées.** Les mesures de macOS sont estimées, puis calibrables dans `menubar.json` ; comme pour le Dock, rien n'est copié.

## 2. Architecture

### 2.1 Livrable : `MacMenuBar.exe`, processus séparé

La barre est un exécutable distinct de `MacDock.exe`. Il partage les mêmes modules de logique (configuration, JSON, journal, identité des apps, suivi des fenêtres, menus en verre, verre Liquid Glass).

Pourquoi un processus séparé :
- **Isolement.** Un plantage de la barre n'emporte pas le Dock, et inversement.
- **Capture de l'écran.** Windows n'autorise qu'une duplication de l'écran par processus et par sortie. Dans un processus à part, les menus de la barre capturent l'écran sans suspendre le verre du Dock.
- **Lanceur.** `MacDockLauncher.exe` surveille les deux exécutables, chacun avec sa propre politique de relance (3 plantages en 60 s : abandon pour celui-là seulement).

Lien avec le Dock :
- Quand le Dock se ferme normalement (« Quitter MacDock »), le lanceur ferme aussi la barre.
- `MacMenuBar.exe --quit` ferme la barre seule.

### 2.2 Modules (`src/menubar/`)

Les modules marqués *pur* n'appellent aucune API graphique ou système et sont testés unitairement.

| Module | Rôle |
|---|---|
| `bar_layout` (*pur*) | Position de chaque titre (gauche) et de chaque icône d'état (droite) à partir de leurs largeurs. Quand tout ne tient pas, masque les derniers menus d'app avant qu'ils ne touchent la partie droite. Test de collision. |
| `bar_color` (*pur*) | Luminance moyenne d'une bande d'image (sRGB → luminance relative), puis choix du texte clair ou foncé, avec une hystérésis pour ne pas basculer sans cesse. |
| `clock_format` (*pur*) | Texte de l'horloge à la française, comme macOS : `mar. 7 oct. 14:32`. Options : jour de la semaine, date, secondes, 12 ou 24 h. |
| `shortcut` (*pur*) | Analyse d'un raccourci (`Ctrl+Maj+S`, `Alt+F4`, `Win+L`, `F11`) en touches virtuelles, et texte affiché dans les menus. |
| `foreground_rules` (*pur*) | Classement de la fenêtre au premier plan : app normale, bureau ou Explorateur, ou interface à ignorer (barre, Dock, menu Démarrer, Alt+Tab…). Dans le dernier cas, l'app affichée reste la précédente. |
| `app_menus` (*pur*) | Modèles des menus : menu du système, menu de l'app (en gras), menus génériques (Fichier, Édition, Présentation, Fenêtre, Aide), menus de l'Explorateur (dont « Aller »). Chaque entrée porte une **action** typée (raccourci, commande de fenêtre, ouverture d'une URI, action système, commande d'app). |
| `menubar_settings` (*pur*) | Lecture et écriture de `menubar.json` (réglages et mesures), bornes et valeurs par défaut. |
| `bar_renderer` | Rendu DirectComposition + Direct2D + DirectWrite de la bande transparente : textes, logo, icônes d'état, capsule du titre ouvert. Rendu hors écran pour `--snapshot`. |
| `backdrop_sampler` | Échantillonne par Desktop Duplication la bande d'écran sous la barre (la barre est exclue de la capture) et en donne la luminance. |
| `app_menu_reader` (plan 7) | Lit les vrais menus : `HMENU` d'une fenêtre Win32 classique, ou barre de menus exposée par UI Automation. |
| `status_items` (plan 8) | Wi-Fi (WlanAPI), batterie (`GetSystemPowerStatus`), son (Core Audio), luminosité (WMI ou DDC/CI), lecture en cours et radios (WinRT). |
| `menubar_window` | Fenêtre, barre d'application `ABE_TOP`, minuteries, suivi de l'app active, ouverture des menus, exécution des actions. |

Modules réutilisés sans changement d'interface : `WindowTracker`, `identifyWindow`, `AppModel` (fenêtres par app), `config_store`, `log`, `GlassRenderer`, `BackdropCapture`, `png_io`.

### 2.3 Évolutions des modules partagés
- **`MenuItem.shortcut`** : texte du raccourci, aligné à droite en gris, comme les « ⌘S » de macOS. La largeur du menu en tient compte.
- **`MenuWindow::Side::Below`** : le menu s'ouvre sous l'ancrage. Son bord gauche est aligné sur le titre cliqué, moins la marge intérieure, comme les menus de la barre de macOS.
- **`MenuWindow::BarLink`** : rectangles des titres voisins de la barre. Pendant qu'un menu est ouvert :
  - survoler un autre titre ferme le menu et renvoie « passer au titre k » ;
  - de même pour les flèches gauche et droite au premier niveau ;
  - un clic sur le titre déjà ouvert ferme le menu sans rien choisir.
- **Lanceur** : il surveille plusieurs processus.

### 2.4 Flux

```
WinEvent (premier plan, fenêtres) ─► WindowTracker ─► AppModel ─► app active (foreground_rules)
                                                                     │
menubar.json ─► menubar_settings ─► bar_layout ◄── app_menus ◄───────┘
                                        │
backdrop_sampler ─► bar_color ─► bar_renderer ─► écran
                                        │
clic sur un titre ─► MenuWindow (Below + BarLink) ─► action ─► premier plan rendu à l'app ─► SendInput / ShowWindow / ShellExecute
```

## 3. Apparence

### 3.1 Mesures (points, calibrables dans `menubar.json` → `metrics`)

| Mesure | Valeur | Origine |
|---|---|---|
| Hauteur de la barre | 24 pt | macOS sur écran sans encoche |
| Police | 13 pt ; nom de l'app en gras ; même police que le Dock (SF Pro > Inter > Segoe UI Variable) | macOS |
| Marge gauche avant le logo | 10 pt | Estimation |
| Marge horizontale d'un titre | 10 pt de chaque côté du texte | Estimation |
| Logo | 14 pt de haut, centré dans un titre de 34 pt | Estimation |
| Capsule du titre ouvert | hauteur 22 pt, rayon 6 pt ; blanc à 22 % (texte clair) ou noir à 10 % (texte foncé) | Estimation (Tahoe) |
| Icônes d'état | cases de 30 pt, glyphes de 16 pt | Estimation |
| Marge droite après l'horloge | 10 pt | Estimation |
| Écart entre la barre et un menu | 1 pt | Estimation |

### 3.2 Transparence et couleur du texte
- La barre n'a aucun fond : seuls les textes et les icônes sont opaques. Une fenêtre DirectComposition à alpha prémultiplié laisse voir le fond d'écran.
- **Couleur.** La luminance moyenne de la bande sous la barre décide :
  - au-dessus de 0,45 (luminance relative linéaire, environ L* 73), le texte devient foncé ;
  - sous 0,35 (environ L* 66), il redevient clair (hystérésis entre les deux) ;
  - le texte clair est blanc, avec une ombre noire à 25 % décalée de 1 px ;
  - le texte foncé est noir à 85 %.
- **Quand la bande est échantillonnée :**
  - au démarrage ;
  - à un changement de fond d'écran, de thème ou d'affichage ;
  - toutes les 60 s (diaporama) ;
  - jamais pendant qu'un menu est ouvert : une seule duplication par processus.
- **Si l'échantillonnage échoue**, la couleur suit le mode clair ou sombre de Windows : texte clair en sombre, foncé en clair.

### 3.3 Menus déroulants
- Ce sont les menus en verre du Dock (`MenuWindow`), ouverts sous la barre.
- Les raccourcis s'affichent à droite en gris : texte à 45 % d'opacité, ou blanc à 70 % sur l'entrée survolée.
- Le menu suit le mode clair ou sombre de Windows, comme ceux du Dock.

## 4. Comportements

### 4.1 App active
- Le nom affiché est celui de l'app au premier plan (son nom lisible, comme dans le Dock).
- **Interfaces ignorées** : la barre, le Dock (et ses menus, piles, sprites), le menu Démarrer, la recherche, Alt+Tab, le centre de notifications. Pendant qu'elles ont le clavier, l'app affichée ne change pas, comme sur macOS quand on clique dans le Dock.
- **Bureau ou fenêtre de l'Explorateur** : la barre affiche « Explorateur » avec les menus de l'Explorateur. C'est l'équivalent du Finder.

### 4.2 Ouverture des menus
- Un clic (appui) sur un titre ouvre son menu sous la barre ; le titre reçoit la capsule.
- Tant qu'un menu est ouvert, survoler un autre titre l'ouvre aussi ; les flèches gauche et droite passent au titre voisin.
- Un clic ailleurs, ou Échap, ferme le menu.
- **Le clavier reste à l'app.** La barre est `WS_EX_NOACTIVATE`. Les panneaux de menu prennent le clavier le temps du menu. Avant d'exécuter l'action, la barre rend le premier plan à la fenêtre qui l'avait (celle de l'app affichée), puis agit.

### 4.3 Actions
| Type | Exécution |
|---|---|
| Raccourci | Premier plan rendu à l'app, pause de 40 ms, puis `SendInput` des touches. |
| Fenêtre | `ShowWindow` (réduire, zoom = agrandir ou restaurer), Remplir, Centrer, Déplacer et redimensionner (moitiés, quarts, taille précédente : `tileWindow`, plan 50), activation d'une fenêtre de la liste, « Tout ramener au premier plan ». |
| App | Masquer (réduire toutes ses fenêtres), Masquer les autres, Tout afficher, Quitter (`WM_CLOSE` à chaque fenêtre, comme le Dock), À propos (propriétés de l'exécutable). |
| URI | `ShellExecute` (`ms-settings:`, `ms-windows-store:`, dossiers connus). |
| Système | Suspendre, Verrouiller l'écran, Fermer la session, Redémarrer, Éteindre. Les trois dernières demandent une confirmation (boîte de dialogue). Ces actions passent par une interface injectée : **les tests n'éteignent jamais rien**. |

### 4.4 Menus fixes
- **Menu du système (logo)** : À propos de ce PC · Réglages système… · Microsoft Store… · Éléments récents ▸ (plan 7) · Forcer à quitter… (`Ctrl+Maj+Échap`) · Suspendre · Redémarrer… · Éteindre… · Verrouiller l'écran (`Win+L`) · Fermer la session de *Nom*…
- **Menu de l'app (en gras)** : À propos de *App* · Réglages… (`Ctrl+,`) · Masquer *App* · Masquer les autres · Tout afficher · Quitter *App* (`Alt+F4`).
- **Menus génériques** (app sans menus lisibles) :
  - **Fichier** : Nouvelle fenêtre `Ctrl+N` · Nouvel onglet `Ctrl+T` · Ouvrir… `Ctrl+O` · Fermer l'onglet `Ctrl+W` · Fermer la fenêtre `Alt+F4` · Enregistrer `Ctrl+S` · Enregistrer sous… `Ctrl+Maj+S` · Imprimer… `Ctrl+P`.
  - **Édition** : Annuler `Ctrl+Z` · Rétablir `Ctrl+Y` · Couper `Ctrl+X` · Copier `Ctrl+C` · Coller `Ctrl+V` · Tout sélectionner `Ctrl+A` · Rechercher… `Ctrl+F` · Emoji et symboles `Win+.`.
  - **Présentation** : Actualiser `F5` · Plein écran `F11` · Zoom avant `Ctrl+Plus` · Zoom arrière `Ctrl+Moins` · Taille réelle `Ctrl+0`.
  - **Fenêtre** : Réduire · Zoom · Remplir · Centrer · Déplacer et redimensionner (plan 50) · Tout ramener au premier plan · liste des fenêtres de l'app (coche sur l'active).
  - **Aide** : Aide sur *App* `F1`.
- **Explorateur** : Fichier (Nouvelle fenêtre, Nouveau dossier `Ctrl+Maj+N`, Fermer la fenêtre) · Édition (générique) · Présentation (générique) · Aller (Précédent `Alt+←`, Suivant `Alt+→`, Dossier parent `Alt+↑`, Récents, Documents, Bureau, Téléchargements, Accueil, Ce PC, Réseau, Applications, Corbeille) · Fenêtre · Aide. Depuis le bureau, les entrées d'« Aller » ouvrent une nouvelle fenêtre de l'Explorateur.
- **Vrais menus (plan 7)** : quand l'app en a, ils remplacent les menus génériques. Le menu de l'app (en gras) reste.

### 4.5 Partie droite
- **Plan 6** : la date et l'heure. Un clic ouvre le centre de notifications (`Win+N`).
- **Plan 8** : le son, le Wi-Fi, la batterie (si l'appareil en a une), la recherche (`Win+S`) et le Centre de contrôle.
  - Chaque icône ouvre son panneau de verre.
  - Le Centre de contrôle réunit : Wi-Fi et Bluetooth en tuiles, le son et la luminosité en curseurs, la lecture en cours, Ne pas déranger (ouvre les réglages).
- **Plan 9** : les icônes des autres apps, relayées par le mod Windhawk, placées à gauche des icônes système.
- Un clic droit dans une zone vide de la barre ouvre « Réglages de la barre des menus… » (ouvre `menubar.json`) et « Quitter la barre des menus ».

### 4.6 Plein écran et masquage
- **Plein écran.** Quand l'app au premier plan est en plein écran sur l'écran de la barre (même règle que le Dock), la barre s'efface. Elle revient au-dessus de l'app quand le curseur touche le haut de l'écran, et repart quand il s'éloigne, sauf si un menu est ouvert.
- **Masquage automatique** (réglage `autohide`, désactivé par défaut) :
  - pas de zone réservée ;
  - même apparition au bord haut.
- **Moteur.** Le module `Visibility` du Dock est réutilisé, avec le bord haut comme bord du curseur.

### 4.7 Écrans
- Plans 6 à 8 : la barre est sur l'écran principal et suit son DPI.
- Plan 9 : une barre par écran, comme sur macOS. Celle de l'écran actif est pleine ; les autres sont atténuées à 60 % (voir 4.10).

### 4.8 Vrais menus et Éléments récents (plan 7)

**Menus Win32 (`HMENU`).** Une fenêtre qui a une barre de menus classique (`GetMenu`) fournit les titres de la barre : texte sans « & », raccourci après la tabulation.
- **Lecture à l'ouverture.** À l'ouverture d'un titre, la barre envoie `WM_INITMENU` puis `WM_INITMENUPOPUP` à l'app (`SendMessageTimeout`, 200 ms, abandon si l'app ne répond pas). Les apps mettent ainsi à jour coches, entrées grisées et listes dynamiques (fichiers récents). Puis la barre relit le sous-menu, sous-menus imbriqués compris.
- **Exécution.** Un choix envoie `WM_COMMAND` avec l'identifiant de l'entrée, après avoir rendu le premier plan à l'app.
- **Entrées non lisibles.** Une entrée dessinée par l'app (owner-draw), sans texte lisible, est omise.

**Menus UI Automation.** Une app sans `HMENU` mais avec une barre de menus accessible (Bloc-notes de Windows 11, apps Qt, Java) : la barre en lit les titres (élément `MenuBar` hors barre système).
- **Lecture d'un menu.** À l'ouverture d'un titre, la barre déplie le menu de l'app, en lit les entrées (nom, état, raccourci), le replie, puis montre son propre menu de verre. Le menu de l'app peut apparaître un instant.
- **Exécution.** Un choix déplie de nouveau le menu de l'app et invoque l'entrée.
- **Sous-menus imbriqués.** Une entrée qui ouvre un sous-menu le déplie dans l'app.
- **Apps exclues.** Ne sont jamais interrogées les fenêtres Chromium et Electron (`Chrome_WidgetWin_*`), Firefox (`MozillaWindowClass`), et celles qui hébergent un navigateur intégré (WebView2, CEF : fenêtre enfant `Chrome_*`).
- **Recherche bornée.** La barre de menus n'est cherchée qu'à 4 niveaux sous la fenêtre au plus. Une fenêtre sans barre n'est pas réinterrogée pendant 5 minutes. Une requête UI Automation y active tout l'arbre d'accessibilité, ce qui ralentit l'app ; elles gardent les menus génériques.
- **Délais.** Les requêtes ont des délais courts (connexion 1 s, transaction 1,5 s) : une app figée ne bloque pas la barre plus longtemps.

**Menu Fenêtre.** Si l'app n'a pas de menu « Fenêtre » (ou « Window »), celui de la barre est ajouté avec la liste de ses fenêtres : avant son menu d'aide, comme sur macOS, sinon en dernier.

**Barre qui change.** Une app peut changer sa barre sans changer de fenêtre (document ouvert, fenêtre MDI). La barre relit donc les titres Win32 à chaque ouverture d'un menu.

**Éléments récents** (menu du système) :
- **Applications** : les dernières apps passées au premier plan, 10 au plus, gardées dans `menubar-recent.json` (fichier à part : la barre ne réécrit jamais `menubar.json`).
- **Documents** : les derniers documents du dossier Récents de Windows (raccourcis dont le nom porte une extension), 10 au plus, avec l'icône de leur type.
- **Effacer le menu** : oublie les applications et masque les documents ouverts avant ce moment (date gardée dans `menubar-recent.json`) ; le dossier Récents de Windows n'est pas touché.

### 4.9 Icônes d'état et Centre de contrôle (plan 8)

**Partie droite**, de droite à gauche :
- l'horloge ;
- le Centre de contrôle (deux interrupteurs dessinés) ;
- la recherche (loupe : `Win+S`) ;
- la batterie (si l'appareil en a une : icône remplie selon la charge, éclair en charge) ;
- le réseau : Wi-Fi (arcs selon le signal) si une interface sans fil existe, sinon rien, comme macOS sur un Mac filaire ;
- le son (haut-parleur et 0 à 3 ondes selon le volume, barré en sourdine).

Chaque élément se masque par un réglage : `showSound`, `showNetwork`, `showBattery`, `showSearch`.

Les icônes sont dessinées par le code, dans la couleur du texte. Un clic ouvre le menu de l'élément, en verre, sous l'icône, aligné à droite. Tant qu'un menu est ouvert, survoler un autre élément de la barre l'ouvre, à gauche comme à droite.

**Lignes de menu enrichies.** Comme les `NSMenuItem` à vue de macOS, une entrée peut être :
- un intitulé (petit texte gris en gras) ;
- un curseur, avec une icône à gauche, qui agit en direct ;
- un interrupteur à droite du texte ;
- une rangée de tuiles : deux tuiles, chacune avec icône, titre et état, bleue quand elle est active ;
- la lecture en cours : titre, artiste, et les boutons précédent, lecture/pause et suivant.

Toucher un curseur, un interrupteur, une tuile ou un bouton de lecture ne ferme pas le menu. Pendant qu'il est ouvert, le menu se met à jour toutes les 500 ms (volume changé ailleurs, morceau suivant).

**Menus des éléments :**
- **Son** : intitulé « Son », curseur du volume, puis « Sortie » avec les périphériques de sortie, l'actuel coché ; un clic en fait la sortie par défaut. Dernière entrée : « Réglages Son… » (`ms-settings:sound`).
- **Wi-Fi** : interrupteur Wi-Fi, réseau connecté coché, puis « Autres réseaux ». Un réseau déjà connu se connecte d'un clic ; un nouveau réseau ouvre les réglages Wi-Fi. Dernière entrée : « Réglages Wi-Fi… ».
- **Batterie** : pourcentage, source d'alimentation, « Réglages de la batterie… ».
- **Centre de contrôle** :
  - tuiles « Réseau » (Wi-Fi activable, ou Ethernet) et « Bluetooth » (activable si une radio existe) ;
  - tuile « Concentration » (Ne pas déranger : ouvre les réglages de notification) et tuile « Recopie d'écran » (`Win+K`) ;
  - curseur « Écran » (luminosité, s'il y a moyen de la régler) ;
  - curseur « Son » ;
  - lecture en cours, s'il y en a une ;
  - « Réglages de la barre des menus… » (ouvre `menubar.json`).

**Sources** (fichiers `src/menubar/status_*`) :
- **Son** : Core Audio (`IAudioEndpointVolume`), avec notification de changement. La sortie par défaut change par l'interface `IPolicyConfig` de Windows (non documentée mais stable depuis Windows 7). Si elle échoue, les réglages Son s'ouvrent.
- **Réseau** : WlanAPI (interface, réseau connecté, qualité du signal, réseaux visibles, connexion par profil).
- **Radios** : Wi-Fi et Bluetooth par `Windows.Devices.Radios` (C++/WinRT).
- **Batterie** : `GetSystemPowerStatus`.
- **Luminosité** : WMI (`WmiMonitorBrightness`) pour l'écran intégré ; sinon DDC/CI (`dxva2`) pour l'écran principal ; sinon pas de curseur.
- **Lecture en cours** : `GlobalSystemMediaTransportControlsSessionManager` (C++/WinRT).

**Blocages et défaillances.**
- Les sources lentes (DDC/CI, WinRT, WlanAPI) sont lues sur un fil de travail ; la barre ne les attend jamais.
- Une source indisponible masque son élément ou sa tuile ; aucune erreur n'est affichée.

### 4.10 Icônes des autres apps et écrans multiples (plan 9)

**Icônes des apps (zone de notification).** Sur macOS, les apps posent leurs icônes dans la barre de menus, à gauche des icônes système. Sous Windows, elles appellent `Shell_NotifyIcon`, qui envoie un `WM_COPYDATA` à la fenêtre `Shell_TrayWnd` d'explorer.
- **Relais par le mod Windhawk** (`macdock-hide-taskbar`, version 1.2), seul code qui tourne dans explorer :
  - un crochet `WH_CALLWNDPROC` sur le fil de `Shell_TrayWnd` lit chaque `WM_COPYDATA` de la zone de notification (`dwData == 1`, signature `0x34753423`, `NOTIFYICONDATA` 32 bits) : ajout, modification, suppression, version ;
  - le mod tient la liste des icônes, convertit chaque icône en image BGRA 32 × 32 et l'envoie à la barre par le pipe `\\.\pipe\MacMenuBar` (même format d'en-tête que `\\.\pipe\MacDock`, nouveaux types `TrayUpdate` et `TrayRemove`) ;
  - à la connexion de la barre, il renvoie toute la liste ; au démarrage du mod, il diffuse `TaskbarCreated` pour que les apps déjà lancées redéclarent leurs icônes ;
  - explorer garde ses propres icônes : le mod observe sans rien bloquer.
- **Dans la barre** : les icônes visibles (sans `NIS_HIDDEN`) sont placées à gauche des icônes système, la plus récente à gauche, en couleurs, à 16 pt. Réglage `showAppIcons` (activé par défaut).
- **Clic** : la barre poste à la fenêtre de l'app le message de rappel, au format de sa version :
  - versions 0 à 3 : `wParam` = identifiant, `lParam` = message souris ;
  - version 4 : `wParam` = point d'ancrage (`x`, `y`), `lParam` = message souris (mot bas) et identifiant (mot haut) ; après le relâchement, `NIN_SELECT` (gauche) ou `WM_CONTEXTMENU` (droit).
  
  Avant de poster, la barre autorise l'app à passer au premier plan (`AllowSetForegroundWindow`) : son menu ou sa fenêtre s'ouvre devant.
- **Sans le mod** : aucune icône d'app ; rien d'autre ne change.
- **Une app disparue** (fenêtre détruite) : son icône est retirée au relevé suivant.

**Écrans multiples.** Une barre par écran, comme sur macOS :
- chaque barre a sa zone réservée, son DPI, la couleur de son texte (fond mesuré sous elle) et son plein écran ;
- l'écran actif est celui de la fenêtre au premier plan (le bureau : celui du curseur). Sa barre est pleine ; les autres sont atténuées à 60 % ;
- toutes les barres montrent les mêmes menus et icônes ; un menu s'ouvre sous la barre cliquée ;
- un écran branché ou débranché ajoute ou retire sa barre ;
- `--snapshot` rend la barre de l'écran principal.

## 5. Robustesse
- **Mutex** `Local\MacMenuBar` : une seule barre à la fois.
- **Plantage** : il est journalisé, et le lanceur relance la barre.
- **Journal** : `%APPDATA%\MacDock\logs\menubar\` (séparé de celui du Dock pour éviter deux écrivains sur le même fichier).
- **Réglages** :
  - `menubar.json` invalide : il est sauvegardé en `.bak` et la barre démarre avec les valeurs par défaut ;
  - le rechargement est à chaud.
- **Commandes vers les apps** :
  - aucune n'est envoyée si la fenêtre cible a disparu ;
  - si une app ne répond pas, rien ne bloque : `SendMessageTimeout` pour les lectures de menus, `PostMessage` pour les commandes.
- **Sortie** : la barre libère sa zone réservée (`ABM_REMOVE`), même à la fermeture par le lanceur.

## 6. Réglages (`%APPDATA%\MacDock\menubar.json`)

```json
{
  "version": 1,
  "autohide": false,
  "font": "",
  "clock": { "weekday": true, "date": true, "seconds": false, "hour24": true },
  "showSound": true,
  "metrics": { "height": 24, "fontSize": 13, "leftMargin": 10, "titlePadding": 10, "logoSize": 14,
               "highlightHeight": 22, "highlightRadius": 6, "statusWidth": 30, "rightMargin": 10 }
}
```

## 7. Tests
- **Unitaires** (dans `tests.exe`) pour chaque module *pur* : mise en page, couleur et hystérésis, horloge, raccourcis, classement du premier plan, modèles de menus et actions, réglages. S'y ajoutent les extensions de `MenuItem` et de la mise en page des menus (largeur des raccourcis).
- **Rendu hors écran** : `MacMenuBar.exe --snapshot f.png [--wallpaper fond.png] [--app "Nom"] [--theme light|dark]` dessine la barre sur un fond donné, en WARP, sans fenêtre. Sert à vérifier le texte clair ou foncé, la capsule et les icônes.
- **Essais réels** : lancement avec `--trace` (zone réservée, app active suivie dans le journal), puis `--quit`. **Aucun essai qui pilote la souris pendant que l'utilisateur est présent.**

## 8. Découpage en plans

| Plan | Contenu |
|---|---|
| **6 — Barre de menus** | `MacMenuBar.exe` : barre transparente en haut, couleur du texte selon le fond, logo et menu du système, nom de l'app active et menu de l'app, menus génériques et de l'Explorateur, horloge, ouverture des menus avec passage d'un titre à l'autre, actions, plein écran et masquage automatique, lanceur à plusieurs processus, `--snapshot`. |
| **7 — Vrais menus** | Lecture des menus Win32 (`HMENU`) et UI Automation, exécution de leurs commandes ; Éléments récents. |
| **8 — Icônes d'état et Centre de contrôle** | Son, Wi-Fi, batterie, recherche, Centre de contrôle (tuiles, curseurs, lecture en cours). |
| **9 — Icônes des autres apps et écrans multiples** | Relais de la zone de notification par le mod Windhawk ; une barre par écran. |

## 9. Hors périmètre
- **Barre de menus globale.** Elle exigerait d'injecter du code dans chaque app pour masquer ses menus internes : leurs propres barres de menus restent visibles dans leurs fenêtres.
- Les widgets du centre de notifications de macOS (on ouvre celui de Windows).
- Siri, Spotlight (on ouvre la recherche de Windows).
