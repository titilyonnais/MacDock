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
- [x] 1. **Logique** (`src/menubar/status_input.*`, `status_menus.*`, `menubar_settings.*`) : `layoutCode`, lecture des
  dispositions et de celle au premier plan, `StatusKind::Input` (icône et menu), action de changement, réglage
  `showInput`. Tests.
- [x] 2. **Dessin** (`bar_renderer.*`) : texte dans une boîte arrondie (`BarDrawItem::boxed`). Test hors écran.
- [x] 3. **Barre** (`menubar_window.*`) : état, minuterie de 500 ms, image, changement de disposition ; Réglages >
  Barre des menus > Icônes > « Saisie ». Tests (rangée, aller-retour).
- [x] 4. **Essais réels** : barre relancée ; l'icône montre la disposition de la fenêtre au premier plan (lue en
  parallèle, en lecture seule) ; capture de la seule barre ; aucun changement de disposition sur tes apps (essayé avec
  toi).

  Puis documentation, relecture indépendante, corrections, fusion locale.

## Fait
- Suite : 792 tests verts ; barre, Dock, Réglages, outil d'icônes et sonde compilés (Debug et Release).
- **Essais réels** :
  - MacDock relancé (Release, diagnostic) : la barre montre « FR » dans sa boîte, à gauche du son ; la disposition
    de la fenêtre au premier plan (Brave), lue en parallèle, est bien le français (0x040C040C) ;
  - menu ouvert par un clic envoyé à la seule barre : « Français (France) » coché, « Anglais (États-Unis) »,
    émojis, clavier visuel, réglages Clavier ; refermé par Échap sans rien choisir ; captures effacées.
- Non essayé en réel : le changement de disposition depuis le menu, sur tes apps ; à faire avec toi.

## Relecture indépendante
Rien de critique ; deux importants et dix mineurs. Corrigés en une passe, chacun avec un test vu en échec avant
(798 tests verts) :
- **Importants** :
  - deux dispositions d'une même langue (AZERTY et BÉPO, US et US-International, ou une langue sur deux claviers)
    avaient le même nom dans le menu : elles prennent leur nom complet, langue et clavier, lu dans le registre
    (« Français (France) – Français (Standard, BÉPO) ») ;
  - une console conhost était lue sur le fil de son processus client (cmd, pwsh), qui ne traite pas le clavier :
    elle est lue sur celui de sa fenêtre IME, qui appartient à conhost (vérifié en lecture seule sur deux consoles
    de ce PC).
- **Mineurs regradés** :
  - « Afficher les émojis et symboles » et le clavier visuel rendent d'abord le premier plan à l'app (sinon
    l'émoji allait à la fenêtre que Windows choisissait) ;
  - au démarrage, l'icône est là dès la première image (plus de « ? » ni de glissement après 500 ms).
- **Choix gardé** : deux claviers d'un même pays gardent le même code (« FR ») ; le menu les distingue.
- **Essais réels refaits** : barre recompilée et relancée, « FR » sur les deux écrans, menu inchangé pour tes deux
  dispositions, refermé par Échap ; captures effacées.
- Mineurs reportés : voir le journal (« Mineurs reportés — plan 58 »).
