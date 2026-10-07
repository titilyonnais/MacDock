# Effet génie — Spécification de conception (sous-projet 3 : animations de fenêtres)

- **Date :** 2026-10-07
- **Statut :** validée par délégation. L'utilisateur a demandé « fais la barre de menus et fais le reste ensuite, je veux tout faire » et a choisi « tout en autonomie » pour les sous-projets 2 à 6. Tout est tranché ici et signalé dans le journal.
- **Référence :** réduction et restauration des fenêtres dans le Dock de macOS Tahoe 26 (Réglages → Bureau et Dock → « Réduire les fenêtres avec : Effet Génie / Effet Échelle »).
- **Spec parente :** `2026-10-06-macos-dock-design.md` (découpage en sous-projets, Dock, miniatures des fenêtres réduites).

## 1. Objectif

Quand une fenêtre est réduite, elle s'écoule dans sa case du Dock comme sur macOS (effet génie) ; quand on la restaure depuis le Dock, elle en ressort par le chemin inverse. L'effet « Échelle » de macOS est proposé aussi.

**Critère de réussite :** réduire une fenêtre (bouton, `Win+↓`, menu) la fait glisser dans la case de sa miniature du Dock, sans à-coup ni double animation (celle de Windows est coupée) ; un clic sur cette miniature la fait ressortir à sa place. Rien ne reste à l'écran après l'animation, et Windows retrouve ses réglages quand le Dock s'arrête.

### 1.1 Décisions (prises en autonomie)

| Question | Décision | Raison |
|---|---|---|
| Où vit l'animation | Dans `MacDock.exe` | Le Dock connaît déjà la case cible et sait afficher une fenêtre réduite (miniature DWM). |
| Comment déformer une fenêtre d'un autre processus | Des bandes de miniatures DWM (`DwmRegisterThumbnail`, `rcSource` / `rcDestination`) dans une fenêtre transparente au-dessus de tout | Pas d'injection ni de capture d'écran ; DWM garde l'image d'une fenêtre réduite. 48 bandes donnent une courbe lisse. |
| L'animation de Windows | Coupée pendant que le Dock tourne avec l'effet Génie ou Échelle : `SPI_SETANIMATION` (`iMinAnimate = 0`) **sans** `SPIF_UPDATEINIFILE` | Sinon deux animations se superposent. Le réglage n'est pas écrit dans le profil : la valeur d'origine reste dans le registre et revient à l'arrêt du Dock, ou à la prochaine session après un plantage. |
| Effet par défaut | Génie | C'est celui de macOS. |
| Choix de l'effet | `settings.json` → `minimizeEffect` : `genie`, `scale`, `windows` (animation de Windows, rien n'est coupé) ; et dans le menu du séparateur du Dock : « Effet de réduction » ▸ Génie, Échelle, Windows | Comme le réglage de macOS, à portée de clic. |
| Restauration | Animée seulement depuis le Dock (clic sur la miniature, « Restaurer ») | Une fenêtre restaurée ailleurs (Alt+Tab, barre de menus, l'app elle-même) est déjà affichée quand on l'apprend : l'animer par-dessus montrerait deux fenêtres. Elle apparaît alors sans animation. |
| Ouverture et fermeture des fenêtres | Celles de Windows 11, gardées | Elles sont déjà proches de macOS (zoom et fondu) et ne dépendent pas de `iMinAnimate`. |
| Agrandissement | Sans animation tant que l'effet Génie ou Échelle est actif | `iMinAnimate` couvre aussi l'agrandissement. C'est le prix de la coupure ; `windows` le rend. |
| Ralenti | Maj enfoncée au début de l'animation : durée × 8 | Comme sur macOS. |

### 1.2 Limites assumées
- Le Dock est masqué (masquage automatique) : la fenêtre va vers la case où elle serait si le Dock était visible.
- Une fenêtre qui n'a pas de case dans le Dock (app masquée du Dock) se réduit sans animation.
- L'image animée est la dernière image de la fenêtre gardée par DWM ; une fenêtre qui ne se dessine pas (rare) donne une bande vide.
- Pas de « réduire dans l'icône de l'app » (option de macOS) : la case est toujours la miniature à droite du Dock.

## 2. Géométrie (logique pure, testée)

`src/anim/genie.h/.cpp`.

- `enum class MinimizeEffect { Genie, Scale, Windows }`.
- `struct GenieSlice { RECT src; RECT dst; }` : `src` en pixels de la fenêtre source (taille `SIZE src`), `dst` en pixels écran.
- `std::vector<GenieSlice> minimizeFrame(MinimizeEffect e, SIZE src, const RECT& from, const RECT& to, DockPosition edge, double t, int slices)` ; `t` = 0 : la fenêtre à sa place (`from`), `t` = 1 : dans la case (`to`).
- **Repère local** : l'axe `v` va vers le Dock, `u` le long du Dock. En bas : `u = x`, `v = y` ; à gauche : `u = y`, `v = -x` ; à droite : `u = y`, `v = x`. Les bandes sont perpendiculaires à `v` : des lignes de la source en bas, des colonnes à gauche et à droite.
- **Génie** (fenêtre `[u0,u1]×[v0,v1]`, case `[a0,a1]×[b0,b1]`, `b0 > v0`) :
  - courbure `p = easeInOut(clamp(t / 0.45))`, glissement `q = easeInOut(clamp((t − 0.2) / 0.8))` ;
  - poids `w(v) = smoothstep(clamp((v − v0) / (b0 − v0)))` : 0 en haut de la fenêtre, 1 au haut de la case ;
  - bords : `left(v) = u0 + (a0 − u0)·w(v)·p`, `right(v) = u1 + (a1 − u1)·w(v)·p` ;
  - le contenu occupe `[lerp(v0, b0, q), lerp(v1, b1, q)]` ; la bande `k` (fraction `[k/n, (k+1)/n]` de la source) va à la portion correspondante, de largeur `[left, right]` prise au milieu de la bande ;
  - `n = min(slices, côté de la source le long de v)` ; les bandes de destination sont jointives (le bas de l'une est le haut de la suivante).
- **Échelle** : une bande, rectangle interpolé de `from` à `to` avec `easeInOut(t)`.
- **Windows** : aucune bande.
- Durées : génie 0,55 s, échelle 0,3 s ; × 8 avec Maj.
- `RECT restoredRect(const WINDOWPLACEMENT&, const RECT& monitor, const RECT& work, bool toolWindow, SIZE src)` : rectangle écran de la fenêtre avant réduction. `rcNormalPosition` est en coordonnées de la zone de travail, sauf pour une fenêtre outil ; une fenêtre réduite depuis l'état agrandi (`WPF_RESTORETOMAXIMIZED`) occupe la zone de travail, débordée de ses bordures invisibles (`src` plus grand que la zone de travail, centré).
- `RECT fitThumbnail(...)` (existant) donne la forme de la miniature dans la case.

## 3. Animation à l'écran

`src/app/genie_window.h/.cpp`, classe `GenieWindow` :

- fenêtre `WS_POPUP` au même style que le Dock (`WS_EX_NOREDIRECTIONBITMAP | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW`, alpha 255) : rien n'y est dessiné, seules les miniatures DWM y apparaissent ; elle laisse passer les clics ;
- `start(source, from, toCell, edge, effect, restore, now)` : place la fenêtre sur l'union de `from` et de la case, au-dessus du Dock, enregistre une miniature par bande, calcule la case (`fitThumbnail` selon la taille de la source) et rend `false` si DWM refuse (pas d'animation) ;
- `step(now)` : met à jour `rcSource` / `rcDestination` de chaque bande (coordonnées relatives à la fenêtre) ; à la fin, masque la fenêtre et retire les miniatures, et rend `false` ;
- `cancel()` ; `source()`, `restoring()`, `running()`.

## 4. Intégration au Dock

- **Réduction** (`EVENT_SYSTEM_MINIMIZESTART`, hors `--snapshot`, effet ≠ `windows`, Dock affiché sur un écran) : la case de la fenêtre au repos (`DockController::restingTile(window)` : sans agrandissement, sans masquage, en pixels de la fenêtre du Dock) est convertie en coordonnées écran ; `restoredRect` donne le départ ; l'animation part. Une animation en cours est terminée d'abord.
- **Restauration depuis le Dock** : l'animation inverse part de la case ; à la fin, la fenêtre est restaurée (`restoreWindow`). Si l'animation ne peut pas partir, restauration immédiate.
- **Restauration d'ailleurs** pendant une réduction animée : l'animation est annulée.
- Pendant l'animation, la miniature de cette fenêtre n'est pas affichée dans sa case du Dock (elle y arrive avec la fin de l'animation).
- La boucle du Dock fait avancer l'animation comme ses autres animations (horloge du compositeur).
- **Coupure de l'animation de Windows** (`src/app/min_animate.h/.cpp`, classe `MinAnimateGuard`, fonctions système injectables pour les tests) : à l'application des réglages, `iMinAnimate` passe à 0 si l'effet est Génie ou Échelle, et revient à la valeur d'origine (`HKCU\Control Panel\Desktop\WindowMetrics\MinAnimate`, défaut 1) si l'effet est `windows` et à l'arrêt du Dock. Aucune écriture dans le profil.

## 5. Vérification

- Tests unitaires de la géométrie (bandes jointives, départ = fenêtre, arrivée = case, bords en bas, à gauche, à droite, échelle), de `restoredRect`, du réglage, du menu, de `restingTile` et de `MinAnimateGuard` (fonctions factices : les tests ne touchent jamais au réglage de Windows).
- `MacDock.exe --genie-snapshot planche.png [--effect genie|scale] [--edge bottom|left|right]` : planche d'images hors écran (t = 0 ; 0,2 ; 0,4 ; 0,6 ; 0,8 ; 1) avec une fenêtre synthétique, dessinée par les mêmes bandes. Rien n'est affiché et aucun réglage n'est touché.
- Pas d'essai réel à l'écran (l'utilisateur travaille) : la vérification visuelle sur une vraie fenêtre est dans le journal, à faire soi-même.
