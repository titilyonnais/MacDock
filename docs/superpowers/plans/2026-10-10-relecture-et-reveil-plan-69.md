# Relecture de la nuit et premiers retours du réveil — plan 69

> **Pour les agents :** exécution en ligne, tests d'abord. Reprendre à la première tâche non cochée.

**But :** corriger ce que la relecture d'ensemble de la nuit (plans 61 à 68, leurs croisements) a trouvé, et les premiers
défauts que tu as signalés au réveil, le 10 octobre : ⊞⇧S, Dock pas toujours fluide, contour blanc du verre.

## Relecture d'ensemble de la nuit (opus, sur `ddf0d1b..58669bb`)
Verdict : l'essentiel tient ; aucun critique. Aucune reprise de capture sans exclusion, aucun accès après destruction,
arrêt dans le bon ordre et borné.
- **Important 1** : chaque écriture dans `%APPDATA%\MacDock` rechargeait toute la config du Dock (la barre y réécrit
  ses récents 5 s après chaque changement d'app). Le rechargement relançait la capture du verre mise en pause exprès
  (Dock masqué : verre redessiné pour rien ; pendant une révélation : verre dépoli) et relisait toutes les piles sur le
  fil du Dock.
- **Important 2** : chaque renouvellement de l'aperçu d'une pile (téléchargement) restait à vie dans le cache du Dock,
  en mémoire comme sur la carte graphique.
- **Mineurs** : crochet clavier jamais réinstallé ; capture du verre arrêtée avant de tout révéler (100 ms de retard
  pour les pastilles et les menus) ; fil des icônes détaché face aux statiques à la sortie (plantage possible, et le
  lanceur relancerait MacDock) ; chemins synchrones qui ignorent les images du disque ; fenêtres ouvertes pendant une
  révélation jamais révélées ; documentation (README, journal, feuille de route).

## Tes retours du réveil traités ici
- **⊞⇧S** : le Dock et la barre restaient devant le voile de l'Outil Capture, et le Dock réagissait encore à la souris.
  Sonde d'empilement : barre, Dock, puis seulement les voiles de l'outil, tous dans la même couche « toujours au premier
  plan » ; le Dock suit la souris par un crochet, même couvert.
- **Dock pas toujours fluide, « impression de 30 fps »** : sous une app en administrateur (VSCodium, Gestionnaire des
  tâches), le crochet souris du Dock est aveugle et le relais du plan 62 prend le relais ; il lisait le curseur toutes
  les 16 ms avec la minuterie de Windows : 165 lectures en 5 s, soit ~33 images par seconde. Aussi : les rechargements
  de config de l'important 1.
- **Contour blanc du Dock** : le Liquid Glass de macOS 26 n'en a pas (ta VM, sans accélération graphique, montre un
  Dock gris uni : pas de référence pour le verre, ton retour fait foi).

## Choix
- **Outil Capture** : pendant sa session, le Dock et la barre quittent la couche « toujours au premier plan » (sous le
  voile, toujours au-dessus des apps), et le Dock prend le chemin « pointeur ailleurs » (au repos, clics traversants) ;
  tout revient à la fin. Règle pure `placementDuringToolSession`.
- **Verre** : reflet du liseré (verre GPU) et liseré clair (rendu de repli) à 0 ; migration v8 de `dock-metrics.json`
  qui les met à 0 quelle que soit la valeur d'avant (0,6 chez toi), le reste du verre inchangé.
- **Config** : le Dock compare l'empreinte (existence, date, taille) de `settings.json` et `dock-metrics.json` avant de
  relire ; une capture du verre en pause n'est plus relancée par un rechargement (sa reprise s'en charge).
- **Aperçus de piles** : une seule entrée par pile et par taille dans le cache du Dock.
- **Relais du curseur** : au rythme de l'écran du Dock (6 ms à 165 Hz) après un mouvement, sur une minuterie haute
  résolution ; ~10 Hz au repos, comme avant.

## Tâches
- [x] 1. ⊞⇧S (5a54594) : test de la règle rouge, puis vert ; sonde réelle avant (Dock et barre devant les voiles) et
  après (voiles devant, Dock traversant, aucune animation pendant le voile).
- [x] 2. Verre sans contour (59b306d, 78d257b) : test de la migration v8 rouge, puis vert ; test de rendu retourné (plus
  de liseré clair au bord, toujours pas de halo).
- [x] 3. Rechargement de config et pause du verre (a4cd979) : test de l'empreinte rouge, puis vert.
- [x] 4. Aperçus de piles (54f1f44) : 100 renouvellements, un seul aperçu en vie ; rouge, puis vert.
- [x] 5. Relais du curseur (2a52548) : test rouge, puis vert ; mesure réelle, crochet aveugle (curseur déplacé par
  SetCursorPos) : 803 lectures et 805 images en 5 s (avant : 165 lectures, ~33 images par seconde).
- [x] 6. Relecture indépendante (opus : avec corrections, aucun critique), corrections, fusion locale.

## Relecture indépendante du plan (opus) et corrections
- **Coin actif déclenché à la fin d'un ⊞⇧S** (important) : un coin atteint sous le voile partait quand le Dock reprenait
  le pointeur. Les coins sont suivis en mode bloqué pendant ce temps (33d2a89).
- **Barre abaissée pour toutes les captures** (important) : ⊞⇧3 la faisait passer sous les fenêtres « toujours au premier
  plan », puis devant Apps et Mission Control ouverts. Le message du Dock a maintenant un mode « sous le voile » ; la
  barre ne s'abaisse que pour lui et ne remonte que si elle l'était (7a1d698).
- **Dock insensible pendant l'éditeur de l'outil** (mineur regradé important : jusqu'à 2 min tant que l'éditeur est
  devant, 3 s après ⊞Impr. écran) : abaissés seulement quand le voile est attendu (⊞⇧S, Impr. écran seul) et tant qu'il
  est devant (fenêtre de l'outil qui couvre son écran), en l'attendant au plus 1,5 s ; le Dock passe sous le voile
  avant d'arrêter la capture du verre (7a1d698).
- **Empreinte des réglages prise après leur lecture** (important) : une écriture de l'app Réglages entre les deux était
  ignorée ; empreintes prises avant (6c00f51).
- **Verre figé dépoli** après une perte du périphérique graphique ou un changement de carte pendant une pause
  (important) : la pause ne tient que si sa raison tient encore (58abd7f).
- **Essai réel** (⊞⇧S simulé au clavier, MacDock compilé depuis la branche) : Dock et barre sous le voile 15 ms après
  la touche ; le voile (fenêtre XamlWindow de SnippingTool, 3840×2160, non agrandie) est reconnu, ils y restent ; Échap :
  de nouveau devant tout en 106 ms.

## Mesures
- **Balayage du Dock, crochet actif** : 946 images en 5 s (~190 par seconde), aucune image au-dessus de 4 ms.
- **Arrêt de MacDock** (version de `main`, avant ce plan) : 410 ms.

## Reportés
- Poignée des séparateurs (curseur ↕, redimensionner le Dock en glissant) : plan à part, mesuré sur ta VM (zone de
  ±4 points environ autour du trait).
- Mineurs de la relecture d'ensemble non traités ici (journal).
- Mineurs de la relecture de ce plan : fil des crochets (lecture de la fréquence après la pose des crochets, boucle à
  100 % si les minuteries échouent, fréquence lue une seule fois, arrêt sans événement) ; reflet des panneaux retiré
  aussi en mode clair par la v8 (à confirmer avec toi) ; dates grossières sur un partage réseau ; préfixe des aperçus de
  piles trouvé en comptant les « | ».
