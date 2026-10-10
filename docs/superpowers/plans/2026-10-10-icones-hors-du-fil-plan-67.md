# Icônes du Dock préparées hors de son fil — plan 67

> **Pour les agents :** exécution en ligne, tâche par tâche, tests d'abord. Reprendre à la première tâche non cochée.

**But :** qu'une icône lente ne fasse plus jamais saccader le Dock. Aujourd'hui, ses icônes (extraction Shell et WIC,
mise en forme, aperçus des piles) sont préparées sur son fil, pendant la construction de l'image. Le budget de 10 ms par
image (plan 62) s'arrête entre deux icônes, jamais pendant : une seule icône lente bloque la boucle. Tu as demandé un
Dock « tout le temps fluide ».

## Constat (journal du mode diagnostic, nuit du 10 octobre)
- **Démarrage** de 03:21:41 : première image 170,5 ms de « modèle », puis 20 à 52 ms par image, et 137,5 ms à
  03:21:42.634.
- **Nouvelle app** : 22,5 ms à 01:22:51.483, 20,5 ms à 01:31:41.655 (la fenêtre PowerShell d'essai).
- Une icône d'app (extraction 256 px, forme, ombre) ou un aperçu de pile (vignettes) coûte 20 à 137 ms d'un coup, sur
  le fil qui suit la souris et anime le Dock.

## Choix
- **Un fil des icônes**, propre au Dock : COM en STA, messages pompés pendant l'attente (MsgWaitForMultipleObjectsEx),
  CoUninitialize à la sortie. Il a son propre fournisseur d'icônes (son cache ne sert qu'aux vignettes des piles). Les
  images, des tampons immuables (`shared_ptr<const Image>`), reviennent au Dock par un message (WM_APP_ICONS, un seul en
  route à la fois). Le Dock les range dans son cache, puis demande une image.
- **Seul le rendu courant du Dock** (`renderNow`) passe par la file (`beginAsync` / `endAsync`, à la place du budget).
  Les autres utilisateurs du fournisseur gardent leur comportement d'aujourd'hui, synchrone, sur le même cache :
  - rendu hors écran (`--snapshot`, toujours complet) ;
  - ancrage des piles et des menus ;
  - glisser ;
  - sélecteur ⌥⇥ ;
  - liste d'une pile.
  Apps, Spotlight, Forcer à quitter et les piles en grille ont déjà leur propre fil.
- **Chaque travail emporte une copie des réglages des icônes** (`IconStyle` : Tahoe strict, sombre, grille, dossier
  personnalisé) et la **génération** du cache.
  - Tout vidage (thème, réglages des icônes, écran) fait monter la génération.
  - Un résultat d'une génération dépassée est jeté, et les travaux en file des générations précédentes sont abandonnés.
  - Le fil des icônes vide aussi son propre cache à chaque nouvelle génération.
- **Pas de doublon** : une image déjà en file, en cours ou prête à reprendre n'est pas redemandée.
- **Priorité** : les cases (apps, Corbeille, bouton Apps, dossiers) d'abord, les aperçus des piles ensuite ; à priorité
  égale, dans l'ordre des demandes.
- **Pendant l'attente**, l'ancienne image reste affichée (réserve `stale_`).
  - Sans ancienne image, **la case reste vide** jusqu'à l'arrivée de l'icône, une seule fois : jamais d'image, puis
    vide, puis image.
  - Choisi plutôt qu'une première image retardée de 300 ms : c'est déjà le comportement depuis le plan 62 (les icônes
    arrivent après la première image), et la file les apporte sans bloquer la boucle.
- **Pas de rendu à vide** : en attente d'icônes, le Dock ne redessine plus à chaque tour (le budget le faisait) ; c'est
  l'arrivée des résultats qui demande une image.
- **Arrêt** : la file est vidée, plus aucun résultat n'est publié, puis le fil est rejoint.
  - S'il est pris dans un travail long (vignette sur un partage réseau), on l'attend 300 ms au plus, puis on le laisse
    finir seul : ses données sont partagées, et il ne publie plus rien.
  - MacDock quitte donc aussi vite qu'avant.
- **Repli** : si le fil ne démarre pas, le budget de 10 ms d'aujourd'hui reste en place.

## Tâches
- [ ] 1. **File pure** (`src/icons/icon_jobs.*`) : demande, en cours, prête, reprise ; doublons ; génération dépassée
  jetée ; priorité ; arrêt avec des travaux en attente. Tests rouges sur une ébauche, puis verts.
- [ ] 2. **Fournisseur, mode « hors du fil »** :
  - `IconStyle`, `style()`, `generation()`, `beginAsync` / `endAsync`, `adopt`, `forget()` ;
  - `get`, `composeStack` et `appsButton` demandent au lieu de préparer ;
  - tests sans Shell (faux puits).
- [ ] 3. **Fil des icônes** (`src/icons/icon_worker.*`) : COM STA, attente qui pompe les messages, notification unique,
  arrêt borné. Tests :
  - avec un faux constructeur : ordre, génération, STA, arrêt avec des travaux en attente, rien publié après l'arrêt ;
  - avec le vrai : notepad.
- [ ] 4. **Dock** :
  - fil démarré hors rendu hors écran ;
  - WM_APP_ICONS ;
  - `renderNow` en mode hors du fil (budget en repli) ;
  - arrêt avant la destruction de la fenêtre.
- [ ] 5. **Mesure réelle**, MacDock relancé en diagnostic : démarrage, pile Téléchargements, fenêtre d'essai, repos.
  Puis relecture indépendante, corrections, fusion locale.

## Gardes (comportements d'aujourd'hui, inchangés)
- Rendu hors écran complet.
- Sélecteur ⌥⇥, liste d'une pile et glisser : synchrones, sur le même cache.
- Ancienne image gardée pendant un rechargement.
- Budget de 10 ms en repli ; ses tests restent tels quels.

## Points pour la relecture
- Données partagées entre les fils.
- COM sur le fil des icônes.
- Arrêt.
- Générations.
- Images publiées après la destruction.
- Comportement des autres utilisateurs du fournisseur.
