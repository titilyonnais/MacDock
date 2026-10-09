# Feuille de route : Windows aussi proche que possible de macOS 26 Tahoe

> Depuis le 8 octobre 2026, la référence est **macOS 26 Tahoe** : macOS 27 ne tourne pas en machine virtuelle, et ta VM
> macOS 26 sert de point de comparaison. Les choix faits pour Golden Gate passent à Tahoe au fil des plans (plan 43 :
> pastilles plates, barre latérale flottante).

Ordre de travail en autonomie. Chaque étape suit le même chemin :
- tests d'abord ;
- essai en réel sur le PC quand tu es à distance ;
- relecture indépendante ;
- fusion dans `main`.

## Fait
- Dock, barre de menus, Spotlight (« Recherche Spotlight »), Mission Control, Launchpad, sélecteur d'apps, coins actifs, génie,
  durées des animations de Tahoe (plan 44).
- Verre Liquid Glass et fond d'écran recalés sur des captures de macOS 27 (plan 26), à revoir sur Tahoe.
- Feux tricolores sur toutes les fenêtres, à la place des boutons réduire / agrandir / fermer de Windows (plans 25 et 27, puis ta demande du 8 octobre).
- Apparence macOS des fenêtres des autres apps : coins arrondis, plus de liseré, barre de titre grise (plan 26).
- Menus de la barre lus dans chaque app ; raccourcis en symboles macOS (plan 27).
- **Mod Windhawk « MacDock - macOS Look »** : SF Pro dans toutes les apps (plan 28). Il reste **à installer par toi** dans Windhawk.
- **Coup d'œil** (Quick Look) : Espace dans l'Explorateur et sur le bureau (plan 28).
- **Captures d'écran façon macOS** : ⊞⇧3, ⊞⇧4 (zone ou fenêtre), vignette flottante, fichier sur le Bureau (plan 29).
- **Coup d'œil, finitions** : vrais aperçus des documents, vidéos et sons, zoom depuis l'icône, plein écran (plan 30).
- **Exposé d'une app** : ⌃⌥↓ ou « Afficher toutes les fenêtres », fenêtres réduites en rangée en bas (plan 31).
- **Sons système originaux** : capture, Corbeille vidée, « poof », « pop » du volume (plan 32).
- **Touche ⌘** (option) : Alt de gauche joue ⌘, raccourcis du Finder dans l'Explorateur (plan 33).
- **Centre de notifications** : date, calendrier du mois, lecture en cours, au clic sur l'horloge (plan 34).
- **Menus, Spotlight et pastille visibles sur les captures** (plans 35 et 36).
- **Moteur Liquid Glass v2** (formes qui fusionnent, lumière réglable) **et moteur d'animations** (ressorts SwiftUI, courbes Core Animation) (plan 37), branchés dans les menus et l'infobulle du Dock (plan 38).
- **⊞⇧5 et enregistrement de l'écran** (plan 39).

## À faire, par ordre d'effet
1. **App Réglages de MacDock** (plan 41 fait : fenêtre, sections Dock, Barre des menus, Fenêtres, liens) :
   - **plan 42** : Général (démarrage avec Windows, sauvegarde), Mission Control et coins actifs, Clavier (raccourcis modifiables), Captures, Sons, Police, Mods Windhawk, À propos, recherche ;
   - **plan 43** : finitions et icône de l'exécutable.
2. **Apps de Windows façon macOS** (recherches du 8 octobre faites) :
   - un mod de contrôles Win32 (uxtheme : boutons, menus, barres de défilement, listes, dialogues) ;
   - des stylers XAML (Explorateur → Finder, Paramètres → Réglages, Bloc-notes, Calculatrice, Photos, Horloge) ;
   - la liste de fichiers et le volet de l'Explorateur (DirectUI).
3. **Glitch des bords** de Brave en sortie de plein écran : à enregistrer image par image.
4. **Centre de notifications, suite** : notifications de Windows listées dans le panneau (UserNotificationListener demande une identité d'app empaquetée), météo.
5. **Captures, suite** : ⊞⇧5 (barre d'outils, enregistrement de l'écran), annotations dans la vignette ; le Dock dans les vidéos ; le son dans les vidéos (option).

## Limites connues
- Les pastilles sont un calque posé sur les boutons de la fenêtre : elles suivent la fenêtre avec un léger temps de retard quand on la déplace vite.
- Restyler l'intérieur des apps (boutons, listes) demande d'injecter du code : c'est le rôle des mods Windhawk, que tu installes toi-même. Windhawk tourne en administrateur et ne peut pas être piloté depuis MacDock.
