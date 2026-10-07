# MacDock

Un Dock et une barre de menus façon **macOS Tahoe** pour Windows 11.

- **Dock** : magnification, rebonds, infobulles, apps épinglées et ouvertes, fenêtres réduites, Téléchargements et Corbeille. Un mod **Windhawk** cache la barre des tâches Windows tant que le Dock tourne.
- **Barre de menus** (`MacMenuBar.exe`) : transparente en haut de l'écran, avec le menu du système, le nom de l'app active, ses menus, et la date et l'heure.

> État : le Dock est complet (plans 1 à 5). La barre de menus est en cours : menus du système, de l'app et génériques, horloge (plan 6), vrais menus des apps et Éléments récents (plan 7) ; viennent ensuite les icônes d'état et le Centre de contrôle (plan 8), les icônes des autres apps et les écrans multiples (plan 9). Voir `docs/superpowers/` et `docs/journal-de-nuit.md`.

## Installation

1. **Compiler** (Visual Studio 2022 avec les outils C++ requis) :
   ```powershell
   ./build.ps1 -Target all -Config Release
   ```
   Les exécutables sont dans `build\Release\`.

2. **Installer le mod Windhawk** :
   - Windhawk → *Créer un nouveau mod*.
   - Remplacer tout le code par le contenu de `windhawk\macdock-hide-taskbar.wh.cpp`.
   - *Compiler le mod*, puis *Quitter l'éditeur* et vérifier qu'il est activé.

   Sans Dock lancé, le mod ne cache rien.

3. **Lancer le Dock et la barre de menus** : double-cliquer sur `build\Release\MacDockLauncher.exe`. Le lanceur démarre les deux et relance celui qui plante. *Quitter MacDock* ferme aussi la barre de menus.

4. **Démarrage automatique** (facultatif) :
   ```powershell
   build\Release\MacDockLauncher.exe --install
   ```
   Pour le retirer : `--uninstall`.

## Utilisation

- **Clic** sur une app fermée : elle se lance en rebondissant. Sur une app ouverte : elle passe au premier plan.
- **Clic sur une pile** (Téléchargements…) : son contenu s'ouvre comme sur macOS, en **éventail** (icônes en arc au-dessus de la pile, nom à gauche) jusqu'à 9 éléments, en **grille** de verre au-delà (molette pour défiler), ou en **liste** (menu en verre, sous-dossiers en sous-menus) si tu la choisis. Un clic ouvre l'élément ; *Ouvrir dans l'Explorateur* ouvre le dossier ; Échap ou un clic à côté referme. Dans le Dock, l'icône d'une pile montre ses derniers fichiers empilés (ou l'icône du dossier, au choix), et se met à jour en direct.
- **Clic droit** : menus en verre, comme sur macOS.
  - *App* : ses fenêtres ouvertes, *Options* (Garder dans le Dock, Ouvrir à la connexion — aussi pour les apps du Store, par un raccourci dans le dossier Démarrage —, Afficher dans l'Explorateur), Afficher toutes les fenêtres, Masquer, Quitter.
  - *Séparateur ou zone vide* : masquage automatique, agrandissement, position à l'écran (Gauche, En bas, Droite), Réglages du Dock.
  - *Pile* : Trier par (Nom, Date d'ajout, Date de modification, Type), Afficher comme (Pile, Dossier), Présenter le contenu comme (Éventail, Grille, Liste, Automatiquement), Ouvrir dans l'Explorateur, Retirer du Dock.
  - *Corbeille* : Ouvrir, Vider la Corbeille. *Fenêtre réduite* : Restaurer, Fermer.
- **Position** : en bas, à gauche ou à droite de l'écran (clic droit sur le séparateur), changée à chaud.
- **Plusieurs écrans** : pousse le curseur contre le bord du Dock sur un autre écran (un court instant) et le Dock y passe ; il s'en souvient au prochain démarrage et revient sur l'écran principal si celui-ci est débranché.
- **Glisser une icône** : la déposer ailleurs dans le Dock la déplace ; la tirer vers le haut (« Supprimer ») puis la lâcher la retire, avec le nuage « poof ». Une app ouverte n'est que désépinglée. Échap annule.
- **Déposer des fichiers** depuis l'Explorateur :
  - un `.exe`, `.lnk` ou `.appref-ms` **entre deux icônes** : il s'épingle à cet endroit ;
  - des fichiers **sur une app** : ils s'ouvrent avec elle ;
  - **sur la Corbeille** : ils y partent (annulable) ; **sur une pile** : ils y sont déplacés.
- **Masquage automatique** (clic droit sur le séparateur) : le Dock glisse sous le bord de l'écran et revient quand le curseur touche ce bord. En plein écran (jeu, vidéo, F11), il s'efface toujours.
- **Fenêtres réduites** : miniature en direct dans le Dock, avec la petite icône de l'app dans le coin.
- **Corbeille** : son icône passe de vide à pleine selon son contenu.
- **Quitter le Dock** : clic droit → *Quitter MacDock*, ou `MacDock.exe --quit`. La barre Windows revient immédiatement.

## Barre de menus

- **Transparente**, comme sur Tahoe : le texte est clair ou foncé selon ton fond d'écran (mesuré sous la barre, puis toutes les minutes et à chaque changement de fond).
- **À gauche** :
  - le menu du système (logo) : À propos de ce PC, Réglages système, Microsoft Store, Éléments récents (dernières apps et derniers documents, « Effacer le menu »), Forcer à quitter, Suspendre, Redémarrer, Éteindre, Verrouiller l'écran, Fermer la session. Redémarrer, Éteindre et Fermer la session demandent confirmation ;
  - le nom de l'app active en gras, avec son menu : À propos, Réglages, Masquer, Masquer les autres, Tout afficher, Quitter ;
  - **ses vrais menus** quand elle en a : barre de menus classique (Bloc-notes historique, Notepad++, 7-Zip, regedit…) ou barre de menus accessible (Bloc-notes de Windows 11, apps Qt…). Les entrées, coches, entrées grisées et sous-menus sont ceux de l'app, relus à chaque ouverture ; la commande choisie est exécutée par l'app. Un menu Fenêtre est ajouté s'il manque ;
  - sinon, des menus génériques Fichier, Édition, Présentation, Fenêtre, Aide. Ils envoient les raccourcis standard (`Ctrl+S`, `Ctrl+Z`…), affichés à droite de chaque entrée. Les apps Chromium, Electron et Firefox gardent toujours ces menus génériques. Le menu Fenêtre liste les fenêtres de l'app ;
  - sur le bureau ou dans l'Explorateur, les menus de l'Explorateur, avec **Aller** (Téléchargements, Documents, Applications, Corbeille…), comme le Finder.
- **Ouvrir un menu** : un clic sur un titre ; tant qu'un menu est ouvert, survoler un autre titre l'ouvre aussi, et les flèches ← → passent au voisin. Le clavier reste à ton app : la commande choisie lui est envoyée.
- **À droite** : la date et l'heure (`mer. 7 oct. 14:32`) ; un clic ouvre le centre de notifications.
- **Plein écran** : la barre s'efface et revient quand le curseur touche le haut de l'écran.
- **Clic droit dans le vide de la barre** : réglages, masquage automatique, quitter la barre.
- **Logo** : par défaut celui de Windows. Pour le remplacer, mets une image `menubar-logo.png` dans `%APPDATA%\MacDock\` ; seule sa transparence compte, elle prend la couleur du texte.

## Réglages

Tout est dans `%APPDATA%\MacDock\`, rechargé à chaud quand tu enregistres :

| Fichier | Contenu |
|---|---|
| `settings.json` | Position (`position` : `bottom`, `left`, `right`), écran (`screen`), taille des icônes (`tileSize`), agrandissement (`magnification`, `largeSize`), masquage automatique (`autohide`), apps récentes, mode « Tahoe strict » des icônes, police, verre Liquid Glass (`glass`), épingles (pour une pile : `view` = `auto`/`fan`/`grid`/`list`, `sort` = `dateAdded`/`name`/`modified`/`kind`, `display` = `stack`/`folder`). |
| `dock-metrics.json` | Toutes les mesures visuelles et d'animation (marges, rayon, ressorts, rebonds…), bornées pour éviter les valeurs absurdes. |
| `icons\<id>.png` | Icônes personnalisées (une par app, nommée d'après son identifiant). Comme sur macOS, prévois une toile de 1024 px avec la forme à 824 px au centre : l'image est utilisée telle quelle. |
| `menubar.json` | Barre de menus : masquage automatique (`autohide`), police, horloge (`clock` : `weekday`, `date`, `seconds`, `hour24`), mesures (`metrics` : hauteur, taille du texte, marges…). |
| `menubar-logo.png` | Logo personnalisé du menu du système (facultatif). |
| `menubar-recent.json` | Apps récentes du menu du système, écrit par la barre (les documents viennent du dossier Récents de Windows, jamais modifié). |
| `logs\` | Journaux (`logs\menubar\` pour la barre de menus). |

Un fichier invalide n'efface rien : une copie `.bak` est faite et les réglages actuels sont conservés.

## Liquid Glass

Avec `"glass": true` (par défaut), le fond du Dock et les infobulles sont en verre : ce qui se trouve derrière est flouté, réfracté sur les bords, teinté selon le thème, avec un liseré lumineux. Le Dock ne se redessine que si le contenu sous lui change vraiment.

- Pour lire l'écran sous lui, le Dock s'exclut des captures : **il n'apparaît pas sur les captures d'écran** ni dans les partages d'écran. Mets `"glass": false` si tu en as besoin ; le Dock passe alors en verre dépoli classique.
- Écran HDR : pris en charge (le blanc SDR de Windows est respecté).
- Si la capture est impossible (écran tourné, carte graphique sans accélération, bureau sécurisé), le Dock passe en verre dépoli et reprend tout seul.

## Sécurité

- Si le Dock plante, se fige ou est fermé, la barre des tâches Windows revient en 5 secondes au plus.
- Désactiver le mod rétablit la barre et son mode d'affichage d'origine.

## Diagnostic

- `MacDock.exe --trace-windows` : journalise le suivi des fenêtres et les performances.
- `MacDock.exe --snapshot capture.png [--hover 0] [--theme light|dark]` : rendu du Dock dans une image, sans l'afficher.
- `MacDock.exe --snapshot capture.png --wallpaper fond.png --reference mac.png --diff diff.png` : comparaison avec une capture de macOS (voir `reference/README.md`).
- `MacDock.exe --capture-test bas.png` : capture réelle du bas de l'écran, telle que le verre la voit.
- `Ctrl+Alt+Maj+O` : superpose `%APPDATA%\MacDock\reference\overlay.png` au Dock ; `Ctrl+Alt+Maj+Haut/Bas` règle son opacité.
- `MacMenuBar.exe --trace` : journalise l'app active, la couleur du texte et les menus ouverts.
- `MacMenuBar.exe --snapshot barre.png [--wallpaper fond.png] [--app "Nom"] [--theme light|dark] [--open 1]` : rendu de la barre dans une image, sans l'afficher.
- `MacMenuBar.exe --quit` : ferme la barre seule.
- `./build.ps1 -Target tests -Run` : tests automatiques.

## Note

Aucune ressource Apple (icônes, logo, police SF Pro) n'est incluse. Si SF Pro est installé sur ta machine, le Dock et la barre l'utilisent ; sinon Inter, puis Segoe UI Variable.
