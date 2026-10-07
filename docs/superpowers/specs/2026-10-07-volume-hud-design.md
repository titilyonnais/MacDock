# HUD du volume et de la luminosité — spec (sous-projet 10)

- **Date :** 7 octobre 2026
- **Contexte :** suite de la série « un Windows qui ressemble complètement à macOS », en autonomie.

## 1. But

Sur macOS Tahoe, les touches de volume et de luminosité font apparaître en haut à droite, sous la barre de menus, une petite pastille en verre : le pictogramme, le nom du réglage, la sortie audio, et une jauge qui suit la valeur. Elle s'efface un peu plus d'une seconde après le dernier appui. Le volume avance par seizièmes ; Maj+Option donne des quarts de seizième.

**Critère de réussite :** avec MacMenuBar lancé, les touches volume +, volume − et sourdine changent le volume par pas de 1/16 et montrent la pastille macOS au lieu du panneau de Windows ; Maj+Alt+touche donne un pas de 1/64 ; un changement de luminosité (touches d'un portable, curseur de Windows) montre la pastille de luminosité ; la pastille s'efface 1,5 s après le dernier changement.

## 2. Décisions

| Sujet | Choix | Raison |
|---|---|---|
| Hôte | `MacMenuBar.exe` | Il lit déjà le volume (Core Audio), la luminosité, et dessine les pictogrammes ; la pastille est sous sa barre. |
| Touches de volume | `RegisterHotKey` sur `VK_VOLUME_UP`, `VK_VOLUME_DOWN`, `VK_VOLUME_MUTE` (et Maj+Alt+volume) ; la barre règle alors elle-même le volume (`AudioStatus`). Volume + enlève la sourdine. Si l'enregistrement échoue : journalisé, Windows garde ses touches, et la pastille suit les changements de volume venus d'ailleurs (avis Core Audio). | Pas de crochet clavier. Une touche enregistrée n'atteint pas Windows : son propre panneau ne s'affiche plus. |
| Pas | 1/16 (6,25 %), aligné sur la grille ; 1/64 avec Maj+Alt | Comme macOS (Maj+Option). |
| Luminosité | `RegisterPowerSettingNotification(GUID_VIDEO_CURRENT_MONITOR_BRIGHTNESS)` : `WM_POWERBROADCAST` avec un pourcentage. Le premier avis (envoyé à l'enregistrement) est ignoré. Les touches de luminosité des portables restent gérées par Windows (le microprogramme les traite) : son panneau reste visible à côté. | Lecture seule, sans WMI ni crochet. |
| Silence | Pas de pastille pendant qu'un menu de la barre est ouvert (curseurs du Centre de contrôle et du menu Son) ; pas de pastille de luminosité dans la seconde qui suit un réglage fait par la barre | Le curseur est déjà sous les yeux. |
| Réglage | `hud` dans `menubar.json` : `true` (défaut) ou `false` (ni pastille ni prise des touches) | Pour rendre les touches à Windows. |
| Pastille | 280 × 64 pt, coins de 20 pt, à 12 pt du bord droit et 8 pt sous la barre, sur l'écran du curseur ; verre des menus (capture le temps de l'affichage), verre dépoli de repli ; ligne 1 : titre « Volume » ou « Luminosité » en gras 13 pt, à droite le nom de la sortie en gris ; ligne 2 : pictogramme 16 pt puis jauge en capsule de 6 pt (remplie selon la valeur ; vide et haut-parleur barré en sourdine) | Mesures de la pastille de Tahoe. |
| Durée | Visible 1,5 s après le dernier changement, puis fondu de 0,25 s ; un nouveau changement la ravive sans la refaire apparaître | Comme macOS. |
| Fenêtre | `WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT`, jamais activée, les clics la traversent | Ne prend ni le clavier ni la souris. |

## 3. Composants

- `src/hud/hud_logic.h/.cpp` (pur) : `volumeStep(v, dir, fine)`, `HudFade` (montrer, opacité selon le temps), `hudRect(monitor, barBottom, scale)`, `BrightnessGate` (premier avis, silence après un réglage de la barre).
- `src/hud/hud_window.h/.cpp` : `HudWindow` (`show(env, mon, content)`, `tick(now)`, `hide`) ; `BgraImage hudSnapshot(content, dark, w, h)`.
- Branchements : `MenuBarSettings::hud`, `MenuBarApp` (touches, avis Core Audio et luminosité, minuterie du fondu), `MacMenuBar.exe --hud-snapshot f.png [--kind volume|brightness] [--level x] [--muted] [--theme dark]`.

## 4. Tests et vérifications

- Tests : pas (grille, bornes, fin), fondu (durée, ravivage, fin), place (coin droit, sous la barre, échelle), garde de la luminosité, réglage, rendu hors écran.
- À l'œil : `--hud-snapshot` (volume 50 %, sourdine, luminosité, sombre).
- **Aucun essai n'enregistre les touches de volume, ne change le volume ou la luminosité, ni n'affiche la pastille devant l'utilisateur.**
