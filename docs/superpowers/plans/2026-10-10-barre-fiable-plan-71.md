# Barre de menus fiable et vive — plan 71

> **Pour les agents :** exécution en ligne, tâche par tâche, tests d'abord. Reprendre à la première tâche non cochée.

**But :** tes retours du 10 octobre sur la barre : « toute buggée », titres qui se décalent, tantôt Édition et
Présentation seules, tantôt Fenêtre et Aide en plus ; dans VMware, « Système » ouvre un encart qui fait tout bugger ;
menus lents à s'ouvrir, et au glisser d'un titre à l'autre, nouveau menu tardif ou absent ; en quittant une app, titres
qui clignotent, titres décalés, pastilles qui sautent.

## Causes établies (sondes et essais du 10 octobre)
1. **« Système » chez VMware** : la barre de titre de VMware expose son menu système avec l'AutomationId « MenuBar »
   (les fenêtres ordinaires : « SystemMenuBar », exclu). La recherche de la barre de menus de l'app (UI Automation,
   4 niveaux) le prend : la barre affiche « VMware Workstation · Système · Fenêtre ». Un clic sur « Système » déplie le
   vrai menu système de Windows (ta photo 2 : Restaurer, Déplacer…), boucle modale chez VMware, attente jusqu'à 2,5 s.
   Les vrais menus de VMware (Fichier, Modifier, Afficher, VM, Onglets, Aide) sont des entrées d'une barre d'outils.
2. **Titres décalés, Fenêtre et Aide absents** (ta photo 1 : « Claude » centré dans une case de ~160 pt, celle de
   « VMware Workstation », 4 cases visibles) : `openMenu` reconstruit les menus à chaque titre parcouru sans refaire la
   mise en page ; si l'app active change pendant une séance de menus, la mise en page reste celle de l'ancienne app
   jusqu'à la fin de la séance.
3. **Bascule au survol lente** (fenêtre d'essai à vraie barre Win32) : survol vu tout de suite (crochet) ; ancien menu
   fermé ~40 ms après ; nouveau menu : premiers pixels ~140 ms, complet ~300 ms ; ~90 ms sans menu entre les deux.
   Chaque menu recrée Direct2D, DirectComposition, DirectWrite, le verre et la duplication de l'écran (~75 ms), puis un
   fondu de 0,18 s.
4. **Première ouverture après le démarrage de la barre : ~0,9 s** (suivantes ~0,13 s) : à mesurer finement ; suspect,
   les éléments récents et leurs icônes préparés pour chaque menu alors que seul le menu du logo les montre.
5. **En quittant une app** : non reproduit sur une fenêtre d'essai (une seule bascule propre en ~80 ms) ; tes trois
   symptômes : titres qui clignotent, titres décalés (cause 2), pastilles qui sautent. À reproduire avec de vraies apps
   (fermeture par la croix, Alt+F4, « Quitter » de la barre), rafales de captures et traces.

## Choix
- La recherche de la barre de menus ne descend jamais dans une barre de titre (TitleBar) ; VMware prend alors les menus
  génériques (ses vrais menus, dans une barre d'outils, ne se lisent qu'en dépliant ses propres menus : reporté).
- Séance de menus : l'app change → la séance se ferme (comme macOS) ; mise en page attendue (horloge, icônes) → refaite
  avant le titre suivant, le titre retrouvé par son texte.
- Bascule : graphismes et capture de l'écran gardés le temps de la séance de la barre ; d'un titre à l'autre, pas de
  fondu, et l'ancien menu reste jusqu'au premier rendu du nouveau. Cible : nouveau menu complet en moins de 40 ms.
- Récents et leurs icônes : seulement pour le menu du logo.

## Tâches
- [x] 1. « Système » : la recherche ne descend pas dans une barre de titre. Tests : arbre factice façon VMware (barre de
  titre seule : aucun titre), barre de titre et vraie barre (titres de la vraie) (a091c33). Vérification réelle (vrai
  code de la barre, lecture seule) : 0 titre pour les deux fenêtres de VMware (avant : « Système »).
- [x] 2. Séance de menus : app changée → fin ; mise en page attendue → refaite, titre retrouvé. Test de la règle
  (8be249b).
- [x] 3. Bascule instantanée : graphismes gardés, sans fondu entre titres, relais sans trou (b3edb2a). Mesure réelle
  (fenêtre d'essai, rafales, souris laissée par toi) : nouveau menu complet 30 à 45 ms après le survol (avant : premiers
  pixels ~140 ms, complet ~300 ms, ~90 ms sans menu) ; « choix » → panneau en verre réel en ~2 ms (avant ~75 ms).
- [x] 4. Première ouverture (f532343) : mesures (trace, tes clics) : ~70 ms de fenêtre de menu à la première ouverture
  (0,9 s ce matin : les icônes des apps récentes sont prêtes dès le démarrage de la barre, 144 ms avant tout clic), 2 ms
  de préparation d'un titre à l'autre ; les icônes des documents récents (~90 ms au premier menu) ne sont plus préparées
  que pour le menu du logo.
- [x] 5. Quitter une app (d0ae632) : enregistrement pendant que tu fermais la VM : Windows a donné la main au Terminal
  caché derrière VMware, puis à Claude 265 ms plus tard ; la barre affichait les deux. Quand la fenêtre d'app active
  vient de se fermer ou de se cacher, la barre (titres et pastilles) attend que le premier plan se pose, au plus
  350 ms ; un changement ordinaire passe tout de suite.
- [x] 6. Relecture indépendante (opus : prêt avec corrections, aucun critique), corrections, fusion locale.

## Relecture indépendante (opus) et corrections
- **Attente du premier plan sur des changements ordinaires** (important) : elle partait aussi à la fermeture d'un
  dialogue, de Spotlight, d'Alt+Tab ou d'une fenêtre de la zone de notification (pastilles grises un instant de trop).
  Seules les fenêtres adoptées par la barre comptent, et un dialogue qui rend la main à sa fenêtre principale passe
  tout de suite ; l'attente ne dépasse plus 350 ms (1dac1e2).
- **Menu ouvert pendant l'attente** (important) : il visait la fenêtre fermée ; l'attente est appliquée avant d'ouvrir
  un menu (1dac1e2).
- **Test de la séance gardée** (mineur regradé : il pouvait fermer un menu ouvert de ta barre pendant les tests) :
  limité à ses propres menus (1dac1e2).

## Reportés
- Après une remise en page pendant une séance de menus : icône d'état retrouvée par sa place (pas par sa sorte), titre
  introuvable qui ferme la séance, surbrillance redessinée sur l'ancienne case.
- Séance gardée détruite après la fin du menu (ancien panneau figé un instant) ; état « visible aux captures » non remis
  à zéro entre deux titres (⊞⇧3 entre deux titres : verre figé jusqu'à la fin de la séance).
- Une vraie barre de menus placée dans une barre de titre (WinUI 3 ?) n'est plus lue ; coût d'un appel d'UI Automation
  de plus par élément visité.
- Les vrais menus de VMware (dans une barre d'outils) : VMware prend les menus génériques.
- Deux tests de rendu du texte (chiffres de l'horloge, majuscules centrées) échouent aussi sur `main` depuis le
  redémarrage du PC du 11 octobre : à étudier.
