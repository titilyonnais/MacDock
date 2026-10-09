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
- **App Réglages de MacDock** : onze sections, recherche, raccourcis, sauvegarde (plans 41 et 42) ; noms réels des
  écrans, menus en appuyer-glisser-relâcher (plan 46).
- **Passage à macOS 26 Tahoe** : pastilles plates, barre latérale flottante, durées des animations (plans 43 et 44).
- **Fenêtres** : génie à l'appui sur « réduire », « Masquer » instantané (plan 44) ; pastilles des fenêtres
  recouvertes ou masquées (plan 45) ; plein écran (Brave) sans coins arrondis ni liseré (8 octobre).
- **Apps et Mission Control** : zones de clic justes, miniatures sans les bordures invisibles, flèches (plan 48).
- **Icônes des exécutables**, dessinées par le code (plan 49).
- **« Déplacer et redimensionner »** de macOS 26 : menu Fenêtre (plan 50), Organiser et menu au survol de la pastille
  verte (plan 51).

## À faire, par ordre d'effet
1. **Publication sur GitHub et mises à jour automatiques** (plans 52 et 53, demandés le 9 octobre).
2. **Forcer à quitter** (⌥⌘⎋) : liste des apps, « ne répond pas », confirmation.
3. **Spotlight** : pages des Réglages de Windows, conversions d'unités.
4. **Apps de Windows façon macOS** (recherches du 8 octobre faites) :
   - un mod de contrôles Win32 (uxtheme : boutons, menus, barres de défilement, listes, dialogues) ;
   - des stylers XAML (Explorateur → Finder, Paramètres → Réglages, Bloc-notes, Calculatrice, Photos, Horloge) ;
   - la liste de fichiers et le volet de l'Explorateur (DirectUI).
5. **Centre de notifications, suite** : notifications de Windows listées dans le panneau (UserNotificationListener demande une identité d'app empaquetée), météo.
6. **Captures, suite** : options de ⊞⇧5 (minuterie, dossier), annotations dans la vignette ; le Dock dans les vidéos ; le son dans les vidéos (option).
7. **Couleur d'accentuation** de Réglages Système (huit accents).

## Limites connues
- Les pastilles sont un calque posé sur les boutons de la fenêtre : elles suivent la fenêtre avec un léger temps de retard quand on la déplace vite.
- Restyler l'intérieur des apps (boutons, listes) demande d'injecter du code : c'est le rôle des mods Windhawk, que tu installes toi-même. Windhawk tourne en administrateur et ne peut pas être piloté depuis MacDock.
