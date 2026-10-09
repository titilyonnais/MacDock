# Pastilles en double sur les fenêtres de MacDock — plan 56

> **Pour les agents :** exécution en ligne, tests d'abord. Plan court, né d'un défaut vu à l'essai réel du plan 55.

**But :** la barre de menus posait ses pastilles (et son fond de barre de titre) en haut à droite des fenêtres de
MacDock qui dessinent déjà les leurs : l'app Réglages MacDock et « Forcer des applications à quitter », dont la fin du
titre était même cachée (« Forcer des applic… »).

Session autonome : aucune question possible, choix faits ici ; aucun push.

## Cause
Les deux fenêtres retirent le cadre de Windows (WM_NCCALCSIZE) mais gardent les styles WS_CAPTION, WS_SYSMENU et
WS_MINIMIZEBOX : le DWM leur compte des boutons de légende (`DWMWA_CAPTION_BUTTON_BOUNDS`, mesuré : 146 px en haut à
droite), et la barre de menus, qui donne des pastilles à toute fenêtre à barre de titre, les posait sur ces boutons.

## Choix
- **Marque explicite** plutôt que deviner : une propriété de fenêtre `MacDock.OwnLights` (`src/core/own_lights.h`),
  posée après la création et retirée à la destruction. La barre de menus la lit (`LightsWindowInfo::ownLights`) : ni
  pastilles ni apparence de macOS pour ces fenêtres.
- **Styles gardés** : retirer WS_SYSMENU aurait aussi retiré ces boutons du DWM, mais au prix du menu de la fenêtre, et
  avec un doute sur Alt+F4 et la fermeture par la barre des tâches ; la marque ne change rien d'autre.

## Tâches
- [x] 1. Tests : une fenêtre marquée n'a ni pastilles ni calque ni apparence (`wantsLights`, `lightsLayerWanted`,
  `macWindowLook`) ; la marque posée, lue puis retirée sur une vraie fenêtre.
- [x] 2. Marque et lecture : `own_lights.h`, barre de menus (`readInfo`, `wantsLights`), app Réglages et Forcer à
  quitter.
- [x] 3. Essais réels, avant puis après, sur l'app Réglages en mode d'essai (`--data` dans un dossier temporaire,
  effacé) et sur Forcer à quitter (ouvert par son message, fermé par Échap sans rien forcer) : pastilles de la barre
  visibles avant, absentes après, titre entier ; captures de leurs seules fenêtres, effacées.
- Suite : 768 tests verts.

## Relecture indépendante
Rien de critique ; un important, quatre mineurs.
- **Important** : la barre ne réglant plus l'apparence de ces fenêtres, elle ne leur retirait plus le liseré de
  Windows. Mesuré en réel (option « couleur d'accentuation sur les bordures » activée, fenêtres rendues actives par un
  clic sur leur propre barre de titre, coins agrandis) : aucun liseré visible, avant comme après. Corrigé quand même,
  pour ne plus dépendre de la barre : les fenêtres posent elles-mêmes coins arrondis et liseré absent
  (`applyOwnFrame`) ; test vu en échec avant (coins relus ; le liseré ne se relit pas, seule son acceptation par le
  DWM est vérifiée). 769 tests verts.
- **Mineurs corrigés en passant** (commentaires et journal, sans effet sur le comportement) : commentaire de test
  remis au-dessus du sien ; cause du refus dans le journal `[diag]` de la barre ; contrat de la marque précisé
  (avant le premier affichage, retirée au plus tard à WM_NCDESTROY).
- **Mineur gardé** : le branchement réel n'a pas de test automatique (la barre n'est pas dans `tests.exe`) ; couvert
  par l'essai réel, avant et après.
