# Spotlight — spec (sous-projet 7)

- **Date :** 7 octobre 2026
- **Contexte :** après les sous-projets 1 à 6, l'utilisateur veut « un Windows qui ressemble complètement à macOS » et a demandé de continuer en autonomie. Feuille de route : 7 Spotlight, 8 Mission Control, 9 sélecteur d'apps façon Cmd+Tab, 10 affichages volume et luminosité, 11 coins actifs.

## 1. But

Sur macOS, Cmd+Espace ouvre **Spotlight** : un champ de recherche en verre au tiers haut de l'écran ; en tapant, les résultats apparaissent dessous dans le même panneau (meilleur résultat, applications, documents, calcul). Entrée ouvre, Échap ferme.

**Critère de réussite :** Alt+Espace (réglable) ou la loupe de la barre de menus ouvre le panneau sur l'écran du curseur ; taper « calc » propose la Calculatrice en meilleur résultat, « rapport » propose des documents indexés par Windows, « 12*(3+4) » affiche « 84 » ; Entrée ouvre le résultat sélectionné ; Ctrl+Entrée montre un document dans l'Explorateur ; Échap, Alt+Espace ou un clic ailleurs ferment.

## 2. Décisions

| Sujet | Choix | Raison |
|---|---|---|
| Raccourci | Alt+Espace par défaut (`spotlightHotkey` dans `settings.json` : `alt+space`, `ctrl+space`, `off`) ; `RegisterHotKey` dans `MacDock.exe` ; un second appui ferme | Cmd+Espace n'existe pas ; Win+Espace change la langue du clavier. Alt+Espace est le choix de PowerToys Run. Si le raccourci est pris, c'est journalisé. |
| Loupe de la barre de menus | Ouvre Spotlight (message `MacDockSpotlight` enregistré, envoyé à la fenêtre du Dock) ; Win+S si le Dock ne tourne pas | Comme la loupe de macOS. |
| Apps | Catalogue de l'écran Apps (`AppCatalog`, `searchApps`) | Déjà lu et trié. |
| Documents | Recherche Windows indexée par le Shell : dossier `search-ms:query=…&crumb=location:<profil>` énuméré dans un fil à part, 12 résultats au plus, fichiers et dossiers du profil | Utilise l'index de Windows sans OLE DB ; rien n'est écrit. |
| Calcul | `+ - * / × ÷ ^`, parenthèses, moins unaire, `%` final (pourcentage), virgule ou point décimal ; résultat en français (« 1 234,5 », 10 chiffres significatifs) ; au moins un opérateur | Comme Spotlight ; une date (« 2024 ») n'est pas un calcul. |
| Ordre | Meilleur résultat (le calcul s'il y en a un, sinon la première app), Applications (6 au plus), Documents (8 au plus) | Ordre de Spotlight. |
| Actions | App : lancée ; document : ouvert ; Ctrl+Entrée : montré dans l'Explorateur ; calcul : copié dans le presse-papiers | Spotlight copie le résultat avec Cmd+C ; ici Entrée suffit. |
| Panneau | 680 pt de large, à 22 % de la hauteur de l'écran, champ de 52 pt (police 22 pt), lignes de 40 pt avec icône de 28 pt, titres de section en petites capitales grises ; verre Liquid Glass (comme les menus), coins de 24 pt ; verre dépoli Direct2D sinon | Mesures de Spotlight sur Tahoe. |
| Fenêtre | Fenêtre à la taille du plus grand panneau, région de fenêtre limitée au panneau affiché ; activée pour le clavier ; perte d'activation = fermeture (le clic ailleurs atteint sa cible) | Pas de crochet souris ; comportement de macOS. |
| Recherche de documents | Lancée 150 ms après la dernière frappe, une seule à la fois ; un résultat d'une ancienne frappe est ignoré | L'index peut répondre en centaines de ms. |

## 3. Composants

- `src/spotlight/spot_calc.h/.cpp` (pur) : `std::optional<double> evaluateExpression(std::wstring_view)` ; `std::wstring formatNumber(double)`.
- `src/spotlight/spot_results.h/.cpp` (pur) : `enum class SpotKind { Calc, App, File }` ; `struct SpotItem { SpotKind kind; std::wstring title, subtitle, target; }` ; `struct SpotSection { std::wstring title; std::vector<SpotItem> items; }` ; `std::vector<SpotSection> spotlightResults(const std::wstring& query, const std::vector<AppEntry>& apps, const std::vector<SpotItem>& files)` ; `std::size_t spotCount(sections)`, `const SpotItem* spotAt(sections, i)` ; `std::wstring searchMsUrl(const std::wstring& query, const std::wstring& folder)` ; `std::optional<HotkeySpec> parseSpotlightHotkey(const std::wstring&)` (`{UINT mods; UINT vk;}`, nullopt pour `off` ou inconnu).
- `src/spotlight/file_search.h/.cpp` : `std::vector<SpotItem> searchFiles(const std::wstring& query, const std::wstring& folder, std::size_t max)` (COM par l'appelant) ; `class FileSearcher` (fil à part, génération, résultat posté à une fenêtre).
- `src/spotlight/spotlight_window.h/.cpp` : `SpotlightWindow::track(env, request)` modale ; `SpotlightWindow::closeOpen()` ; `BgraImage spotlightSnapshot(query, apps, files, dark, w, h)`.
- Branchements : `Settings::spotlightHotkey`, `DockApp` (raccourci, message enregistré, lancement), barre de menus (loupe), `MacDock.exe --spotlight-snapshot f.png --query texte [--theme dark]`.

## 4. Tests et vérifications

- Tests : calcul (priorités, parenthèses, unaire, pourcentage, virgule, division par zéro, refus d'un nombre seul ou d'un texte), format français, ordre des sections et meilleur résultat, `searchMsUrl` (caractères réservés encodés), analyse du raccourci, recherche réelle de fichiers en lecture seule (aucun plantage, temps borné), rendu hors écran.
- À l'œil : `--spotlight-snapshot` (vide, « calc », « 12*(3+4) », sombre).
- **Aucun essai n'ouvre le panneau devant l'utilisateur**, ne lance d'app ni n'écrit dans le presse-papiers.
