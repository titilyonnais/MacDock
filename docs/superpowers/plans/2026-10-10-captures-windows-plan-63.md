# MacDock dans les captures de Windows — plan 63

> **Pour les agents :** exécution en ligne, tâche par tâche, tests d'abord. Reprendre à la première tâche non cochée.

**But :** ta remarque du 10 octobre au soir : « le Dock, la barre en haut et tout ne s'affichent pas dans les captures
d'écran Windows » (⊞⇧S ou Impr. écran, l'Outil Capture de Windows 11).

## Constat
- Le Dock (verre vivant), ses menus, Spotlight, Apps, les piles, le sélecteur d'apps (⌥⇥), les pastilles de la barre
  et la pastille du volume sont exclus des captures (`WDA_EXCLUDEFROMCAPTURE`) : sans cela, le verre du Dock se
  capturerait lui-même. La barre elle-même ne l'est que pendant ses échantillons de couleur.
- MacDock sait déjà se montrer à ses propres captures (⊞⇧3, ⊞⇧4, ⊞⇧5) : `DockApp::revealForCapture` gèle le verre
  sur sa dernière image, rend le Dock visible aux captures, prévient la barre (`MacDockScreenshotReveal` : pastilles,
  menus, pastille du volume ; sécurité de 3 s côté barre), puis `concealAfterCapture` défait tout.
- L'Outil Capture de Windows, lui, n'est pas prévenu : il photographie l'écran 100 à 300 ms après la touche, sans MacDock.

## Choix
- Le crochet clavier du Dock voit ⊞⇧S et Impr. écran avant Windows. **Sans retenir ni retarder la touche**, il poste au
  Dock « outil de capture de Windows » ; le Dock se révèle comme pour ses propres captures (WDA_NONE prend effet à la
  composition suivante, ~16 ms).
- **Durée** : au moins 3 s ; prolongée tant que l'Outil Capture est au premier plan (processus `SnippingTool.exe` ou
  `ScreenClippingHost.exe`), relu toutes les 250 ms ; fin 500 ms après son départ ; jamais plus de 2 min. Le verre reste
  gelé pendant ce temps (l'écran l'est aussi sous l'outil). Pendant la révélation, le Dock relance la barre chaque
  seconde (sa sécurité de 3 s ne coupe pas en cours de route) ; si le Dock s'arrête, la barre se referme seule.
- Fenêtres révélées : celles de `revealForCapture`, plus les piles ouvertes et le sélecteur d'apps. Pas le viseur ni la
  barre d'outils de ⊞⇧5, ni les sprites (glisser, fumée).
- Les captures de MacDock pendant une révélation pour Windows ne referment rien avant la fin de celle-ci.
- Rien ne relance la capture du verre tant que le Dock est visible aux captures (sinon il se verrait lui-même).

## Précautions
- Aucune touche retenue, avalée ni rejouée ; aucun fichier écrit ; presse-papiers jamais lu ni modifié par les essais.
- Le crochet clavier est aveugle tant qu'une app en administrateur tient le premier plan (Gestionnaire des tâches
  cette nuit) : le vrai essai avec ⊞⇧S se fait au réveil.

## Tâches
- [x] 1. **Logique pure** (`src/screenshot/windows_tool.*`) : `windowsShotKey(vk, down, repeat, win, shift)` (Impr.
  écran avec ou sans modificateurs, ⊞⇧S ; jamais à la répétition ni au relâchement) ; `ToolReveal` (début, prolongation
  tant que l'outil est devant, fin 500 ms après, minimum 3 s, maximum 2 min, nouvel appui pendant la révélation :
  prolonge) ; `isCaptureToolProcess(nom)`. Tests.
- [ ] 2. **Dock** (`dock_window.*`) : crochet → `WM_APP_TOOL_SHOT` ; révélation tenue (minuterie de 250 ms : outil
  devant ?, relance de la barre chaque seconde) ; `concealAfterCapture` n'éteint rien pendant une révélation pour
  Windows ; `restartCapture`/`resumeCapture` attendent sa fin. Piles et sélecteur : `setCaptureVisible`. Tests de ce qui
  est pur (ordre des décisions) ; le reste à l'essai.
- [ ] 3. **Essai sans clavier** : message enregistré `MacDockTestToolReveal` (même chemin que la touche) ; sonde qui
  copie l'écran elle-même (BitBlt, en mémoire) avant et pendant : le Dock absent avant, présent pendant, absent de
  nouveau après ; aucun fichier, presse-papiers intact.
- [ ] 4. **Documentation** : README (captures de Windows), feuille de route, journal ; vrai essai ⊞⇧S et Impr. écran
  au réveil.

## Limites connues
- ⊞Impr. écran (capture enregistrée tout de suite) et Alt+Impr. écran (fenêtre active dans le presse-papiers) copient
  l'écran sans attendre : la révélation arrive peut-être trop tard, sans retarder la touche.
- Enregistrements et partages d'écran (OBS, Teams, Xbox Game Bar) : non couverts.
