# Fenêtre suivante de l'app au clavier (⌥`) — plan 59

> **Pour les agents :** exécution en ligne, tâche par tâche, tests d'abord. Reprendre à la première tâche non cochée.

**But :** ⌘` de macOS (« Déplacer la cible vers la fenêtre suivante ») : Alt + la touche sous Échap fait passer devant
la fenêtre suivante de l'app au premier plan, Alt+Maj la précédente. Aujourd'hui, passer d'une fenêtre de Brave à une
autre demande la souris, le menu Fenêtre de la barre ou Alt+Tab de Windows (que MacDock remplace par son sélecteur
d'apps). Proposé par l'agent de la nuit (n° 2 de sa liste).

Session autonome : aucune question possible, choix faits ici ; aucun push.

## Choix
- **Touche** : Alt + la touche sous Échap, reconnue à sa place (code de balayage 0x29, non étendu) et non à son
  caractère : ` sur un clavier US, ² sur un AZERTY, ^ sur un QWERTZ ; comme macOS, qui garde la touche à gauche de 1
  quelle que soit la disposition. Maj : sens inverse. Jamais avec Ctrl (Alt Gr = Ctrl+Alt) ni ⊞. Un appui tenu ne
  répète pas. Les frappes simulées passent, sauf en diagnostic (essais), comme pour ⊞⇧3.
- **Fenêtres** : celles que le Dock suit pour l'app au premier plan (sa fenêtre racine propriétaire), visibles, non
  réduites (macOS les saute), non voilées, sur le bureau virtuel courant ; dans l'ordre du Dock (ordre d'ouverture).
  L'ordre est stable : des appuis successifs passent par toutes les fenêtres et reviennent à la première, comme macOS
  (le va-et-vient entre les deux dernières est le rôle du sélecteur d'apps).
- **Effet** : la fenêtre choisie passe devant tout de suite, sans panneau, comme macOS ; les autres ne bougent pas.
  Une touche neutre est envoyée d'abord : Alt relâché sans autre frappe ouvrirait le menu de l'app, et cette frappe
  donne au Dock le droit de changer le premier plan. Pas de frappe Alt synthétique (Alt est tenu : la relâcher
  casserait l'appui suivant).
- **Rien à faire** (une seule fenêtre, app non suivie, bureau, menu ou sélecteur ouvert) : la frappe est prise quand
  même, comme ⌥⇥ ; rien ne bouge.
- **Pendant le sélecteur d'apps** (⌥⇥ tenu) : la touche recule d'une case (Maj : avance), comme ⌘` pendant ⌘⇥ sur
  macOS.
- **Alt de gauche joue ⌘** (option) : Alt de gauche + ` fait de même (Alt rendu à Windows, comme pour ⌥⇥).
- **Réglage** : `windowCycleHotkey` dans `settings.json` : `alt+backtick` (par défaut) ou `off` ; Réglages > Clavier >
  Raccourcis > « Fenêtre suivante de l'app » : « ⌥` façon macOS » ou « Désactivée », avec « La touche sous Échap (²
  sur un clavier AZERTY) » en dessous. Spotlight : « fenêtre suivante » mène à Réglages > Clavier.

## Interfaces
- `src/switcher/window_cycle.h` (logique pure, dans les tests) :
  - `bool isCycleKey(unsigned scanCode, bool extended);` — 0x29 non étendu ;
  - `enum class CycleKey { Pass, Swallow, Next, Prev, SwitcherBack, SwitcherNext };`
  - `CycleKey cycleKeyAction(const CycleKeyEvent& e);` — `CycleKeyEvent` : `down`, `repeat`, `alt`, `ctrl`, `win`,
    `shift`, `injected`, `taken`, `session` (comme `ShotKeyEvent`) ; appelée pour la seule touche 0x29 ; relâchement
    d'une frappe prise (taken) : avalé ;
  - `std::optional<std::size_t> cycleTarget(const std::vector<bool>& eligible, std::optional<std::size_t> current, bool back);`
  - `bool parseWindowCycleHotkey(const std::wstring& text);` — vrai pour « alt+backtick » seulement.
- `DockSettings::windowCycleHotkey` (`std::wstring`, `L"alt+backtick"`), clé JSON `windowCycleHotkey`.
- Dock : `kHotCycle = 15`, `kHotCycleBack = 16` (WM_APP_SWITCHKEY), `DockApp::cycleWindows(bool back)`,
  `std::atomic<bool> cycleKeysOn_`.

## Tâches
- [ ] 1. **Logique** (`src/switcher/window_cycle.*`, `config/settings.*`) : `isCycleKey`, `cycleKeyAction`,
  `cycleTarget`, `parseWindowCycleHotkey`, réglage `windowCycleHotkey` (défaut, `OFF` lu en minuscules, valeur
  inconnue → défaut, aller-retour). Tests.
- [ ] 2. **Dock** (`app/dock_window.*`, `settings/panes.cpp`, `spotlight/spot_settings.cpp`) : le crochet clavier prend
  la touche (avant le repli de la touche ⌘) et poste `kHotCycle` / `kHotCycleBack`, ou recule dans le sélecteur ; le
  Dock fait passer la fenêtre devant ; ligne de Réglages > Clavier ; mots-clés de Spotlight. Tests (ligne et valeurs
  écrites, recherche « fenêtre suivante »).
- [ ] 3. **Essais réels** : une sonde (`tests/real/window_cycle_probe.cpp`, cible `windowcycleprobe`) ouvre trois
  fenêtres à elle, prend le premier plan, puis envoie Alt+` trois fois (1 → 2 → 3 → 1), Alt+Maj+` une fois (1 → 3),
  réduit la deuxième et vérifie qu'elle est sautée ; avant chaque touche, une de ses fenêtres doit avoir le premier
  plan, sinon elle s'arrête sans rien envoyer. MacDock relancé en diagnostic ; build Release.

  Puis documentation, relecture indépendante, corrections, fusion locale.
