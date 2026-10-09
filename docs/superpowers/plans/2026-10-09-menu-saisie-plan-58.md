# Menu de saisie du clavier — plan 58

> **Pour les agents :** exécution en ligne, tâche par tâche, tests d'abord. Reprendre à la première tâche non cochée.

**But :** le menu de saisie de macOS dans la barre de menus. Avec plusieurs dispositions de clavier (sur ce PC :
français et anglais US), une icône montre celle de l'app au premier plan (« FR », « US ») ; son menu permet d'en
changer et ouvre les émojis, le clavier visuel et les réglages. La barre des tâches de Windows, qui portait cet
indicateur, est cachée par MacDock : aujourd'hui, plus rien ne montre la disposition. Proposé par l'agent de la nuit
(n° 1 de sa liste).

Session autonome : aucune question possible, choix faits ici ; aucun push.

## Choix
- **Icône** : le code de pays de la disposition (« FR », « US », « GB », « CH »…) en petites capitales dans une boîte
  arrondie, comme les sources de saisie sans drapeau de macOS (« ABC ») ; pas de drapeaux (Windows n'en a pas, et pas de
  ressource Apple). Masquée avec une seule disposition, comme le Wi-Fi sans carte.
- **Place** : à gauche des autres icônes d'état (avant le son), comme le menu de saisie de macOS.
- **Disposition suivie** : celle du fil de la fenêtre au premier plan (`GetKeyboardLayout`), relue toutes les 500 ms
  (aucun évènement de Windows ne la signale pour une autre app) ; la liste des dispositions est relue à l'ouverture du
  menu et toutes les dix secondes.
- **Code** : le pays de la disposition (mot haut du HKL quand c'est une langue), sinon celui de la langue (dispositions
  « variantes », mot haut 0xF…), par `LOCALE_SISO3166CTRYNAME`.
- **Nom dans le menu** : le nom de la langue de la disposition dans la langue de Windows (« Français (France) »,
  « Anglais (États-Unis) »).
- **Menu** :
  - une ligne par disposition, cochée pour l'actuelle ; un clic la donne à l'app au premier plan
    (`WM_INPUTLANGCHANGEREQUEST` à sa fenêtre, comme la barre de langue de Windows) ;
  - « Afficher les émojis et symboles » (⊞+.), « Afficher le clavier visuel » (osk.exe) ;
  - « Ouvrir les réglages Clavier… » (`ms-settings:regionlanguage`).
- **Réglage** : `showInput` dans `menubar.json` (oui par défaut), ligne « Saisie » de Réglages > Barre des menus >
  Icônes.

## Tâches
- [ ] 1. **Logique** (`src/menubar/status_input.*`, `status_menus.*`, `menubar_settings.*`) : `layoutCode`, lecture des
  dispositions et de celle au premier plan, `StatusKind::Input` (icône et menu), action de changement, réglage
  `showInput`. Tests.
- [ ] 2. **Dessin** (`bar_renderer.*`) : texte dans une boîte arrondie (`BarDrawItem::boxed`). Test hors écran.
- [ ] 3. **Barre** (`menubar_window.*`) : état, minuterie de 500 ms, image, changement de disposition ; Réglages >
  Barre des menus > Icônes > « Saisie ». Tests (rangée, aller-retour).
- [ ] 4. **Essais réels** : barre relancée ; l'icône montre la disposition de la fenêtre au premier plan (lue en
  parallèle, en lecture seule) ; capture de la seule barre ; aucun changement de disposition sur tes apps (essayé avec
  toi).

  Puis documentation, relecture indépendante, corrections, fusion locale.
