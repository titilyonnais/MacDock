# Thème macOS — Spécification de conception (sous-projet 5 : thème global)

- **Date :** 2026-10-07
- **Statut :** validée par délégation (« tout en autonomie » pour les sous-projets 2 à 6). Tout est tranché ici et signalé dans le journal.
- **Spec parente :** `2026-10-06-macos-dock-design.md` (sous-projet 5 : « polices, coins, ombres, curseurs, fond d'écran »).

## 1. Objectif

Donner au reste de Windows l'allure de macOS là où c'est possible sans risque : **curseurs** dessinés façon macOS (flèche noire bordée de blanc, flèches de redimensionnement, attente), et **fond d'écran** abstrait façon Tahoe, généré par le code. Le thème s'applique et se retire d'un clic ; ce qu'il remplace est sauvegardé et rendu à l'identique.

**Critère de réussite :** « Appliquer le thème macOS » change les curseurs et le fond d'écran de tous les écrans ; « Rétablir le thème Windows » remet exactement les curseurs et fonds d'écran d'avant. Rien n'est changé sans action de l'utilisateur.

### 1.1 Décisions (prises en autonomie)

| Question | Décision | Raison |
|---|---|---|
| Ce qui est couvert | Curseurs et fond d'écran | Ce sont les deux éléments réglables proprement et réversibles. |
| Polices système | Non | Remplacer « Segoe UI » (`FontSubstitutes`) demande un redémarrage et casse des apps ; le Dock et la barre utilisent déjà SF Pro, Inter ou Segoe UI Variable. |
| Coins et ombres | Non | Windows 11 arrondit déjà et ombre les fenêtres ; les changer demanderait une injection dans DWM. |
| Couleur d'accentuation | Non | Pas d'interface publique : il faudrait écrire des clés non documentées. |
| Ressources Apple | Aucune : curseurs et fond d'écran dessinés par le code | Règle du projet. Pas de roue arc-en-ciel : l'attente est un anneau gris qui tourne. |
| Application | À la demande seulement : menu du séparateur du Dock → « Thème macOS » ▸ « Appliquer (curseurs et fond d'écran) », « Rétablir le thème Windows » ; et `MacDock.exe --theme apply` / `--theme restore` (sans lancer le Dock) | Changer l'apparence du système est une décision de l'utilisateur. |
| Sauvegarde | `%APPDATA%\MacDock\theme-backup.json` : valeurs de `HKCU\Control Panel\Cursors` remplacées et fond d'écran de chaque écran, écrite avant le premier changement ; une deuxième application ne l'écrase pas | Rendre exactement l'état d'avant, même après plusieurs applications. |
| Curseurs remplacés | `Arrow`, `AppStarting`, `Wait`, `SizeNS`, `SizeWE`, `SizeNWSE`, `SizeNESW`, `SizeAll`, `Crosshair`, `No` | La main, le I de texte et l'aide de Windows ressemblent déjà à ceux de macOS ; les autres restent. |
| Tailles | Chaque `.cur` contient 32, 48, 64, 96 et 128 px | Windows choisit selon l'échelle et la taille de curseur réglée. |
| Fond d'écran | Ondes douces bleues, turquoise et violettes (clair) ou nuit bleue (sombre, si Windows est en mode sombre), à la résolution de chaque écran, en PNG dans `%APPDATA%\MacDock\theme\` | Ambiance Tahoe sans copier d'image. |

### 1.2 Limites assumées
- Un fond d'écran en diaporama, en couleur unie ou « Windows à la une » n'a pas de fichier : au rétablissement, Windows reprend le dernier fichier connu, ou garde notre fond si aucun n'était connu (signalé dans le journal du Dock).
- Les curseurs n'ont pas l'ombre portée de macOS au-delà de ce que l'image contient (ombre douce dessinée dans l'image).

## 2. Logique pure (testée)

### 2.1 Dessin vectoriel — `src/theme/vector_art.h/.cpp`
- `struct Poly { std::vector<std::pair<double,double>> pts; }` ; `struct Layer { std::vector<Poly> shapes; std::uint32_t fill; std::uint32_t outline; double outlineWidth; }` (couleurs `0xAARRGGBB`).
- `BgraImage rasterize(const std::vector<Layer>&, int size, double unit, double shadow)` : coordonnées en unités d'un canevas de 32 (×`unit` = `size / 32`), sur-échantillonnage 4 × 4 ; un point est dans une couche s'il est dans l'un de ses polygones (pair-impair par polygone) ; bordure = distance au bord ≤ `outlineWidth` hors du remplissage ; ombre douce (alpha 0,3, décalée de 0,6 unité vers le bas, floutée) sous l'ensemble.

### 2.2 Curseurs — `src/theme/cursor_art.h/.cpp`
- `enum class CursorKind { Arrow, AppStarting, Wait, SizeNS, SizeWE, SizeNWSE, SizeNESW, SizeAll, Crosshair, No }` ; `const wchar_t* cursorRegistryName(CursorKind)`.
- `struct CursorFrame { BgraImage image; POINT hotspot; }` ; `std::vector<CursorFrame> cursorFrames(CursorKind, int size)` : une image (fixes) ou 12 (attente : anneau de 12 segments gris dont l'opacité tourne ; `AppStarting` : flèche + petit anneau).
- Flèche : noire, bordure blanche de 1,6 unité, pointe en (3, 2), point actif sur la pointe. Doubles flèches : tige et deux pointes noires bordées de blanc, tournées de 0, 90, 45 et −45°. Quatre directions : deux doubles flèches. Croix : deux traits fins. Interdit : flèche + pastille cerclée et barrée.

### 2.3 Fichiers de curseur — `src/theme/cursor_file.h/.cpp`
- `std::vector<std::uint8_t> encodeCur(const std::vector<CursorFrame>& sizes)` : en-tête `ICONDIR` de type 2, une entrée par taille (point actif dans les champs `wPlanes`/`wBitCount`), image DIB 32 bits de bas en haut + masque ET à zéro.
- `std::vector<std::uint8_t> encodeAni(const std::vector<std::vector<std::uint8_t>>& curFiles, int jiffies)` : `RIFF` `ACON`, `anih` (36 octets, `AF_ICON`), `LIST` `fram` de chunks `icon`.

### 2.4 Fond d'écran — `src/theme/wallpaper_art.h/.cpp`
- `BgraImage tahoeWallpaper(int w, int h, bool dark)` : dégradé de fond + quatre ondes douces (somme de sinus, bords adoucis), opaque, sans bande visible (tramage léger).

### 2.5 Application — `src/theme/theme_apply.h/.cpp`
- `struct ThemeApi` (fonctions injectables) : `readCursor(name) → optional<wstring>`, `writeCursor(name, value) → bool`, `reloadCursors() → bool`, `monitors() → vector<wstring>` (identifiants), `getWallpaper(id) → wstring`, `setWallpaper(id, path) → bool`, `writeFile(path, bytes) → bool`, `darkMode() → bool`, `monitorSize(id) → SIZE`.
- `struct ThemeBackup { std::map<std::wstring, std::wstring> cursors; std::map<std::wstring, std::wstring> wallpapers; }` ↔ JSON.
- `ThemeResult applyTheme(ThemeApi&, const std::wstring& dir, std::optional<ThemeBackup>& backup)` : écrit les fichiers de curseurs et de fonds, sauvegarde (si `backup` est vide) puis écrit le registre et les fonds ; `restoreTheme(ThemeApi&, const ThemeBackup&)` rend tout. Une valeur absente à l'origine est rendue vide (curseur par défaut de Windows).

## 3. Branchements

- `MacDock.exe` : sous-menu « Thème macOS » du menu du séparateur ; `realThemeApi()` (registre `HKCU\Control Panel\Cursors`, `SystemParametersInfo(SPI_SETCURSORS, 0, nullptr, SPIF_SENDCHANGE)`, `IDesktopWallpaper`) ; sauvegarde dans `theme-backup.json`, effacée après un rétablissement réussi.
- `MacDock.exe --theme apply|restore` : même code, traité comme `--genie-snapshot` (pas de Dock, pas de verrou d'instance).
- `MacDock.exe --theme-snapshot dossier` : planche des curseurs (toutes les formes, 32 et 64 px, sur fond clair et sombre) et aperçus des deux fonds d'écran (1920 × 1080), sans rien appliquer.

## 4. Vérification

- Tests : rasterisation (pixel noir au cœur de la flèche, blanc sur sa bordure, transparent au loin), formes de chaque curseur (point actif, nombre d'images), `encodeCur` relu (en-tête, tailles, point actif), `encodeAni` relu (chunks), fond d'écran (taille, opaque, clair plus lumineux que sombre), application et rétablissement avec une `ThemeApi` factice (sauvegarde écrite une seule fois, valeurs rendues, échec d'écriture signalé).
- Aucun test ni essai ne touche aux vrais curseurs ou au vrai fond d'écran ; la vérification à l'œil passe par `--theme-snapshot`.
