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
  Ensuite plus aucun mouvement simulé ne déplace le curseur (ni SendInput, ni SetCursorPos) : de 00:30:43 à 00:34:56,
  le curseur de Windows reste en 2272,2090, sur le Dock. Le Dock croit la souris sur lui et reste agrandi : c'est le
  « figé agrandi » décrit. **Le relais n'y peut rien** : il relit la position du curseur, et elle ne bouge pas. Ce que
  VMware fait du pointeur (le confine-t-il, le cache-t-il, le capture-t-il ?) n'est pas encore connu : une trace du mode
  diagnostic le relève désormais, à essayer avec toi.
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
- **Rebond, un seul train par épisode** (décision prise sans toi, réversible) : comme une demande d'attention
  « informative » sur macOS, l'icône fait un train de rebonds, même si l'app continue de clignoter (Windows fait
  clignoter sans fin le bouton d'une app à qui le premier plan a été refusé). L'épisode se termine quand l'app devient
  active ou se ferme, ou après 10 s sans clignotement ; un clignotement ensuite relance un train.
- **App active** : lue dans le vrai premier plan (GetForegroundWindow, ramené à la fenêtre propriétaire), au démarrage,
  à chaque changement de premier plan et à chaque clignotement ; jamais le hwnd d'un événement de premier plan (une
  demande refusée en produit un pour une fenêtre qui n'est pas passée devant).
- **Pointeur confiné par une autre app** dans une zone qui ne contient pas tout le Dock (ClipCursor) : le Dock fait
  comme si le pointeur était ailleurs. MacDock n'appelle jamais ClipCursor lui-même.
- **Limite** : avec une app en administrateur devant, les raccourcis du crochet clavier (⌥⇥, ⊞⇧3/4/5, ⌥`, touche ⌘)
  restent à Windows. Le remède (uiAccess : exécutable signé dans un dossier protégé) attend ta décision.

## Tâches
- [x] 1. **Rebond** : DockController::setActiveApp, attention ignorée pour l'app active, arrêt au clic sur l'icône
  (tests controller_attention_ignored_for_active_app, controller_attention_stops_for_good_when_app_activates,
  controller_attention_again_once_app_inactive ; rouges sans la garde, verts avec).
- [x] 2. **Dock figé** : logique pure du relais (tests), fil de lecture, réinstallation du crochet, sentinelle (relais
  actif ou arrêté, crochet réinstallé).
- [ ] 3. **Lenteurs** : WM_APP_BACKDROP après le génie, premier rendu, capture du verre quand le Dock est masqué.
- [ ] 4. **Essais réels** : Gestionnaire des tâches, VMware, fenêtre d'essai qui clignote ; journal avant et après.

  Puis documentation, relecture indépendante, corrections, fusion locale.

## Fait
- **Tâche 1** (d45d0ec) : tests rouges sans la garde « app active » (2 échecs), verts avec. Essai réel : fenêtre
  d'essai inactive qui clignote dix fois (FLASHW_ALL, nombre fixe) ; son icône rebondit ; clic sur l'icône à
  00:36:40 ; six clignotements tardifs jusqu'à 00:36:45 ; le Dock retombe au repos (20 à 28 tours de boucle en 5 s,
  aucune image), alors qu'avant la correction il animait sans fin (825 images en 5 s, 9 octobre à 23:08).
- **Tâche 2** (0cb7c09) : `src/interact/cursor_relay.*` (logique pure, 9 tests : 7 rouges sur l'ébauche, puis verts) ;
  relais sur le fil du crochet (`DockApp::startMouseThread`), livraison commune `DockApp::deliverMouse`, position du
  crochet seul (`hookX_`, `hookY_`) pour la sentinelle ; traces « relais de la souris actif / arrêté », « Crochet
  souris réinstallé », et pour le rebond « début du rebond / rebond en cours / ignorée (app active) / fin du rebond ».
  Suite : 827 tests verts. Essais réels (souris simulée) :
  - curseur déplacé par SetCursorPos (invisible au crochet), premier plan normal : le Dock prend la souris en 55 à
    106 ms et la rend en 29 à 46 ms ; avant, il restait figé (le clic passait au travers jusqu'au bureau) ;
  - Gestionnaire des tâches au premier plan : Windows refuse alors tout déplacement simulé, SendInput comme
    SetCursorPos ; le relais ne peut pas y être essayé avec des entrées simulées. GetCursorPos, lui, n'est pas
    bloqué : la vraie souris sera suivie (le journal de 00:17 montre ces mouvements que le crochet ne voyait pas) ;
  - **VMware n'est pas couvert par le relais** (corrigé après coup : le message du commit 0cb7c09 le disait à tort) :
    le curseur de Windows y reste immobile sur le Dock, le relais ne voit donc aucun mouvement ;
  - au repos : 20 tours de boucle en 5 s, comme avant ; en mouvement, 23 à 26 messages de souris pour deux glissés,
    comme avant (pas de double envoi) ;
  - un crochet muet pendant des déplacements par SetCursorPos est réinstallé (au plus toutes les 10 s), comme voulu.
- **Effet de bord de l'essai** : le Gestionnaire des tâches, réduit de nouveau comme je l'avais trouvé, garde le
  premier plan ; tant qu'il l'a, Windows bloque toutes les entrées simulées (et le délai de verrouillage du premier
  plan est au maximum sur ce PC). Ton premier clic rend la main. Les essais suivants évitent les fenêtres élevées.
- **Vu en chemin, pour la tâche 3** : curseur posé sur l'icône d'une app réduite, la boucle tourne ~100 fois par
  seconde sans dessiner.
- **Reprises demandées par le chef d'orchestre** (39d4251, 25edf78) : app active lue dans le vrai premier plan ; un
  train de rebonds par épisode ; relais lu vite seulement dans la seconde qui suit un mouvement ; silence du crochet
  compté depuis son installation ; pointeur confiné ailleurs ; trace du pointeur. Tests : `controller_attention_*`
  (`one_train_per_episode` et `new_train_after_silence` rouges, puis verts ; `silence_counts_while_idle` rouge, puis
  vert), `cursor_relay_polls_fast_only_after_a_move` et `cursor_relay_counts_silence_from_hook_installation` (rouges
  avec l'ancien comportement remis exprès, verts), `pointer_confined_away_from_the_dock` (rouge sur l'ébauche, vert).
  Suite : 834 cas, un seul échec, `uia_menus_read_and_invoke_fake_app`, dû au Gestionnaire des tâches qui tient le
  premier plan (vert à 00:44 sur le même code).
- **Essais réels sans entrée simulée** (Gestionnaire des tâches devant), MacDock relancé deux fois (la seconde pour le
  défaut trouvé à la première) :
  - au démarrage, « app active : taskmgr.exe » (le vrai premier plan) ; aucune réinstallation du crochet ;
  - relais au repos : 46 lectures en 5 s (100 ms) ;
  - fenêtre d'essai à moi : dix clignotements → « début du rebond » à 01:31:45, un train (~1,9 s), puis « même épisode,
    train fini » ; 12,9 s de calme puis trois clignotements → nouveau « début du rebond » à 01:32:08 ; fermée à
    01:32:26 : plus aucune image ensuite ;
  - trouvé à la première relance et corrigé : le silence d'un épisode se comptait avec l'horloge de l'animation, qui
    n'avance pas quand le Dock dort ; le second train ne partait pas.
