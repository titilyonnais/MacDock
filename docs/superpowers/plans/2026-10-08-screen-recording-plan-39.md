# ⊞⇧5 et enregistrement de l'écran — plan 39

> **Pour les agents :** exécution en ligne, tâche par tâche, tests d'abord. Reprendre à la première tâche non cochée.

**But :** ⊞⇧5 comme ⌘⇧5 sur macOS.
- Une barre d'outils flottante en bas de l'écran, avec les modes :
  - capturer tout l'écran, une fenêtre ou une zone ;
  - enregistrer tout l'écran ou une zone.
- La vidéo est écrite sur le Bureau, au nom « Enregistrement de l'écran AAAA-MM-JJ à HH.MM.SS.mp4 ».
- On arrête l'enregistrement par une pastille ⏹ avec la durée, ou par ⊞⇧5. Une vignette apparaît ensuite.

**Architecture :**
- `screenshot_logic` (pur) : nom du fichier vidéo, taille de la vidéo (au plus 1920 px de large, dimensions paires pour H.264), touche ⊞⇧5 (`ShotKey::Toolbar`), boutons de la barre (place et clic).
- `screen_recorder.*` : un fil qui copie l'écran ou la zone par GDI (`StretchBlt` HALFTONE vers une DIB à la taille de la vidéo, curseur dessiné par `DrawIconEx`), 30 images par seconde horodatées par QPC, puis Media Foundation (`IMFSinkWriter`, H.264, entrée RGB32) vers le MP4. `stop()` finalise et rend la dernière image (pour la vignette).
- `shot_toolbar.*` : la barre (fenêtre en couches, dessinée par Direct2D, sans activation, exclue des captures).
  - Les modes, puis « Capturer » ou « Enregistrer », puis ×.
  - Échap ferme, par la session clavier du crochet comme le viseur.
- `recording_pill` : pastille ⏹ et durée en haut au centre, cliquable et exclue des captures.
- Dock (`dock_window`) : `WM_APP_SHOT` avec 3 = barre ; les actions de la barre mènent aux chemins existants (`takeScreenShot`, viseur en mode zone ou fenêtre) ou à `startRecording(RECT)`.

## Contraintes
- Les tests n'enregistrent ni l'écran ni le son. Ils n'écrivent que dans le dossier temporaire.
- Le Dock, exclu des captures pour son verre, n'apparaît pas dans les vidéos. C'est une limite documentée.
- Pas de son dans la vidéo, comme le réglage par défaut de macOS.

## Tâches
- [x] 1. Logique : nom, taille, touche ⊞⇧5, boutons de la barre (tests).
- [x] 2. Enregistreur : fil, GDI, Media Foundation ; test hors écran (une petite zone, 10 images, fichier MP4 valide dans le dossier temporaire).
- [x] 3. Barre ⊞⇧5 et pastille ⏹ ; branchement dans le Dock ; vignette à l'arrêt.
- [x] 4. Essai réel en diagnostic, documentation, fusion.
