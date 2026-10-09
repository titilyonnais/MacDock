# Clavier : espace pendant la saisie, raccourcis réessayés — plan 47

> **Pour les agents :** exécution en ligne, tâche par tâche, tests d'abord. Reprendre à la première tâche non cochée.

**But :** deux défauts du clavier relevés par l'agent de propositions du 9 octobre.

## Constat (dans le code)
- `quickLookKey` (src/quicklook/quicklook_logic.cpp) ouvre le Coup d'œil à chaque Espace dans la liste des fichiers de
  l'Explorateur. Or l'Explorateur sélectionne un fichier quand on tape le début de son nom : « mon rapport » s'arrête à
  « mon », et l'espace ouvre l'aperçu. Le Finder, lui, garde l'espace dans la saisie en cours (moins d'une seconde
  après la lettre précédente).
- `registerMissionHotkey`, `registerAppExposeHotkey`, `registerSpotlightHotkey` (src/app/dock_window.cpp) retiennent le
  réglage même quand `RegisterHotKey` échoue (raccourci pris par une autre app au démarrage, PowerToys sur Alt+Espace) :
  il n'est jamais réessayé, même une fois libéré.

## Tâches
- [x] 1. **Espace dans la saisie en cours** :
  - le crochet clavier note l'instant de la dernière lettre, chiffre ou signe frappé sans modificateur ;
  - moins d'une seconde après : l'espace va à l'Explorateur (appui et relâchement), sinon le Coup d'œil s'ouvre.

  Tests : `typeAheadKey` (lettres, chiffres, signes ; pas avec Ctrl, Alt ou ⊞), `typeAheadActive` (999 ms oui, 1 s
  non), `quickLookKey` avec `typeAhead` (passe, appui comme relâchement).
- [x] 2. **Raccourcis réessayés** :
  - `HotkeySlot` (réglage essayé, enregistré ou non) et `hotkeyNeedsRegister` ;
  - enregistrement refusé : nouvel essai toutes les 30 s, avertissement une seule fois, puis « raccourci libéré ».

  Tests : réglage changé, même réglage enregistré (rien), même réglage refusé (nouvel essai), raccourci désactivé.
- [ ] 3. Essai réel (MacDock lancé pour l'essai puis arrêté), documentation, relecture, fusion.

## Essais réels
MacDock en diagnostic, lancé puis arrêté :
- Ctrl+Alt+↑ retenu par un script avant le lancement : « déjà pris par une autre app (1409) ; nouvel essai toutes les
  30 s » ; libéré à 05:22:59, repris par le Dock à 05:23:29 ;
- dossier d'essai ouvert dans l'Explorateur (« mon autre fichier.txt », « mon rapport.txt », « zèbre.txt ») : « mon r »
  tapé vite sélectionne « mon rapport.txt » sans Coup d'œil ; une espace 1,5 s plus tard l'ouvre. Fenêtre et dossier
  retirés ensuite.
