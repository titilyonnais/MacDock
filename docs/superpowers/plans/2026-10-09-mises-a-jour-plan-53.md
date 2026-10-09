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
- **Où** : l'API publique de GitHub, sans jeton : `api.github.com/repos/titilyonnais/MacDock/releases/latest`. Les
  préversions et les brouillons sont ignorés.
- **Signature** :
  - clé ECDSA P-256 « MacDock Release », créée une fois dans le magasin de clés Windows de l'utilisateur (CNG), non
    exportable : elle ne quitte jamais ce PC ;
  - sa clé publique est inscrite dans MacDock (`src/update/release_key.h`) ;
  - `release.yml` publie la version ; tant qu'elle n'est pas signée, les mises à jour l'ignorent ;
  - `tools/sign-release.ps1 -Tag vX.Y.Z` la télécharge, revérifie l'empreinte de l'installateur, signe
    `SHA256SUMS.txt` et joint `SHA256SUMS.txt.sig` (pas de brouillon : `gh` ne retrouve pas les brouillons par
    étiquette, et une version non signée ne gêne personne en attendant).
- **Téléchargement** (WinHTTP, HTTPS seulement) :
  - `SHA256SUMS.txt` et sa signature d'abord, vérifiée avec la clé publique (CNG) ; mal signée : rien d'autre ;
  - puis l'installateur `MacDock-Setup-X.Y.Z.exe`, dans `%LOCALAPPDATA%\MacDock\updates` ;
  - taille bornée, empreinte SHA-256 vérifiée avant toute exécution ;
  - un fichier qui ne correspond pas est effacé.
- **Installation** : l'installateur en silence (`/VERYSILENT /SUPPRESSMSGBOXES /NORESTART`). Il quitte MacDock,
  remplace les fichiers, puis relance MacDock ; s'il échoue, il relance l'ancien (plan 52). Le lanceur s'arrête
  avec le Dock.
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
- [ ] 4. **Lanceur** :
  - fil de mise à jour, notification et clic ;
  - installation au démarrage, `--check-update`, `--install-update` ;
  - journal.
- [ ] 5. **App Réglages** : « Mise à jour de logiciels » (version, état, Rechercher maintenant, Mettre à jour,
  Rechercher automatiquement).
- [ ] 6. **Essai de bout en bout** :
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
