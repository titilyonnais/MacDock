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
- [ ] 1. **Conversions** : `formatNumber(v, chiffres)` ; `convertUnits` (analyse, table des unités, unité par défaut,
  échelle des durées). Tests : formes, casse, nombres, chaque catégorie, températures négatives, refus.
- [ ] 2. **Réglages** : `pane_catalog.*` (déplacement) ; `spot_settings.*` (table, mots-clés, recherche classée).
  Tests : « wifi », « fond d'écran », « mot de passe », accents et casse, « clavier » (MacDock d'abord), cibles
  `ms-settings:` toutes valides, clés MacDock toutes connues de `paneFromKey`.
- [ ] 3. **Résultats** : `spotlightResults` avec conversions et réglages (meilleur résultat, section, limites),
  `wantsFileSearch`. Tests.
- [ ] 4. **Fenêtre et Dock** : `SpotKind::Setting` et `SpotKind::Pane` ; icône de l'app Paramètres (chargeur), tuiles
  des sections (dessin) ; quatre sections ; choix dans le Dock (`spotAction` pure, testée) ; rendu hors écran.
- [ ] 5. **Essais réels** : rendus hors écran (« wifi », « 10 km », « dock »), Dock recompilé et relancé, Spotlight
  ouvert par son message, requêtes envoyées à sa fenêtre, capture de sa seule fenêtre, fermeture par Échap (rien
  lancé, presse-papiers non touché).

  Puis documentation, relecture indépendante, corrections, fusion locale.
