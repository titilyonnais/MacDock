# Coins actifs — spec (sous-projet 11)

- **Date :** 7 octobre 2026
- **Contexte :** suite de la série « un Windows qui ressemble complètement à macOS », en autonomie.

## 1. But

Sur macOS, pousser le pointeur dans un coin de l'écran déclenche une action choisie dans Réglages › Bureau et Dock › Coins actifs : Mission Control, Bureau, Launchpad, Centre de notifications, économiseur d'écran, suspension de l'écran, verrouillage… L'action part dès que le pointeur touche le coin ; il faut en ressortir pour la relancer.

**Critère de réussite :** avec le Dock lancé, pousser le pointeur dans un coin réglé lance son action une fois ; rien ne se passe pendant un glisser (bouton enfoncé), en plein écran, ou dans un coin intérieur entre deux écrans.

## 2. Décisions

| Sujet | Choix | Raison |
|---|---|---|
| Hôte | Le Dock : il suit déjà le pointeur (crochet souris existant, `onMouse`) et ouvre Mission Control et Apps | Aucun crochet de plus. |
| Coins | Coins de chaque écran où le pointeur bute dans les deux sens (les trois points voisins vers l'extérieur ne sont sur aucun écran), zone de 2 px | Ailleurs, le pointeur glisse vers l'écran voisin au lieu de s'arrêter dans le coin. |
| Déclenchement | À l'entrée dans le coin ; réarmé quand le pointeur s'en éloigne de plus de 24 px ; ignoré bouton enfoncé, en plein écran, ou pendant une vue modale (sauf Mission Control, qu'un second passage referme) | Comme macOS, sans déclenchement en jeu ou en glisser. |
| Actions | `off`, `missionControl`, `desktop` (bureau : `IShellDispatch4::ToggleDesktop`), `apps` (écran Apps), `notificationCenter` (`ms-actioncenter:`), `lockScreen` (`LockWorkStation`), `displaySleep` (`SC_MONITORPOWER` à la fenêtre du Dock), `screenSaver` (`SC_SCREENSAVE`) | Les actions de macOS qui existent sous Windows. |
| Réglage | `hotCorners` dans `settings.json` : `topLeft`, `topRight`, `bottomLeft`, `bottomRight` ; défaut : tous `off` sauf `bottomRight` = `desktop` | macOS met une action en bas à droite par défaut ; Windows y montre le bureau. |
| Mission Control par message | Le message `MacDockMissionControl` reste ouvert aux autres processus | Déjà en place. |

## 3. Composants

- `src/interact/hot_corners.h/.cpp` (pur) : `enum class Corner`, `enum class HotCornerAction`, `parseHotCornerAction`, `hotCornerName`, `cornerAt(pt, monitors)`, `HotCornerTracker::update(at, pt, blocked) → optional<Corner>`.
- Branchements : `Settings::hotCorners` (lecture, écriture), `DockApp::onMouse` → `runHotCorner(action)`.

## 4. Tests et vérifications

- Tests : coins d'un écran, de deux écrans côte à côte (coins intérieurs exclus), zone de 2 px ; suivi (une fois par entrée, réarmement à 24 px, bloqué) ; lecture du réglage (défauts, valeurs inconnues ignorées, casse).
- **Aucun essai ne déplace le pointeur ni ne lance une action (bureau, verrouillage, veille de l'écran…).**
