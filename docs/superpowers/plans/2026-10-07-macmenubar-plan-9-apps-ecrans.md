# MacMenuBar — Plan 9 : icônes des autres apps et écrans multiples

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :**
- les icônes de la zone de notification des apps (Discord, Steam, OneDrive…) apparaissent dans la barre, à gauche des icônes système, et réagissent au clic ;
- une barre par écran, celle de l'écran actif pleine, les autres atténuées.

**Architecture :**
- **Protocole.** `src/ipc/protocol` gagne `TrayUpdate` (6) et `TrayRemove` (7). La version reste 1 : les types inconnus sont ignorés des deux côtés, et le pipe est nouveau (`\\.\pipe\MacMenuBar`).
- **Mod Windhawk.** `macdock-hide-taskbar` 1.2 observe les `WM_COPYDATA` de `Shell_TrayWnd` (crochet `WH_CALLWNDPROC`) et relaie la liste des icônes à la barre. Ses fonctions pures sont compilées dans `tests.exe`, avec un faux `windhawk_api.h`.
- **Modèle pur.** `tray_model` tient la liste et calcule les messages de clic.
- **Barre.**
  - Elle sert le pipe (`PipeServer`, déjà utilisé par le Dock) et dessine les images des icônes (`BarDrawItem::image`).
  - Elle gère un `Screen` par écran : fenêtre, rendu, mise en page, échantillon du fond, zone réservée, plein écran et masquage.

**Tech Stack :** C++ `/std:c++latest`, Win32 (crochets, `WM_COPYDATA`, pipes nommés), Direct2D.

**Spec :** `docs/superpowers/specs/2026-10-07-macmenubar-design.md`, sections 4.5, 4.7 et 4.10.

## Global Constraints
- Aucune ressource Apple. Messages et commentaires en français, identifiants en anglais.
- Les tests ne touchent ni à explorer ni aux apps de l'utilisateur :
  - aucun crochet n'est installé dans un autre processus ;
  - aucun `TaskbarCreated` n'est diffusé ;
  - aucun clic n'est envoyé à une vraie icône.
- Aucun essai qui pilote la souris ou le clavier, aucune fenêtre affichée devant l'utilisateur.
- Le mod reste un fichier unique, compilable par Windhawk (`-luser32 -lshell32 -lgdi32`). Il ne doit jamais bloquer le fil de la barre des tâches : le crochet copie, puis le fil du pipe envoie.
- Le format de l'en-tête du pipe ne change pas.

## Review Focus
1. **Données hostiles dans `WM_COPYDATA`** (taille fausse, `cbSize` inconnu, texte sans zéro final, icône invalide) : rien ne plante dans explorer, le message est ignoré. Test : `tray_mod_parses_copydata`.
2. **Mod sans barre** (barre fermée, relancée) et **barre sans mod** : aucune attente infinie, liste renvoyée à la reconnexion. Test : `tray_model_reset_on_reconnect`, plus revue du fil du mod.
3. **Icône dont l'app est morte** : retirée, et pas de clic vers une fenêtre disparue. Test : `tray_model_prunes_dead_windows`.
4. **Écran débranché pendant qu'un menu est ouvert** sur sa barre : pas d'accès à une barre détruite. Revue de `MenuBarApp::rebuildScreens`.
5. **Écrans de DPI différents** : chaque barre a sa hauteur et sa mise en page, et les rectangles `BarLink` sont ceux de la barre ouverte. Test : `menubar_screens_layout`.

---

### Task 1 : Protocole et modèle de la zone de notification

**Files :**
- Modify `src/ipc/protocol.h|.cpp` : `MsgType::TrayUpdate = 6`, `TrayRemove = 7`, `TrayIconEvent`, `makeTrayUpdate`, `parseTrayUpdate`, `makeTrayRemove`, `parseTrayRemove`.
- Create `src/menubar/tray_model.h|.cpp` (pur).
- Test : `tests/test_tray.cpp`.

**Interfaces :**
```cpp
namespace md::ipc {
struct TrayIconEvent {
    std::uint64_t hwnd = 0;   // fenêtre de l'app
    std::uint32_t uid = 0, callback = 0, version = 0, flags = 0;   // flags : NIF_*
    bool hidden = false;
    std::wstring tip;
    std::uint8_t guid[16] = {};   // NIF_GUID, sinon zéros
    std::uint16_t w = 0, h = 0;   // image BGRA prémultipliée (0 : pas d'icône)
    std::vector<std::uint8_t> bgra;
};
Message makeTrayUpdate(const TrayIconEvent& e);
std::optional<TrayIconEvent> parseTrayUpdate(const Message& m);   // tailles vérifiées
Message makeTrayRemove(std::uint64_t hwnd, std::uint32_t uid, const std::uint8_t guid[16]);
std::optional<TrayIconEvent> parseTrayRemove(const Message& m);   // hwnd, uid, guid seulement
}
namespace md {
struct TrayIcon { ipc::TrayIconEvent e; std::uint64_t order = 0; };
class TrayModel {
public:
    void update(const ipc::TrayIconEvent& e);   // ajoute ou remplace (clé : guid sinon hwnd + uid)
    void remove(const ipc::TrayIconEvent& e);
    void clear();
    bool prune(const std::function<bool(std::uint64_t hwnd)>& alive);   // true si une icône est partie
    std::vector<const TrayIcon*> visible() const;   // sans les masquées, la plus récente d'abord
};
struct TrayPost { WPARAM wp; LPARAM lp; UINT msg; };
// Messages à poster pour un clic (bouton 0 gauche, 1 droit) au point écran pt.
std::vector<TrayPost> trayClick(const ipc::TrayIconEvent& e, int button, POINT pt);
}
```

**Tests :**
- `tray_protocol_roundtrip` ;
- `tray_protocol_rejects_bad_sizes` ;
- `tray_model_update_remove_order` ;
- `tray_model_reset_on_reconnect` ;
- `tray_model_prunes_dead_windows` ;
- `tray_click_versions` : version 3 (wParam = uid) et version 4 (`NIN_SELECT` et `WM_CONTEXTMENU`).

**Étapes :** rouge, implémentation, vert, puis commit `feat(menubar): protocole et modèle de la zone de notification`.

### Task 2 : Relais du mod Windhawk

**Files :**
- Modify `windhawk/macdock-hide-taskbar.wh.cpp` (1.2.0) :
  - fonctions pures `parseTrayCopyData` et `encodeTray*` ;
  - liste des icônes ;
  - crochet `WH_CALLWNDPROC` sur le fil de `Shell_TrayWnd` ;
  - conversion de l'icône en BGRA 32 × 32 (`DrawIconEx` dans une DIB) ;
  - second fil client de `\\.\pipe\MacMenuBar` (liste complète à la connexion) ;
  - `TaskbarCreated` diffusé une fois, après la pose du crochet.
- Create `tests/stubs/windhawk_api.h` : faux Windhawk pour compiler le mod dans `tests.exe`.
- Test : `tests/test_tray_mod.cpp`, qui inclut le mod.

**Interfaces (dans le mod, espace anonyme) :**
```cpp
struct TrayRecord { uint64_t hwnd; uint32_t uid, callback, version, flags; bool hidden; std::wstring tip;
                    uint8_t guid[16]; uint32_t hicon; };
enum class TrayOp { None, Add, Modify, Delete, SetVersion };
TrayOp parseTrayCopyData(const COPYDATASTRUCT* cds, TrayRecord& out);   // explorer : jamais d'exception
std::vector<uint8_t> encodeTrayUpdate(const TrayRecord& r, uint16_t w, uint16_t h, const uint8_t* bgra);
std::vector<uint8_t> encodeTrayRemove(const TrayRecord& r);
```

**Tests :**
- `tray_mod_parses_copydata` : ajout, modification, suppression, version, signature fausse, taille trop courte, texte sans zéro final ;
- `tray_mod_frames_parse_in_bar` : les trames du mod sont lues par `md::ipc::parseTrayUpdate`.

**Étapes :** rouge, implémentation, vert, puis commit `feat(windhawk): relais de la zone de notification vers la barre`.

### Task 3 : Icônes des apps dans la barre

**Files :**
- Modify `src/menubar/bar_renderer.*` : `BarDrawItem::image` (BGRA prémultipliée, `imageW`, `imageH`), dessinée à `statusIconSize` et centrée.
- Modify `src/menubar/menubar_settings.*` : `showAppIcons`.
- Modify `src/menubar/menubar_window.*` :
  - `PipeServer` sur `\\.\pipe\MacMenuBar`, messages postés à la fenêtre (`WM_APP_TRAY`) ;
  - `TrayModel` ;
  - icônes placées avant les icônes système, cases de `statusWidth` ;
  - clic gauche et droit : `AllowSetForegroundWindow`, puis `trayClick` ;
  - `prune` toutes les 5 s.
- Modify `build.ps1` : `src\ipc\*.cpp` dans la cible `menubar`.
- Test : `tests/test_tray.cpp`.

**Tests :**
- `bar_renderer_draws_tray_image` : pixels dans la case, rien autour ;
- `menubar_settings_app_icons_roundtrip`.

**Étapes :** rouge, implémentation, vert, puis commit `feat(menubar): icônes des apps dans la barre`.

### Task 4 : Une barre par écran

**Files :**
- Create `src/menubar/bar_screens.h|.cpp` (pur) : liste des écrans, écran actif.
- Modify `src/menubar/menubar_window.*` :
  - `struct Screen` (fenêtre, rendu, moniteur, échelle, hauteur, mise en page, couleur du texte, échantillonneur, visibilité, plein écran, zone réservée) ;
  - `rebuildScreens()` au démarrage et à `WM_DISPLAYCHANGE` ;
  - la barre active est pleine, les autres à 60 % (`BarFrame::opacity`) ;
  - menus ouverts sous la barre cliquée.
- Modify `src/menubar/bar_renderer.*` : `BarFrame::opacity`.
- Test : `tests/test_bar_screens.cpp`.

**Interfaces :**
```cpp
struct ScreenInfo { RECT rect; UINT dpi = 96; bool primary = false; };
std::vector<ScreenInfo> orderScreens(std::vector<ScreenInfo> s);   // principal d'abord, puis de gauche à droite
// Écran actif : celui qui contient le plus de la fenêtre au premier plan ; sans fenêtre (bureau), celui du curseur.
std::size_t activeScreen(const std::vector<ScreenInfo>& s, const RECT* foreground, POINT cursor);
```

**Tests :**
- `menubar_screens_order_and_active` ;
- `menubar_screens_layout` : deux écrans de DPI différents, chaque mise en page à sa largeur ;
- `bar_renderer_dims_inactive` : alpha du texte à 60 %.

**Étapes :** rouge, implémentation, vert, puis commit `feat(menubar): une barre par écran`.

---

## Fin du plan
- Relecture finale par un agent sur le modèle le plus capable, puis une passe de corrections en TDD.
- README (installation du mod 1.2), journal.
- Fusion locale dans `main`.
