# App « Réglages MacDock » — conception

## Contexte

Aujourd'hui, « Réglages du Dock… » et « Réglages de la barre des menus… » ouvrent les fichiers JSON
(`settings.json`, `menubar.json`) dans un éditeur de texte. Le 8 octobre 2026, tu as demandé une vraie app,
« propre, clean, évidemment au style de macOS, absolument magnifique, et surtout 100 % fonctionnelle », pour tout
gérer dans MacDock.

Tes choix :
- **allure :** comme les Réglages Système de macOS 27 ;
- **fonctions en plus des réglages :** gestion des mods Windhawk, démarrage avec Windows, sauvegarde des réglages,
  raccourcis modifiables.

Cotes de référence : `docs/recherches/2026-10-08-cotes-macos.md`, §2 (Réglages Système), §3 (contrôles),
§4 (menus) et §7 (couleurs).

## Objectif

Créer un exécutable `MacDockSettings.exe` qui :
- s'ouvre depuis le Dock (« Réglages du Dock… »), la barre (« Réglages de la barre des menus… », et  > « Réglages
  MacDock… ») et le menu Démarrer ;
- règle tout ce que les fichiers JSON permettent, avec application immédiate : le Dock et la barre relisent déjà leurs
  fichiers à chaque écriture ;
- respecte les règles du projet : aucune ressource Apple (pictogrammes et icône dessinés par nous en Direct2D), SF Pro
  si elle est installée.

## Architecture

### Processus et fichiers
- **Nouvelle cible `settings` dans `build.ps1`** : `MacDockSettings.exe`, Win32, Direct2D, DirectWrite et DirectComposition,
  sans dépendance nouvelle.
- **Une seule instance :** une mutation nommée. Une seconde ouverture remet la fenêtre existante au premier plan et
  ouvre la section demandée (`--pane dock|menubar|…`), par un message enregistré.
- **Lecture et écriture des réglages :** `settings.json` et `menubar.json` passent par les fonctions existantes
  (`settingsFromJson`/`ToJson`, `menuBarSettingsFromJson`/`ToJson`, `config_store`).
  - L'écriture est atomique (fichier temporaire puis `MoveFileEx` avec remplacement), pour que le Dock ne lise jamais
    un fichier à moitié écrit.
  - Les clés inconnues du JSON sont conservées : on relit le fichier, on fusionne nos valeurs et on réécrit.
- **Fichier modifié à la main pendant que l'app est ouverte :** l'app surveille le dossier et recharge l'affichage.

### Boîte à outils d'interface `src/ui/` (logique testable, sans fenêtre)
- **Arbre de vues retenu :**
  - une vue a un cadre, des enfants, une mise en page, un dessin, un test de position, le focus et des événements ;
  - chaque contrôle est une vue.
- **Mise en page :** des piles verticales et des lignes aux hauteurs fixes en points. L'échelle (DPI) est appliquée
  au dessin, pas dans la mise en page.
- **Dessin :** Direct2D sur une chaîne d'échange DirectComposition. Les animations passent par le moteur
  `src/anim/motion` (ressorts façon SwiftUI pour les interrupteurs, fondu 0,18 s pour les sections).
- **Contrôles**, aux cotes de la recherche :

  | Contrôle | Cotes |
  |---|---|
  | Interrupteur | Mini dans les lignes : 26×15 pt ; normal pour le réglage principal d'une section : 38×22 pt |
  | Menu déroulant | Hauteur 20 pt, double chevron ; ouvre un menu `MenuWindow` existant |
  | Curseur | Piste de 4 pt, bouton de 20 pt, graduations facultatives, valeur affichée |
  | Contrôle segmenté | Hauteur 22 pt |
  | Bouton poussoir | 22 pt de haut, rayon 6, bouton par défaut en couleur d'accent |
  | Champ de recherche | Hauteur 28 pt, rayon de capsule, loupe dessinée |
  | Enregistreur de raccourci | Champ qui affiche le raccourci en symboles (⌃⌥⇧⊞), écoute au clic, Échap pour annuler |
  | Ligne de réglage | 36 pt (44 pt avec un sous-titre) : libellé de 13 pt à gauche, contrôle à droite, séparateur aligné sur le texte |
  | Groupe arrondi | Rayon 12, fond gris très clair (clair) ou blanc à 5 % (sombre), marges latérales de 20 pt |

  La fenêtre a aussi ses propres pastilles rouge, jaune et vert, à gauche, comme une vraie app macOS : c'est notre
  fenêtre, sans superposition.

### Fenêtre
- **Cadre :** largeur fixe de 715 pt, hauteur redimensionnable (470 pt au minimum), coins arrondis par DWM.
- **Cadre personnalisé :** `DwmExtendFrameIntoClientArea` et `WM_NCCALCSIZE` ; la zone de titre déplace la fenêtre et
  le double-clic l'agrandit.
- **Barre latérale :** 215 pt, bord à bord, sans ombre (Golden Gate), verre du projet ou fond translucide.
  - Champ de recherche en haut.
  - Sections précédées d'une tuile colorée de 20 pt (pictogramme blanc dessiné en Direct2D), ligne de 28 pt,
    sélection par un rectangle arrondi gris de rayon 6.
- **Contenu :** titre de la section (Title 2, 17 pt semi-gras) dans la zone de titre, puis groupes et lignes. Défilement
  à la molette et au pavé, avec une barre superposée fine qui disparaît.
- **Clair et sombre :** suivent `AppsUseLightTheme`, avec bascule en direct.
- **Clavier :**
  - Tab et Maj+Tab entre contrôles, flèches dans la barre latérale ;
  - Espace bascule un interrupteur ;
  - Ctrl+F (⌘F) va à la recherche, Ctrl+W ferme ;
  - anneau de focus bleu de 3 pt.

### Sections et réglages

1. **Général**
   - Démarrage avec Windows : clé `HKCU\…\Run` vers `MacDockLauncher.exe`.
   - État de MacDock (en marche ou arrêté), boutons Relancer et Quitter.
   - Sauvegarde : exporter tous les réglages dans un `.json`, importer, rétablir les réglages par défaut (avec une
     alerte de confirmation).
2. **Dock**
   - Taille (curseur 16 à 128) ; agrandissement (interrupteur et curseur de taille agrandie).
   - Position (Gauche, Bas, Droite) et écran (menu des écrans).
   - Masquer automatiquement ; afficher les apps récentes.
   - Effet de réduction (Génie, Échelle, Windows) ; verre Liquid Glass ; icônes strictes Tahoe.
3. **Barre des menus**
   - Masquer automatiquement.
   - Horloge : options de `ClockOptions`.
   - Icônes : son, Wi-Fi, batterie, recherche, icônes des autres apps.
   - Pastille du volume et « pop » du volume.
4. **Fenêtres** : pastilles (Fenêtres à barre de titre, Toutes, Aucune) ; apparence macOS des fenêtres (coins, barre
   de titre).
5. **Bureau et Mission Control** : coins actifs (quatre menus d'actions), raccourcis de Mission Control et d'Exposé.
6. **Clavier**
   - Alt de gauche joue ⌘.
   - Raccourcis modifiables, avec l'enregistreur : Spotlight, Mission Control, Exposé de l'app, sélecteur d'apps.
   - Un conflit entre deux raccourcis est signalé en rouge sous la ligne.
7. **Captures d'écran** : ⊞⇧3, ⊞⇧4 et ⊞⇧5 façon macOS (interrupteur).
8. **Sons** : sons système.
9. **Police** : police du Dock et de la barre (Automatique, ou les familles installées).
10. **Mods Windhawk**
    - Windhawk installé ou non.
    - Pour chaque mod MacDock (`macdock-look` aujourd'hui, les autres ensuite) : version installée et version du
      dépôt, état activé ou non.
    - Boutons Installer, Mettre à jour et Désinstaller, qui lancent l'installateur PowerShell existant (Windows demande
      l'autorisation administrateur).
11. **À propos** : version de MacDock, dossier des réglages (« Afficher dans l'Explorateur »), journal.

**Recherche :** elle filtre la barre latérale et surligne les lignes qui correspondent, sur leur libellé et leurs
mots-clés.

### Raccourcis
- Le format reste celui des réglages (`alt+space`, `ctrl+alt+up`…).
- L'enregistreur accepte toute combinaison avec au moins un modificateur, Win compris. Les combinaisons réservées de
  Windows (Win+L, Ctrl+Alt+Suppr) sont refusées avec un message.
- **À vérifier dans le code du Dock :** il ne reconnaît peut-être que des valeurs fixes. Si c'est le cas, l'analyse des
  raccourcis (`menubar/shortcut`) est généralisée et testée, et le Dock enregistre n'importe quelle combinaison valide.

### Mods Windhawk
- **Lecture sans droits administrateur :** `HKLM\SOFTWARE\Windhawk\Engine\Mods\local@<id>` (Version, Disabled) et la
  présence de Windhawk.
- **Version du dépôt :** lue dans `@version` des sources livrées avec MacDock.
- **Actions :** `ShellExecuteEx` avec le verbe « runas » sur `install-macdock-look.ps1` (et ses frères), avec ou sans
  `-Uninstall`. L'app attend la fin de l'installateur, puis relit l'état.

## Tests
- **Logique pure** (dans `tests.exe`) :
  - mise en page des lignes et des groupes ;
  - test de position et ordre du focus ;
  - filtrage de la recherche ;
  - fusion JSON qui conserve les clés inconnues ;
  - écriture atomique ;
  - analyse et affichage des raccourcis (symboles) ;
  - détection des conflits de raccourcis ;
  - lecture de la version d'un mod dans sa source ;
  - commande de démarrage (clé Run) ;
  - export, import et retour aux défauts.
- **Rendu hors écran (WARP)** de chaque contrôle en clair et en sombre : pixels de couleur aux bons endroits.
- **Essai réel :**
  - captures de chaque section en clair et en sombre ;
  - basculer un réglage et vérifier que le Dock ou la barre change ;
  - l'écriture atomique sous écritures concurrentes.

## Hors champ
- Traduction dans d'autres langues : tout est en français, comme le reste du projet.
- Réglages de `dock-metrics.json` (mesures fines, pour l'étalonnage).
- Fond d'écran, thème et curseurs (déjà gérés à part).

## Découpage en plans
- **Plan 41 :**
  - boîte à outils `src/ui/` (vues, mise en page, contrôles, rendu, focus, défilement) ;
  - fenêtre et barre latérale ;
  - sections Dock, Barre des menus et Fenêtres, appliquées en direct ;
  - liaison depuis le Dock et la barre.
- **Plan 42 :**
  - sections Général (démarrage, sauvegarde), Bureau et Mission Control, Clavier (enregistreur), Captures, Sons,
    Police, Mods Windhawk, À propos ;
  - recherche.
- **Plan 43 :** finitions (animations, accessibilité au clavier complète, icône de l'app dans le Dock), relecture
  complète et essais réels.
