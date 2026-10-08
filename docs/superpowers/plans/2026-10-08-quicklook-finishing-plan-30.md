# Coup d'œil, finitions — plan 30

> **Pour les agents :** exécution en ligne (superpowers:executing-plans), tâche par tâche, tests d'abord.

**But :** le Coup d'œil se rapproche de Quick Look :
- vrais aperçus des documents (PDF, Word, Excel, PowerPoint, HTML, polices) ;
- lecture des vidéos et des sons ;
- ouverture en zoom depuis l'icône du fichier ;
- plein écran.

**Architecture :**
- La fenêtre du Coup d'œil passe sur son propre fil d'interface. Les appels COM vers `prevhost.exe` ou Media Foundation peuvent prendre une demi-seconde, et ne doivent jamais figer le Dock.
- Les documents passent par les gestionnaires d'aperçu du Shell (`IPreviewHandler`, hors processus), dans une fenêtre enfant sous la barre d'outils.
- Les vidéos et les sons passent par MFPlay (`MFPCreateMediaPlayer`), dans une fenêtre enfant.
- La miniature actuelle reste le repli en cas d'échec.

**Technologies :** Win32, COM (gestionnaires d'aperçu), Media Foundation (MFPlay), UI Automation (rectangle de l'élément sélectionné), Direct2D.

## Contraintes générales
- Le Dock ne se fige jamais : rien de lent sur son fil.
- Les gestionnaires d'aperçu sont créés en `CLSCTX_LOCAL_SERVER` (`prevhost.exe`) : un gestionnaire défaillant ne peut pas faire tomber MacDock.
- Aucune ressource d'Apple.
- Les tests ne pilotent ni la souris ni le clavier.

## Points à surveiller à la relecture
1. La fermeture pendant un `DoPreview` lent, et le changement de sélection pendant le chargement : aucune fuite, aucun gestionnaire orphelin.
2. Le son d'une vidéo s'arrête toujours à la fermeture, et au passage à un autre fichier.
3. La touche Espace reste au crochet du Dock : la fenêtre enfant d'un aperçu ne doit jamais prendre le focus de l'Explorateur.
4. Plusieurs écrans d'échelles différentes : en plein écran, la fenêtre va sur l'écran de l'Explorateur.
5. Arrêt de MacDock pendant un aperçu : le fil est rejoint, `Unload` et `Shutdown` sont appelés.

### Tâche 1 : logique pure (`quicklook_logic`)
- `quickLookMedia(path)` → `None`, `Video` ou `Audio` (mp4, m4v, mov, wmv, avi, mkv, webm ; mp3, m4a, aac, wav, flac, wma).
- `quickLookDocumentSize(ext)` : portrait pour pdf, doc et docx (620 × 820 points), paysage pour xls, xlsx, ppt, pptx, html (900 × 600), sinon 700 × 560.
- `quickLookZoom(from, to, t)` : rectangle interpolé, ralenti à l'arrivée.
- `quickLookFullscreen(content, monitor)` : image ajustée dans l'écran, centrée.
- Tests : extensions (casse, point dans le dossier), tailles bornées à l'écran, zoom à 0, 1 et mi-chemin, plein écran en portrait comme en paysage.

### Tâche 2 : fil d'interface du Coup d'œil
- `QuickLookWindow` crée son fil, sa boucle de messages et sa fenêtre.
- `show` et `close` postent leurs messages au fil ; `isOpen`, `owner` et `currentPath` sont lisibles depuis le Dock sans course.
- Le fil de chargement des miniatures reste en place.

### Tâche 3 : aperçus des documents
- `PreviewHost` :
  - le CLSID vient de `AssocQueryString(ASSOCSTR_SHELLEXTENSION, ext, "{8895b1c6-b41f-4c1c-a562-0d564250836f}")` ;
  - création en `CLSCTX_LOCAL_SERVER` ;
  - initialisation par `IInitializeWithStream`, sinon `IInitializeWithItem`, sinon `IInitializeWithFile` ;
  - puis `SetWindow`, `DoPreview`, `SetRect` au redimensionnement, `Unload` à la fermeture ;
  - `IPreviewHandlerVisuals` : fond clair ou sombre.
- Le texte garde la vue maison ; les images gardent la miniature.

### Tâche 4 : vidéos et sons
- MFPlay dans une fenêtre enfant ; la lecture démarre seule.
- Un clic met en pause ou relance ; un bouton lecture/pause est ajouté dans la barre.
- La fenêtre prend les proportions de la miniature.
- Le son s'arrête toujours à la fermeture.

### Tâche 5 : animation d'ouverture et plein écran
- Le rectangle de l'élément sélectionné vient d'UI Automation (élément qui a le focus, sur le fil du Coup d'œil). La fenêtre s'ouvre en zoom depuis lui, en 0,2 s ; sans rectangle, depuis le centre, avec un fondu.
- Un bouton plein écran (deux flèches) est ajouté dans la barre ; il fait l'aller et le retour.

### Tâche 6 : documentation, essai réel, relecture, fusion
