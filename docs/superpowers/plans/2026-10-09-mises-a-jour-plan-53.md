# Mises à jour automatiques — plan 53

> **Pour les agents :** exécution en ligne, tâche par tâche, tests d'abord. Reprendre à la première tâche non cochée.

**But :** MacDock se met à jour tout seul depuis les versions publiées sur GitHub (plan 52). L'utilisateur ne
réinstalle plus rien sur ses PC. « Teste bien et sois sûr que ça marche » : essai de bout en bout sur de vraies
versions publiées.

## Choix de l'utilisateur (9 octobre)
- **Proposer, puis installer** : la version est téléchargée et vérifiée en arrière-plan, puis une notification propose
  de redémarrer MacDock maintenant. Sinon, elle s'installe au prochain démarrage de MacDock.
- **Signer avec une clé de ce PC** (après la relecture du plan 52) : l'empreinte prouve qu'un installateur est intact,
  pas qu'il vient de lui. Chaque version est signée sur son PC ; MacDock refuse toute version mal signée. Clé perdue :
  une réinstallation à la main par PC, accepté.

## Choix
- **Qui** : le lanceur (`MacDockLauncher.exe`), qui tourne toujours et démarre MacDock.
- **Quand** : une minute après le démarrage, puis toutes les 12 heures. L'app Réglages permet aussi « Rechercher
  maintenant », et l'option « Rechercher automatiquement » coupe les recherches de fond.
- **Où** : l'API publique de GitHub, sans jeton : la liste `api.github.com/repos/titilyonnais/MacDock/releases?per_page=20`
  (et non `releases/latest` : une version tout juste publiée, pas encore signée, ne doit pas cacher la précédente,
  signée). Les préversions (drapeau de l'API ou numéro « -rc.N », le seul signé) et les brouillons sont ignorés.
- **Quelle copie** : seule celle que l'installateur a posée (dossier noté dans sa clé de désinstallation). Une copie
  compilée à la main ou la variante d'essai ne se met jamais à jour (elle installerait la vraie ailleurs).
- **Signature** :
  - clé ECDSA P-256 « MacDock Release », créée une fois dans le magasin de clés Windows de l'utilisateur (CNG), non
    exportable : elle ne quitte jamais ce PC ;
  - sa clé publique est inscrite dans MacDock (`src/update/release_key.h`) ;
  - `release.yml` publie la version ; tant qu'elle n'est pas signée, les mises à jour l'ignorent ;
  - `tools/sign-release.ps1 -Tag vX.Y.Z` vérifie d'où vient la version (étiquette sur le même commit qu'ici, fichiers
    envoyés par le run de `release.yml` pendant ce run, aucun autre run récent d'un commit inconnu ici, une version
    vient de `main`), la télécharge, revérifie l'empreinte de l'installateur et que `SHA256SUMS.txt` ne contient que sa
    ligne, signe ce fichier et joint `SHA256SUMS.txt.sig` (pas de brouillon : `gh` ne retrouve pas les brouillons par
    étiquette, et une version non signée ne gêne personne en attendant) ;
  - MacDock refuse un `SHA256SUMS.txt` signé qui contient autre chose que la ligne de l'installateur proposé.
- **Téléchargement** (WinHTTP, HTTPS seulement) :
  - `SHA256SUMS.txt` et sa signature d'abord, vérifiée avec la clé publique (CNG) ; mal signée : rien d'autre ;
  - puis l'installateur `MacDock-Setup-X.Y.Z.exe`, dans `%LOCALAPPDATA%\MacDock\updates` ;
  - taille bornée, empreinte SHA-256 vérifiée avant toute exécution ;
  - un fichier qui ne correspond pas est effacé.
- **Installation** : l'installateur en silence (`/VERYSILENT /SUPPRESSMSGBOXES /NORESTART`). Il quitte MacDock,
  remplace les fichiers, puis relance MacDock ; s'il échoue, il relance l'ancien (plan 52). Le lanceur s'arrête
  avec le Dock. Au démarrage, un relais (`cmd.exe`, sans fenêtre) attend la fin de l'installateur et relance le
  lanceur, même si l'installateur s'est arrêté tôt.
- **Arrêt** : une recherche en cours ne retarde jamais l'arrêt de MacDock (installateur, « Quitter MacDock ») : elle
  s'interrompt (attente du verrou, téléchargement), et le lanceur ne l'attend pas plus de 3 s. `--check-update`
  s'arrête à l'ordre d'arrêt ; l'installateur attend aussi le verrou des mises à jour.
- **Notification** : bulle de Windows (icône de notification du lanceur, relayée dans la barre des menus) : « MacDock
  X.Y.Z est prêt — clique pour redémarrer MacDock et l'installer ». Clic : installation tout de suite. Sans clic,
  installation au prochain démarrage, avant que le Dock apparaisse.
- **État** : `%APPDATA%\MacDock\update.json` (dernière recherche, version prête et son empreinte, essai déjà tenté, option
  automatique, dernière erreur). L'app Réglages le lit.
- **Jamais de boucle** : une installation tentée qui n'a pas pris (même version au démarrage suivant) est abandonnée.
  Elle sera retéléchargée à la recherche suivante.
- **Essais** (jamais en usage normal) :
  - `--check-update` : recherche, téléchargement, vérification, puis code de sortie ;
  - `--install-update` : installe la version prête ;
  - `MACDOCK_UPDATE_PRERELEASE=1` : prend aussi les préversions ;
  - `MACDOCK_UPDATE_DELAY` : délai avant la première recherche.

## Tâches
- [x] 1. **Logique pure** (`src/update/update_logic.*`) :
  - `parseReleases` : JSON de l'API, version, préversion, brouillon, ressources ;
  - `pickUpdate` : la plus récente, plus récente que la version courante, préversions selon l'option ;
  - `expectedSha256` : lecture de `SHA256SUMS.txt` ;
  - `checkDue` : délais ;
  - `UpdateState` lu et écrit en JSON ;
  - `startupAction` : installer, abandonner une tentative ratée, ou rien.

  Tests sur de vrais exemples de l'API de GitHub.
- [x] 2. **Réseau, empreinte et signature** (`src/update/update_net.*`, `update_sign.*`) :
  - `httpsGet` et `httpsDownload` (WinHTTP, redirections HTTPS, taille bornée, délais) ;
  - `sha256File` (CNG) ;
  - `verifySignature(données, signature, clé publique)` (ECDSA P-256, CNG).

  Tests : empreintes connues (vecteurs du NIST) ; signature d'une clé jetable vérifiée, refusée si un octet change ou
  avec une autre clé ; téléchargement réel de la release 0.52.0 (si le réseau manque, le test le dit sans échouer).
- [x] 3. **Signature des versions** : clé créée, clé publique inscrite, `tools/sign-release.ps1`.
- [x] 4. **Lanceur** :
  - fil de mise à jour, notification et clic ;
  - installation au démarrage, `--check-update`, `--install-update` ;
  - journal.
- [x] 5. **App Réglages** : « Mise à jour de logiciels » (version, état, Rechercher maintenant, Mettre à jour,
  Rechercher automatiquement).
- [x] 6. **Essai de bout en bout** (fait autrement, voir « Décisions ») :
  - la 0.53.0 publiée, installée dans un dossier d'essai ;
  - une préversion 0.53.1-rc.1 publiée pour l'essai ;
  - copie d'essai lancée avec `MACDOCK_UPDATE_PRERELEASE=1` : téléchargement, vérification, notification, clic
    simulé, installation, nouvelle version relancée ;
  - installation au démarrage suivant ;
  - empreinte falsifiée ou signature absente refusées ;
  - nettoyage : copie désinstallée, préversion et étiquette supprimées.

  Puis documentation, relecture, fusion, publication de la 0.53.0.

## Revue : points d'attention
- Pas de réseau, GitHub injoignable ou limite de l'API atteinte (60 requêtes par heure) : silence, nouvel essai plus
  tard, rien de cassé.
- Installateur corrompu, tronqué ou remplacé, ou version publiée sans la bonne signature : jamais exécuté.
- Deux lanceurs, ou l'app Réglages et le lanceur en même temps : un seul téléchargement, état cohérent.
- PC éteint pendant un téléchargement : fichier partiel jamais pris pour bon.
- Version installée plus récente que la dernière publiée (compilée à la main) : rien proposé.

## Décisions prises en cours de route
- Tâche 2 : pas de téléchargement réel dans les tests automatiques (ils tournent aussi en intégration continue, sans
  réseau garanti) ; les vrais téléchargements sont essayés de bout en bout. Coût si c'est faux : une régression du
  réseau vue à l'essai réel plutôt qu'aux tests.
- Tâche 6 : essai sur des préversions 0.53.0-rc.N publiées depuis une branche jetable (seul `version_defs.h` change),
  et non sur la 0.53.0 publiée puis une 0.53.1-rc.1 : la 0.53.0 ne se publie qu'une fois essayée, et les préversions
  portent le même code. La 0.53.0 publiée est vérifiée ensuite par une copie rc.6 installée, qui doit la trouver, la
  télécharger, la vérifier et l'installer.
- Liste des versions plutôt que `releases/latest` (essai v1) : une version tout juste publiée, pas encore signée,
  cachait la précédente.
- `MACDOCK_KEEP_THEME=1` (environnement) plutôt qu'un paramètre du désinstalleur, qui se relance sans les paramètres
  maison (essai v1).
- Clé logicielle gardée plutôt que le TPM (relecture, mineur) : un TPM du processeur (fTPM) peut être effacé par une
  mise à jour du BIOS, ce qui obligerait à réinstaller à la main sur chaque PC ; et un programme malveillant sur ce PC
  pourrait de toute façon pousser du code que la signature couvrirait. Coût si c'est faux : une clé copiée par un
  programme malveillant sous ce compte signerait des versions jusqu'au changement de clé (réinstallation à la main
  partout).

## Essais réels
- **v1** (préversions rc.1 à rc.3) : la fausse signature est refusée, la préversion non signée ignorée. Deux défauts
  trouvés et corrigés (liste des versions, `MACDOCK_KEEP_THEME`). Constat : l'API de GitHub sans jeton est mise en
  cache 60 s, et une signature remplacée reste servie quelques minutes : attendre une minute après la signature.
- **v2** : notification, clic, installation et relance ; installateur altéré jamais lancé ; installation au démarrage.
- **Après la relecture** :
  - relais du démarrage avec un faux installateur qui échoue aussitôt (copie installée dans un dossier d'essai) :
    MacDock ne revenait pas avant le correctif, revient après ;
  - signature à blanc de la vraie v0.52.0 (provenance vérifiée), puis des préversions rc.5 et rc.6 avec tous les
    contrôles.
- **v3** (rc.4 → rc.6, `tests/real/update_e2e.ps1`), tout exact :
  - A. notification et clic : rc.6 installée et relancée, état nettoyé ;
  - B. installateur altéré jamais lancé ; installation au démarrage par le relais ;
  - C. bouton *Installer* (`--install-update`) : installée et relancée, démarrage avec Windows pas touché ;
  - D. recherche bloquée sur le verrou : lanceur arrêté en 0,5 s, `--check-update` arrêté par l'ordre d'arrêt, rien
    d'écrit ;
  - E. installateur lancé pendant une recherche : il attend, abandonne (code 7, 34 s) sans rien remplacer, et
    MacDock revient ;
  - F. copie compilée à la main : aucune recherche, Réglages dit pourquoi.

  Seul le nettoyage du script a raté : il gardait le verrou des mises à jour ouvert (désinstallation refusée). Script
  corrigé, copie d'essai désinstallée à la main. Constat au passage : une désinstallation silencieuse qui ne peut pas
  arrêter MacDock affiche quand même sa boîte d'erreur (mineur gardé).

## Relecture (agent indépendant)
Corrigés, chacun avec un test vu en échec avant le correctif :
- **critique** : « Installer » de Réglages (`--install-update`) était pris pour `--install` : rien d'installé, et le
  démarrage avec Windows rallumé (arguments désormais comparés en entier) ;
- **importants** :
  - la signature valait pour tout `SHA256SUMS.txt` (une ligne glissée avant la signature aurait fait accepter un autre
    installateur) : une seule ligne acceptée, octet pour octet côté script ;
  - le script de signature signait ce que GitHub lui donnait : provenance vérifiée (étiquette, run de `release.yml`,
    auteur et date des fichiers, autres runs, `main`) ;
  - le lanceur pouvait rester bloqué à l'arrêt (arrêt demandé avant que le fil soit prêt) ;
  - une recherche en cours retardait l'arrêt : l'installateur abandonnait au bout de 30 s et MacDock ne revenait pas ;
    pareil pour Réglages fermé pendant une recherche, et pour `--check-update` qui verrouillait le lanceur ;
  - au démarrage, MacDock ne revenait que par l'installateur : relais ;
  - `update.json` réécrit à l'aveugle (« Rechercher automatiquement » rallumé, verrou ignoré) ;
- **mineurs remontés** : préversion décidée par le numéro signé, pas seulement par l'API ; seule la copie installée
  se met à jour (isolation des essais, PC de développement).

Mineurs gardés pour plus tard :
- `launchInstaller` ne revérifie pas que la version prête est plus récente ni que son empreinte notée est non vide
  (un `update.json` abîmé par ce même compte) ; l'installateur part même si la tentative n'a pas pu être notée ;
  fichier échangeable par ce compte entre la vérification et le lancement ; dossiers à la racine si le dossier du
  profil est introuvable.
- Une relance de `release.yml` garde l'ancienne signature : « signature invalide » jusqu'à la nouvelle signature.
- Recherche ratée (hors ligne à la première minute) : la suivante 12 h plus tard ; API appelée toutes les heures tant
  qu'une version est prête.
- Après l'abandon d'une installation ratée, Réglages dit « à jour » (rien dans `lastError`) ; le dossier `updates`
  n'est pas effacé à la désinstallation.
- Réglages : « est prête » / « est prêt » ; « Installée au prochain démarrage » à préciser ; erreurs brutes
  (« HTTP 403 ») ; pas d'indicateur pendant *Rechercher* ; état pas rafraîchi tout de suite quand le lanceur trouve
  une version ; `MACDOCK_UPDATE_DIR` ignoré par Réglages.
- Notification : icône retirée avant de savoir si l'installation part ; double clic ; perdue après un redémarrage de
  l'Explorateur.
- Pas de test automatique de `check()` avec un réseau simulé.
- Désinstallation silencieuse qui ne peut pas arrêter MacDock : la boîte d'erreur du `[Code]` s'affiche quand même
  (`/SUPPRESSMSGBOXES` ne la cache pas) et attend un clic.
