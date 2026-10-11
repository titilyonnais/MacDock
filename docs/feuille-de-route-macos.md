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
- **Publication sur GitHub** (plan 52) et **mises à jour automatiques signées** (plan 53, version 0.53.0).
- **Forcer à quitter** (⌥⌘⎋, menu ) : la fenêtre de macOS 26 (plan 54).
- **Spotlight** : conversions d'unités et section « Réglages » (pages des Paramètres de Windows, sections de
  Réglages MacDock) (plan 55) ; finitions la nuit du 10 octobre : « –40 °C » au tiret, espaces fines, « EN », « wi fi »,
  « capture d'écran ».
- **Couleur d'accentuation** de Réglages Système (huit accents), appliquée partout dans MacDock (plan 57).
- **Menu de saisie** du clavier dans la barre de menus (« FR », « US »), comme celui de macOS (plan 58).
- **Fenêtre suivante de l'app** au clavier (`` ⌥` ``, comme `` ⌘` `` de macOS) (plan 59).
- **Forme des icônes de macOS 26**, mesurée sur la VM (rayon de 25,9 % du côté) (plan 60).
- **Proportions du Dock de macOS 26**, mesurées sur la VM : marges, espacement, séparateurs, points (plan 61).
- **Dock plus réactif** : relais du crochet souris, rebond d'attention comme sur macOS, boucle qui ne tourne plus
  à vide, démarrage plus rapide (plan 62 ; VMware, qui garde le curseur posé sur le Dock, reste à essayer avec toi).
- **Barre des menus de macOS 26**, mesurée sur la VM : texte sur sa ligne de base, loupe et Centre de contrôle, icônes
  espacées de 36 pt, horloge à chiffres fixes (plan 64).
- **MacDock visible à l'Outil Capture de Windows** (⊞⇧S, Impr. écran), le temps qu'il est ouvert (plan 63 ; vrai essai
  au clavier à faire avec toi).
- **Icônes du Dock préparées hors de son fil** : une icône lente ne fait plus saccader le Dock (plan 67).
- **Dock complet dès ses premières images** : icônes gardées sur disque, relues au démarrage (plan 68).
- **⊞⇧S propre, Dock fluide, verre sans contour** : Dock et barre sous le voile de l'Outil Capture, relais du curseur
  au rythme de l'écran, plus de liseré blanc (plan 69).
- **Poignée des séparateurs et cotes du Dock de macOS 26** mesurées à trois tailles dans ta VM (plan 70).
- **Barre de menus fiable et vive** : plus de « Système » dans VMware ni de titres décalés, bascule instantanée d'un
  menu à l'autre, plus de clignotement en quittant une app (plan 71).
- **Fenêtres de macOS 26** : agrandir remplit l'écran entre la barre et le Dock, pastille verte en vrai plein écran,
  coins de 12 pt par le mod « Custom Window Corner Radius » de Windhawk (plan 72).

## À faire, par ordre d'effet
- **Apparence Clair / Sombre / Automatique** (ta demande du 10 octobre, MacDock et Windows, automatique au lever et au
  coucher du soleil) : logique pure faite sur la branche `feat/apparence-65`, **non fusionnée** (plan 65, tâche 1) ;
  le reste avec toi : Réglages > Général > Apparence, MacDock qui suit, écriture côté Windows, et tes réponses aux
  questions du plan (où prendre ta position, ce que Windows doit suivre).
0. **Approche de SF Pro (« trak »)** : CoreText resserre SF Pro selon sa taille (−0,076 pt par caractère à 13 pt,
   d'après la table de la police installée), DirectWrite ne le fait pas ; à appliquer à tout le texte de MacDock (barre,
   menus, infobulle du Dock, Spotlight, Réglages), puis recaler la marge des titres de la barre (~10,4 pt).
1. **Mises à jour, finitions** : mineurs du plan 53 (nouvel essai plus tôt après une recherche ratée, état après une
   installation abandonnée, notification après un redémarrage de l'Explorateur, dossier des téléchargements à la
   désinstallation).
2. **Apps de Windows façon macOS** (recherches du 8 octobre faites) :
   - un mod de contrôles Win32 (uxtheme : boutons, menus, barres de défilement, listes, dialogues) ;
   - des stylers XAML (Explorateur → Finder, Paramètres → Réglages, Bloc-notes, Calculatrice, Photos, Horloge) ;
   - la liste de fichiers et le volet de l'Explorateur (DirectUI).
3. **Centre de notifications, suite** : notifications de Windows listées dans le panneau (UserNotificationListener demande une identité d'app empaquetée), météo.
4. **Captures, suite** : options de ⊞⇧5 (minuterie, dossier), annotations dans la vignette ; le Dock dans les vidéos ; le son dans les vidéos (option) ; ⊞Impr. écran et Alt+Impr. écran (capture immédiate, avant que MacDock se montre).

## Limites connues
- Les pastilles sont un calque posé sur les boutons de la fenêtre : elles suivent la fenêtre avec un léger temps de retard quand on la déplace vite.
- Restyler l'intérieur des apps (boutons, listes) demande d'injecter du code : c'est le rôle des mods Windhawk, que tu installes toi-même. Windhawk tourne en administrateur et ne peut pas être piloté depuis MacDock.
