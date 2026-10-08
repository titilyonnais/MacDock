# App Réglages MacDock (1/3) — plan 41

> **Pour les agents :** exécution en ligne, tâche par tâche, tests d'abord. Reprendre à la première tâche non cochée.

**Spec :** `docs/superpowers/specs/2026-10-08-reglages-macdock-design.md`.

**But :** `MacDockSettings.exe`, une fenêtre façon Réglages Système de macOS 27 :
- barre latérale de sections ;
- groupes arrondis et lignes avec interrupteurs, curseurs, menus et contrôles segmentés ;
- sections Dock, Barre des menus et Fenêtres, appliquées en direct ;
- ouverte depuis le Dock et la barre.

## Architecture

### `src/settings/settings_doc` (pur, testé)
- `SettingsModel` regroupe `Settings` (Dock) et `MenuBarSettings`.
- `loadModel(dir)` lit les deux fichiers.
- `mergeChanged(file, before, after)` ne réécrit dans le fichier que les clés dont la valeur sérialisée change : les
  clés inconnues et les épingles du Dock restent intactes.
- `commit(dir, edit)` recharge les fichiers, applique `edit` au modèle, puis fusionne et sauve atomiquement
  (`saveJsonFileAtomic`) le ou les fichiers changés.

### `src/settings/panes` (pur, testé)
- La liste des sections : identifiant, titre, clé `--pane`, couleur de tuile, pictogramme.
- Les sections décrites en groupes de lignes, chaque ligne ayant :
  - un type : interrupteur, curseur, choix (menu), segmenté ou info ;
  - un libellé, un sous-titre et des mots-clés ;
  - une valeur numérique (bool : 0 ou 1 ; choix : indice) ;
  - des fonctions `get`, `set` et `enabled` sur `SettingsModel`.
- `PaneEnv` donne les noms des écrans.

### `src/ui/ui_theme` (pur)
La palette claire et sombre de `docs/recherches/2026-10-08-cotes-macos.md` et les cotes en points :

| Élément | Cote |
|---|---|
| Fenêtre | 715 pt de large |
| Barre latérale | 215 pt |
| Zone de titre | 52 pt |
| Lignes | 36 / 44 pt |
| Groupes | rayon 12, marges de 20 pt, 18 pt entre groupes |
| Lignes de la barre latérale | 28 pt |
| Tuiles | 20 pt |

### `src/ui/ui_layout` (pur, testé)
Mise en page d'une section, sous forme de positions :
- titres de groupe ;
- groupes et lignes ;
- hauteur totale ;
- lignes de la barre latérale.

Calculs des contrôles et du clavier :
- curseur : valeur ↔ position, avec pas ;
- contrôle segmenté : segment touché ;
- menu : ligne touchée ;
- ordre du focus.

### `src/ui/ui_draw` (Direct2D, rendu testé hors écran)
- Les contrôles : interrupteur (progression animée de 0 à 1), curseur, bouton de menu (double chevron), contrôle
  segmenté, groupe, séparateurs, anneau de focus.
- Le menu ouvert.
- Les tuiles de section et leurs pictogrammes (dessinés par nous).
- Les pastilles de la fenêtre.

### `src/settings_app/`
**`settings_window` :**
- Fenêtre `WS_EX_NOREDIRECTIONBITMAP` sans cadre Windows (`WM_NCCALCSIZE`), fond acrylique de DWM
  (`DWMWA_SYSTEMBACKDROP_TYPE`), cadre étendu.
- Rendu DirectComposition et chaîne d'échange Direct2D en alpha prémultiplié. La barre latérale est une teinte
  translucide sur l'acrylique ; le contenu est opaque.
- Largeur fixe et hauteur réglable (`WM_GETMINMAXINFO`).
- Zone de titre déplaçable (`WM_NCHITTEST`) ; bords haut et bas pour redimensionner.
- Pastilles à gauche.
- Défilement à la molette, avec une barre fine qui s'efface.
- Clavier : Tab, flèches, Espace, Échap et Ctrl+W.
- Clair ou sombre selon Windows, en direct.
- Ressorts du moteur `motion` pour les interrupteurs.
- Une seule instance (mutex). La seconde passe `--pane` par un message enregistré.

**`settings_main` :** l'arrière-plan d'une ligne modifiée appelle `commit`. Le dossier est surveillé
(`FindFirstChangeNotification`) pour recharger l'affichage si un fichier change ailleurs.

### Liens
- Dock « Réglages du Dock… » → `MacDockSettings.exe --pane dock`.
- Barre « Réglages de la barre des menus… » → `--pane menubar`.
- Menu  → « Réglages MacDock… ».
- Si l'exécutable manque, les deux gardent le repli sur l'éditeur de texte.

## Contraintes
- Aucune ressource Apple : tout est dessiné en Direct2D, y compris l'icône.
- Les tests n'écrivent que dans le dossier temporaire et n'ouvrent pas de fenêtre visible.
- Les autres sections (Général, Clavier, Mods, etc.) apparaissent dans la barre latérale avec « Bientôt » (plan 42).

## Tâches
- [x] 1. `settings_doc` : modèle, fusion par différence, `commit` atomique (tests : clés inconnues et épingles gardées,
  seule la clé changée est écrite, fichier absent).
- [x] 2. `panes` : sections et lignes Dock, Barre des menus, Fenêtres (tests : chaque `set`/`get` fait l'aller-retour,
  `enabled` de la taille agrandie, choix de position et d'effet, `--pane`).
- [x] 3. `ui_theme` et `ui_layout` (tests : hauteurs, positions, curseur, segments, focus, barre latérale).
- [x] 4. `ui_draw` : contrôles, tuiles, menu, pastilles (rendu hors écran en clair et en sombre : couleurs aux bons
  endroits).
- [x] 5. `MacDockSettings.exe` : cible de build, fenêtre, rendu, entrée, défilement, focus, clair/sombre, instance
  unique, `--pane`.
- [x] 6. Liens depuis le Dock et la barre ; essai réel en diagnostic (captures de chaque section en clair et en sombre,
  un réglage change le Dock) ; documentation ; fusion.
