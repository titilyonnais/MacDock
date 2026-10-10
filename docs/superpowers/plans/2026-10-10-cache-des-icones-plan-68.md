# Un Dock complet dès la première image : cache des icônes sur disque — plan 68

> **Pour les agents :** exécution en ligne, tâche par tâche, tests d'abord. Reprendre à la première tâche non cochée.

**But :** depuis le plan 67, le Dock ne saccade plus pendant que ses icônes se préparent, mais au démarrage ses cases
restent vides jusqu'à leur arrivée (0,85 à 2,3 s), là où macOS montre son Dock complet dès l'ouverture de la session.
Les images déjà mises en forme sont gardées sur disque et montrées tout de suite, puis refaites derrière.

## Constat (étape 1, mesure du mode diagnostic, 10 octobre à 08:28:07)
- **Cases** (apps, Corbeille, bouton Apps, dossiers) prêtes 1 077 ms après la première image ; **aperçus des piles**
  1 212 ms après.
- **19 travaux**, préparés l'un après l'autre sur le fil des icônes ; les plus lents :
  - `soundpad.exe` 354,3 ms ;
  - `explorer.exe` 202,7 ms (premier travail : initialisation du Shell sur le fil comprise) ;
  - aperçu de la pile Téléchargements 134,8 ms ;
  - Chrome 115,2 ms ; Telegram 51,7 ms.
- Ce sont donc les cases qui font attendre, par quelques extractions lentes : le cache les supprime au démarrage.

## Choix
- **Où** : `%LOCALAPPDATA%\MacDock\icon-cache`, cache de la machine (pas des réglages itinérants). Jamais lu ni écrit
  sur le fil du Dock : seulement sur le fil des icônes. Ni lecture ni écriture en rendu hors écran (`--snapshot`, le
  fil n'y démarre pas). Les tests n'écrivent que dans des dossiers temporaires.
- **Clé d'une entrée** : la clé de l'image dans le cache du Dock (taille comprise ; pour une pile, ses fichiers et
  leurs dates) et l'empreinte des réglages des icônes (Tahoe strict, sombre, grille, dossier personnalisé) : un réglage
  changé donne une autre entrée. Nom du fichier : empreinte de la clé ; la clé complète est gardée dans le fichier
  (collision de nom : entrée ignorée).
- **Format** : en-tête (signature, version, taille, longueur de la clé), clé, pixels BGRA prémultipliés. Un fichier
  abîmé, tronqué, d'une autre version ou d'une autre clé est ignoré, puis remplacé.
- **Écriture atomique** : fichier temporaire, puis renommage ; un fichier temporaire abandonné (arrêt pendant une
  écriture) est effacé à la borne suivante.
- **Borne** : 300 entrées et 20 Mo au plus ; au-delà, les plus anciennes (date d'écriture) sont effacées. Une entrée
  encore utilisée et inchangée est « touchée » (date mise à jour) pour rester parmi les récentes.
- **Démarrage** : le fil des icônes lit d'abord sur disque l'image de **toutes** les demandes en file, avant toute
  préparation, et les donne au Dock comme anciennes images (réserve `stale_`) : le Dock est complet dès sa deuxième
  image. Puis il refait chaque icône comme aujourd'hui ; si elle est identique à celle du disque, c'est la même image
  qui devient définitive (rien ne change à l'écran, rien n'est réécrit) ; sinon la nouvelle la remplace et le disque est
  réécrit. La justesse ne repose sur aucune invalidation : au pire, une ancienne image reste affichée le temps de sa
  préparation.
- **Pile** : l'aperçu composé passe par le même cache. Une pile sans aperçu précédent (à froid) montre l'icône de son
  dossier en attendant, jamais une case vide durable ; au renouvellement, l'aperçu précédent reste (plan 67).
- **Générations** : une ancienne image du disque passe par la même génération que le reste : jetée si le cache du Dock
  a été vidé entre-temps.
- **Envoi des icônes à la carte graphique** (13 à 19 ms de rendu sur les premières images) : mesuré avec le cache ;
  étalé seulement si les arrivées groupées l'aggravent.

## Tâches
- [x] 1. **Mesure** (7bd7bda) : durée de chaque travail (test rouge, puis vert), cases et aperçus mesurés à part.
- [x] 2. **Cache sur disque** (`src/icons/icon_disk_cache.*`) : empreinte et clé, format, lecture d'un fichier abîmé,
  écriture atomique, borne. Tests dans un dossier temporaire.
- [x] 3. **File, fournisseur, fil** : lecture du disque avant toute préparation (`IconJobs::nextDiskRead`, `offer`),
  anciennes images rangées dans la réserve (`IconProvider::adoptStale`), remplacement seulement si l'image a changé,
  borne quand le fil est au repos. Tests avec un faux constructeur et un dossier temporaire.
- [x] 4. **Dock** : dossier du cache, anciennes images du disque, ligne « [diag] icônes depuis le disque » ; pile à froid
  avec l'icône de son dossier (test du contrôleur).
- [x] 5. **Mesure réelle**, MacDock relancé deux fois en diagnostic (cache vide, puis plein). Puis relecture
  indépendante, corrections, fusion locale.

## Fait
- **Tâche 2** (58a3470) : `IconDiskCache`, 6 tests rouges sur une ébauche, puis verts (dossiers temporaires).
- **Tâches 3 et 4** (c1ef6d1, a637520) : 5 tests rouges sur des ébauches, puis verts (file, réserve, fil avec disque,
  pile à froid avec son dossier, qui remplace le test du plan 67). Suite : 899 cas, un échec dû à l'environnement.
- **Tâche 5, mesure réelle** (MacDock de la branche relancé deux fois en diagnostic, sans entrée simulée) :
  - cache vide (08:45:45) : « modèle » 0,2 ms ; cases prêtes en 683 ms, aperçus en 816 ms ; 20 travaux, les plus lents
    l'Explorateur 207 ms, l'aperçu de la pile Téléchargements 133 ms, Chrome 54 ms ; 20 fichiers écrits, 2 051 134
    octets ;
  - cache plein (08:46:01) : « icônes depuis le disque : 20 en 28 ms », toutes les cases et la pile dès la deuxième
    image (22 ms après la première) ; « modèle » 0,1 ms ; images fraîches identiques, rien de remplacé à l'écran
    (cases 601 ms, aperçus 726 ms) ; rendu 11,8 et 13,0 ms sur les deux premières images (étalement non nécessaire) ;
  - repos : 22, 11, 10 tours de boucle en 5 s ; la sentinelle ne signale rien.

## Relecture indépendante (opus) : « Avec corrections »
Aucun critique ; fichiers partagés, format, borne, générations et arrêt jugés sûrs.
- **Important, la borne gardait des aperçus morts et effaçait les icônes des apps** : la clé disque d'un aperçu de pile
  contenait ses fichiers ; pendant un téléchargement, chaque renouvellement écrivait un nouveau fichier (~100 Ko),
  jamais relu, et au bout de quelques minutes la borne effaçait les icônes des apps (Dock de nouveau vide au
  démarrage). Corrigé (3db8765) : une seule place par pile et par taille (`iconDiskSlot`), réécrite en place ; au
  démarrage, le dernier aperçu de la pile arrive comme les autres, même si ses fichiers ont changé ; plus aucun chemin
  de fichier dans les clés sur disque. Tests `icon_disk_slot_one_per_stack` et `icon_worker_stack_has_one_disk_slot`
  rouges, puis verts.
- **Corrigés en passant** :
  - lot partiel d'anciennes images : les demandes d'une même image du Dock partent ensemble, et le Dock n'est prévenu
    qu'une fois toutes les demandes lues (`icon_worker_disk_images_arrive_in_one_batch`, `diskReadsPending`) ;
  - préparation ratée après une ancienne image : l'entrée périmée est effacée
    (`icon_worker_failed_build_drops_stale_disk_entry`) ;
  - dossiers temporaires des tests uniques (un ancien dossier pouvait resservir) ; tests de l'écriture atomique (fichier
    tenu ouvert : l'ancienne entrée reste entière) et de la non-réécriture (même fichier sur le volume) renforcés ;
  - commentaire juste sur la trace de chaque travail.
- **Mineurs reportés** (journal) : icône du dossier demandée à chaque démarrage ; ni empreinte des pixels ni nouvel
  essai d'écriture ; pas de version du rendu dans l'empreinte ; écritures et mises à jour de date entre deux
  préparations ; aperçus de piles en clair dans le cache, et cache gardé après la désinstallation.
- **Refusé de juger** (par le relecteur) : `MoveFileExW` sur une destination ouverte avec partage de suppression, la
  fréquence réelle des renouvellements, les mesures, la tenue après une coupure de courant, les antivirus.

Suite complète après les corrections : 907 cas, un échec dû à l'environnement.

## Points pour la relecture
- Fichiers partagés et fil des icônes.
- Écriture atomique ; lecture d'un fichier abîmé.
- Borne.
- Générations face au cache sur disque.
- Arrêt pendant une écriture.
