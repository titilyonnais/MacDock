# MacDock

Un Dock façon **macOS Tahoe** pour Windows 11 : magnification, rebonds, infobulles, apps épinglées et ouvertes, fenêtres réduites, Téléchargements et Corbeille. Un mod **Windhawk** cache la barre des tâches Windows tant que le Dock tourne.

> État : **plan 1 (Dock fonctionnel)**. Le vrai verre « Liquid Glass » (plan 2) et les interactions avancées (plan 3 : glisser-déposer, menus en verre, piles, masquage automatique, multi-écran) arrivent ensuite. Voir `docs/superpowers/`.

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

3. **Lancer le Dock** : double-cliquer sur `build\Release\MacDockLauncher.exe`. Le lanceur relance le Dock s'il plante.

4. **Démarrage automatique** (facultatif) :
   ```powershell
   build\Release\MacDockLauncher.exe --install
   ```
   Pour le retirer : `--uninstall`.

## Utilisation

- **Clic** sur une app fermée : elle se lance en rebondissant. Sur une app ouverte : elle passe au premier plan.
- **Clic droit** : Garder dans le Dock / Retirer du Dock, Afficher dans l'Explorateur, Quitter, Réglages du Dock, Quitter MacDock.
- **Quitter le Dock** : clic droit → *Quitter MacDock*, ou `MacDock.exe --quit`. La barre Windows revient immédiatement.

## Réglages

Tout est dans `%APPDATA%\MacDock\`, rechargé à chaud quand tu enregistres :

| Fichier | Contenu |
|---|---|
| `settings.json` | Taille des icônes (`tileSize`), agrandissement (`magnification`, `largeSize`), apps récentes, mode « Tahoe strict » des icônes, police, verre Liquid Glass (`glass`), épingles. |
| `dock-metrics.json` | Toutes les mesures visuelles et d'animation (marges, rayon, ressorts, rebonds…), bornées pour éviter les valeurs absurdes. |
| `icons\<id>.png` | Icônes personnalisées (une par app, nommée d'après son identifiant). Comme sur macOS, prévois une toile de 1024 px avec la forme à 824 px au centre : l'image est utilisée telle quelle. |
| `logs\` | Journaux. |

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
- `./build.ps1 -Target tests -Run` : tests automatiques.

## Note

Aucune ressource Apple (icônes, logo, police SF Pro) n'est incluse. Si SF Pro est installé sur ta machine, le Dock l'utilise ; sinon Inter, puis Segoe UI Variable.
