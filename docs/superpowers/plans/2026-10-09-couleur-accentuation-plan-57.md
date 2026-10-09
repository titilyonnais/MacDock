# Couleur d'accentuation — plan 57

> **Pour les agents :** exécution en ligne, tâche par tâche, tests d'abord. Reprendre à la première tâche non cochée.

**But :** la « Couleur d'accentuation » de Réglages Système > Apparence (macOS 26) : huit accents au choix (Bleu,
Violet, Rose, Rouge, Orange, Jaune, Vert, Graphite), appliqués partout où MacDock montre l'accent (sélection des menus,
de Spotlight, de Forcer à quitter, interrupteurs et curseurs de Réglages, contour de Mission Control, barre ⊞⇧5…).
Aujourd'hui le bleu est figé, et en deux teintes : #007AFF / #0A84FF (menus, Spotlight, Mission Control, ⊞⇧5) et
#0088FF / #0091FF (Réglages, Forcer à quitter). Point 5 de la feuille de route.

Session autonome : aucune question possible, choix faits ici ; aucun push.

## Choix
- **Teintes** : celles de macOS 26 et iOS 26, déjà employées par le projet (tuiles de Réglages, rouge d'alerte), claire
  puis sombre :
  - Bleu #0088FF / #0091FF ; Violet #CB30E0 / #DB34F2 ; Rose #FF2D55 / #FF375F ; Rouge #FF383C / #FF4245 ;
  - Orange #FF8D28 / #FF9230 ; Jaune #FFCC00 / #FFD600 ; Vert #34C759 / #30D158 ; Graphite #8E8E93 / #98989D.
  Les valeurs exactes de l'accent de macOS 26 ne sont pas publiées : à recaler sur la VM macOS 26.
- **Texte sur l'accent** : blanc, sauf sur le jaune (noir, sinon illisible) ; règle de luminance.
- **Un seul bleu** : tous les endroits prennent la teinte de l'accent choisi (Bleu par défaut : #0088FF / #0091FF),
  fini les deux bleus.
- **Réglage** : `accentColor` dans `settings.json` (`blue` par défaut, `purple`, `pink`, `red`, `orange`, `yellow`,
  `green`, `graphite` ; inconnu : bleu). Lu par le Dock (rechargement à chaud), par la barre (relu quand le fichier
  change, comme le raccourci de Forcer à quitter) et par l'app Réglages (tout de suite).
- **Réglages > Général** : rangée « Couleur d'accentuation » de huit pastilles rondes, comme macOS : la choisie porte un
  point blanc ; clic, ou ← → au clavier.
- **Module** : `src/core/accent.h` (logique pure, sans dépendance), utilisable par tous les exécutables.

## Tâches
- [x] 1. **Accents** (`src/core/accent.h`) : table (clé, nom, teintes), `accentFromKey`, `accentRgb(accent, sombre)`,
  `onAccentRgb`. Tests.
- [x] 2. **Réglage** `accentColor` (`src/config/settings.*`) : lecture, écriture, valeur inconnue. Tests.
- [x] 3. **Interface** (`src/ui`) : `palette(sombre, accent)` ; pastilles dessinées (`drawSwatches`, `swatchAt`). Tests
  hors écran.
- [x] 4. **App Réglages** : rangée « Couleur d'accentuation » (Général), dessin, clic, clavier, palette suivant le choix.
  Tests (rangée, aller-retour du modèle, rendu).
- [x] 5. **Dock et barre** : `MenuWindow::Env::accent` (menus, piles, Centre de contrôle, Spotlight, Mission Control),
  barre ⊞⇧5, Forcer à quitter ; texte sur l'accent ; la barre relit l'accent de `settings.json`. Tests hors écran
  (Spotlight, Forcer à quitter).
- [x] 6. **Essais réels** : `settings.json` sauvegardé, accent violet, Spotlight, un menu du Dock et Forcer à quitter
  capturés (leurs seules fenêtres), `settings.json` restauré à l'octet près ; Réglages en mode d'essai.

  Puis documentation, relecture indépendante, corrections, fusion locale.

## Fait
- Suite : 781 tests verts ; Dock, barre, Réglages, outil d'icônes et sonde compilés (Debug et Release).
- **Essais réels** :
  - Réglages en mode d'essai (`--data` temporaire, `--pane general`) : huit pastilles, bleu choisi ; clic sur le violet :
    barre latérale violette, `settings.json` d'essai à « purple » ;
  - ton `settings.json` sauvegardé, accent violet écrit, puis : Spotlight (ligne choisie), Forcer à quitter (sélection
    et bouton) et le menu  de la barre (entrée choisie au clavier) en violet ; restauré à l'empreinte SHA-256 près,
    Spotlight revenu au bleu ; captures et sauvegarde effacées.
- Non essayé en réel : le bouton « Capturer » de ⊞⇧5 (la barre ⊞⇧5 ne s'ouvre qu'au vrai clavier) ; branché comme les
  autres, une ligne.

## Relecture indépendante
Rien de critique ; un important et huit mineurs. Corrigés en une passe, chacun avec un test vu en échec avant
(783 tests verts) :
- **Important** : dans l'éventail des piles, le nom survolé restait sur l'ancien bleu figé ; il prend l'accent, texte
  lisible dessus (`fanLabelColors`).
- **Mineurs regradés** :
  - un `settings.json` avec BOM (édité à la main) laissait la barre en bleu pendant que le Dock prenait l'accent ;
    un fichier illisible pendant une écriture la remettait au bleu. BOM retiré ; illisible : l'accent reste ;
  - Spotlight ne trouvait pas le nouveau réglage : « accentuation », « apparence » mènent à Général.
- **Promesse du plan tenue** : Forcer à quitter testé hors écran sur l'accent choisi.
- **Tests fiabilisés** : la teinte du contour de Mission Control n'est plus celle d'une fenêtre factice ; une variable
  masquée renommée.
- Mineurs reportés : voir le journal (« Mineurs reportés — plan 57 »).
