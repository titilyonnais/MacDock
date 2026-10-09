# App Réglages : les mineurs du plan 41 — plan 46

> **Pour les agents :** exécution en ligne, tâche par tâche, tests d'abord. Reprendre à la première tâche non cochée.

**But :** les quatre mineurs reportés à la relecture du plan 41 (journal, « Plan 41 — app Réglages MacDock (1/3) »).

## Constat (dans le code)
- `DockApp::saveSettings` réécrit tout `settings.json` depuis ses réglages en mémoire. Une clé que l'app Réglages vient
  d'écrire, avant que le Dock ait relu le fichier (fenêtre d'environ 200 ms), est remise à l'ancienne valeur.
- Les écrans de « Écran du Dock » s'appellent « Écran 2 — 2560 × 1440 » : macOS donne leur nom (« DELL U2720Q »,
  « Écran intégré »).
- Les menus de l'app (lignes à choix) s'ouvrent à l'appui, mais un glisser jusqu'à un élément puis un relâchement ne le
  choisit pas, comme le permet macOS : il faut cliquer une seconde fois.
- Chaque image recrée un `ui::Painter`, donc tous ses formats de texte DirectWrite ; `paragraph` et `paragraphHeight`
  en recréent un à chaque appel.

## Tâches
- [x] 1. **Le Dock n'écrit que ce qu'il a changé** :
  - `mergeChanged` passe de `src/settings` à `src/config/config_store` (le Dock ne compile pas `src/settings`) ;
  - `dockSettingsToWrite(fichier, dernier écrit ou lu, maintenant)` : fichier absent, invalide ou illisible, tout ;
    sinon, la fusion par différence ;
  - le Dock garde le JSON de ses réglages au dernier chargement ou à la dernière écriture.

  Tests : une clé écrite par l'app Réglages entre-temps reste quand le Dock enregistre la sienne ; fichier absent :
  tout est écrit.

  Décision : pas d'essai réel de la course entre le Dock et l'app (il faudrait changer tes vrais réglages du Dock) ;
  les tests de la fusion et de `dockSettingsToWrite` la couvrent.
- [x] 2. **Vrais noms des écrans** :
  - `QueryDisplayConfig` et `DisplayConfigGetDeviceInfo` : nom du moniteur pour chaque source GDI (`\\.\DISPLAY1`) ;
    écran interne sans nom : « Écran intégré » ;
  - libellé : nom, puis la définition, puis « (principal) » ; sans nom, « Écran N » comme avant.

  Tests : `screenLabels` (avec et sans nom, principal, deux écrans du même modèle) ; `monitorNames` sur ce PC.

  Essai réel (app en mode d'essai) : le menu « Écran du Dock » propose « MAG 272U E16 — 3840 × 2160 (principal) » et
  « LG ULTRAGEAR — 1920 × 1080 ». `ScreenInfo` existait déjà dans la barre de menus : la structure s'appelle
  `ScreenChoice` (sinon violation de la règle de définition unique, plantage des tests).
- [x] 3. **Menus en appuyer-glisser-relâcher** :
  - le menu ouvert par un appui suit le doigt (survol) ;
  - relâché sur un élément après un glisser (4 pt) ou un appui tenu (0,3 s) : choisi ; sinon le menu reste ouvert.

  Test : `menuReleaseChoice` (clic court : reste ouvert ; glisser puis relâcher sur un élément : choisi ; hors du
  menu : rien).

  Essai réel (app en mode d'essai, messages postés) : clic court sur « Génie », le menu reste ouvert ; appui, glisser
  jusqu'à « Windows » et relâcher : choisi, menu fermé ; relâché hors du menu : rien.
- [x] 4. **Formats de texte gardés d'une image à l'autre** : cache de la fenêtre (`ui::FormatCache`) passé au peintre,
  formats des paragraphes compris ; vidé au changement de police.

  Test : deux peintres qui partagent le cache donnent le même format ; un paragraphe dessiné deux fois n'en crée
  qu'un.

  La police de la fenêtre n'est choisie qu'au démarrage : le cache, rangé par police, n'a pas à être vidé.
  Essai réel : section « À propos » (ligne coupée par « … », paragraphe replié) identique.
- [ ] 5. Essai réel (app Réglages en mode d'essai `--data`, MacDock lancé pour l'essai puis arrêté), documentation,
  relecture, fusion.
