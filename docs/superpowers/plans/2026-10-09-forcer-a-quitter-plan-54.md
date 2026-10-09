# Forcer à quitter — plan 54

> **Pour les agents :** exécution en ligne, tâche par tâche, tests d'abord. Reprendre à la première tâche non cochée.

**But :** la fenêtre « Forcer des applications à quitter » de macOS 26 (⌥⌘⎋, menu  > Forcer à quitter…) : les apps
ouvertes avec leur icône, « (ne répond pas) » en rouge pour une app figée, une confirmation, et « Relancer » pour
l'Explorateur (le Finder de Windows). Aujourd'hui, « Forcer à quitter… » ouvre le Gestionnaire des tâches.

Session autonome : aucune question possible, choix faits ici ; aucun push.

## Choix
- **Où** : dans le Dock (`MacDock.exe`), qui tourne toujours, connaît les apps et leurs icônes, et enregistre déjà les
  raccourcis globaux. Habillage de l'app Réglages (`src/ui` : pastilles, bouton en capsule, feuille d'alerte),
  désormais lié dans le Dock.
- **Raccourci** : `forceQuitHotkey` dans `settings.json`, `alt+win+escape` par défaut : sur un clavier de PC, Alt et ⊞
  sont aux places de ⌘ et ⌥ (libre sur ce PC, essayé avec RegisterHotKey). Réglable dans Réglages > Clavier, avec
  détection des conflits. Identifiant 13.
- **Menu ** : « Forcer à quitter… » (raccourci affiché d'après le réglage) demande au Dock d'ouvrir la fenêtre
  (message « MacDockForceQuit », comme Spotlight) ; sans Dock, le Gestionnaire des tâches comme avant.
- **Liste** (relue chaque seconde, fenêtre ouverte) :
  - les apps qui ont une fenêtre éligible au Dock (mêmes règles que le Dock), regroupées comme dans le Dock, triées
    par nom (ordre du français) ; l'Explorateur toujours présent, en dernier, avec « Relancer » ;
  - « (ne répond pas) » en rouge si une de ses fenêtres est figée (`IsHungAppWindow`) ; une fenêtre fantôme
    (« Ghost », posée par Windows sur une app figée) compte pour l'app figée (`HungWindowFromGhostWindow`), jamais
    comme une app à part.
- **Forcer à quitter** : feuille « Voulez-vous forcer « X » à quitter ? / Les modifications non enregistrées seront
  perdues. » [Annuler] [Forcer à quitter] ; puis `TerminateProcess` des processus de l'app :
  - les propriétaires de ses fenêtres (pour une app du Store, le processus de la CoreWindow, jamais
    ApplicationFrameHost) ;
  - pour une app empaquetée, tous les processus de même AUMID (une app du Store réduite n'a pas de fenêtre à elle) ;
  - jamais le PID 0 ou 4, ni MacDock, ni l'Explorateur (sauf « Relancer ») ;
  - refusé (app lancée en administrateur) : feuille « Impossible de forcer « X » à quitter » avec la raison.
- **Relancer** (Explorateur) : feuille « Voulez-vous relancer l'Explorateur ? / Ses fenêtres seront fermées. » ; puis
  le redémarrage de l'Explorateur déjà écrit pour Réglages (déplacé dans `src/shell`).
- **Fenêtre** : 380 × 420 pt, au premier plan (outil de secours), centrée sur l'écran du pointeur, coins arrondis et
  ombre de Windows, thème clair ou sombre du système, à l'échelle de l'écran :
  - barre de titre : pastilles (fermer, réduire ; agrandir grisée), titre centré « Forcer des applications à
    quitter » ;
  - texte : « Si une app ne répond pas pendant un moment, sélectionnez son nom et cliquez sur Forcer à quitter. » ;
  - liste arrondie, lignes de 26 pt, icône de 20 pt, sélection sur l'accent ;
  - en bas : « Vous pouvez ouvrir cette fenêtre en appuyant sur ⌥⊞⎋. » (le vrai raccourci), bouton « Forcer à
    quitter » ou « Relancer », grisé sans sélection ;
  - clavier : ↑ ↓, Entrée (confirmation), Échap (ferme la feuille, sinon la fenêtre), Ctrl+W ;
  - après une app quittée, la sélection passe à la suivante.
- **Essais** : la fenêtre reçoit l'action « forcer à quitter » de son hôte. Le Dock lui donne la vraie ; la sonde
  d'essai réel l'héberge elle-même avec une action gardée qui ne peut tuer que ses propres processus d'essai (règle de
  la session : ne jamais fermer tes apps).

## Tâches
- [x] 1. **Raccourcis libres relus** (défaut trouvé en préparant ce plan) : `settingsFromJson` n'acceptait qu'une liste
  fixe pour Spotlight, Mission Control et Exposé, alors que Réglages enregistre n'importe quel raccourci ; un raccourci
  libre revenait en silence à sa valeur par défaut. Désormais : tout raccourci que `parseHotkey` accepte, ou « off ».
  Nouvelle clé `forceQuitHotkey`. Tests : aller-retour par le fichier, valeur invalide, défaut.
- [x] 2. **Logique** (`src/forcequit/force_quit_logic.*`) : regroupement des fenêtres en apps, tri, Explorateur en
  dernier, app figée, fenêtres fantômes ; processus à arrêter ; sélection après une app quittée. Tests.
- [x] 3. **Système** (`src/forcequit/force_quit_system.*`, `src/shell/explorer_restart.*`) : énumération réelle
  (fenêtres, fantômes, processus et AUMID), arrêt des processus avec la raison d'un refus, relance de l'Explorateur
  (Réglages s'en sert aussi). `AppIdentity` garde le PID de l'app.
- [x] 4. **Fenêtre** (`src/forcequit/force_quit_window.*`, mise en page pure testée) : dessin, souris, clavier,
  feuilles, relecture de la liste ; `MacDock.exe --force-quit-snapshot f.png [--theme dark]`.
- [x] 5. **Branchements** : Dock (raccourci 13, message « MacDockForceQuit », icônes), Réglages > Clavier (ligne et
  conflits), menu  (action ForceQuit, raccourci affiché, Gestionnaire des tâches sans Dock). Tests.
- [x] 6. **Essais réels** :
  - sonde `tests/real/force_quit_probe.cpp` : deux apps d'essai à elle (une figée, une vivante), vraie énumération,
    vraie fenêtre, clics et touches réels ; « (ne répond pas) » vu, app figée forcée à quitter après confirmation,
    jamais autre chose ;
  - Dock recompilé et relancé : message de la barre et raccourci enregistrés, fenêtre ouverte puis fermée, sans rien
    forcer.

  Puis documentation, relecture indépendante, corrections, fusion locale.

## Revue : points d'attention
- Une fenêtre fantôme ne doit jamais faire apparaître une app « Gestionnaire de fenêtres » ni cacher l'app figée.
- Jamais d'arrêt d'un processus qui n'est pas l'app choisie : ApplicationFrameHost, l'Explorateur, MacDock.
- Une app qui se ferme seule pendant la confirmation : rien à arrêter, pas d'erreur trompeuse.
- Plusieurs fenêtres et plusieurs processus pour une même app ; une app du Store réduite.
- Changement d'écran ou de DPI, thème clair ou sombre, raccourci désactivé (« off ») ou pris par une autre app.

## Décisions prises en cours de route
- La fenêtre suit le thème des apps de Windows (AppsUseLightTheme), comme une fenêtre ordinaire, et non celui du
  Dock.
- Liste relue chaque seconde sans cache des identités : mesurée sous 60 ms sur ce PC (le Dock l'écrit au journal
  au-delà). Coût si c'est faux : de petits à-coups du Dock pendant que la fenêtre est ouverte.
- La sonde héberge la fenêtre elle-même (même code que le Dock) avec une action gardée : le seul moyen d'essayer
  « Forcer à quitter » pour de vrai sans risquer tes apps. Le Dock, lui, est essayé sans rien forcer.
- Défaut d'identité trouvé à l'essai réel et corrigé dans ce plan : une app à l'AUMID explicite inconnu de
  Windows (un Chrome lancé par un outil) gardait shell:AppsFolder\<AUMID> comme cible : icône générique dans
  le Dock et ici, relance impossible. Désormais son exe, sauf pour une app empaquetée.

## Essais réels
- **Sonde** (`./build.ps1 -Target forcequitprobe`) : deux apps d'essai à elle, l'une figée (Windows a posé sa
  fenêtre fantôme d'elle-même), l'autre vivante, au milieu de tes apps ouvertes. Tout exact : app figée marquée
  « (ne répond pas) » une seule fois, sans app fantôme ; clic, confirmation, Échap, Entrée, ↓ ↑, souris ; les deux
  apps d'essai arrêtées, tes apps listées mais jamais visées (la garde n'a rien refusé).
- **Dock** recompilé et relancé : raccourci alt+win+escape enregistré, fenêtre ouverte par le message de la barre
  avec tes apps, leurs icônes et l'app du premier plan choisie, puis fermée sans rien forcer. Le raccourci lui-même
  n'a pas été tapé (pas de touche ⊞ simulée cette nuit).
- **Images** : `MacDock.exe --force-quit-snapshot` clair, sombre et avec la confirmation, regardées ; le titre
  coupé a été corrigé.
- **Après la relecture** : la sonde vérifie aussi la relecture à la confirmation, la taille fixe, Échap et Alt+F4 :
  tout exact ; le Dock recompilé ouvre et ferme la fenêtre comme avant.

## Relecture (agent indépendant)
Corrigés, chacun avec un test vu en échec avant (sauf le dernier, une course de données) :
- **critiques** :
  - les processus de même AUMID étaient tous visés : forcer une fenêtre PowerShell 7 (du Store) arrêtait tous les
    PowerShell 7 de la session. Désormais seulement pour une app du Store réduite (aucune fenêtre à elle) ;
  - la feuille gardait des PID lus jusqu'à une seconde plus tôt : une app fermée entre-temps, son PID repris, et
    Entrée arrêtait un autre processus. L'app est relue à la confirmation (forceQuitNow), et chaque processus
    ouvert est comparé au programme relu ;
- **importants** : garde fermée (un PID inconnu n'est jamais visé, les processus protégés gardés par leur nom) ;
  les apps qui partagent le processus (applications web d'un navigateur) nommées dans la confirmation ; la
  sélection montrée dès la première image ; le Gestionnaire des tâches quand le Dock ne répond pas ;
- **mineurs remontés** (effet réel) : Alt+F4 sans effet ; fenêtre agrandissable par Aero Snap sans être repeinte ;
  app du Store figée jamais marquée (sa CoreWindow testée désormais) ; la barre relisait settings.json à chaque
  changement d'app, en gênant l'enregistrement du Dock ; Ctrl+C acceptable comme raccourci global depuis un
  fichier ; table des formes d'icônes et fabrique WIC sans verrou entre les fils.

Mineurs gardés pour plus tard :
- Échap tenue ferme la feuille, puis la fenêtre.
- Icônes en route perdues (et non libérées) quand la fenêtre se ferme pendant leur chargement.
- *Relancer* : pas de COM initialisé sur son fil, deux relances de suite possibles, succès annoncé avant d'être
  réel.
- Relecture de la liste : l'Explorateur cherché en ouvrant chaque processus (environ 16 ms) ; l'avertissement
  « liste relue en … ms » répété chaque seconde au-delà de 60 ms.
- Deux apps de même nom échangent leur place selon l'ordre des fenêtres.
- L'Explorateur reconnu à son seul nom de fichier (un autre explorer.exe hors de Windows serait rangé avec lui).
- Le raccourci est affiché dans le menu  même quand Windows l'a refusé au Dock.
- À essayer quand tu seras là : taper vraiment ⌥⊞⎋ (le menu Démarrer ne doit pas s'ouvrir au relâchement de ⊞),
  aussi avec « Alt de gauche joue ⌘ ».
