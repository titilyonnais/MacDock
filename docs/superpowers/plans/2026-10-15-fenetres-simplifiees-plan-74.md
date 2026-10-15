# Fenêtres simplifiées, installateurs autonomes (plan 74) — version 0.54.0

**Demande (15 octobre) :** « le système des trois bulles de couleur de macOS pour agrandir, réduire… tu ne vas pas le
faire, c'est beaucoup trop compliqué : supprime tout et laisse tel quel » ; « que le script télécharge lui-même la
police SF Pro et l'installe de façon smooth, et de même pour les angles arrondis » ; « sur les toutes petites fenêtres,
genre Windows R, ça rend pas très bien les contours » ; nouvelle version publiée sur GitHub.

## Tâches
- [x] 1. **Pastilles retirées** : calques sur les fenêtres des autres apps (traffic_window, traffic_lights), réglage
  `trafficLights`, menu de la pastille verte et son plein écran (toggleFullscreen), instantanés `--lights-snapshot` et
  `--zoom-snapshot`. Gardés : apparence des fenêtres (aides de lecture déplacées dans window_info), Remplir au lieu
  d'agrandir, pastilles propres aux fenêtres de MacDock (Réglages, Forcer à quitter). Tests de l'éligibilité repris dans
  test_window_look (styledWindow).
- [x] 2. **Petits coins** : mesure du rendu de DWM de 8 à 26 pt sur une boîte de dialogue d'essai (taille d'Exécuter).
  `useSmallCorners` : taille fixe ou plus petit côté sous 300 pt → DWMWCP_ROUNDSMALL, que le mod arrondit à 12 pt ;
  seulement si le mod est actif avec un petit rayon d'au moins 8 pt (sans lui, 4 pt de Windows).
- [x] 2. **Installateurs autonomes** (windhawk-common.ps1) : Windhawk par winget s'il manque (accord demandé) ;
  coins-macos.ps1 télécharge la source du mod « Custom Window Corner Radius » (commit 16834cd, SHA-256 vérifié), la
  compile et l'enregistre comme Windhawk ; install-macdock-look.ps1 télécharge SF Pro sur le site d'Apple après accord
  sur sa licence, l'ouvre avec un 7-Zip portable (MSI officiel 25.01, SHA-256 vérifié, extrait par `msiexec /a`) et
  installe SF Pro Text et Display pour tout le PC.
- [x] 3. Version 0.54.0, documents, relecture, fusion, publication.

## Décisions
- Mod public plutôt qu'un mod maison dans dwm.exe : il est maintenu et se retire seul si DWM plante ; sa licence (GPL)
  n'entre pas dans le dépôt (téléchargé à l'installation).
- 26 pt gardé pour les fenêtres ordinaires (rayon des fenêtres à barre d'outils de Tahoe) ; 12 pt pour les petites.
- SF Pro n'est jamais dans le dépôt ni dans l'installateur de MacDock : téléchargée chez Apple sur le PC, avec accord.

## Essais
- Suite : 916 tests réussis. Sonde réelle de Remplir : exacte.
- Scripts essayés sans droits administrateur : coins-macos.ps1 -DryRun, compilation des deux mods, empreinte fausse
  refusée, chaîne SF Pro complète (36 polices extraites, rien installé).

## Relecture indépendante (opus : prêt avec corrections, aucun critique) et corrections
- **Sortie de winget** (important) : elle se mêlait à la valeur renvoyée, un échec passait pour une réussite. Envoyée à
  l'écran, vrai booléen.
- **Délai de 3 min de l'app Réglages** (important) : trop court pour télécharger SF Pro ; 30 min pour l'installation d'un
  mod (`ActionCommand::waitMs`, test).
- **Fichiers vérifiés puis exécutés en administrateur depuis %TEMP%** (important) : remplaçables par un programme de la
  session entre la vérification et l'usage. Dossier de travail à la racine de ProgramData, réservé aux administrateurs
  et à SYSTEM, vérifié vide après la pose des droits (source du mod de coins, 7-Zip, SF Pro, en-tête de compilation).
- **Licence de SF Pro** (important) : le message dit maintenant clairement que le contrat d'Apple la réserve aux
  maquettes pour les systèmes d'Apple, et demande un accord en connaissance de cause.
- **README** (important) : passages sur les feux tricolores et `trafficLights` retirés ou réécrits.
- Mineurs corrigés au passage : fenêtres réduites exclues des petits coins (test), mod de coins actif seulement avec
  dwm.exe dans l'inclusion de Windhawk, messages justes sans Windhawk, fichiers temporaires toujours supprimés, critère
  « mod absent » identique à blanc et en vrai, commentaires orphelins.

## Reportés
- Petits coins recalculés seulement au premier plan (une fenêtre redimensionnée garde les siens jusque-là).
- `smallRadius` à 12 touche aussi les menus contextuels de Windows (DWMWCP_ROUNDSMALL) : à regarder sur ton PC.
- Restes morts côté Dock : classe `MacMenuBarLights`, messages de la pastille jaune (génie à l'appui) ;
  `Builder::tile` et les rangées d'icônes des menus n'ont plus d'appelant hors tests.
- La source du mod de police est dans le dossier de MacDock (modifiable par l'utilisateur) et compilée en
  administrateur : comportement d'avant ce plan.
