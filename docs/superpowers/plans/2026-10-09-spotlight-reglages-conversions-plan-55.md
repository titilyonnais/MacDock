# Spotlight : réglages et conversions — plan 55

> **Pour les agents :** exécution en ligne, tâche par tâche, tests d'abord. Reprendre à la première tâche non cochée.

**But :** comme Spotlight sur macOS 26, trouver les réglages (pages des Paramètres de Windows et sections de Réglages
MacDock, dans une section « Réglages ») et convertir les unités (« 10 km », « 100 °F en °C ») à la manière de la
calculatrice. Point 1 de la feuille de route.

Session autonome : aucune question possible, choix faits ici ; aucun push.

## Choix
- **Conversions** (`src/spotlight/spot_convert.*`, logique pure) :
  - formes : « <nombre> <unité> » (vers l'unité usuelle de l'autre système) et « <nombre> <unité> en|in|to|vers|→|->
    <unité> » ;
  - nombre : signe (« - » ou « − »), virgule ou point décimal, milliers séparés par des espaces (« 1 500 m ») ;
  - catégories : longueur, masse, température, volume, surface, vitesse, durée, données (décimales et binaires),
    énergie, pression ;
  - symboles en casse exacte (« K » kelvin, mais « 4k » n'est rien ; « B » octet), noms sans casse ni accents
    (« kilomètres », « pouces », « nœuds ») ; jamais « d » pour jour (« 3d » cherche les apps 3D) ni « mb » (millibar
    ou mégaoctet ?) ;
  - unité d'arrivée par défaut : km → mi, mi → km, m → ft, kg → lb, °C → °F, °F → °C, K → °C, L → gal, km/h → mph,
    Go → Gio, Gio → Go, kcal → kJ, bar → psi… ; durée : la plus grande unité (sauf la sienne, jamais la milliseconde)
    où le nombre vaut au moins 1, sinon l'unité juste au-dessus (90 min → 1,5 h ; 48 h → 2 j ; 30 s → 0,5 min) ;
  - affichage : 6 chiffres significatifs à la française ; titre « 6,21371 mi », sous-titre « 10 km = » ; Entrée copie
    le nombre seul, comme pour un calcul (même ligne, même tuile orange) ;
  - « 10 km en kg » (catégories différentes) n'est pas une conversion ; une conversion ne lance pas la recherche de
    documents (comme un calcul).
- **Réglages** (`src/spotlight/spot_settings.*`, logique pure) :
  - les 11 sections de Réglages MacDock (nom, tuile) puis une soixantaine de pages des Paramètres de Windows 11
    (`ms-settings:`), avec leur nom français et des mots-clés (« fond d'écran » → Arrière-plan, « mot de passe » →
    Options de connexion) ; à égalité, la section de MacDock passe devant ;
  - recherche sans casse ni accents : début du nom, puis début d'un mot du nom, puis début d'un mot-clé ;
  - section « Réglages » après Applications, 4 lignes au plus ; sous-titre « Paramètres Windows » ou
    « Réglages MacDock » ;
  - meilleur résultat : un réglage dont le nom est exactement la requête passe devant les apps (« écran », « son ») ;
    sinon la première app ; sans app, le premier réglage ;
  - icônes : l'icône de l'app Paramètres pour les pages de Windows ; la tuile de la section, dessinée par le code
    comme dans l'app, pour Réglages MacDock ;
  - Entrée : la page `ms-settings:` ouverte par le Shell (sur un fil à part) ; la section par
    `MacDockSettings.exe --pane <clé>`.
- **Panneau** : quatre sections au plus (Meilleur résultat, Applications, Réglages, Documents) ; la hauteur réservée
  en tient compte.
- **Sections de Réglages MacDock** : `paneList` et ses types passent dans `src/settings/pane_catalog.*` (déplacement
  sans changement de comportement), liés dans le Dock avec `pane_icons.cpp`.

## Tâches
- [x] 1. **Conversions** : `formatNumber(v, chiffres)` ; `convertUnits` (analyse, table des unités, unité par défaut,
  échelle des durées). Tests : formes, casse, nombres, chaque catégorie, températures négatives, refus.
- [x] 2. **Réglages** : `pane_catalog.*` (déplacement) ; `spot_settings.*` (table, mots-clés, recherche classée).
  Tests : « wifi », « fond d'écran », « mot de passe », accents et casse, « clavier » (MacDock d'abord), cibles
  `ms-settings:` toutes valides, clés MacDock toutes connues de `paneFromKey`.
- [x] 3. **Résultats** : `spotlightResults` avec conversions et réglages (meilleur résultat, section, limites),
  `wantsFileSearch`. Tests.
- [x] 4. **Fenêtre et Dock** : `SpotKind::Setting` et `SpotKind::Pane` ; icône de l'app Paramètres (chargeur), tuiles
  des sections (dessin) ; quatre sections ; choix dans le Dock (`spotAction` pure, testée) ; rendu hors écran.
- [x] 5. **Essais réels** : rendus hors écran (« wifi », « 10 km », « dock »), Dock recompilé et relancé, Spotlight
  ouvert par son message, requêtes envoyées à sa fenêtre, capture de sa seule fenêtre, fermeture par Échap (rien
  lancé, presse-papiers non touché).

  Puis documentation, relecture indépendante, corrections, fusion locale.

## Fait
- **Décision en chemin** : « wifi » ne trouvait « Wi-Fi » que par un mot-clé ; les traits d'union du nom sont
  facultatifs, pour la recherche comme pour le nom exact (« wifi », « email »).
- **Défaut trouvé à l'essai réel** (antérieur au plan) : quand le panneau grandit sous un pointeur immobile, Windows
  envoie WM_MOUSEMOVE et la ligne survolée était choisie ; Entrée ouvrait alors un document au lieu du meilleur
  résultat. `HoverGate` : seul un vrai déplacement choisit la ligne survolée. Test vu en échec avant ; revérifié en
  réel avec le même pointeur.
- **Essais réels** :
  - rendus hors écran (`--spotlight-snapshot`) : « 10 km », « wifi », « écran » en sombre, « clavier » : sections,
    ordre, tuiles des sections de MacDock ; images effacées (elles montraient des noms de fichiers indexés) ;
  - Dock Release recompilé, relancé en diagnostic ; Spotlight ouvert par le message « MacDockSpotlight », requêtes
    envoyées à sa fenêtre (WM_CHAR) : « 10 km en miles » → 6,21371 mi, « wifi » → Wi-Fi avec l'icône de l'app
    Paramètres, « dock » → la section Dock en meilleur résultat ; fermé par Échap, sans Entrée (rien lancé, presse-
    papiers intact) ; captures de sa seule fenêtre, effacées.
- Suite : 760 tests verts (Debug et Release).

## Relecture indépendante
Rien de critique ; deux importants, et trois mineurs regradés importants par leur effet. Corrigés en une passe,
chacun avec un test vu en échec avant (765 tests verts) :
- **Survol** : la région du panneau change alors que le pointeur a bougé hors du panneau ; le mouvement que Windows
  envoie ensuite choisissait la ligne survolée. `HoverGate::regionChanged` à chaque changement de région ; défaut
  reproduit en réel avant le correctif (pointeur déplacé sous le champ, puis remis), vérifié après.
- **App au nom exact** : « caméra », « loupe », « narrateur » mettaient la page des Paramètres devant l'app du même
  nom ; l'app reste le meilleur résultat, la page passe dans Réglages.
- **App face à une conversion** (mineur regradé) : « 7 days » ou « 60 sec » donnaient une conversion et Entrée
  remplaçait le presse-papiers au lieu de lancer « 7 Days to Die » ; une app dont le nom commence par la requête
  passe devant.
- **Symboles courts** (mineur regradé) : « 12 pt » (pintes), « 2nd » (nœuds), « 100C », « 2F », « 1o » ; les
  symboles d'une lettre demandent une espace, « pt » et « nd » retirés.
- **Mots-clés seuls** (mineur regradé) : « image », « carte », « licence » mettaient un réglage en meilleur résultat,
  devant le premier document que Entrée ouvrait avant le plan ; un mot-clé seul n'est jamais le meilleur résultat,
  et sa section passe après les documents.
- **Entrée essayée en réel** (mineur 9) : « dock » ouvre Réglages MacDock sur la section Dock
  (`--pane dock`), « son » la page Son des Paramètres ; fenêtres ouvertes par l'essai refermées, Paramètres et
  Réglages MacDock n'étaient pas ouverts avant.
- Mineurs reportés : voir le journal (« Mineurs reportés — plan 55 »).
