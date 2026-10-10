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
- **Durée** : 3 s après la touche si l'Outil Capture n'apparaît pas ; s'il apparaît au premier plan (processus
  `SnippingTool.exe`, `ScreenSketch.exe` ou `ScreenClippingHost.exe`, relu toutes les 250 ms), tant qu'il y reste puis
  500 ms ; jamais plus de 2 min. Le verre reste
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
- [x] 2. **Dock** (`dock_window.*`) : crochet → `WM_APP_TOOL_SHOT` ; révélation tenue (minuterie de 250 ms : outil
  devant ?, relance de la barre chaque seconde) ; `concealAfterCapture` n'éteint rien pendant une révélation pour
  Windows ; `restartCapture`/`resumeCapture` attendent sa fin. Piles et sélecteur : `setCaptureVisible`. Tests de ce qui
  est pur (ordre des décisions) ; le reste à l'essai.
- [x] 3. **Essai sans clavier** : message enregistré `MacDockTestToolReveal` (même chemin que la touche) ; sonde qui
  copie l'écran elle-même (BitBlt, en mémoire) avant et pendant : le Dock absent avant, présent pendant, absent de
  nouveau après ; aucun fichier, presse-papiers intact.
- [x] 4. **Documentation** : README (captures de Windows), feuille de route, journal ; vrai essai ⊞⇧S et Impr. écran
  au réveil.

## Fait
- Commits b17dc0e (logique pure), 359ea41 (Dock, piles, sélecteur), ff74c78 (Impr. écran au seul relâchement),
  553a960 (message d'essai réservé au diagnostic, traces), 503d365 (corrections de la relecture).
- **Essai sans clavier** (sonde `tests/real/tool_reveal_probe.ps1`, copie de l'écran par BitBlt sur la zone du Dock, 3840 x 396 px,
  en mémoire ; aucun fichier, presse-papiers intact ; message d'essai à 05:27:04.105) :

  | Comparé à « avant » | Écart moyen (sur 255) | Pixels qui changent |
  |---|---|---|
  | avant, 300 ms plus tard (bruit) | 0,00 | 0,00 % |
  | 0,4 s après le message | 8,35 | 7,54 % |
  | 2 s après | 8,35 | 7,54 % |
  | 4,2 s après | 0,00 | 0,00 % |

  Luminance moyenne 42,1 / 48,1 / 48,1 / 42,1 : les copies ne sont pas noires (écran HDR).
- **Journal** pendant la révélation : visible à 05:27:04.183 (Dock révélé, verre en pause, barre prévenue), barre
  relancée à +1 s et +2 s, fin après 3 078 ms (aucun outil devant), Dock de nouveau exclu, capture du verre reprise
  157 ms plus tard ; aucune reprise du verre pendant la révélation. Après les corrections (05:54:33) : mêmes chiffres de
  la sonde, Dock visible 28 ms après le message (78 avant), fin après 3 015 ms, verre repris 156 ms plus tard.
- Le vrai essai avec ⊞⇧S et Impr. écran se fait au réveil : le crochet clavier est aveugle tant que le Gestionnaire
  des tâches, en administrateur, tient le premier plan.

## Relecture indépendante (opus)
Verdict « avec corrections », aucun critique. Corrigé (503d365), rouge puis vert quand une décision pure le permet :
- **Important** : Dock masqué (plein écran, masquage automatique) : à la fin de la révélation, la capture du verre
  repartait et tournait pendant tout le plein écran. **Important** : révélation finie pendant le viseur ⊞⇧4/⊞⇧5,
  Mission Control ou l'Exposé : la reprise était abandonnée, verre figé jusqu'au prochain menu. Les deux passent par
  `shotResume` (tout de suite, plus tard, jamais), testée (`shot_resume_after_a_capture`, rouge puis vert).
- **Important** : tâches 3 et 4 : sonde rangée dans `tests/real/tool_reveal_probe.ps1`, documentation faite ; le vrai
  essai au clavier reste pour le réveil.
- **Recommandation retenue** (le temps compte : l'outil copie l'écran 100 à 300 ms après la touche) : Dock visible
  avant l'arrêt de la capture du verre, barre prévenue sans attendre, plus de DwmFlush ; délai touche → Dock visible
  tracé. Mesuré avec le message d'essai : 28 ms au lieu de 78.
- **Mineurs corrigés aussi** : piles et sélecteur prennent l'image à chaque avis (leur verre pouvait rester figé pour de
  bon) ; Dock exclu de nouveau seulement si le verre l'exige encore ; minuterie tardive ignorée ; crochet qui teste la
  touche avant de lire les modificateurs.
- **Durée** : le plan disait « au moins 3 s » ; c'est 3 s si l'outil n'apparaît pas, sinon tant qu'il est devant puis
  500 ms (un Échap tout de suite finit plus tôt). Test `windows_tool_reveal_tool_gone_early`.

## Mineurs reportés
- Capture « fenêtre » de MacDock (⊞⇧4, Espace) pendant une révélation : le Dock apparaît par-dessus la fenêtre.
- Enregistrement ⊞⇧5 pendant une révélation : le Dock (verre figé), les pastilles et les menus apparaissent dans la
  vidéo, puis disparaissent.
- Relance de la barre chaque seconde : elle refait toute sa révélation (aller-retour avec le fil des pastilles) et
  écrit une ligne de trace par seconde ; un message « prolonger » suffirait.
- Réglages relus pendant une révélation : `restartCapture` passe le verre en dépoli avant de retenir la reprise.
- Le branchement du Dock (ordre des appels de la révélation) n'est vérifié que par la sonde et la relecture.

## Limites connues
- ⊞Impr. écran (capture enregistrée tout de suite) et Alt+Impr. écran (fenêtre active dans le presse-papiers) copient
  l'écran sans attendre : la révélation arrive peut-être trop tard, sans retarder la touche.
- App en administrateur au premier plan : le crochet clavier ne voit pas la touche, MacDock reste exclu de la capture.
- Enregistrement vidéo de l'Outil Capture : couvert seulement tant qu'il est au premier plan, et 2 min au plus.
- Enregistrements et partages d'écran (OBS, Teams, Xbox Game Bar) : non couverts.
- Impr. écran quand le réglage de Windows « ouvrir l'Outil Capture » est coupé : copie immédiate dans le
  presse-papiers, comme ⊞Impr. écran.
- Jamais révélés : le viseur et la barre de ⊞⇧5, la vignette flottante, les sprites (glisser, fumée) et la surface du
  génie pendant une réduction.
