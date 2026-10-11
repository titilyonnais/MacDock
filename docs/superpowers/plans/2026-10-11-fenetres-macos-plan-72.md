# Fenêtres de macOS 26 (plan 72) — plan d'implémentation

> **Pour les agents :** exécution en ligne (superpowers:executing-plans), cases `- [ ]` cochées au fil de l'eau.

**But :** que les fenêtres de Windows se comportent et s'arrondissent comme celles de macOS 26 : une fenêtre qu'on
agrandit (glissée contre le haut de l'écran, double-clic sur sa barre de titre, ⊞↑) se **remplit** entre la barre des
menus et le Dock, coins arrondis, au lieu de s'agrandir à coins carrés ; la **pastille verte** passe en **vrai plein
écran** (la barre des menus et le Dock se cachent) et le quitte ; les **coins** prennent le rayon de macOS (12 pt).

**Demande (10 octobre, 12 h 30 à 14 h 40) :** « de vrais arrondis, même sur les fenêtres normales, l'arrondi de macOS sur
n'importe quoi » ; « le bouton vert fait vraiment aller en plein écran et cache en haut et en bas ; si je glisse la
fenêtre en haut et que je lâche, la barre du haut et le Dock s'afficheront toujours : deux modes différents » ; accord
pour un mod Windhawk dans `dwm.exe` (« Oui, prépare-le »).

**Architecture :**
- Remplir : la barre de menus suit les déplacements des fenêtres de l'app au premier plan (crochet
  `EVENT_OBJECT_LOCATIONCHANGE` limité à son processus, comme les pastilles). Une fenêtre qui passe à l'état agrandi est
  rendue à sa taille sans animation, puis rangée par `tileWindow(…, Fill)` (plan 50 : marges de macOS, bordures
  invisibles, taille minimale). Agrandie de nouveau alors qu'elle était remplie : `tileWindow(…, Previous)` (bascule de
  macOS). Une app qui se réagrandit d'elle-même aussitôt est laissée agrandie.
- Plein écran : `toggleFullscreen` (shell/window_tile) garde le cadre d'avant et cale le cadre visible sur l'écran
  entier ; la détection existante (`isFullscreenWindow`) cache alors la barre et le Dock et carre les coins. La pastille
  verte et l'entrée « Plein écran » du menu de la pastille passent par la barre.
- Coins : le mod public « Custom Window Corner Radius » de Windhawk (m417z, v1.3, crochets de `udwm.dll`, garde contre
  les plantages de DWM) fait exactement ce qu'il faut ; un script de MacDock l'active avec les valeurs de macOS au lieu
  d'un crochet maison dans `dwm.exe`.

**Spec :** pas de document séparé ; ce plan, le brouillon `brouillon-plan-barre.md` (sections du 10 octobre) et les
mesures de la VM font foi.

## Contraintes globales
- Dépôt en LF ; tests sans écriture dans le vrai registre ni dans %APPDATA% ; rien de destructif.
- Réglage `fillOnMaximize` (défaut : oui) ; désactivé : comportement de Windows inchangé (agrandir, pastille verte =
  agrandir).
- Jamais les fenêtres de MacDock, les fenêtres sans barre de titre, sans bord redimensionnable ou sans bouton agrandir.
- Mesures : rayon des fenêtres de macOS 26 ≈ 12 pt (Terminal), ≈ 10 pt remplie (Finder) ; marges de Remplir : 8 pt
  (plan 50).

## Revue (points que les tests ne couvrent pas)
1. App qui se réagrandit d'elle-même après le remplissage (jeux, lecteurs) : pas de va-et-vient sans fin.
2. Fenêtre agrandie au démarrage de l'app (état restauré) : laissée telle quelle tant qu'on ne l'agrandit pas.
3. Plein écran puis la fenêtre déplacée ou fermée à la main : l'état gardé ne ressort pas à contretemps.
4. Pastille verte d'une fenêtre élevée (administrateur) : rien ne casse (UIPI).
5. Plusieurs écrans : Remplir et plein écran sur l'écran de la fenêtre.

---

### Tâche 1 : réglage et logique pure

**Fichiers :** `src/shell/window_fill.h` (nouveau, pur), `src/menubar/menubar_settings.h/.cpp`, `src/settings/panes.cpp`,
`tests/test_window_fill.cpp` (nouveau), `build.ps1` (si les tests listent leurs sources).

**Produit :**
```cpp
namespace md {
// Fenêtre que MacDock remplit ou met en plein écran : de premier niveau, à barre de titre, bord redimensionnable et
// bouton agrandir, ni outil ni enfant, d'un autre processus.
bool fillCandidate(LONG style, LONG exStyle, bool ownProcess);
enum class MaximizeReply { Ignore, Fill, Previous };
// Fenêtre passée à l'état agrandi : rien (réglage coupé, fenêtre exclue ou app qui insiste), Remplir, ou retour à la
// taille d'avant quand elle était déjà remplie par MacDock (bascule du double-clic de macOS).
MaximizeReply replyToMaximize(bool enabled, bool candidate, bool givenUp, bool wasFilled);
// App qui se réagrandit d'elle-même : agrandie de nouveau moins de kRefillMs après notre remplissage, deux fois de suite
// → laissée agrandie (jusqu'à la fermeture de la fenêtre).
class FillGuard {
public:
    static constexpr unsigned long long kRefillMs = 1500;
    bool givenUp(HWND h) const;
    void filled(HWND h, unsigned long long now);      // nous venons de la remplir
    void maximized(HWND h, unsigned long long now);   // elle vient de passer à l'état agrandi
    void forget(HWND h);
};
enum class FullscreenStep { Enter, Exit };
// Pastille verte : sortie si MacDock l'a mise en plein écran et qu'elle couvre toujours son écran (à 1 px près), entrée
// sinon (déplacée depuis : l'ancien état est oublié).
FullscreenStep fullscreenStep(bool saved, const RECT& frame, const RECT& monitor);
}
```
Réglage : `MenuBarSettings::fillOnMaximize = true`, clé JSON `fillOnMaximize`. Panneau Fenêtres : interrupteur
« Remplir l'écran au lieu d'agrandir », détail « Comme macOS : la barre des menus et le Dock restent visibles ; la
pastille verte passe en plein écran ».

- [ ] Tests (rouges) : `window_fill_candidates` (fenêtre ordinaire oui ; sans WS_THICKFRAME, sans WS_MAXIMIZEBOX, outil,
  enfant, sans barre de titre, de MacDock : non), `window_fill_replies` (les quatre cas), `window_fill_guard` (une
  réagrandie à 1 s : pas encore ; deux : abandon ; à 2 s : rien ; forget), `window_fill_fullscreen_step`,
  `menubar_settings_fill_on_maximize` (défaut oui, aller-retour JSON).
- [ ] Implémentation, tests verts, commit.

### Tâche 2 : Remplir au lieu d'agrandir

**Fichiers :** `src/menubar/maximize_watch.h/.cpp` (nouveau), `src/menubar/menubar_window.h/.cpp`, `src/shell/window_tile.h/.cpp`,
`build.ps1` (menubar).

**Produit :**
```cpp
// Fenêtres de premier niveau d'un processus qui passent à l'état agrandi (crochet EVENT_OBJECT_LOCATIONCHANGE de ce
// processus seulement) : `message` posté à `sink`, wParam la fenêtre. État de départ relu à follow (aucune action sur
// une fenêtre déjà agrandie).
class MaximizeWatch {
public:
    void setSink(HWND sink, UINT message);
    void follow(DWORD pid);   // 0 : plus rien
    void stop();
};
// window_tile : rend une fenêtre agrandie à sa taille sans l'animation de Windows ; false si elle reste agrandie.
bool restoreWithoutAnimation(HWND window);
// Cadre visible de la fenêtre (DWMWA_EXTENDED_FRAME_BOUNDS, sinon GetWindowRect).
RECT windowFrame(HWND window);
```
MenuBarApp : `maximizeWatch_.follow(pid)` à chaque premier plan adopté (réglage actif) ; `WM_APP_MAXIMIZED` →
`onMaximized(h)` : candidat ? → `guard.maximized` → restauration sans animation → `wasFilled` = cadre restauré égal au
cadre noté après notre dernier remplissage → `replyToMaximize` → `tileWindow(Fill | Previous)` → cadre noté,
`guard.filled`. Réglage changé : suivi arrêté ou repris.

- [ ] Test (rouge) de la logique de suivi, pure (le crochet saute notre propre processus, une fenêtre d'essai ne
  l'atteindrait pas) : `ZoomTracker { bool changed(HWND, bool zoomed); void seed(HWND, bool); void forget(HWND); }`,
  `zoom_tracker_reports_transitions_only` (fenêtre inconnue → notée sans rapport ; non agrandie puis agrandie →
  rapport ; agrandie puis agrandie → rien ; oubliée → inconnue).
- [ ] Implémentation, branchement, tests verts, essai réel avec une fenêtre d'essai (agrandir → remplie ; agrandir de
  nouveau → taille d'avant), commit.

### Tâche 3 : pastille verte = plein écran

**Fichiers :** `src/shell/window_tile.h/.cpp`, `src/menubar/traffic_window.h/.cpp`, `src/menubar/traffic_lights.h/.cpp`,
`src/menubar/bar_actions.cpp`, `src/menubar/menubar_window.cpp`, `src/menubar/app_menus.*` (libellé).

**Produit :**
```cpp
// Plein écran de macOS : cadre visible calé sur l'écran entier (la barre et le Dock se cachent d'eux-mêmes), cadre et
// état agrandi d'avant gardés ; de nouveau : rendus. false si la fenêtre est disparue, réduite, figée ou refusée.
bool toggleFullscreen(HWND window);
bool fullscreenByMacDock(HWND window);   // mise en plein écran par MacDock et couvre toujours son écran
// traffic_lights : la pastille verte demande le plein écran à la barre (mode Remplir) ou envoie SC_MAXIMIZE/SC_RESTORE.
bool greenGoesFullscreen(bool fillMode, bool maximizable);
// traffic_window : `message` posté à `sink` (wParam la fenêtre) au lieu de SC_MAXIMIZE.
void TrafficWindow::setFullscreenSink(HWND sink, UINT message, bool on);
```
- [ ] Tests (rouges) : `lights_green_goes_fullscreen` ; `fullscreen_toggle_real_window` (fenêtre d'essai de ce
  processus : entre → cadre = écran, sort → cadre d'avant ; agrandie avant → agrandie après).
- [ ] Implémentation, branchement (pastille, entrée « Plein écran » / « Quitter le plein écran » du menu de la pastille,
  ActionKind::Zoom), tests verts, essai réel, commit.

### Tâche 4 : coins de macOS (mod public de Windhawk)

**Fichiers :** `windhawk/coins-macos.ps1`, `windhawk/coins-macos.cmd`, `windhawk/coins-windows.cmd` (nouveaux).

Le script vérifie que Windhawk et le mod « Custom Window Corner Radius » (`custom-corner-radius`) sont installés, que
`dwm.exe` est dans la liste d'inclusion de Windhawk (l'ajoute sinon), puis active le mod avec rayon 12, petit rayon 6,
fenêtres ancrées arrondies (`snapped`) et horodate le changement. `-Off` le désactive. `-DryRun` : affiche seulement.
Demande les droits d'administrateur (HKLM) comme `install-macdock-look.ps1`.

- [ ] Essai `-DryRun` sur ce PC (aucune écriture), commit.

### Tâche 5 : relecture, corrections, documents, fusion
- [ ] Relecture indépendante (opus), corrections (rouge → vert), suite complète, Release, MacDock relancé.
- [ ] Journal, feuille de route, ce plan ; fusion locale dans `main`.
