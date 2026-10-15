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
