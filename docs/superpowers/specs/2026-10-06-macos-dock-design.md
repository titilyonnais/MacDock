# MacDock — Spécification de conception (sous-projet 1 : le Dock)

- **Date :** 2026-10-06
- **Statut :** en attente de relecture
- **Référence visuelle :** macOS Tahoe 26 (Liquid Glass)

## 1. Contexte et objectif

L'objectif global est de donner à Windows 11 l'apparence et le comportement de macOS. Le projet est découpé en sous-projets indépendants, chacun avec sa propre spec, son plan et son implémentation :

1. **Le Dock** ← *cette spec*
2. La barre de menus (logo, menus de l'app active, icônes d'état, heure, Centre de contrôle)
3. Les animations de fenêtres (effet génie / échelle à la réduction et à la restauration, ouverture/fermeture)
4. Les « feux tricolores » sur les barres de titre
5. Le thème global (polices, coins, ombres, curseurs, fond d'écran)
6. *(optionnel)* La réplique de l'écran « Apps » de Tahoe (grille plein écran en verre)

**Critère de réussite du sous-projet 1 :** la barre des tâches Windows est remplacée par un Dock visuellement et fonctionnellement fidèle à celui de macOS Tahoe 26, stable au quotidien, qui ne laisse jamais l'utilisateur sans barre en cas de problème.

### Ce que l'utilisateur a demandé
- Un Dock « à l'identique » de macOS, avec toutes ses fonctions.
- La version de référence est macOS Tahoe 26 (Liquid Glass).
- L'utilisation de Windhawk.

### Limites assumées (validées avec l'utilisateur)
- Le code de macOS est fermé : la fidélité s'obtient par **mesure et calibration** contre des références, pas par copie.
- **Aucune ressource Apple n'est intégrée au projet** (icônes, logos, police SF Pro). L'utilisateur peut fournir ses propres icônes et installer SF Pro lui-même ; sinon on utilise Inter puis Segoe UI Variable.
- « Zéro bug » ne peut pas être garanti ; la conception privilégie la récupération automatique.

## 2. Architecture

Approche **hybride** : une application native pour tout ce qui se voit, plus un petit mod Windhawk pour ce qui exige une injection dans `explorer.exe`.

### 2.1 Livrables

| Livrable | Rôle |
|---|---|
| `MacDock.exe` | Application C++20 native (Win32 + D3D11 + DirectComposition + Direct2D/DirectWrite). Dessine et pilote le Dock. |
| `MacDockLauncher.exe` | Lanceur minuscule qui démarre `MacDock.exe` et le relance en cas de plantage. |
| `macdock-hide-taskbar.wh.cpp` | Mod Windhawk injecté dans `explorer.exe` : cache la barre des tâches native et relaie ses informations (badges, progression, attention). |

### 2.2 Modules de `MacDock.exe`

Chaque module a une seule responsabilité et une interface claire. Les modules « logique » (`AppModel`, `DockLayout`, `Animator`, `Config`, `Ipc`) ne dépendent d'aucune API graphique et sont testables unitairement.

| Module | Responsabilité | Dépend de |
|---|---|---|
| `Config` | Lecture et écriture de `settings.json` (préférences) et `dock-metrics.json` (mesures visuelles), rechargement à chaud, repli sur les valeurs par défaut. | — |
| `AppModel` | Liste ordonnée des éléments du Dock : apps épinglées, apps ouvertes non épinglées, apps récentes, fenêtres réduites, piles, Corbeille. État de chaque élément (ouvert, en lancement, demande d'attention, badge, progression). Persistance de la liste épinglée. | `Config` |
| `WindowTracker` | Suivi des fenêtres de premier niveau via `SetWinEventHook` et `RegisterShellHookWindow`. Regroupe les fenêtres par application (AppUserModelID, sinon chemin de l'exécutable). Émet des événements vers `AppModel`. | Win32 |
| `IconProvider` | Extraction des icônes en 256 px et plus (`IShellItemImageFactory` ; apps du Store via leur manifeste de package). Application du masque squircle, « icon jail » Tahoe, dossier d'icônes personnalisées, cache. | WIC, Shell |
| `DockLayout` | Fonctions pures : position et taille de chaque élément selon la position du curseur, magnification, écartement, séparateurs, dimensions du fond. | `Config` |
| `Animator` | Ressorts amortis et courbes : magnification, rebonds, apparition/disparition d'éléments, « poof », masquage automatique, réorganisation. | — |
| `GlassRenderer` | Capture de l'arrière-plan, flou, shader Liquid Glass, composition des icônes, indicateurs, infobulles, menus. | D3D11, DComp, D2D |
| `Interaction` | Test de collision, survol, clics, glisser-déposer (interne et OLE depuis l'Explorateur), menus contextuels. | `AppModel`, `DockLayout` |
| `Shell` | Lancement des apps, activation/restauration des fenêtres, Corbeille (état, vidage), piles de dossiers, ouverture du menu Démarrer, miniatures DWM. | Shell, DWM |
| `Ipc` | Serveur du named pipe `\\.\pipe\MacDock` : battement de cœur et réception des événements du mod. | Win32 |

### 2.3 Rôle du mod Windhawk `macdock-hide-taskbar`

- Cache `Shell_TrayWnd` et toutes les `Shell_SecondaryTrayWnd`, et rend leur zone de travail à l'écran.
- Ne cache la barre **que tant que** `MacDock.exe` envoie un battement de cœur (toutes les 1 s). Après **5 s** de silence, il réaffiche la barre native.
- Intercepte, côté `explorer.exe`, les appels `ITaskbarList3` reçus par la barre (`SetOverlayIcon`, `SetProgressState`, `SetProgressValue`) ainsi que les clignotements (`HSHELL_FLASH`), et les transmet au Dock par le pipe.
- Se désactive proprement : couper le mod dans Windhawk remet la barre Windows en place immédiatement.

### 2.4 Flux de données

```
Système (fenêtres, Shell)            explorer.exe + mod Windhawk
        │ WinEvent / ShellHook              │ named pipe (badges, progression, attention)
        ▼                                   ▼
  WindowTracker ──événements──►  AppModel  ◄── Ipc
                                    │
                         Config ──► DockLayout ──► Animator
                                    │                 │
                                    ▼                 ▼
                     Interaction ◄────────── GlassRenderer ──► écran
                          │
                          └──► Shell (lancer, activer, restaurer, Corbeille…)
```

## 3. Apparence et rendu

### 3.1 Unités
Toutes les mesures sont exprimées en **points macOS** puis multipliées par l'échelle DPI Windows de l'écran (par exemple 1 pt = 1,5 px à 150 %). Le Dock gère le DPI par écran (`PER_MONITOR_AWARE_V2`).

### 3.2 Calibration « à l'identique »
- **Références :** captures haute définition et vidéos à 60 ou 120 i/s du Dock de Tahoe (clair/sombre, avec/sans magnification, lancement, réduction, masquage), stockées dans `reference/` avec leur source.
- **Mode calibration** (raccourci de débogage) : superpose une image de référence en transparence réglable au-dessus du rendu, et peut afficher une carte de différence pixel par pixel.
- **Animations :** durées et courbes mesurées image par image sur les vidéos de référence.
- **Toutes les mesures visuelles** (rayon, marges, tailles, opacités, flou, réfraction, ressorts, délais) sont regroupées dans `dock-metrics.json`, rechargé à chaud sans recompilation.

### 3.3 Valeurs de départ (à calibrer)

| Paramètre | Départ | Source |
|---|---|---|
| Taille d'icône (`tilesize`) | 48 pt | Valeur par défaut de macOS |
| Taille maximale en magnification (`largesize`) | 128 pt (plage 16–128) | Plage de macOS |
| Magnification | activée | Choix de l'utilisateur (désactivée par défaut sur macOS) |
| Rayon d'influence de la magnification | ≈ 3 icônes de chaque côté | Estimation, à calibrer |
| Forme du fond et des icônes | coins continus (superellipse) | Apparence de Tahoe |
| Point indicateur | ≈ 4 pt de diamètre, sous l'icône | Estimation, à calibrer |

### 3.4 Pipeline Liquid Glass (à chaque image)
1. **Capture** de l'arrière-plan avec l'API Desktop Duplication. La fenêtre du Dock en est exclue avec `SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE)`. Seule la zone sous le Dock, avec une marge, est copiée.
2. **Flou** gaussien séparable, à résolution réduite.
3. **Shader** (HLSL) :
   - forme : distance signée d'un rectangle à coins continus ;
   - bord biseauté qui **réfracte** l'arrière-plan selon un profil de lentille (loi de Snell), avec une légère aberration chromatique concentrée sur le biseau ;
   - reflet de Fresnel sur le pourtour et liseré spéculaire ;
   - teinte adaptative selon la luminance moyenne de l'arrière-plan ;
   - ombre portée douce.
4. **Repli :** si la duplication est indisponible (contenu protégé, plein écran exclusif, session distante), on utilise le flou acrylique système (`DWMWA_SYSTEMBACKDROP_TYPE`) avec une teinte équivalente. Pas d'écran noir, pas de plantage.

### 3.5 Icônes
- Extraction à la meilleure résolution disponible, puis masque squircle.
- **Mode « Tahoe strict »** (activé par défaut) : une icône non carrée est placée dans un squircle de verre gris, comme sur macOS. Désactivable dans les réglages.
- **Icônes personnalisées :** `%APPDATA%\MacDock\icons\<identifiant-app>.png`, prioritaires sur l'icône extraite.
- Le Dock dessine ses propres icônes pour les éléments système (Corbeille vide ou pleine, Apps, piles).

### 3.6 Thème et typographie
- Le Dock suit le mode clair ou sombre de Windows et se met à jour à chaud.
- Police des infobulles et menus, par ordre de préférence : SF Pro (si installée par l'utilisateur), Inter, Segoe UI Variable. Rendu DirectWrite.

## 4. Comportements

### 4.1 Magnification
- L'échelle de chaque icône suit une courbe en cloche selon sa distance au curseur, sur le rayon d'influence.
- Les voisines s'écartent et le fond s'étire en conséquence.
- Les icônes grossissent depuis leur base. La fenêtre du Dock est plus haute que sa partie visible, transparente aux clics hors des zones utiles, donc **aucun rognage**.
- La magnification s'enclenche et se relâche avec une animation à l'entrée et à la sortie du curseur.

### 4.2 Indicateurs et retours
- **Point** sous chaque application ouverte.
- **Rebond de lancement :** répété jusqu'à l'apparition de la première fenêtre de l'app, avec un délai maximal configurable.
- **Rebond d'attention :** plus haut, déclenché par un clignotement de fenêtre relayé par le mod.
- **Badge rouge :** affiché quand l'app pose une icône de superposition dans la barre Windows. Les badges Windows sont des icônes, pas des nombres : le Dock affiche une pastille rouge simple, avec un nombre seulement s'il peut être déduit de l'icône.
- **Barre de progression** sous l'icône, à partir de `SetProgressValue` et `SetProgressState`.
- **Infobulle :** nom de l'app dans une bulle de verre au-dessus de l'icône survolée, avec un fondu.

### 4.3 Clics
- App non ouverte → lancement et rebond.
- App ouverte → mise au premier plan de toutes ses fenêtres ; ses fenêtres réduites se restaurent.
- **Fenêtres réduites :** elles apparaissent dans la partie droite du Dock sous forme de miniatures DWM en direct (`DwmRegisterThumbnail`). Un clic les restaure.
- Pile → ouverture en éventail, en grille ou en liste.
- Corbeille → ouverture de la Corbeille Windows.
- Apps → ouverture du menu Démarrer (version 1).

### 4.4 Glisser-déposer
- **Réorganisation** des éléments épinglés : les voisines glissent pour faire de la place.
- **Retrait :** une icône tirée au-delà d'un seuil au-dessus du Dock affiche l'étiquette « Supprimer » ; relâchée, elle disparaît dans un nuage « poof » animé dessiné par le Dock. Une app ouverte n'est que désépinglée et reste dans le Dock.
- **Épinglage :** déposer un raccourci ou un exécutable dans la zone des apps l'épingle à cet endroit.
- **Fichiers déposés** sur une app → ouverture avec cette app ; sur la Corbeille → mise à la Corbeille ; sur une pile → déplacement dans le dossier.

### 4.5 Menus contextuels (en verre)
- **Sur une app :** Options › (Garder dans le Dock, Ouvrir à la connexion, Afficher dans l'Explorateur), liste de ses fenêtres, Afficher toutes les fenêtres, Masquer (réduit toutes ses fenêtres sans créer de miniatures dans le Dock ; un clic sur l'app les restaure), Quitter (envoie `WM_CLOSE` à toutes ses fenêtres).
- **Sur le séparateur :** Activer/désactiver le masquage, Activer/désactiver l'agrandissement, Position à l'écran › (Gauche, En bas, Droite), Réglages du Dock (ouvre `settings.json` dans la version 1).
- **Sur la Corbeille :** Ouvrir, Vider la Corbeille (avec la confirmation standard de Windows).
- **Sur une pile :** Afficher comme (Pile, Dossier), Présentation (Éventail, Grille, Liste, Automatique), Trier par, Ouvrir dans l'Explorateur.

### 4.6 Partie droite et sections
- Ordre : apps épinglées, séparateur, apps récentes (3 au maximum, option activée par défaut comme sur macOS), séparateur, piles, fenêtres réduites, Corbeille.
- Pile par défaut : Téléchargements.

### 4.7 Écran et masquage
- Position en bas, à gauche ou à droite.
- **Masquage automatique :** délai d'apparition et animation calibrés sur macOS. Le Dock masqué ne réserve aucune zone de travail.
- **Dock non masqué :** il réserve sa hauteur de repos (hors magnification) comme zone de travail de l'écran, via une barre d'application (`SHAppBarMessage`).
- **Multi-écran :** le Dock passe sur l'écran où le curseur est poussé contre le bord du Dock et y reste.
- **Plein écran :** le Dock se cache quand l'application au premier plan occupe tout l'écran.

## 5. Robustesse

### 5.1 Communication avec le mod
- Canal : named pipe `\\.\pipe\MacDock`, messages binaires à longueur préfixée et versionnés.
- Battement de cœur toutes les 1 s ; la barre native réapparaît après 5 s de silence.
- Si le pipe n'existe pas encore, le mod garde la barre native visible et réessaie périodiquement.

### 5.2 Relance
- `MacDockLauncher.exe` relance `MacDock.exe` en cas de sortie anormale.
- Après **3 plantages en 60 s**, il abandonne : la barre native revient d'elle-même (plus de battement) et une notification Windows indique l'emplacement du journal.

### 5.3 Situations gérées
| Situation | Comportement |
|---|---|
| Redémarrage de l'Explorateur (`TaskbarCreated`) | Réenregistrement des hooks et de la barre d'application |
| Changement de DPI, de résolution, d'écrans | Recalcul de la mise en page et recréation des ressources graphiques |
| Perte du périphérique GPU | Recréation du périphérique D3D et des ressources |
| Icône introuvable | Icône générique |
| Duplication d'écran indisponible | Repli acrylique |
| `settings.json` ou `dock-metrics.json` invalide | Valeurs par défaut, fichier fautif sauvegardé en `.bak`, entrée dans le journal |

### 5.4 Performance
- Au repos, aucun redessin : rendu uniquement sur changement (animation, survol, nouvelle image de l'arrière-plan).
- La duplication d'écran ne livre une image que si le contenu change.
- Animations cadencées sur la fréquence de rafraîchissement de l'écran (DirectComposition).

### 5.5 Journal
`%APPDATA%\MacDock\logs\`, rotation des fichiers.

## 6. Installation et construction

- **Construction :** CMake et Ninja fournis par Visual Studio 2022, compilateur MSVC, C++20. Dépôt Git dans `macos-dock`.
- **Démarrage :** entrée `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` pointant vers `MacDockLauncher.exe`. Aucun droit administrateur.
- **Mod :** le fichier `windhawk/macdock-hide-taskbar.wh.cpp` est compilé et activé depuis l'éditeur de Windhawk.
- **Désinstallation :** couper le mod et retirer l'entrée de démarrage. Rien d'autre n'est modifié dans le système.

### Arborescence prévue
```
macos-dock/
  CMakeLists.txt
  src/
    app/        (main, boucle de messages, lanceur)
    config/  model/  tracker/  icons/  layout/  anim/
    render/     (D3D11, DComp, D2D, shaders HLSL)
    interact/  shell/  ipc/
  windhawk/macdock-hide-taskbar.wh.cpp
  tests/        (doctest)
  reference/    (captures et vidéos de référence + sources)
  docs/superpowers/specs/
```

## 7. Tests

- **Unitaires (doctest) :** `DockLayout` (magnification, écartement, dimensions), `Animator` (ressorts, rebonds), regroupement des fenêtres par app, `Config` (lecture, valeurs par défaut, fichiers invalides), protocole `Ipc`.
- **Rendu :** rendu hors écran vers une texture, comparé à des images de référence avec une tolérance.
- **Calibration :** comparaison visuelle avec le vrai macOS via le mode calibration.
- **Vérifications manuelles :** multi-écran, plein écran, redémarrage de l'Explorateur, plantage simulé (la barre native doit revenir), changement de DPI, mode clair/sombre, glisser-déposer depuis l'Explorateur.

## 8. Hors périmètre de ce sous-projet

- Barre de menus, animations de fenêtres (génie), feux tricolores, thème global : sous-projets 2 à 5.
- Réplique de l'écran « Apps » : sous-projet optionnel 6.
- Fenêtre de réglages graphique façon « Bureau et Dock » : la version 1 passe par le menu du séparateur et `settings.json`.
