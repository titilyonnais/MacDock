# Captures d'écran façon macOS — plan 29

> **Pour les agents :** exécution en ligne (superpowers:executing-plans), tâche par tâche, tests d'abord.

**But :** ⊞⇧3 et ⊞⇧4 font comme ⌘⇧3 et ⌘⇧4 sur macOS : fichier PNG sur le Bureau, nommé comme sur un Mac français, et vignette flottante en bas à droite.

**Architecture :**
- Un module `src/screenshot/` :
  - une logique pure (noms, touches, sélection, vignette, images) ;
  - une capture Win32 (copie du bureau composé, ou `PrintWindow`) ;
  - le viseur (fenêtres en couches) et la vignette.
- Le crochet clavier bas niveau du Dock prend les raccourcis, car l'Explorateur les garde (`RegisterHotKey` : erreur 1409).
- Pendant la copie d'écran, le Dock et les pastilles de la barre de menus redeviennent visibles aux captures, puis sont de nouveau exclus. Pour les pastilles, le Dock passe par un message enregistré `MacDockScreenshotReveal`.

**Technologies :** Win32, GDI (`BitBlt`), DWM, WIC (PNG), Direct2D et DirectWrite (étiquette des coordonnées).

## Contraintes générales
- Aucune ressource d'Apple : ni son, ni image, ni curseur. Le curseur appareil photo est dessiné par le code.
- Les tests n'écrivent jamais dans le presse-papiers. Ils ne pilotent ni la souris ni le clavier, et ne capturent pas l'écran.
- Les fichiers sont nommés « Capture d’écran AAAA-MM-JJ à HH.MM.SS.png », avec l'apostrophe typographique.
  - Écrans suivants, ou même seconde : « … (2).png », « … (3).png ».
- Le dossier est le Bureau de l'utilisateur (`FOLDERID_Desktop`), même s'il est redirigé vers OneDrive.
- Les réglages restent compatibles : `"screenshots": true` par défaut ; `false` rend ⊞⇧3 et ⊞⇧4 à Windows.

## Points à surveiller à la relecture
1. Après ⊞⇧3, le menu Démarrer ne doit jamais s'ouvrir au relâchement de ⊞ : une touche neutre `0xE8` est envoyée.
2. Si la capture échoue au milieu, le Dock et les pastilles doivent quand même retrouver leur exclusion. La barre a un minuteur de sécurité de 3 s.
3. La sélection ne doit pas déborder de l'écran où elle a commencé : elle est bornée au moniteur, comme sur macOS.
4. Une fenêtre cachée par une autre, en mode fenêtre, est capturée seule (`PrintWindow`), jamais avec ce qui la recouvre.
5. Plusieurs captures de suite : la nouvelle vignette remplace l'ancienne, et aucun fichier n'est écrasé.

---

### Tâche 1 : logique pure (`src/screenshot/screenshot_logic.*`, dans `LogicSources`)
- `screenshotBaseName(SYSTEMTIME)` → `Capture d’écran 2026-10-08 à 12.52.34`
- `screenshotFileName(base, n)` → `base.png` si n ≤ 1, sinon `base (n).png`
- `uniqueScreenshotPath(dir, base, first, exists)` : premier nom libre à partir de `first`
- `screenshotKey(vk, down, ShotMods, injected)` → `ShotKey { Pass, Screen, Region, Swallow }`
- `screenshotSessionKey(vk, down)` → `ShotSessionKey { Pass, Cancel, ToggleWindow, Swallow }`
- `selectionRect(a, b, bounds)` (rectangle normalisé et borné) et `selectionUsable(r)` (au moins 4 × 4 pixels)
- `thumbnailRect(work, image, scale)` : tient dans 200 × 150 points, jamais agrandie, à 20 points du coin bas droit
- `thumbnailOffset(phase, t, distance)` : glissement d'entrée (0,3 s) et de sortie (0,25 s)
- Images (`BgraImage`, alpha non prémultiplié) :
  - `roundCorners(img, radius)` ;
  - `windowShadowSpec(scale)` ;
  - `withShadow(img, spec)` ;
  - `premultiply(img)` ;
  - `thumbnailPixels(small, scale, w, h, margin)` ;
  - `cameraCursorPixels(size)`.
- Tests (`tests/test_screenshot.cpp`) :
  - nom exact ; numéros ; nom libre ;
  - touches : ⊞⇧3, ⊞⇧4, ⌃ en plus, relâchement avalé, Alt, frappes simulées, chiffres sans ⊞ ;
  - touches de session ;
  - sélection inversée et bornée ;
  - vignette dans la zone de travail et non agrandie ;
  - coins transparents, centre opaque ;
  - ombre plus large que la fenêtre, plus forte dessous ;
  - prémultiplication ;
  - curseur non vide.

### Tâche 2 : réglage `screenshots`
- `Settings::screenshots` (vrai par défaut), lu et écrit dans `settings.json`.
- Test : défaut, lecture de `false`, aller-retour.

### Tâche 3 : capture, visibilité, enregistrement
- `screen_grab.*` :
  - `grabScreen(RECT)` : `BitBlt` du bureau composé, alpha 255 ;
  - `grabWindow(HWND, RECT&)` : `PrintWindow` avec `PW_RENDERFULLCONTENT`, rognée aux bords DWM ;
  - `windowUnobscured(HWND, RECT, ignore)` ;
  - `desktopFolder()` ;
  - `saveScreenshotPng(img, path)`.
- Barre de menus :
  - message `MacDockScreenshotReveal` : 1 rend les pastilles visibles aux captures et arrête l'échantillonnage, 0 les exclut de nouveau ;
  - minuteur de sécurité de 3 s ;
  - `TrafficWindow::setCaptureVisible(bool)`, synchrone, attente bornée à 200 ms.
- Dock : `revealForCapture(dock, lights)` et `concealAfterCapture()`. La capture du verre est mise en pause, puis reprend 150 ms plus tard.

### Tâche 4 : ⊞⇧3 et vignette
- Le crochet clavier prend les raccourcis et poste `WM_APP_SHOT`. La touche neutre `0xE8` empêche le menu Démarrer de s'ouvrir.
- Un fichier par écran ; l'enregistrement se fait sur un fil à part, qui poste `WM_APP_SHOT_SAVED`.
- `ShotThumbnail`, la vignette :
  - elle glisse depuis la droite, reste 5 s (en pause au survol) et repart vers la droite ;
  - un clic ouvre le fichier ; un glisser vers la droite la renvoie ;
  - un glisser dans une autre direction dépose le fichier ailleurs (`SHDoDragDrop`).
- ⌃ en plus : l'image va dans le presse-papiers, sans fichier ni vignette.

### Tâche 5 : ⊞⇧4, le viseur
- Une fenêtre de saisie par écran (en couches, alpha 1, curseur croix, exclue des captures, sans activation).
- La sélection est une fenêtre grise translucide. L'étiquette des coordonnées affiche x et y, puis largeur et hauteur, en points.
- Espace bascule en mode fenêtre :
  - la fenêtre survolée prend un voile bleu ; le curseur devient un appareil photo ;
  - un clic la capture avec ses coins arrondis et une ombre (sans ombre avec Alt).
- Échap annule.

### Tâche 6 : documentation, essai réel, relecture, fusion
- README, journal et feuille de route mis à jour.
- Essai en diagnostic, avec des frappes simulées et une fenêtre du Bloc-notes placée par script.
- Relecture Opus de la branche, correctifs, puis fusion `--no-ff`.
