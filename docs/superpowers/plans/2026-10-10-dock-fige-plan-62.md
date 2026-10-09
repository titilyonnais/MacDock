# Dock figé et rebond sans fin — plan 62

> **Pour les agents :** exécution en ligne, tâche par tâche, tests d'abord. Reprendre à la première tâche non cochée.

**But :** les deux bugs que tu as signalés le 9 octobre au soir. Le Dock reste figé (agrandi, ou au repos sans plus
s'agrandir) après un clic sur une app comme VMware ou un changement de fenêtre, jusqu'au clic suivant. L'icône d'une
app comme Claude rebondit sans fin, même quand on clique dessus. Puis les lenteurs vues en chemin par la sentinelle du
mode diagnostic.

## Constat (journal du mode diagnostic)
- **Rebond** : Claude (fenêtre 0x60DC8) fait clignoter sa fenêtre un nombre fixe de fois (FlashWindowEx sans
  FLASHW_TIMERNOFG). Le 9 octobre, de 23:08:42 à 23:08:49, huit avis HSHELL_FLASH, dont six après son activation
  (23:08:43). Le Dock relançait le rebond d'attention, puis l'animait sans fin (825 images en 5 s) : l'app était déjà
  au premier plan, aucun changement de premier plan ne l'arrêtait, un clic sur l'icône non plus.
- **Dock figé, app en administrateur** : 10 octobre, 00:17:45, tu restaures le Gestionnaire des tâches depuis sa case
  du Dock ; 00:17:47.681 « crochet souris muet … premier plan Taskmgr.exe (élevé) » ; le crochet revient 6,2 s plus
  tard, à ton clic dans Claude. Le crochet bas niveau d'un processus non élevé ne voit pas la souris quand une fenêtre
  élevée a l'entrée (UIPI). Or le Dock ne suit la souris que par ce crochet (onMouse ← WM_APP_MOUSE ; son propre
  WM_MOUSEMOVE ne sert qu'au glisser) : il reste dans son dernier état, agrandi, ou traversé par les clics.
- **Dock figé, VMware** (non élevé) : 00:30:42, clic simulé (SendInput) sur l'icône de VMware, qui passe devant.
  Ensuite plus aucun mouvement simulé ne déplace le curseur : VMware avale la souris quand il a le focus. Le Dock croit
  la souris sur lui et reste agrandi ; il ne se rétablit qu'au clic qui donne le premier plan à une autre app.
- **SetCursorPos** ne passe pas par les crochets : un curseur déplacé par une app est invisible pour le Dock (essai :
  « crochet souris muet », premier plan non élevé).
- **Lenteurs** : boucle du Dock bloquée 1 094 ms sur WM_APP_BACKDROP (0x8007) juste après une restauration par le génie
  (00:17:46) ; premier rendu vers 2,2 s au démarrage (00:14:46 et 00:26:29) ; Dock masqué derrière une app en plein
  écran qui rend encore ~30 images/s (00:14:47 à 00:17:01).

## Choix
- **Rebond**, comme requestUserAttention sur macOS : sans effet pour l'app active (suivie à chaque changement de premier
  plan, un dialogue comptant pour sa propriétaire, aucune pour le bureau) ; arrêté quand l'app devient active, et au
  clic sur son icône.
- **Dock figé** : la position réelle du curseur (GetCursorPos) prend le relais du crochet quand il se tait. Lecture sur
  un fil à part : ~10 Hz au repos, ~60 Hz pendant le relais, tout de suite si une app en administrateur est devant.
  Un crochet muet plus de 2 s alors qu'aucune app élevée n'est devant est réinstallé (au plus toutes les 10 s).
- **Limite** : avec une app en administrateur devant, les raccourcis du crochet clavier (⌥⇥, ⊞⇧3/4/5, ⌥`, touche ⌘)
  restent à Windows. Le remède (uiAccess : exécutable signé dans un dossier protégé) attend ta décision.

## Tâches
- [x] 1. **Rebond** : DockController::setActiveApp, attention ignorée pour l'app active, arrêt au clic sur l'icône
  (tests controller_attention_ignored_for_active_app, controller_attention_stops_for_good_when_app_activates,
  controller_attention_again_once_app_inactive ; rouges sans la garde, verts avec).
- [ ] 2. **Dock figé** : logique pure du relais (tests), fil de lecture, réinstallation du crochet, sentinelle (relais
  actif ou arrêté, crochet réinstallé).
- [ ] 3. **Lenteurs** : WM_APP_BACKDROP après le génie, premier rendu, capture du verre quand le Dock est masqué.
- [ ] 4. **Essais réels** : Gestionnaire des tâches, VMware, fenêtre d'essai qui clignote ; journal avant et après.

  Puis documentation, relecture indépendante, corrections, fusion locale.
