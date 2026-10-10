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
- [ ] 1. « Système » : la recherche ne descend pas dans une barre de titre. Tests : arbre factice façon VMware (barre de
  titre seule : aucun titre), barre de titre et vraie barre (titres de la vraie). Essai réel : VMware devant, barre en
  trace.
- [ ] 2. Séance de menus : app changée → fin ; mise en page attendue → refaite, titre retrouvé. Test de la règle ; essai
  réel.
- [ ] 3. Bascule instantanée : graphismes gardés, sans fondu entre titres, relais sans trou. Mesure avant / après
  (rafales), tests de la fenêtre de menu.
- [ ] 4. Première ouverture : mesure (traces de durée), récents seulement pour le logo, puis mesure.
- [ ] 5. Quitter une app : reproduction (vraies apps, rafales, traces), causes, corrections testées.
- [ ] 6. Relecture indépendante, corrections, fusion locale.
