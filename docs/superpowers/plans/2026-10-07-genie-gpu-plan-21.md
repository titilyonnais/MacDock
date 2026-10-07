# Génie fluide sur le GPU — plan 21

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans. Étapes en `- [ ]`.

**Goal :** un effet génie (et échelle) fluide et net comme sur macOS : une seule capture de la fenêtre, déformée
sur la carte graphique par un maillage lisse, avec mipmaps et anticrénelage (dans le shader, sans MSAA), au lieu de centaines de miniatures DWM.

**Architecture :** la fenêtre réduite n'est plus capturable directement (Windows.Graphics.Capture ne donne aucune
image d'une fenêtre réduite), mais sa miniature DWM l'est : une fenêtre relais hors écran porte la miniature à
taille réelle, WGC capture le relais (10 à 20 ms, sondé). La texture (mipmaps générées) est dessinée par D3D11 dans
une chaîne d'échange DirectComposition, maillage de `genieMesh` (logique pure, sous-pixel). Pendant
l'attente de la première image, l'ancien rendu par bandes (48, toutes au-delà de 150 ms) assure l'animation ; repli complet sur lui
si WGC est indisponible ou échoue.

**Tech Stack :** C++20/MSVC, D3D11, DXGI, DirectComposition, C++/WinRT (Windows.Graphics.Capture), DWM.

**Spec :** les retours de l'utilisateur (« c'est tout pixelisé, c'est pas fluide… il faut que ça ressemble à
l'identique à macOS ») et la mesure : 400 bandes = 13,5 ms par image d'appels DWM, 128 = 4,3 ms, 16 = 0,5 ms.

## Global Constraints
- Aucune fenêtre visible par l'utilisateur dans les tests ; le relais est toujours hors de tous les écrans.
- Le relais n'est pas exclu de la capture (sinon WGC ne voit rien) ; la fenêtre du génie l'est (`WDA_EXCLUDEFROMCAPTURE`).
- Ne jamais toucher `SPI_SETANIMATION` dans les tests.
- Repli silencieux (journalisé une fois) vers les bandes DWM si WGC, D3D ou la capture échouent.
- Clics : la fenêtre du génie laisse passer les clics (`WS_EX_TRANSPARENT | WS_EX_LAYERED`).

## Review Focus
- Fenêtre fermée pendant la capture ou l'animation : rien ne reste à l'écran, pas de plantage.
- Relais au mauvais endroit (écran à gauche en coordonnées négatives) : jamais visible.
- Première image WGC jamais reçue : l'animation par bandes continue jusqu'au bout.
- Périphérique D3D perdu en cours d'animation : repli, pas de fenêtre figée.
- Deux réductions rapprochées : la capture de la première est abandonnée proprement.

---

### Task 1 : maillage pur `genieMesh` et bandes de repli plafonnées
**Files :** `src/anim/genie.h/.cpp`, `tests/test_genie.cpp`
- Produces : `struct GenieVertex { float x, y, u, v; };`
  `std::vector<GenieVertex> genieMesh(MinimizeEffect e, SIZE src, const RECT& from, const RECT& to, DockPosition edge, double t, int rows);`
  (rows + 1 lignes de 2 sommets : gauche puis droite ; u, v dans [0, 1] de la source ; positions écran non arrondies) ;
  `genieSliceCount(extent)` = clamp(extent / 4, 16, 128).
- [ ] Tests : t = 0 → coins de `from` ; t = 1 → dans `to` ; bord gauche : u = colonnes de droite à gauche ;
  échelle : 1 rangée ; Windows : vide ; lignes monotones vers le Dock ; plafond 128.
- [ ] Implémentation, commit.

### Task 2 : rendu GPU hors écran `GenieGpu`
**Files :** `src/app/genie_gpu.h/.cpp`, `src/glass/shaders/genie_vs.hlsl`, `genie_ps.hlsl`, `tests/test_genie_gpu.cpp`
- Produces : `class GenieGpu { bool init(ID3D11Device*); bool setSource(ID3D11Texture2D* frame, UINT w, UINT h);
  bool draw(ID3D11RenderTargetView* rt, UINT w, UINT h, const std::vector<GenieVertex>&, POINT origin);
  std::vector<std::uint8_t> renderToBgra(...)` (tests, WARP).
- [ ] Tests (WARP) : à t = 0 la sortie reproduit la source ; hors du maillage : transparent ; une source rouge
  réduite garde sa couleur (mipmaps prémultipliées).
- [ ] Implémentation, commit.

### Task 3 : capture par relais `WindowCapture`
**Files :** `src/app/window_capture.h/.cpp`, `build.ps1` (windowsapp.lib pour le Dock), `src/app/main.cpp`
- Produces : `class WindowCapture { bool start(ID3D11Device*, HWND source, SIZE size); ID3D11Texture2D* poll(); void stop(); }`.
- [ ] Sonde `MacDock.exe --genie-capture-probe f.png` : fenêtre outil hors écran, réduite sans activation, capturée
  par le relais, rendue par `GenieGpu` à t = 0,5 (aucune fenêtre visible).
- [ ] Implémentation, commit.

### Task 4 : intégration dans `GenieWindow`
**Files :** `src/app/genie_window.h/.cpp`, `src/app/dock_window.cpp`
- [ ] Fenêtre DComp du génie (MSAA, chaîne d'échange), relais à la première image, bandes masquées une image après.
- [ ] Repli sur les bandes si la capture n'arrive pas ; fin et annulation libèrent capture et relais.
- [ ] Commit, Release, suite complète.
