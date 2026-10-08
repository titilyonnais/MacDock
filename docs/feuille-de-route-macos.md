# Feuille de route : Windows aussi proche que possible de macOS 27 Golden Gate

Ordre de travail en autonomie. Chaque étape suit le même chemin :
- tests d'abord ;
- essai en réel sur le PC quand tu es à distance ;
- relecture indépendante ;
- fusion dans `main`.

## Fait
- Dock, barre de menus, Spotlight (« Rechercher ou demander »), Mission Control, Launchpad, sélecteur d'apps, coins actifs, génie.
- Verre Liquid Glass et fond d'écran recalés sur des captures de macOS 27 (plan 26).
- Feux tricolores sur toutes les fenêtres, toujours à gauche (plans 25 et 27).
- Apparence macOS des fenêtres des autres apps : coins arrondis, plus de liseré, barre de titre grise (plan 26).
- Menus de la barre lus dans chaque app ; raccourcis en symboles macOS (plan 27).
- **Mod Windhawk « MacDock - macOS Look »** : SF Pro dans toutes les apps (plan 28). Il reste **à installer par toi** dans Windhawk.
- **Coup d'œil** (Quick Look) : Espace dans l'Explorateur et sur le bureau (plan 28).

## À faire, par ordre d'effet
1. **Captures d'écran façon macOS** : ⊞⇧3 pour tout l'écran, ⊞⇧4 pour une zone au viseur. Le fichier « Capture d'écran AAAA-MM-JJ à HH.MM.SS.png » va sur le Bureau, et une vignette flottante apparaît en bas à droite.
2. **Coup d'œil, finitions** : animation d'ouverture depuis l'icône ; plein écran ; vidéos et PDF lisibles (gestionnaires d'aperçu du Shell).
3. **Centre de notifications et widgets** : panneau à droite à l'ouverture de l'horloge.
4. **Finder** : style de l'Explorateur (barre latérale, barre d'outils), avec un mod Windhawk de style XAML.
5. **Exposé d'une app** : les fenêtres d'une seule app, depuis le menu du Dock ou avec ⌃↓.
6. **Raccourcis ⌘** : Alt+C, V, X, Z, A, S, W, Q, T et N joués comme Ctrl, en option (« la touche ⌘ »).
7. **Sons système façon macOS** : sons originaux (aucun son Apple).

## Limites connues
- Les apps qui dessinent des onglets ou des menus tout en haut à gauche (navigateurs, Explorateur, Bloc-notes) n'ont pas de place libre pour les pastilles. Elles s'y posent, mais les clics autour passent à l'app.
- Restyler l'intérieur des apps (boutons, listes) demande d'injecter du code : c'est le rôle des mods Windhawk, que tu installes toi-même. Windhawk tourne en administrateur et ne peut pas être piloté depuis MacDock.
