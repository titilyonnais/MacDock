# Pastilles : fenêtres recouvertes et fenêtres masquées — plan 45

> **Pour les agents :** exécution en ligne, tâche par tâche, tests d'abord. Reprendre à la première tâche non cochée.

**But :** deux mineurs reportés des correctifs du 8 octobre (journal, « Correctifs du 8 octobre, fin de soirée »).

## Constat (dans le code)
- `TrafficWindow::sample` lit neuf points de la barre de titre juste à gauche du calque, sans vérifier qu'ils
  appartiennent à la fenêtre. Sur une fenêtre inactive à moitié recouverte, la couleur de la fenêtre du dessus
  devient celle du fond des pastilles, qui cache les boutons de Windows : un rectangle de la mauvaise couleur.
- Un calque vit tant que sa fenêtre existe, même masquée (zone de notification, fenêtre recyclée). Le crochet
  `EVENT_OBJECT_LOCATIONCHANGE` de son processus, bavard (curseur, caret), reste donc posé tant que la fenêtre existe.

## Tâches
- [x] 1. **Couleur prise sur la fenêtre elle-même** :
  - points candidats de droite à gauche, du calque jusqu'au bord gauche du cadre (au plus 128) ;
  - on garde les neuf premiers où la fenêtre est visible (`WindowFromPoint`, racine = la fenêtre) ;
  - aucun point visible : la couleur d'avant reste ; jamais mesurée : clair ou sombre selon la barre de titre de la
    fenêtre (`DWMWA_USE_IMMERSIVE_DARK_MODE`).

  Test : `captionSampleXs` (tous visibles : les neuf de toujours ; les plus proches recouverts : les suivants ;
  aucun : vide ; cadre trop étroit : vide).
- [x] 2. **Fenêtre masquée pour de bon** :
  - calque caché tout de suite ;
  - relue par la boucle du fil (aucun calque n'est alors en train de traiter un message) : encore masquée, calque
    retiré, avec le crochet de son processus s'il était le dernier ; revenue, calque replacé ;
  - à son prochain `SHOW`, elle retrouve un calque (déjà le cas pour une fenêtre sans calque).

  Test : `onTargetHidden` (immédiat : cacher ; reporté et encore masquée : retirer ; reporté et revenue : replacer).
- [ ] 3. Essai réel (MacDock lancé pour l'essai puis arrêté), documentation, relecture, fusion.

## Essai réel
Fenêtres d'essai à moi, MacDock en diagnostic, lancé puis arrêté ; ancienne barre puis nouvelle :
- barre de titre recouverte par une bande rouge à gauche des pastilles, couleur remesurée : avant, fond des pastilles
  rouge (`E61414`) ; après, celui de la barre de titre (`F3F3F3`) ;
- fenêtre masquée : avant, son calque restait ; après, il est retiré avec le crochet de son processus, et un nouveau
  calque arrive quand elle est montrée de nouveau.
