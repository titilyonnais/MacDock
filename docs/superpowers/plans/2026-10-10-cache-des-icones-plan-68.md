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
- [ ] 2. **Cache sur disque** (`src/icons/icon_disk_cache.*`) : empreinte et clé, format, lecture d'un fichier abîmé,
  écriture atomique, borne. Tests dans un dossier temporaire.
- [ ] 3. **File, fournisseur, fil** : lecture du disque avant toute préparation (`IconJobs::nextDiskRead`, `offer`),
  anciennes images rangées dans la réserve (`IconProvider::adoptStale`), remplacement seulement si l'image a changé,
  borne quand le fil est au repos. Tests avec un faux constructeur et un dossier temporaire.
- [ ] 4. **Dock** : dossier du cache, anciennes images du disque, ligne « [diag] icônes depuis le disque » ; pile à froid
  avec l'icône de son dossier (test du contrôleur).
- [ ] 5. **Mesure réelle**, MacDock relancé deux fois en diagnostic (cache vide, puis plein). Puis relecture
  indépendante, corrections, fusion locale.

## Points pour la relecture
- Fichiers partagés et fil des icônes.
- Écriture atomique ; lecture d'un fichier abîmé.
- Borne.
- Générations face au cache sur disque.
- Arrêt pendant une écriture.
