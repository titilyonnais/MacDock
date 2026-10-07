# Feux tricolores — Spécification de conception (sous-projet 4)

- **Date :** 2026-10-07
- **Statut :** validée par délégation (« tout en autonomie » pour les sous-projets 2 à 6). Tout est tranché ici et signalé dans le journal.
- **Référence :** boutons de fenêtre de macOS Tahoe 26 (fermer, réduire, zoom) en haut à gauche des fenêtres.
- **Spec parente :** `2026-10-06-macos-dock-design.md` ; la barre de menus (`2026-10-07-macmenubar-design.md`) héberge la fonction.

## 1. Objectif

Afficher, en haut à gauche de la fenêtre active, trois pastilles rouge, jaune et verte qui ferment, réduisent et agrandissent la fenêtre, comme sur macOS. Au survol du groupe, les trois montrent leur symbole (×, −, +).

**Critère de réussite :** sur une fenêtre Win32 classique (Bloc-notes historique, Paint, regedit, Explorateur en mode classique, la plupart des outils), les pastilles apparaissent dans la barre de titre, suivent la fenêtre quand elle bouge, et agissent ; elles ne recouvrent jamais le contenu d'une app qui dessine sa propre barre de titre (onglets de Chrome, d'Edge, de l'Explorateur).

### 1.1 Décisions (prises en autonomie)

| Question | Décision | Raison |
|---|---|---|
| Comment | Une petite fenêtre superposée (calque) posée au-dessus de la barre de titre de la fenêtre active, dans `MacMenuBar.exe` | Les boutons de Windows sont dessinés par DWM ou par l'app ; les déplacer demanderait une injection dans chaque processus. La barre suit déjà la fenêtre au premier plan. |
| Quelles fenêtres | Par défaut, celles dont Windows dessine la barre de titre : la zone client commence sous une barre de titre d'au moins 20 px (`client.top − cadre visible.top`), style `WS_CAPTION` + `WS_SYSMENU`, pas fenêtre outil, pas le bureau ni la barre des tâches, pas nos fenêtres | Une app à barre de titre personnalisée (Chrome, Edge, VS Code, Explorateur à onglets, apps WinUI) a sa zone client dès le haut : des pastilles y cacheraient des onglets ou des boutons. |
| Réglage | `menubar.json` → `trafficLights` : `standard` (défaut), `all` (toutes les fenêtres à `WS_CAPTION`), `off` | `all` pour qui accepte de recouvrir le coin des apps personnalisées. |
| Fenêtres inactives | Pas de pastilles : seule la fenêtre active en a | Un calque par fenêtre visible demanderait de suivre l'ordre de toutes les fenêtres. |
| Ce qu'on cache | Un fond de la couleur de la barre de titre (mesurée à l'écran) passe sous les pastilles, avec un fondu à droite : l'icône de l'app et le début du titre de Windows ne transparaissent pas | Sans fond, l'icône de Windows apparaîtrait entre les pastilles. |
| Boutons de Windows à droite | Gardés | Impossible de les retirer sans injection. |
| Actions | Fermer : `WM_SYSCOMMAND SC_CLOSE` ; réduire : `SC_MINIMIZE` (l'effet génie du Dock joue) ; zoom : `SC_MAXIMIZE`, ou `SC_RESTORE` si la fenêtre est agrandie | Les commandes système respectent les confirmations des apps (« Enregistrer ? »). |
| Pastilles indisponibles | Grises, sans action : réduire sans `WS_MINIMIZEBOX`, zoom sans `WS_MAXIMIZEBOX`, fermer avec `CS_NOCLOSE` | Comme macOS. |
| Clic sur le fond (hors pastilles) | Relayé à la fenêtre comme un clic sur sa barre de titre (`WM_NCLBUTTONDOWN`, `HTCAPTION`) : on peut la déplacer depuis là ; double-clic = zoom | Le calque ne doit pas créer de zone morte dans la barre de titre. |
| Ressources Apple | Aucune : couleurs et symboles dessinés par le code | Règle du projet. |

### 1.2 Limites assumées
- Les apps à barre de titre personnalisée n'ont pas de pastilles par défaut (la plupart des apps modernes).
- Pendant un déplacement rapide de la fenêtre, les pastilles peuvent suivre avec une image de retard.
- Le fond est d'une couleur unie : sur une barre de titre Mica très contrastée, un léger écart de teinte reste visible.

## 2. Logique pure (testée) — `src/menubar/traffic_lights.h/.cpp`

- `enum class LightsMode { Standard, All, Off }` ; `trafficLights` lu dans `MenuBarSettings`.
- `struct LightsWindowInfo { LONG style, exStyle; UINT classStyle; std::wstring className; RECT frame; RECT client; bool zoomed, iconic, ownProcess; }` (`frame` = cadre visible `DWMWA_EXTENDED_FRAME_BOUNDS`, `client` = zone client en coordonnées écran).
- `bool wantsLights(const LightsWindowInfo&, LightsMode, UINT dpi)` : refus si `Off`, réduite, sans `WS_CAPTION` (`== WS_CAPTION`) ou sans `WS_SYSMENU`, `WS_EX_TOOLWINDOW`, `WS_CHILD`, classes du shell (`Shell_TrayWnd`, `Shell_SecondaryTrayWnd`, `Progman`, `WorkerW`), notre processus ; en `Standard`, refus si `client.top − frame.top < 20 px × dpi / 96`.
- `struct LightsLayout { RECT window; RECT circles[3]; double radius; RECT patch; }` : `lightsLayout(frame, client, dpi)` — diamètre 12 pt, centres espacés de 20 pt, premier centre à 20 pt du bord gauche du cadre, centrés verticalement dans la barre de titre (`frame.top` → `client.top`, ou 28 pt si la barre de titre est plus fine) ; le calque couvre de `frame.left + 4 px` à 8 pt après la dernière pastille, sur la hauteur de la barre de titre, sans déborder du cadre.
- `int hitLight(const LightsLayout&, POINT)` : 0 fermer, 1 réduire, 2 zoom, −1 ailleurs (cercle élargi de 2 px).
- `struct LightsState { bool hover; bool enabled[3]; bool dark; std::uint32_t patchColor; }`.
- `std::vector<std::uint8_t> renderLights(const LightsLayout&, const LightsState&, double scale)` : image BGRA prémultipliée de la taille du calque — fond uni de `patchColor` (fondu sur les 8 derniers pt), pastilles anticrénelées (sur-échantillonnage 4 × 4) : rouge `#FF5F57` bord `#E0443E`, jaune `#FEBC2E` bord `#DEA123`, vert `#28C840` bord `#1AAB29` ; indisponible : gris (`#D0D0D0` clair, `#5A5A5A` sombre) ; au survol, symboles sombres à 55 % : ×, −, +.
- `std::uint32_t dominantColor(const std::vector<std::uint32_t>& samples)` : la couleur la plus fréquente (à 8 niveaux près par canal), pour le fond.
- `SysCommand lightCommand(int light, bool zoomed)` → `SC_CLOSE`, `SC_MINIMIZE`, `SC_MAXIMIZE` / `SC_RESTORE`.

## 3. Calque — `src/menubar/traffic_window.h/.cpp`

- Fenêtre `WS_POPUP`, `WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE`, exclue des captures (`WDA_EXCLUDEFROMCAPTURE`), image posée par `UpdateLayeredWindow`.
- `attach(HWND target)` : lit les informations, décide (`wantsLights`), mesure la couleur de la barre de titre (`GetPixel` sur l'écran, 9 points sur une ligne à 4 px sous le haut du cadre, de 24 à 120 px du bord gauche ; `dominantColor`), place le calque juste au-dessus de la fenêtre dans l'ordre d'affichage (`SetWindowPos` après la fenêtre qui précède la cible, sinon `HWND_TOP`), l'affiche. `detach()` le masque.
- Suivi : `SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE)` hors contexte, limité au processus de la cible ; un déplacement de la cible replace le calque ; sa réduction, sa fermeture ou son passage en plein écran le masquent. Couleur remesurée 200 ms après le dernier déplacement.
- Souris : survol du groupe → symboles ; `WM_LBUTTONUP` sur une pastille disponible → la commande, postée à la cible ; ailleurs, `WM_LBUTTONDOWN` → `ReleaseCapture` puis `WM_NCLBUTTONDOWN(HTCAPTION)` posté à la cible ; `WM_LBUTTONDBLCLK` → zoom. `WM_MOUSEACTIVATE` → `MA_NOACTIVATE`.
- `MenuBarApp` : `onForeground` appelle `lights_.attach(h)` (fenêtre réellement au premier plan), un changement de `trafficLights` est appliqué au rechargement des réglages, l'arrêt détruit le calque.

## 4. Vérification

- Tests : `wantsLights` (classique, personnalisée, outil, sans `WS_SYSMENU`, shell, mode `all`/`off`, DPI 200 %), `lightsLayout` (100 % et 200 %, barre de titre fine), `hitLight`, `renderLights` (centre de chaque pastille à la bonne couleur, gris si indisponible, alpha du fondu décroissant), `dominantColor`, `lightCommand`, réglage `trafficLights`.
- `MacMenuBar.exe --lights-snapshot planche.png` : planche hors écran (clair et sombre ; normal, survol, indisponible). Rien n'est affiché.
- Pas d'essai réel à l'écran (l'utilisateur travaille) : à vérifier soi-même, listé dans le journal.
