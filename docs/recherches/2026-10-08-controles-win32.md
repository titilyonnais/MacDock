# Redessiner les contrôles Win32 façon macOS (recherche du 8 octobre 2026)

**En bref :** on intercepte le moteur de thèmes uxtheme et on redessine chaque partie de contrôle en Direct2D. Deux mods
Windhawk le font déjà à grande échelle : **Win32 UI Modernizer** (style Fluent) et **Translucent Windows**. On les lit
pour la technique ; le Modernizer est en GPL-3.0, donc on s'en inspire sans copier. Environ 70 % des contrôles Win32
classiques sont couverts. Ce qui reste hors d'atteinte : WPF, Qt récent, Chromium, et les contrôles que l'app dessine
elle-même.

## 1. Crochets uxtheme

### Suivre les thèmes ouverts
- Accrocher `OpenThemeData`, `OpenThemeDataEx`, `OpenThemeDataForDpi` et `CloseThemeData`.
- Accrocher aussi `OpenNcThemeData` (ordinal 49, non documentée) : les barres de défilement de la zone non cliente
  passent uniquement par elle.
- La classe d'un `HTHEME` s'obtient avec `GetThemeClass` (ordinal 74, non documentée), puis on la met en cache. Elle ne
  donne que la classe de base : pour distinguer « DarkMode::Menu » de « Menu », il faut mémoriser le nom passé à
  l'ouverture.

### Dessiner
- Dans `DrawThemeBackground(Ex)`, on choisit selon la classe : si notre dessin réussit, on renvoie `S_OK` ; sinon, on
  appelle la fonction d'origine.
- `DrawThemeText(Ex)` et `GetThemeColor` servent aux couleurs de texte.
- `GetThemeMargins` et `GetThemeTransitionDuration` règlent les marges et les animations.

### Classes et parties utiles
Référence complète : la page Microsoft « Parts and States ».

| Classe | Parties |
|---|---|
| MENU | `MENU_POPUPITEM`, `MENU_POPUPBACKGROUND`, `MENU_POPUPSEPARATOR`, `MENU_POPUPCHECK`, `MENU_BARITEM`, `MENU_BARBACKGROUND` |
| SCROLLBAR | `SBP_THUMBBTNVERT`, `SBP_*TRACK*`, `SBP_ARROWBTN` |
| BUTTON | `BP_PUSHBUTTON`, `BP_CHECKBOX`, `BP_RADIOBUTTON`, `BP_GROUPBOX`, `BP_COMMANDLINK` |
| TAB | `TABP_PANE`, `TABP_TOPTABITEM` |
| PROGRESS | `PP_BAR`, `PP_FILL` |
| TREEVIEW | `TVP_GLYPH` |
| HEADER | `HP_HEADERITEM` |
| TOOLTIP | `TTP_STANDARD` |
| TASKDIALOG | `TDLG_PRIMARYPANEL`… |

Sont aussi concernées : EDIT, COMBOBOX, LISTBOX, TRACKBAR, LISTVIEW, SPIN, REBAR, TOOLBAR.

### Menus
- Les menus thémés passent par des messages non documentés que traite `DefWindowProc` : `WM_UAHDRAWMENU` (0x91),
  `WM_UAHDRAWMENUITEM` (0x92) et `WM_UAHMEASUREMENUITEM` (0x94).
- Le mod Custom Menu Height agrandit les éléments en accrochant `DefWindowProc`, `DefFrameProc` et `DefDlgProc`.
- Le mod Dark mode context menus s'appuie sur `SetPreferredAppMode` (ordinal 135) et `FlushMenuThemes` (ordinal 136).

### Pratique, d'après leur code
- **Cible de dessin :** un `ID2D1DCRenderTarget` logiciel attaché au HDC par `BindDC`, avec une fabrique multithread et
  une cible par thread (`BindDC` prend environ 5 µs, contre environ 500 µs pour `CreateDCRenderTarget`).
- **Cache :** des bitmaps pré-rendues par état.

## 2. Ce qu'uxtheme ne couvre pas

| Élément | Comment le traiter |
|---|---|
| Menus « immersifs » du shell | Faire renvoyer `false` à `ImmersiveContextMenuHelper::CanApplyOwnerDrawToMenu` : ils retombent sur les menus uxtheme. |
| Menu contextuel XAML de Windows 11 | C'est du WinUI 3 (fenêtre `XamlExplorerHostIslandWindow_WASDK`) : il se traite avec un styler XAML, voir l'autre rapport. |
| Apps non thémées (comctl32 v5) | Common Controls Hook force comctl32 v6. |
| Couleurs | Jamais `SetSysColors` : son effet couvre tout le système et persiste. Accrocher plutôt `GetSysColor` et `GetSysColorBrush` dans le processus, et répondre aux `WM_CTLCOLOR*`. |
| MessageBox | Changer la mise en page demande de réimplémenter `SoftModalMessageBox` ou de la convertir en TaskDialog. Les boutons, eux, sont déjà couverts. |
| TaskDialog | Classes TASKDIALOG et TEXTSTYLE. |
| Ouvrir / Enregistrer | Vues DirectUI de l'Explorateur (ItemsView, Navigation, CommandModule, AddressBand, SearchBox) : thémables, mais avec beaucoup de cas particuliers. |
| Contrôles dessinés par l'app | Rien à faire, sauf du cas par cas sur les appels GDI. |

## 3. Barres de défilement fines
- **FlatSB est inutilisable** : l'API n'existe plus depuis comctl32 6.00.
- **La largeur est fixée dans user32.** Accrocher `GetSystemMetrics` ne change que le code qui lit cette valeur ; le seul
  levier global est `iScrollWidth`, via `SystemParametersInfo`. Ce réglage touche tout le système : il faut sauvegarder
  la valeur d'origine pour pouvoir la restaurer.
- **Le look macOS reste faisable :** on peint la piste et les flèches dans la couleur de fond, et on ne garde visible
  qu'un pouce arrondi étroit, plus large au survol.
- **Pas de vraies barres superposées en Win32 classique :** la gouttière est réservée dans la zone non cliente.

## 4. Coût, stabilité, compatibilité
- **Hors d'atteinte :**
  - WPF ;
  - ToolStrip et MenuStrip de WinForms ;
  - Qt 6.7 et plus avec le style windows11 ;
  - Chromium et Electron.
- **Plantages connus :** l'auteur de TranslucentFlyouts a abandonné à cause de plantages dans .NET, dans des jeux
  protégés par anti-triche, et dans le multithread.
- **Exclusions :** `dwm.exe`, `msiexec.exe` et `mmc.exe` (fragile), en plus de nos exclusions de jeux.
- **Thèmes :** les `HTHEME` sont liés à un DPI et deviennent invalides après `WM_THEMECHANGED`. Il faut vider le cache
  dans `CloseThemeData`.

## 5. WindowBlinds, pour comparaison
Stardock suit la même architecture : il intercepte le dessin et le redirige vers `wblind.dll`. Les skins sont des
bitmaps (formats UIS1 et UIS2), avec des exclusions par app. Il n'habille pas non plus les apps qui dessinent leur
propre cadre.

## 6. Architecture recommandée

### Un mod `windhawk/macdock-controls.wh.cpp`
Il est séparé de `macdock-look`, avec une option par famille de contrôles.

**Fonctions à accrocher :**
1. **Suivi des thèmes :** `OpenThemeData*`, `OpenNcThemeData` et `CloseThemeData`, pour tenir une table HTHEME →
   {classe, sombre, DPI}.
2. **Dessin :** `DrawThemeBackground(Ex)`, `DrawThemeText(Ex)`, `GetThemeColor`, `GetThemePartSize` (notre ajout) et
   `GetThemeMargins`.
3. **Menus :**
   - `DefWindowProc`, `DefFrameProc` et `DefDlgProc`, pour des éléments d'environ 22 dip ;
   - `TrackPopupMenu(Ex)` et un crochet CBT sur `#32768`, pour les coins arrondis ;
   - `CanApplyOwnerDrawToMenu` renvoyant `false`.
4. **Couleurs :** `GetSysColor(Brush)` et les `WM_CTLCOLOR*`.

**Dessin :**
- Direct2D sur un DC (`ID2D1DCRenderTarget`), avec un cache de bitmaps par classe, partie, état, taille, DPI et mode
  clair ou sombre.
- Le texte est laissé à l'original : on change seulement sa couleur, et notre police SF est gardée.
- Dans le doute, on renvoie `false` et le dessin d'origine reprend la main.
- Une garde contre la réentrance, et aucun verrou tenu pendant l'appel à l'original.

### Ordre de réalisation
1. Infrastructure, palette Aqua claire et sombre, et une app de test interne avec tous les contrôles.
2. BUTTON.
3. Menus contextuels.
4. SCROLLBAR (+ option « fin » par `SystemParametersInfo`, valeur d'origine sauvegardée et restaurée par les Réglages
   MacDock).
5. EDIT, COMBOBOX, TAB, PROGRESS, TRACKBAR.
6. LISTVIEW, TREEVIEW, HEADER, TOOLTIP.
7. TaskDialog et fond des dialogues.
8. Barre de menus, Ouvrir/Enregistrer, menus translucides.

**Plan B :** un `.msstyles` entièrement dessiné par nous, chargé par SecureUxTheme. C'est plus rapide, mais les images
sont figées et la maintenance est lourde.

## Sources
- Mods Windhawk dans `ramensoftware/windhawk-mods/mods/` : `win32-ui-modernizer`, `translucent-windows`, `dark-menus`,
  `custom-menu-height`, `classic-menus`, `hide-scrollbars`, `common-controls-hook`, `msg-box-font-fix`,
  `better-dialogs`, `eradicate-immersive-menus`, `photoshop-dark-menus`, `windows-11-file-explorer-styler`,
  `menu-tooltip-slide-animation`.
- https://github.com/adzm/win32-custom-menubar-aero-theme
- https://github.com/ysc3839/win32-darkmode
- https://github.com/ALTaleX531/TranslucentFlyouts
- https://github.com/namazso/SecureUxTheme
- https://learn.microsoft.com/en-us/windows/win32/controls/parts-and-states
- https://learn.microsoft.com/en-us/windows/win32/api/uxtheme/nf-uxtheme-openthemedatafordpi
- https://learn.microsoft.com/en-us/windows/win32/controls/flat-scroll-bars
- https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-nonclientmetricsw
- https://wiki.qt.io/QtCS2024_Qt_on_Windows
- https://archive.stardock.com/products/windowblinds/wb3/wb3_guide_final.htm
