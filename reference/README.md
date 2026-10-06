# Références macOS pour la calibration

Ce dossier accueille des **captures du vrai Dock de macOS Tahoe** pour régler MacDock au plus près.
Les images sont du contenu Apple : elles **ne sont jamais versionnées** (`reference/*.png` est ignoré par git).

## Faire les captures (sur un Mac sous Tahoe)

1. Régler le Dock comme MacDock :
   ```bash
   defaults write com.apple.dock tilesize -int 48
   defaults write com.apple.dock largesize -int 80
   defaults write com.apple.dock magnification -bool true
   killall Dock
   ```
2. Choisir un fond d'écran uni gris moyen (Réglages → Fond d'écran → Couleurs).
3. Faire une capture plein écran (`Cmd+Maj+3`) sur un écran Retina :
   - au repos, en mode clair puis en mode sombre ;
   - souris posée sur une icône (survol), en clair et en sombre.
4. Les copier ici avec ces noms :

| Fichier | Contenu |
|---|---|
| `tahoe-rest-light.png` | repos, mode clair |
| `tahoe-rest-dark.png` | repos, mode sombre |
| `tahoe-hover-light.png` | survol, mode clair |
| `tahoe-hover-dark.png` | survol, mode sombre |

## Comparer avec MacDock

Rendu de MacDock sur le même fond, puis comparaison avec la bande du bas de la référence :

```bash
MacDock.exe --snapshot rendu.png --wallpaper fond-gris.png --reference reference/tahoe-rest-dark.png --diff diff.png
```

- `diff.png` : gris = identique, rouge = MacDock plus clair, bleu = macOS plus clair.
- `diff.png.txt` : écart moyen (`meanAbs`), écart maximal (`maxAbs`), part des pixels qui diffèrent de plus de 24 (`fractionAbove24`).

Les mesures se règlent dans `%APPDATA%\MacDock\dock-metrics.json` (rechargé à chaud).

## Superposition en direct

1. Copier une capture vers `%APPDATA%\MacDock\reference\overlay.png`.
2. Dans Windows, avec le Dock lancé :
   - `Ctrl+Alt+Maj+O` affiche ou masque la capture, calée en bas et au centre de l'écran (une capture Retina @2x est ramenée à l'échelle de l'écran) ;
   - `Ctrl+Alt+Maj+Haut` / `Bas` règle son opacité.
