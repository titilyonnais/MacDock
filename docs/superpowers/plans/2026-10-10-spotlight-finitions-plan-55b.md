# Spotlight, finitions du plan 55 — mineurs

> **Pour les agents :** exécution en ligne, tests d'abord.

**But :** les mineurs du plan 55 que la logique seule règle (relevés dans le journal, « Mineurs reportés — plan 55 »).

## Périmètre
- **Conversions** (`spot_convert`) : moins écrit avec un tiret demi-cadratin (U+2013) ou le signe moins (U+2212) ;
  espaces fine (U+2009), fine insécable (U+202F) et insécable (U+00A0) entre les milliers ; mots de liaison sans
  tenir compte de la casse (« EN », « IN », « TO »).
- **Recherche des réglages** (`spot_settings`) : espaces et traits d'union interchangeables ou facultatifs (« wi fi »,
  « wifi », « wi-fi » ; « haut parleurs », « hautparleurs ») ; singulier et pluriel tolérés dans les noms des sections
  (« capture d'écran » trouve « Captures d'écran »).
- **Hors périmètre** (à décider avec toi) : « 1 GO », « 1 ML » (la casse y change le sens) ; l'ordre de « mise à jour ».

## Choix
- Pluriels tolérés dans les **noms** seulement, pour les mots de quatre lettres ou plus : dans les mots-clés,
  « mises à jour » trouverait aussi « mise à jour de logiciels » (À propos de MacDock) avant Windows Update.
- Nom exact aux pluriels près **en second recours** : « son » reste la page Son de Windows (nom exact) avant la section
  Sons de MacDock ; « capture d'écran » donne la section Captures d'écran quand aucun nom n'est exact tel quel.
- Une recherche vidée par la normalisation (« -- ») ne trouve rien, comme avant.
- Le dernier mot tapé n'est jamais mis au singulier : il est peut-être en cours de frappe, ou n'est pas un pluriel.

## Tâches
- [x] 1. Conversions : tests puis code.
- [x] 2. Recherche des réglages : tests puis code ; gardes (« son », « mises à jour », « -- », « pas »).
- [x] 3. Rendus `--spotlight-snapshot` de « –40 °C en °F », « wi fi », « capture d'écran » : bon premier résultat
  (−40 °F ; Wi-Fi des Paramètres Windows ; Captures d'écran de Réglages MacDock), puis effacés.
- [x] 4. Relecture indépendante (opus) : « Avec corrections ». Le dernier mot tapé n'est plus mis au singulier
  (« tous », « docs », « mais », « mods » trouvaient de faux noms devant les documents) ; les deux passes du nom exact
  sont couvertes avec la vraie table (vues rouges par mutation) ; test « EN » renforcé ; commentaires à jour. Coût
  accepté : « sons » ne trouve plus la page Son de Windows (la section Sons reste), « couleurs » plus Filtres de couleur.

## Gardes (comportements d'aujourd'hui, inchangés)
« 4K », « 3d », « 12 pt », « 2nd », « 100C » ne sont pas convertis ; « 7 days » cherche d'abord l'app « 7 Days to Die » ;
l'app Caméra reste devant la page Caméra ; « son » ne trouve pas « Personnalisation » ; « 1 IN » n'est pas un pouce ;
« 2–3 km » n'est pas un nombre négatif.
