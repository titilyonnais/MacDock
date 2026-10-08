# App Réglages MacDock (2/3) — plan 42

> **Pour les agents :** exécution en ligne, tâche par tâche, tests d'abord. Reprendre à la première tâche non cochée.

**Spec :** `docs/superpowers/specs/2026-10-08-reglages-macdock-design.md` ; suite du plan 41.

**But :** les huit sections restantes, la recherche, des raccourcis saisis librement, la sauvegarde, le démarrage avec
Windows et la gestion des mods Windhawk, toutes entièrement fonctionnelles. Il ne reste plus aucun « Bientôt ».

## Architecture

### Raccourcis (`src/interact/hotkey`, pur, testé)
- **`parseHotkey(text)`** lit n'importe quelle combinaison `ctrl+alt+shift+win+<touche>` : lettres, chiffres, F1 à F24,
  space, tab, enter, flèches, home, end, pageup, pagedown, insert, delete, signes courants.
  - Il faut au moins un modificateur, sauf pour une touche F.
  - Les combinaisons réservées par Windows sont refusées : Win+L, Ctrl+Alt+Suppr, Alt+Tab (réservé au sélecteur), Alt+F4.
- **`hotkeyText(spec)`** donne le format des réglages ; **`hotkeyLabel(spec)`** l'affiche en symboles macOS (`⌃⌥⇧⊞↑`).
- **`hotkeyConflict(a, b)`** signale deux raccourcis égaux.
- **Côté Dock :** `parseSpotlightHotkey`, `parseMissionHotkey` et `parseAppExposeHotkey` s'appuient sur `parseHotkey`.
  Les anciennes valeurs restent valides, et `off` donne toujours « aucun ».

### Modèle (`src/settings`)
- **`SettingsModel.system`** : démarrage avec Windows, lu et écrit par une interface `SystemIo` (clé
  `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, valeur `MacDock` → lanceur). Les tests passent une version
  simulée. `commit` l'écrit s'il change.
- **`backup`** :
  - exporter : `{"version":1,"settings":…,"menubar":…}` ;
  - importer : validé, puis les deux fichiers écrits atomiquement ;
  - rétablir les défauts : préférences remises, apps épinglées gardées.
- **`mods`** :
  - version d'un mod lue dans `@version` de sa source (`windhawk/*.wh.cpp`, trouvée depuis le dossier de l'exécutable) ;
  - état installé lu dans `HKLM\SOFTWARE\Windhawk\Engine\Mods\local@<id>` (Version, Disabled) ;
  - comparaison des versions ;
  - présence de Windhawk.

### Sections (`panes`)

| Section | Contenu |
|---|---|
| Général | Démarrage avec Windows ; état de MacDock et boutons Relancer, Quitter ; sauvegarde : Exporter…, Importer…, Rétablir les réglages par défaut… |
| Mission Control | Les 4 coins actifs (menus) ; raccourcis de Mission Control et d'Exposé de l'app (enregistreurs) |
| Clavier | Alt de gauche joue ⌘ ; raccourci de Spotlight (enregistreur) ; sélecteur d'apps (Alt+Tab façon Mac ou celui de Windows) |
| Captures d'écran | ⊞⇧3, ⊞⇧4, ⊞⇧5 façon macOS |
| Sons | Sons système ; son du volume |
| Police | Police du Dock et de la barre : « Automatique » ou une famille installée, dans une liste choisie (SF Pro, Inter, Segoe UI Variable, Helvetica Neue, Arial…) |
| Mods Windhawk | Windhawk présent ; pour chaque mod MacDock : versions installée et disponible ; Installer, Mettre à jour ou Retirer |
| À propos | Version ; dossier des réglages et journaux (Afficher dans l'Explorateur) |

**Nouveaux types de ligne :**
- `Buttons` : boutons d'action, identifiés par une énumération `Action` exécutée par la fenêtre ;
- `Shortcut` : un raccourci, avec son conflit ;
- `Value` : un texte gris à droite.

### Contrôles (`ui_draw`)
- **Bouton poussoir** : 22 pt, rayon 6 ; bouton par défaut sur l'accent.
- **Champ de raccourci** : symboles. En écoute, « Tapez le raccourci… » avec un anneau d'accent. Pendant l'écoute, un
  crochet clavier bas niveau garde les combinaisons avec Win pour l'app. Échap annule ; Retour arrière donne « Aucun ».
- **Feuille d'alerte** façon macOS : carte arrondie descendue de la zone de titre, message en gras, texte, boutons.
  Elle sert aux confirmations (rétablir les défauts, redémarrer l'Explorateur après un mod).

### Fenêtre
- **Recherche :** le champ prend le clavier. La barre latérale ne garde que les sections qui correspondent, sans casse ni
  accents. Entrée ouvre la première ; les lignes trouvées sont soulignées d'une teinte d'accent.
- **Boutons :**
  - lanceur et `--quit` ;
  - dialogues Ouvrir et Enregistrer communs pour la sauvegarde ;
  - installateur PowerShell du mod en administrateur (`runas`), avec de nouveaux paramètres `-Restart` / `-NoRestart` à
    la place de la question O/N. L'app attend la fin de l'installateur, puis relit l'état.

## Contraintes
- Les tests n'écrivent ni dans le registre réel ni dans `%APPDATA%` : `SystemIo` est simulé, et les fichiers vont dans le
  dossier temporaire.
- Aucun mod n'est installé pendant les tests ou les essais ; seule l'interface est vérifiée.

## Tâches
- [x] 1. `hotkey` : analyse, texte, symboles, conflits, réservés ; le Dock les utilise (tests, dont les anciennes valeurs).
- [x] 2. Modèle : `SystemIo` (démarrage), `backup` (exporter, importer, défauts), `mods` (versions, état) (tests).
- [x] 3. Sections Général, Mission Control, Clavier, Captures, Sons, Police, Mods, À propos (tests d'aller-retour et
  d'actions).
- [x] 4. Contrôles : bouton, champ de raccourci, valeur, feuille d'alerte (rendu testé hors écran).
- [x] 5. Fenêtre :
  - recherche ;
  - enregistreur de raccourci ;
  - actions des boutons ;
  - feuille d'alerte ;
  - dialogues de fichiers ;
  - installateur des mods ;
  - démarrage.
- [ ] 6. Essai réel, documentation, relecture, fusion.
