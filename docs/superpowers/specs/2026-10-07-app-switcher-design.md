# Sélecteur d'apps — spec (sous-projet 9)

- **Date :** 7 octobre 2026
- **Contexte :** suite de la série « un Windows qui ressemble complètement à macOS », en autonomie.

## 1. But

Sur macOS, Cmd+Tab affiche au centre de l'écran une rangée d'icônes des apps ouvertes, de la plus récemment utilisée à la plus ancienne ; Tab avance, Maj+Tab recule, relâcher Cmd passe à l'app choisie (toutes ses fenêtres). Un appui bref passe à l'app précédente sans rien afficher.

**Critère de réussite :** Alt+Tab (réglable) montre le sélecteur en verre au centre de l'écran du curseur ; les apps ouvertes y sont dans l'ordre d'utilisation ; Tab et Maj+Tab (Alt toujours enfoncé), les flèches et la souris changent la sélection ; relâcher Alt active l'app choisie ; Échap annule ; un Alt+Tab bref (moins de 0,15 s) passe directement à l'app précédente.

## 2. Décisions

| Sujet | Choix | Raison |
|---|---|---|
| Raccourci | `appSwitcherHotkey` dans `settings.json` : `alt+tab` (défaut) ou `off` ; `RegisterHotKey` (Alt+Tab et Alt+Maj+Tab, sans `MOD_NOREPEAT` pour que Tab maintenu fasse défiler) ; si l'enregistrement échoue, le sélecteur de Windows reste et c'est journalisé | Alt est la touche Cmd d'un clavier PC ; aucun crochet clavier. |
| Relâchement d'Alt | Lu par `GetAsyncKeyState` toutes les 15 ms pendant que le sélecteur est actif | Sans crochet clavier ; le sélecteur ne prend pas le clavier. |
| Échap, flèches, Q, H | Enregistrés comme raccourcis (Alt+Échap, Alt+←, Alt+→, Alt+Q, Alt+H) le temps du sélecteur seulement : Échap annule, ←/→ déplacent, Q ferme l'app choisie (WM_CLOSE à ses fenêtres), H la masque | Raccourcis de macOS ; enregistrés, ils n'atteignent pas l'app au premier plan (Alt+← y ferait « Précédent »). |
| Menu de l'app au premier plan | Au début du sélecteur, une touche neutre (code 0xE8) est envoyée pendant qu'Alt est enfoncé | Sinon, relâcher Alt seul activerait la barre de menus de l'app (comme le font les outils du même genre). |
| Verre | Celui des menus (capture de l'écran le temps du sélecteur, la capture du Dock en pause) ; verre dépoli si la capture n'est pas prête | Même aspect que les menus. |
| Ordre | Les apps qui ont une fenêtre, de la plus récemment activée à la plus ancienne (historique tenu par le Dock à chaque activation) ; la sélection part sur la deuxième | Comme macOS. |
| Panneau | Verre des menus, coins de 18 pt, icônes de 64 pt (celles du Dock) avec 16 pt d'écart, fond de sélection gris arrondi, nom de l'app sous la rangée ; trop d'apps : icônes réduites pour tenir dans 90 % de la largeur | Mesures de macOS. |
| Fenêtre | `WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE`, jamais activée ; un clic sur une icône la choisit | L'app au premier plan garde le clavier jusqu'au choix. |
| Activation | `activateApp` sur toutes les fenêtres non réduites de l'app (la dernière active devant) ; si toutes sont réduites, la dernière est restaurée | Comme macOS. |

## 3. Composants

- `src/switcher/app_mru.h/.cpp` (pur) : `class AppMru { void touch(appId); void forget(appId); std::vector<std::wstring> order(const std::vector<std::wstring>& running) const; }`.
- `src/switcher/switcher_logic.h/.cpp` (pur) : `struct SwitcherState { std::size_t count, selected; }` et ses transitions (`next`, `prev`, `quickSwitch`), `switcherLayout(count, screenW, scale)` (taille d'icône, positions).
- `src/switcher/switcher_window.h/.cpp` : `SwitcherWindow` (non modale, pilotée par le Dock : `show`, `select`, `hide`, `hit`) ; `BgraImage switcherSnapshot(names, selected, dark, w, h)`.
- Branchements : `Settings::appSwitcherHotkey`, `DockApp` (historique, raccourcis, minuterie de relâchement, activation), `MacDock.exe --switcher-snapshot f.png [--count n] [--select i] [--theme dark]`.

## 4. Tests et vérifications

- Tests : historique (ordre, oubli, apps sans historique à la fin), transitions (boucle, recul, une seule app, aucune), rangement (tient dans l'écran, centré), réglage, rendu hors écran.
- À l'œil : `--switcher-snapshot` (4 et 25 apps, sombre).
- **Aucun essai n'enregistre Alt+Tab, n'affiche le sélecteur devant l'utilisateur, n'active ni ne ferme d'app.**
