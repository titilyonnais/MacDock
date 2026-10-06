# MacDock — Plan 2 : géométrie fidèle et Liquid Glass — Plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** rendre le Dock fidèle à macOS Tahoe :
- géométrie Apple : coins continus, grille d'icônes, fond concentrique aux icônes, point indicateur bien espacé, magnification plus sobre ;
- vrai verre Liquid Glass : capture de l'arrière-plan, flou, réfraction du biseau, Fresnel, liseré, teinte adaptative, ombre ;
- outils de calibration.

**Architecture :**
- **Géométrie** : des modules purs (`src/geom`, `src/layout/dock_geometry`, `src/icons/icon_grid`) calculent les formes et les mesures ; ils sont testés unitairement.
- **Verre** : un thread capture l'arrière-plan du Dock avec Desktop Duplication et le publie dans une texture partagée. `GlassRenderer` (D3D11 + shaders HLSL compilés par `fxc`) dessine le verre et les ombres dans une texture. `DockRenderer` la pose sous les icônes avec Direct2D. Sans capture, le rendu se replie sur un verre dépoli Direct2D.

**Tech Stack :**
- MSVC 14.44, C++ (`/std:c++latest`) ;
- SDK Windows 10.0.26100 : D3D11, DXGI 1.5 (Desktop Duplication), Direct2D, DirectComposition, DirectWrite, WIC ;
- HLSL Shader Model 5.0 compilé par `fxc.exe` du SDK.

**Spec :** `docs/superpowers/specs/2026-10-06-macos-dock-design.md` (sections 3, 4.1, 4.2 et 5.3).

**Retours de l'utilisateur sur le plan 1 (prioritaires) :**
1. Les rayons ne sont pas bons : le fond doit être harmonisé (concentrique) avec le rayon des icônes.
2. Le point sous les apps ouvertes est trop collé : il doit être plus espacé.
3. La magnification est trop grosse et « brouillon ».

**Causes mesurées :**
- Les icônes remplissent toute leur case de 48 pt. Sur macOS, la case contient la marge transparente de la grille Apple : la forme visible fait 824/1024 de la case (38,6 pt), avec un rayon de 185,4/824.
- Le fond est un rectangle à coins circulaires de 24 pt, sans lien avec le rayon des icônes.
- Le point est à 1 pt sous le bas de l'icône.
- `largeSize` vaut 128 pt, le maximum de macOS.

**Écarts assumés par rapport à la spec** (à confirmer par l'utilisateur à la relecture du plan) :
- **Repli sans capture : verre dépoli Direct2D** au lieu de l'acrylique système. `DWMWA_SYSTEMBACKDROP_TYPE` floute tout le rectangle de la fenêtre, qui fait toute la largeur de l'écran et dépasse le Dock en hauteur ; on ne peut pas lui donner la forme du Dock.
- **Carte de différence : hors ligne**, en ligne de commande (`--reference`, `--diff`). Le mode en direct se limite à la superposition d'une image de référence.
- **Les captures de référence de macOS ne sont pas versionnées** (contenu Apple). `reference/README.md` décrit comment les produire, et `reference/*.png` est ignoré par git.
- **Conséquence de l'exclusion de capture** (`WDA_EXCLUDEFROMCAPTURE`) : quand le verre est actif, le Dock n'apparaît ni dans les captures d'écran Windows ni dans les partages d'écran. Le réglage `glass: false` désactive le verre et rétablit les captures.

**Valeurs de départ** (recherche du 2026-10-07 ; seule la grille d'icône est sourcée, le reste est estimé puis calibré) :

| Mesure | Valeur | Origine |
|---|---|---|
| Forme visible de l'icône / case | 824/1024 = 0,8046875 | Grille Apple (sourcée) |
| Rayon de l'icône / forme visible | 185,4/824 = 0,225, coin continu | Grille Apple (sourcée) |
| Ombre d'icône | σ = 14/1024 de la case, décalage 12/1024 vers le bas, noir 50 % | Grille Apple (sourcée) |
| Rayon du fond | auto = rayon de l'icône + marge visuelle (≈ 21,4 pt pour 48 pt) | Concentricité (principe Liquid Glass, WWDC25) |
| Pas entre icônes | 52 pt de centre à centre (`iconGap` 4) | Estimation 1,05 à 1,1 × la case |
| Centre du point indicateur | 4 pt au-dessus du bas du fond, diamètre 4 pt | Estimation |
| `largeSize` | 80 pt | Retour utilisateur ; calibrable |

## Global Constraints

- C++ `/std:c++latest`, MSVC, `/W4 /permissive- /EHsc /utf-8`, `UNICODE`, x64 uniquement ; SDK figé à 10.0.26100.0 (`build.ps1`).
- Aucune dépendance externe téléchargée. Les shaders sont compilés par le `fxc.exe` du SDK et embarqués en en-têtes générés dans `build\<Config>\shaders\`.
- Aucune ressource Apple (icônes, logos, police, captures) dans le dépôt.
- Toutes les mesures visuelles sont en points macOS × (DPI de l'écran / 96). Elles sont toutes dans `dock-metrics.json`, rechargées à chaud et bornées par la X-macro de `metrics.h`.
- Un fichier JSON invalide donne les valeurs par défaut et une copie `.bak` du fichier fautif. Un fichier d'une version antérieure est migré sans perdre les personnalisations de l'utilisateur.
- Au repos (aucune animation, souris hors du Dock, arrière-plan du Dock inchangé), le Dock ne redessine rien.
- Les modules logiques (`src/core`, `src/config`, `src/layout`, `src/anim`, `src/model`, `src/geom`, `src/icons/squircle.*`, `src/icons/icon_grid.*`, `src/calib/image_diff.*`, `src/glass/capture_policy.*`) n'incluent pas `<windows.h>`.
- Aucun écran noir et aucun plantage si la capture échoue : on se replie sur le verre dépoli et on réessaie avec un délai croissant.
- Les messages de journal et la documentation sont en français.

## Review Focus

1. **Capture indisponible ou perdue** (bureau sécurisé UAC, Ctrl+Alt+Suppr, changement de résolution → `DXGI_ERROR_ACCESS_LOST`, carte graphique hybride → `DXGI_ERROR_UNSUPPORTED`, écran tourné) : repli immédiat sur le verre dépoli, puis reprise automatique. Tests : `capture_backoff_grows_and_resets`, `capture_rotated_output_unsupported` (Task 7).
2. **Écran HDR** (format FP16 scRGB, niveau de blanc SDR réglé par l'utilisateur) : couleurs justes, pas de verre blanc saturé. Tests : `capture_sdr_white_scale`, `render_glass_hdr_backdrop_not_blown_out` (Tasks 7 et 6).
3. **Arrière-plan extrême** (fond blanc pur, noir pur, très coloré) : le verre, les points et le séparateur restent lisibles en mode clair comme en mode sombre. Test : `render_glass_readable_on_white_and_black` (Task 6).
4. **Fichiers de réglages écrits par le plan 1** (`dock-metrics.json` complet, `settings.json` avec `largeSize` 128) : les nouvelles valeurs s'appliquent et les valeurs modifiées par l'utilisateur sont conservées. Tests : `metrics_migrate_v1_*`, `settings_migrate_v1_*` (Task 3).
5. **Tailles extrêmes** (case de 16 ou 128 pt, rayon plus grand que la forme, fond très large, infobulle très courte) : aucun NaN, forme toujours fermée et contenue dans son rectangle. Tests : `smooth_rect_radius_is_limited`, `geometry_extreme_tile_sizes` (Tasks 1 et 3).

---

## Structure des fichiers

```
src/geom/smooth_rect.h|.cpp        coins continus Apple : contour, masque, champ de distance (pur)
src/icons/squircle.h|.cpp          forme d'icône Apple (coins continus) + test « icon jail » (pur, réécrit)
src/icons/icon_grid.h|.cpp         placement sur la grille Apple et ombre portée d'icône (pur)
src/icons/icon_provider.cpp        utilise la grille (modifié)
src/layout/dock_geometry.h|.cpp    épaisseur, rayon concentrique, point, séparateur (pur)
src/config/metrics.h|.cpp          nouvelles mesures, version 2, migration (modifié)
src/config/settings.h|.cpp         glass, largeSize 80, version 2, migration (modifié)
src/app/dock_controller.cpp        géométrie Apple dans les images (modifié)
src/render/dock_renderer.h|.cpp    formes continues, rendu hors écran, verre (modifié)
src/calib/image_diff.h|.cpp        comparaison d'images, carte de différence (pur)
src/calib/png_io.h|.cpp            lecture/écriture PNG (WIC)
src/glass/shaders/*.hlsl           fullscreen_vs, downsample_ps, blur_ps, glass_ps
src/glass/glass_renderer.h|.cpp    flou + composition du verre (D3D11)
src/glass/capture_policy.h|.cpp    régions, délais de reprise, échelle HDR (pur)
src/glass/backdrop_capture.h|.cpp  Desktop Duplication sur un thread, texture partagée
src/app/dock_window.cpp            capture, exclusion, raccourcis de calibration (modifié)
reference/README.md                protocole de capture des références macOS
tests/test_smooth_rect.cpp, test_icon_grid.cpp, test_geometry.cpp, test_migration.cpp,
tests/test_image_diff.cpp, test_render.cpp, test_capture_policy.cpp
```

`build.ps1` ajoute `src\geom\*.cpp`, `src\calib\*.cpp`, `src\glass\capture_policy.cpp`, `src\glass\glass_renderer.cpp` et `src\render\*.cpp` aux sources des tests, avec les bibliothèques graphiques. Les tests de rendu utilisent WARP, donc sans dépendre de la carte graphique.

---

### Task 1 : Coins continus Apple

**Files :** Create `src/geom/smooth_rect.h|.cpp`, `tests/test_smooth_rect.cpp` ; Modify `build.ps1` (ajouter `src\geom\*.cpp` aux sources logiques et à la cible dock).

**Interfaces :**

```cpp
namespace md {
struct Pt { double x = 0, y = 0; };
constexpr double kCornerExtent = 1.52866483;   // le coin continu commence à 1,5287·r du sommet

// Rayon effectivement utilisable : min(r, min(w, h) / 2 / kCornerExtent), jamais négatif ni NaN.
double limitedCornerRadius(double w, double h, double r);
// Contour fermé (sens horaire, y vers le bas) d'un rectangle à coins continus façon Apple.
// Le premier point est (x + kCornerExtent·rl, y). stepsPerCurve >= 1.
std::vector<Pt> smoothRectOutline(double x, double y, double w, double h, double r, int stepsPerCurve = 12);
bool pointInPolygon(const std::vector<Pt>& poly, double x, double y);      // règle pair-impair
double polygonArea(const std::vector<Pt>& poly);                          // aire absolue (lacet)
// Couverture anticrénelée (4x4) d'un carré size x size contenant la forme de rayon cornerRatio·size.
std::vector<float> smoothSquareMask(int size, double cornerRatio);
// Champ de distance signée d'un coin de rayon 1, sur une grille n x n couvrant [lo, hi]² en coordonnées
// du coin (X vers l'intérieur le long du bord horizontal, Y vers l'intérieur le long du bord vertical,
// (0,0) = sommet). Négatif à l'intérieur. Cellule (i, j) au centre (lo + (i+0.5)·(hi-lo)/n, …), stockée [j*n+i].
std::vector<float> cornerDistanceField(int n, double lo, double hi);
// Lecture bilinéaire du champ (bornée aux bords), identique à l'échantillonnage du shader.
double sampleCornerField(const std::vector<float>& f, int n, double lo, double hi, double X, double Y);
}
```

**Géométrie du coin.** La forme reprend le chemin rétro-conçu de UIKit (« iOS 7 rounded rect »), **symétrisé** autour de la diagonale (l'original s'en écarte de 1e-6, à cause d'un segment manquant). Coordonnées du coin en unités de rayon, depuis `(kCornerExtent, 0)` :

```cpp
struct Seg { bool curve; Pt c1, c2, end; };
const Seg kCorner[] = {
    {true,  {1.08849323, 0}, {0.86840689, 0}, {0.66993427, 0.06549600}},
    {false, {}, {}, {0.63149399, 0.07491100}},
    {true,  {0.37282392, 0.16905899}, {0.16905899, 0.37282392}, {0.07491100, 0.63149399}},
    {false, {}, {}, {0.06549600, 0.66993427}},
    {true,  {0, 0.86840689}, {0, 1.08849323}, {0, kCornerExtent}},
};
```

Correspondance des coordonnées du coin `(X, Y)` vers le rectangle (`rl` = rayon limité, `R = x + w`, `B = y + h`), dans l'ordre de parcours :

| Coin | Point |
|---|---|
| haut-droit | `(R − X·rl, y + Y·rl)` |
| bas-droit | `(R − Y·rl, B − X·rl)` |
| bas-gauche | `(x + X·rl, B − Y·rl)` |
| haut-gauche | `(x + Y·rl, y + X·rl)` |

Pour chaque coin, on ajoute le point de départ, puis les segments : une droite pour `curve == false` ; une courbe cubique échantillonnée en `stepsPerCurve` points, `t = k/steps`, pour `curve == true`. Les bords droits relient implicitement deux coins successifs. Si `rl == 0`, on obtient les 4 sommets.

**`smoothSquareMask`.** Un échantillon dont la coordonnée x ou y tombe dans `[e, size − e]` (`e = kCornerExtent·rl`) est intérieur s'il est dans le carré. Seuls les échantillons situés dans les 4 coins passent par `pointInPolygon`, d'où une cinquantaine de ms pour 512 px.

**`cornerDistanceField`.** On aplatit la courbe du coin avec 32 pas par courbe, en coordonnées du coin. On la prolonge par `(hi + 10, 0)` au début et `(0, hi + 10)` à la fin. La distance est la distance minimale aux segments de cette polyligne. Un point est intérieur si `X > 0`, `Y > 0` et s'il se trouve dans le polygone formé par la polyligne fermée par `(hi + 10, hi + 10)`.

- [ ] **Step 1 : tests** (`tests/test_smooth_rect.cpp`)

```cpp
#include <cmath>
#include "minitest.h"
#include "../src/geom/smooth_rect.h"

TEST_CASE(smooth_rect_radius_is_limited) {
    CHECK_NEAR(md::limitedCornerRadius(100, 40, 30), 40 / 2 / md::kCornerExtent, 1e-9);
    CHECK_NEAR(md::limitedCornerRadius(100, 100, 10), 10, 1e-9);
    CHECK_NEAR(md::limitedCornerRadius(100, 100, -5), 0, 1e-9);
    CHECK_NEAR(md::limitedCornerRadius(0, 0, 10), 0, 1e-9);
    CHECK(std::isfinite(md::limitedCornerRadius(NAN, 10, 10)));
}
TEST_CASE(smooth_rect_outline_inside_bounds_and_closed_shape) {
    auto p = md::smoothRectOutline(10, 20, 200, 64, 21.4);
    CHECK(p.size() > 40);
    for (auto& q : p) { CHECK(q.x >= 10 - 1e-9); CHECK(q.x <= 210 + 1e-9); CHECK(q.y >= 20 - 1e-9); CHECK(q.y <= 84 + 1e-9); }
    CHECK(md::pointInPolygon(p, 110, 52));
    CHECK(!md::pointInPolygon(p, 10.5, 20.5));   // sommet du coin : dehors
    CHECK(md::pointInPolygon(p, 110, 20.5));     // milieu du bord haut : dedans
}
TEST_CASE(smooth_rect_corner_is_symmetric) {
    auto p = md::smoothRectOutline(0, 0, 100, 100, 20, 16);
    // Chaque point a son symétrique par rapport à la diagonale y = x (même contour).
    for (auto& q : p) {
        double best = 1e9;
        for (auto& o : p) best = std::min(best, std::hypot(o.x - q.y, o.y - q.x));
        CHECK(best < 1e-6);
    }
}
TEST_CASE(smooth_rect_diagonal_matches_apple_curve) {
    // Le point diagonal du coin continu est à ≈ 0,2915·r du sommet sur chaque axe.
    auto p = md::smoothRectOutline(0, 0, 100, 100, 20, 64);
    double best = 1e9;
    for (auto& q : p) if (q.x < 50 && q.y < 50) best = std::min(best, std::fabs(q.x - q.y) < 0.2 ? q.x : 1e9);
    CHECK_NEAR(best / 20, 0.2915, 0.01);
}
TEST_CASE(smooth_rect_area_close_to_circular_corners) {
    // Un coin continu retire un peu plus d'aire qu'un quart de cercle ((4 - π)·r²), mais du même ordre.
    auto p = md::smoothRectOutline(0, 0, 200, 200, 20, 32);
    double removed = 200.0 * 200.0 - md::polygonArea(p);
    CHECK(removed > (4 - 3.14159265) * 400 * 0.95);
    CHECK(removed < (4 - 3.14159265) * 400 * 1.6);
}
TEST_CASE(smooth_rect_zero_radius_is_rectangle) {
    auto p = md::smoothRectOutline(0, 0, 10, 5, 0);
    CHECK_NEAR(md::polygonArea(p), 50, 1e-9);
}
TEST_CASE(smooth_square_mask_center_corner_edge) {
    auto m = md::smoothSquareMask(100, 0.225);
    CHECK_NEAR(m[50 * 100 + 50], 1, 1e-9);
    CHECK_NEAR(m[0], 0, 1e-9);
    CHECK_NEAR(m[0 * 100 + 50], 1, 1e-9);   // milieu du bord haut : plein
    double e = m[6 * 100 + 6];             // sur la courbe du coin (rayon 22,5 → diagonale ≈ 6,56)
    CHECK(e > 0.05); CHECK(e < 0.95);
}
TEST_CASE(corner_field_signs_and_edges) {
    auto f = md::cornerDistanceField(128, -2, 2);
    CHECK(md::sampleCornerField(f, 128, -2, 2, -1.5, -1.5) > 1.5);   // dehors, loin
    CHECK(md::sampleCornerField(f, 128, -2, 2, 1.9, 1.9) < -1.5);    // dedans, loin
    CHECK_NEAR(md::sampleCornerField(f, 128, -2, 2, 1.8, 0.5), -0.5, 0.03);   // bord horizontal droit
    CHECK_NEAR(md::sampleCornerField(f, 128, -2, 2, 0.2915, 0.2915), 0, 0.03);   // sur la courbe
}
TEST_CASE(corner_field_is_lipschitz) {
    const int n = 64; auto f = md::cornerDistanceField(n, -2, 2);
    double cell = 4.0 / n;
    for (int j = 0; j < n; ++j)
        for (int i = 0; i + 1 < n; ++i) {
            CHECK(std::fabs(f[j * n + i + 1] - f[j * n + i]) <= cell * 1.05);
            CHECK(std::fabs(f[i * n + j] - f[(i + 1) * n + j]) <= cell * 1.05);
        }
}
```

- [ ] **Step 2 : constater l'échec** : `./build.ps1 -Target tests -Run`. Attendu : erreur de compilation, `smooth_rect.h` introuvable.
- [ ] **Step 3 : implémenter** `src/geom/smooth_rect.cpp` comme décrit, et ajouter `src\geom\*.cpp` à `$LogicSources` et à la cible `dock` dans `build.ps1`.
- [ ] **Step 4 : tests verts.** Attendu : `0 en echec`.
- [ ] **Step 5 : commit** `feat(geom): coins continus façon Apple (contour, masque, champ de distance)`.

### Task 2 : Grille d'icônes Apple et ombre d'icône

**Files :** Modify `src/icons/squircle.h|.cpp`, `tests/test_squircle.cpp`, `src/icons/icon_provider.h|.cpp` ; Create `src/icons/icon_grid.h|.cpp`, `tests/test_icon_grid.cpp`.

**Interfaces :**

```cpp
namespace md {
// squircle.h (forme d'icône Apple : carré à coins continus de rayon kIconCornerRatio·size)
constexpr double kIconShapeRatio = 824.0 / 1024.0;   // forme visible / case
constexpr double kIconCornerRatio = 185.4 / 824.0;   // rayon / forme visible
// L'exposant disparaît ; cornerRatio vient des mesures (iconCornerRatio, Task 3), défaut kIconCornerRatio.
bool insideSquircle(double x, double y, double size, double cornerRatio = kIconCornerRatio);
double squircleMaskAlpha(int px, int py, int size, double cornerRatio = kIconCornerRatio);   // smoothSquareMask en cache
double squircleCoverage(const std::uint8_t* bgra, int w, int h, int stride);
bool iconFitsSquircle(const std::uint8_t* bgra, int w, int h, int stride);   // seuil 0,88 inchangé

// icon_grid.h (pur)
int iconShapePx(int tilePx, double shapeRatio);   // max(1, lround(tilePx·shapeRatio))
// Copie content (carré shape x shape, BGRA prémultiplié) au centre d'une case tile x tile transparente.
std::vector<std::uint8_t> placeOnGrid(const std::vector<std::uint8_t>& content, int shape, int tile);
// Ombre portée : alpha flouté (gaussienne séparable σ = sigmaPx), décalé de offsetYPx, noir à opacity,
// composé SOUS l'image. opacity <= 0 : image inchangée.
void addDropShadow(std::vector<std::uint8_t>& bgra, int size, double sigmaPx, double offsetYPx, double opacity);
}
// IconProvider : remplace setJailInset par
void setGrid(double shapeRatio, double cornerRatio, double jailInset, double shadowOpacity);   // vide le cache si changé
```

**Construction d'une icône de `px` (case) dans `IconProvider::build` :** `s = iconShapePx(px, shapeRatio)`.
- **Icône qui remplit le squircle** : redimensionnée à `s`, masquée avec `applySquircleMask(…, s)`, puis `placeOnGrid(…, s, px)`.
- **« Icon jail »** : `jailPlate(s)` avec l'icône réduite de `jailInset` à l'intérieur, puis `placeOnGrid`.
- **Icône générique et bouton Apps** : dessinés à la taille `s`, puis `placeOnGrid`.
- **Icône personnalisée PNG** : supposée déjà dessinée sur la grille Apple (toile complète), donc redimensionnée à `px` sans masque ni ombre. Le `README.md` le précise.
- **Ombre** : ajoutée à tout sauf aux icônes personnalisées, avec `addDropShadow(img, px, px·14/1024, px·12/1024, shadowOpacity)`.

**Tests**

Fichier `tests/test_icon_grid.cpp` :

```cpp
#include "minitest.h"
#include "../src/icons/icon_grid.h"

static std::vector<std::uint8_t> opaque(int n) { return std::vector<std::uint8_t>(size_t(n) * n * 4, 255); }
static int alphaAt(const std::vector<std::uint8_t>& p, int size, int x, int y) { return p[(size_t(y) * size + x) * 4 + 3]; }

TEST_CASE(icon_grid_shape_size) {
    CHECK_EQ(md::iconShapePx(96, 824.0 / 1024), 77);
    CHECK_EQ(md::iconShapePx(1, 0.8), 1);
}
TEST_CASE(icon_grid_place_centers_with_transparent_margin) {
    auto out = md::placeOnGrid(opaque(77), 77, 96);
    CHECK_EQ(out.size(), size_t(96 * 96 * 4));
    CHECK_EQ(alphaAt(out, 96, 2, 48), 0);
    CHECK_EQ(alphaAt(out, 96, 48, 48), 255);
    CHECK_EQ(alphaAt(out, 96, 93, 48), 0);
}
TEST_CASE(icon_grid_shadow_below_shape_only) {
    auto img = md::placeOnGrid(opaque(60), 60, 100);
    md::addDropShadow(img, 100, 100 * 14.0 / 1024, 100 * 12.0 / 1024, 0.5);
    CHECK(alphaAt(img, 100, 50, 82) > 0);                 // sous la forme : ombre
    CHECK(alphaAt(img, 100, 50, 82) < 128);
    CHECK_EQ(img[(82 * 100 + 50) * 4 + 0], 0);            // noire
    CHECK(alphaAt(img, 100, 50, 18) < alphaAt(img, 100, 50, 82));   // moins au-dessus (décalage vers le bas)
    CHECK_EQ(alphaAt(img, 100, 50, 50), 255);             // forme intacte
}
TEST_CASE(icon_grid_shadow_zero_opacity_is_noop) {
    auto img = md::placeOnGrid(opaque(60), 60, 100), copy = img;
    md::addDropShadow(img, 100, 2, 2, 0);
    CHECK((img == copy));
}
```

Fichier `tests/test_squircle.cpp` :
- remplacer l'appel `insideSquircle(…, exponent)` par la nouvelle signature ;
- garder les cas existants (centre dedans, `(1, 1)` dehors, milieu du bord dedans, `(6, 6)` partiel) ;
- ajouter un cas : `squircleMaskAlpha(0, 50, 100)` vaut 1, car le bord gauche est droit loin des coins.

Fichier `tests/test_icons.cpp` (`icons_strict_mode_keeps_corners_transparent`) : vérifier que le pixel `(px/2, 1)` est transparent (marge de grille) et que le centre est opaque.

- [ ] **Step 1 : écrire les tests.** **Step 2 : constater l'échec** (`icon_grid.h` introuvable ; le test d'icône échoue, car le haut de la case est opaque).
- [ ] **Step 3 : implémenter** `icon_grid.cpp`, réécrire `squircle.cpp` sur `smoothSquareMask` (cache par taille), adapter `icon_provider.cpp` (`maskFor(size, cornerRatio)` lit `smoothSquareMask`, avec un cache par taille et par rapport) et appeler `setGrid(kIconShapeRatio, kIconCornerRatio, metrics_.iconJailInset, 0.5)` dans `DockApp::applySettings`. La Task 3 remplacera ces constantes par les mesures.
- [ ] **Step 4 : tests verts.** Puis lancer `build\Debug\MacDock.exe --snapshot <scratch>\t2.png`. Attendu : icônes plus petites dans leurs cases, marges visibles, légère ombre dessous.
- [ ] **Step 5 : commit** `feat(icons): grille Apple (forme 824/1024, coins continus) et ombre d'icône`.

### Task 3 : Géométrie concentrique, point indicateur, magnification et migration des réglages

**Files :** Create `src/layout/dock_geometry.h|.cpp`, `tests/test_geometry.cpp`, `tests/test_migration.cpp` ; Modify `src/config/metrics.h|.cpp`, `src/config/settings.h|.cpp`, `src/app/dock_controller.cpp`, `src/app/dock_window.cpp` (`loadConfig`), `tests/test_config.cpp`, `tests/test_controller.cpp`.

**Mesures (`metrics.h`)**

Valeurs modifiées :

| Mesure | v1 | v2 | Remarque |
|---|---|---|---|
| `iconGap` | 6 | 4 | |
| `dockCornerRadius` | 24 | 0 | borne 0–200 ; **0 = concentrique (auto)** |
| `separatorLengthRatio` | 0,72 | 0,80 | |
| `tooltipGap` | 12 | 10 | mesuré depuis le haut de la forme visible |

Mesure retirée : `indicatorInset`. La mesure `iconJailInset` est conservée telle quelle.

Mesures ajoutées, avec leurs bornes :

| Mesure | Défaut | Bornes |
|---|---|---|
| `indicatorCenterFromBottom` | 4 | 0–30 |
| `iconShapeRatio` | 0,8046875 | 0,5–1 |
| `iconCornerRatio` | 0,225 | 0–0,5 |
| `iconShadowOpacity` | 0,5 | 0–1 |
| `glassBlur` | 10 | 0–60 ; σ en points |
| `glassBevel` | 9 | 0–40 ; largeur du biseau réfractif en points |
| `glassRefraction` | 0,6 | −2 à 2 ; déplacement max / biseau, positif = vers le centre |
| `glassChromatic` | 0,10 | 0–1 |
| `glassFresnel` | 0,18 | 0–1 |
| `glassSpecular` | 0,55 | 0–1 |
| `glassTintLight` | 0,22 | 0–1 |
| `glassTintDark` | 0,30 | 0–1 |
| `glassSaturation` | 1,15 | 0–3 |
| `tooltipGlassStrength` | 0,5 | 0–1 |

`metricsToJson` écrit `"version": 2`.

**Réglages (`settings.h`)**
- `largeSize` passe à 80 par défaut.
- Ajout de `bool glass = true`.
- `settingsToJson` écrit `"version": 2`.

**Migrations (pures, dans `metrics.cpp` et `settings.cpp`)**

```cpp
constexpr int kMetricsVersion = 2, kSettingsVersion = 2;
// Sans "version" (v1) : une valeur égale à l'ancien défaut prend le nouveau ; les autres sont gardées.
json::Value migrateMetricsJson(const json::Value& v);   // retire "indicatorInset", met "version": 2
json::Value migrateSettingsJson(const json::Value& v);  // largeSize 128 → 80 ; met "version": 2
int jsonVersion(const json::Value& v);                  // 1 si absent
```

Dans `DockApp::loadConfig`, si `jsonVersion(fichier) < version courante`, on migre, on journalise « Réglages migrés de la v1 à la v2 », puis on réécrit le fichier de façon atomique.

**Géométrie (`dock_geometry.h`, pur)**

```cpp
namespace md {
struct DockGeometry {
    double thickness = 0;        // tile + 2·padding (fond au repos)
    double iconShape = 0;        // tile·iconShapeRatio
    double iconRadius = 0;       // iconShape·iconCornerRatio
    double visibleInset = 0;     // (tile − iconShape) / 2
    double cornerRadius = 0;     // fond : auto = iconRadius + padding + visibleInset, sinon dockCornerRadius ; ≤ thickness/2
    double indicatorCenter = 0;  // du bas du fond au centre du point
    double separatorLength = 0;  // tile·separatorLengthRatio
};
DockGeometry dockGeometry(double tileSize, const Metrics& m);
}
```

**Contrôleur et icônes**
- `DockApp::applySettings` appelle `setGrid(m.iconShapeRatio, m.iconCornerRatio, m.iconJailInset, m.iconShadowOpacity)`, ce qui garde les icônes et le fond concentriques avec les mêmes mesures.
- `f.cornerRadius = g.cornerRadius·s` (le renderer applique `limitedCornerRadius`).
- Indicateur : la position `RenderIcon::indicatorY = bgBottom − g.indicatorCenter·s` est ajoutée à `RenderIcon`, et le renderer n'utilise plus `indicatorInset`.
- Séparateur : `g.separatorLength·s`.
- Infobulle : `bottom = cy − size/2 + size·(1 − iconShapeRatio)/2 − tooltipGap·s`.

**Tests**

Fichier `tests/test_geometry.cpp` :

```cpp
#include <cmath>
#include "minitest.h"
#include "../src/layout/dock_geometry.h"

TEST_CASE(geometry_concentric_default) {
    md::Metrics m; auto g = md::dockGeometry(48, m);
    CHECK_NEAR(g.thickness, 64, 1e-9);
    CHECK_NEAR(g.iconShape, 38.625, 1e-9);
    CHECK_NEAR(g.iconRadius, 8.690625, 1e-9);
    CHECK_NEAR(g.visibleInset, 4.6875, 1e-9);
    CHECK_NEAR(g.cornerRadius, 8.690625 + 8 + 4.6875, 1e-9);
    CHECK_NEAR(g.indicatorCenter, 4, 1e-9);
    // Le point ne touche pas l'icône : au moins 5 pt entre le haut du point et le bas de la forme visible.
    CHECK(m.dockPadding + g.visibleInset - (g.indicatorCenter + m.indicatorDiameter / 2) >= 5);
}
TEST_CASE(geometry_fixed_radius_is_clamped) {
    md::Metrics m; m.dockCornerRadius = 30; CHECK_NEAR(md::dockGeometry(48, m).cornerRadius, 30, 1e-9);
    m.dockCornerRadius = 100; CHECK_NEAR(md::dockGeometry(48, m).cornerRadius, 32, 1e-9);
}
TEST_CASE(geometry_extreme_tile_sizes) {
    md::Metrics m;
    for (double t : {16.0, 128.0, 0.0, -5.0, NAN}) {
        auto g = md::dockGeometry(t, m);
        CHECK(std::isfinite(g.cornerRadius)); CHECK(g.cornerRadius >= 0); CHECK(g.cornerRadius <= g.thickness / 2 + 1e-9);
    }
}
```

Fichier `tests/test_migration.cpp` :

```cpp
#include "minitest.h"
#include "../src/config/metrics.h"
#include "../src/config/settings.h"

TEST_CASE(metrics_migrate_v1_updates_old_defaults) {
    auto v = md::migrateMetricsJson(*md::json::parse(
        R"({"iconGap":6,"dockCornerRadius":24,"separatorLengthRatio":0.72,"tooltipGap":12,"indicatorInset":3})"));
    auto m = md::metricsFromJson(v);
    CHECK_NEAR(m.iconGap, 4, 1e-9); CHECK_NEAR(m.dockCornerRadius, 0, 1e-9);
    CHECK_NEAR(m.separatorLengthRatio, 0.80, 1e-9); CHECK_NEAR(m.tooltipGap, 10, 1e-9);
    CHECK(v.find("indicatorInset") == nullptr);
    CHECK_EQ(md::jsonVersion(v), 2);
}
TEST_CASE(metrics_migrate_v1_keeps_user_values) {
    auto m = md::metricsFromJson(md::migrateMetricsJson(*md::json::parse(R"({"dockCornerRadius":30,"iconGap":9})")));
    CHECK_NEAR(m.dockCornerRadius, 30, 1e-9); CHECK_NEAR(m.iconGap, 9, 1e-9);
}
TEST_CASE(metrics_v2_is_not_migrated) {
    auto m = md::metricsFromJson(md::migrateMetricsJson(*md::json::parse(R"({"version":2,"iconGap":6})")));
    CHECK_NEAR(m.iconGap, 6, 1e-9);
}
TEST_CASE(settings_migrate_v1_large_size) {
    CHECK_NEAR(md::settingsFromJson(md::migrateSettingsJson(*md::json::parse(R"({"largeSize":128})"))).largeSize, 80, 1e-9);
    CHECK_NEAR(md::settingsFromJson(md::migrateSettingsJson(*md::json::parse(R"({"largeSize":100})"))).largeSize, 100, 1e-9);
    CHECK_NEAR(md::settingsFromJson(md::migrateSettingsJson(*md::json::parse(R"({"version":2,"largeSize":128})"))).largeSize, 128, 1e-9);
}
TEST_CASE(settings_glass_defaults_on_and_roundtrips) {
    CHECK(md::settingsFromJson(*md::json::parse("{}")).glass);
    md::Settings s; s.glass = false;
    CHECK(!md::settingsFromJson(md::settingsToJson(s)).glass);
}
```

Fichier `tests/test_controller.cpp` : ajouter `controller_indicator_is_spaced_from_icon`. Il construit une image avec une app ouverte et vérifie :
- `indicatorY − (cy + size/2 − size·(1 − ratio)/2) ≥ 5·scale`, c'est-à-dire que le point est sous la forme visible avec au moins 5 pt d'écart ;
- `indicatorY < bgBottom`.

Fichier `tests/test_config.cpp` : `metrics_partial_override` garde `dockCornerRadius: 30`. Ajuster les attentes de défaut qui ont changé.

- [ ] **Step 1 : tests.** **Step 2 : constater l'échec** (`dock_geometry.h` et `migrateMetricsJson` introuvables).
- [ ] **Step 3 : implémenter** (géométrie, mesures, migrations, contrôleur, `loadConfig`).
- [ ] **Step 4 : tests verts.** Captures `--snapshot` au repos et `--hover 0`. Attendu :
  - coins du fond plus doux et parallèles à ceux des icônes ;
  - point nettement détaché ;
  - icône survolée à 80 pt au plus.
- [ ] **Step 5 : commit** `feat(layout): fond concentrique, point espacé, magnification 80 pt, migration v1→v2`.

### Task 4 : Rendu des formes continues et rendu hors écran testable

**Files :** Modify `src/render/dock_renderer.h|.cpp`, `build.ps1` ; Create `tests/test_render.cpp`, `tests/render_fixtures.h`.

**Interfaces ajoutées :**

```cpp
class DockRenderer {
public:
    bool initOffscreen();   // device WARP, sans fenêtre ni DirectComposition (tests, captures)
    // Rendu dans une image BGRA prémultipliée w x h, sur un fond donné (wallpaper : BGRA w x h, vide = dégradé factice).
    std::vector<std::uint8_t> renderToBgra(const RenderFrame& f, const Metrics& m, const std::wstring& font, UINT w, UINT h,
                                           const std::vector<std::uint8_t>& wallpaper = {});
    ID3D11Device* device() const;
};
```

`renderToFile` devient `renderToBgra` suivi d'une écriture PNG. L'écriture passe par `src/calib/png_io.h`, créé dès cette tâche :

```cpp
bool writePng(const std::wstring& path, const std::uint8_t* bgra, UINT w, UINT h);
std::vector<std::uint8_t> readPng(const std::wstring& path, UINT& w, UINT& h);   // BGRA prémultiplié ; vide si échec
```

**Formes**
- Le fond, son ombre et l'infobulle utilisent un `ID2D1PathGeometry` construit à partir de `smoothRectOutline` en pixels, avec `limitedCornerRadius`. Un cache garde la dernière géométrie pour chaque clé `(l, t, r, b, rayon)` arrondie à 1/64 px.
- L'infobulle est une capsule : rayon `h/2`, limité.

**`build.ps1`** : la cible tests ajoute `src\render\*.cpp` et `src\calib\*.cpp`, plus les bibliothèques `d3d11 dxgi d2d1 dwrite dcomp dxguid`.

**`tests/render_fixtures.h`**
- `solidIcon(px, bgra)` : icône carrée de couleur unie, posée sur la grille ;
- `sampleFrame(dark, scale)` : 3 icônes, dont la 2e est ouverte, puis un séparateur et une 4e icône. Fond au repos, centré dans 900×220 px à l'échelle 2 ;
- `flatWallpaper(w, h, b, g, r)`.

**Tests (`tests/test_render.cpp`)**

```cpp
TEST_CASE(render_offscreen_corner_is_wallpaper_edge_is_glass) {
    md::DockRenderer r; REQUIRE(r.initOffscreen());
    auto f = sampleFrame(false, 2); md::Metrics m;
    auto wall = flatWallpaper(900, 220, 40, 40, 40);
    auto img = r.renderToBgra(f, m, L"", 900, 220, wall);
    REQUIRE(img.size() == size_t(900 * 220 * 4));
    auto at = [&](int x, int y) { return &img[(size_t(y) * 900 + x) * 4]; };
    // Sommet du coin haut-gauche : fond d'écran intact (à l'ombre près : écart ≤ 12).
    int cx = int(f.bgLeft) + 1, cy = int(f.bgTop) + 1;
    CHECK(std::abs(int(at(cx, cy)[0]) - 40) <= 12);
    // Milieu du bord haut, 3 px sous le bord : verre clair (nettement plus clair que le fond gris foncé).
    CHECK(at(450, int(f.bgTop) + 3)[1] > 70);
}
TEST_CASE(render_indicator_drawn_below_visible_icon) {
    md::DockRenderer r; REQUIRE(r.initOffscreen());
    auto f = sampleFrame(false, 2); md::Metrics m;
    auto img = r.renderToBgra(f, m, L"", 900, 220, flatWallpaper(900, 220, 200, 200, 200));
    const auto& ic = f.icons[1];
    auto px = [&](float x, float y) { return &img[(size_t(y) * 900 + size_t(x)) * 4]; };
    CHECK(px(ic.cx, ic.indicatorY)[0] < 90);                  // point sombre en mode clair
    float visibleBottom = ic.cy + ic.size / 2 - ic.size * float(1 - m.iconShapeRatio) / 2;
    float mid = (visibleBottom + ic.indicatorY - float(m.indicatorDiameter)) / 2;
    CHECK(px(ic.cx, mid)[0] > 120);                           // espace libre entre l'icône et le point
}
TEST_CASE(render_dark_mode_indicator_is_light) {
    md::DockRenderer r; REQUIRE(r.initOffscreen());
    auto f = sampleFrame(true, 2); md::Metrics m;
    auto img = r.renderToBgra(f, m, L"", 900, 220, flatWallpaper(900, 220, 30, 30, 30));
    const auto& ic = f.icons[1];
    CHECK(img[(size_t(ic.indicatorY) * 900 + size_t(ic.cx)) * 4] > 170);
}
```

- [ ] **Step 1 : tests et fixtures.** **Step 2 : constater l'échec** (`initOffscreen` introuvable).
- [ ] **Step 3 : implémenter.** `initOffscreen` passe par `D3D_DRIVER_TYPE_WARP` puis la même chaîne D2D, sans DirectComposition. `render()` et `renderToBgra` partagent `drawFrame`.
- [ ] **Step 4 : tests verts** et capture `--snapshot` à vérifier à l'œil. Attendu : coins continus du fond et de l'infobulle, sans facettes visibles à 200 %.
- [ ] **Step 5 : commit** `feat(render): formes continues, rendu hors écran testé (WARP)`.

### Task 5 : Outils de calibration

**Files :** Create `src/calib/image_diff.h|.cpp`, `tests/test_image_diff.cpp`, `reference/README.md` ; Modify `.gitignore` (`reference/*.png`), `src/app/main.cpp`, `src/app/dock_window.h|.cpp`.

**Interfaces :**

```cpp
namespace md {
struct DiffStats { double meanAbs = 0; int maxAbs = 0; double fractionAbove = 0; bool sameSize = false; };
// Écart par canal (B, G, R), alpha ignoré ; fractionAbove = part des pixels dont l'écart max dépasse threshold.
DiffStats diffImages(const std::uint8_t* a, const std::uint8_t* b, int w, int h, int threshold);
// Carte de différence : gris = identique, rouge = a plus clair, bleu = b plus clair ; intensité ∝ écart.
std::vector<std::uint8_t> diffHeatmap(const std::uint8_t* a, const std::uint8_t* b, int w, int h);
}
```

**Ligne de commande** : `MacDock.exe --snapshot out.png [--hover x] [--wallpaper fond.png] [--reference ref.png --diff diff.png]`.
- `--wallpaper` rend le Dock sur l'image donnée, redimensionnée à la fenêtre.
- `--reference` compare le rendu à la référence : la zone comparée est le bas de la référence, à la taille du rendu.
- `--diff` écrit la carte de différence et un fichier `diff.txt` avec `meanAbs`, `maxAbs` et `fractionAbove(24)`. Ces valeurs vont aussi dans le journal.

**Superposition en direct :** `RegisterHotKey` sur la fenêtre du Dock.
- `Ctrl+Alt+Maj+O` affiche ou masque `%APPDATA%\MacDock\reference\overlay.png`. L'image est calée en bas et au centre de l'écran, avec une échelle de `scale_/2`, car une capture Retina est en @2x.
- `Ctrl+Alt+Maj+Haut/Bas` règle son opacité par pas de 0,1 (0,5 au départ).

Le renderer dessine la superposition en dernier, par-dessus tout. Si le fichier est absent, le journal l'indique et rien n'est affiché.

**`reference/README.md`** :
- protocole sur un Mac Tahoe :
  - `defaults write com.apple.dock tilesize -int 48` ;
  - `defaults write com.apple.dock largesize -int 80` ;
  - `killall Dock` ;
  - fond d'écran uni (gris 50 %), puis capture plein écran `Cmd+Maj+3` en clair et en sombre, au repos et au survol ;
- noms de fichiers attendus ;
- comment lancer la comparaison ;
- rappel : ces images ne sont jamais versionnées.

**Tests (`tests/test_image_diff.cpp`)**

```cpp
TEST_CASE(diff_identical_is_zero) {
    std::vector<std::uint8_t> a(4 * 4 * 4, 100);
    auto s = md::diffImages(a.data(), a.data(), 4, 4, 10);
    CHECK_NEAR(s.meanAbs, 0, 1e-9); CHECK_EQ(s.maxAbs, 0); CHECK_NEAR(s.fractionAbove, 0, 1e-9);
}
TEST_CASE(diff_counts_pixels_above_threshold) {
    std::vector<std::uint8_t> a(2 * 2 * 4, 100), b = a;
    b[0] = 160;                                        // un pixel, canal B, écart 60
    auto s = md::diffImages(a.data(), b.data(), 2, 2, 24);
    CHECK_EQ(s.maxAbs, 60); CHECK_NEAR(s.fractionAbove, 0.25, 1e-9);
}
TEST_CASE(diff_heatmap_colors) {
    std::vector<std::uint8_t> a(4, 0), b(4, 0); a[2] = 200;   // a plus clair (rouge)
    auto h = md::diffHeatmap(a.data(), b.data(), 1, 1);
    CHECK(h[2] > h[0]);
    auto g = md::diffHeatmap(a.data(), a.data(), 1, 1);
    CHECK_EQ(g[0], g[2]);
}
TEST_CASE(diff_null_inputs_are_safe) {
    auto s = md::diffImages(nullptr, nullptr, 0, 0, 10); CHECK(!s.sameSize);
}
```

- [ ] **Step 1 : tests.** **Step 2 : constater l'échec.** **Step 3 : implémenter** (diff, options de ligne de commande, raccourcis, README). **Step 4 : tests verts**, puis :
  - `--snapshot a.png --reference a.png --diff d.png` : attendu `meanAbs 0` dans `diff.txt` ;
  - `Ctrl+Alt+Maj+O` avec une image test : attendu, superposition visible (vérification visuelle si l'écran est accessible, sinon notée comme manuelle).
- [ ] **Step 5 : commit** `feat(calib): comparaison avec une référence, carte de différence, superposition`.

### Task 6 : Moteur de verre (shaders, flou, réfraction) testé hors écran

**Files :** Create `src/glass/shaders/fullscreen_vs.hlsl`, `downsample_ps.hlsl`, `blur_ps.hlsl`, `glass_ps.hlsl`, `src/glass/glass_renderer.h|.cpp` ; Modify `build.ps1`, `src/render/dock_renderer.h|.cpp`, `src/app/dock_controller.cpp`, `tests/test_render.cpp`.

**Construction des shaders (`build.ps1`)**
- Avant `cl`, chaque `src\glass\shaders\*.hlsl` est compilé dans le même `cmd` que `vcvars`, ce qui met `fxc` dans le PATH.
- Commande : `fxc /nologo /O3 /T <ps_5_0|vs_5_0> /E main /Vn g_<nom> /Fh build\<Config>\shaders\<nom>.h <fichier>`. Le profil est `vs_5_0` si le nom finit par `_vs`.
- `cl` reçoit `/I build\<Config>\shaders`.
- Un échec de `fxc` arrête la construction.

**Interfaces :**

```cpp
namespace md {
struct GlassShape {                 // pixels de la cible
    float left, top, right, bottom, radius;   // radius = limitedCornerRadius(...)
    float strength = 1;             // 1 = Dock ; tooltipGlassStrength pour l'infobulle
    float shadowOpacity = 0;        // 0 = pas d'ombre
};
struct GlassParams {
    float scale = 1; bool dark = false;
    float blurSigmaPx, bevelPx, refraction, chromatic, fresnel, specular, tint, saturation;
    float shadowBlurPx, shadowOffsetPx;
    bool backdropIsScRgb = false; float sdrWhiteScale = 1;   // HDR : divise par ce facteur avant conversion sRGB
};
class GlassRenderer {
public:
    bool init(ID3D11Device* dev);   // shaders, échantillonneurs, LUT de coin (R32_FLOAT 128x128, cornerDistanceField(128,-2,2))
    // backdrop : arrière-plan de la cible, en pixels de la cible. target : BGRA8 prémultiplié w x h (RTV), vidé puis rempli
    // du verre et des ombres (transparent ailleurs).
    bool render(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* backdrop, UINT w, UINT h,
                ID3D11RenderTargetView* target, std::span<const GlassShape> shapes, const GlassParams& p);
};
}
```

**Passes de rendu**
1. **Réduction** : `downsample_ps`, réduction par 4 en moyennant 4 lectures bilinéaires, vers une texture BGRA8 `w/4 × h/4`. En HDR, conversion `srgb(saturate(c / sdrWhiteScale))` dans cette même passe.
2. **Flou** : `blur_ps`, gaussienne séparable, horizontale puis verticale (constante `dir`), avec `σ = blurSigmaPx / 4` et 2σ+1 lectures pondérées, sans dépasser 31. La texture finale a des mipmaps (`GenerateMips`), ce qui donne la luminance moyenne au dernier niveau.
3. **Verre** : `glass_ps`, un quad par forme couvrant son rectangle élargi de `3·shadowBlurPx` et clippé à la cible. Mélange prémultiplié (`ONE, INV_SRC_ALPHA`) sur la cible vidée en transparent.

**`glass_ps` (pour chaque pixel `p` de la forme, en pixels locaux)**
- **Distance signée** `d(p)`. Les coordonnées du coin sont `q = (min(px, w−px), min(py, h−py)) / rl`.
  - Si `q ∈ [−2, 2]²` : `d = LUT(q) · rl`, avec une lecture bilinéaire identique à `sampleCornerField`.
  - Sinon, si `q ≥ 0` : `d = −min(q.x, q.y) · rl`.
  - Sinon : `d = length(max(−q, 0)) · rl`.
- **Normale** : `n = normalize(∇d)`, différences centrées sur ±1 px.
- **Couverture** : `cov = saturate(0.5 − d)`.
- **Biseau** : `t = saturate(−d / bevelPx)`. Déplacement `disp = refraction · strength · bevelPx · (1 − t)²`, dirigé vers `−n` (vers le centre si positif).
- **Aberration chromatique** : `R`, `G` et `B` sont lus aux déplacements `disp·(1+c)`, `disp` et `disp·(1−c)`, avec `c = chromatic · (1 − t)`.
- **Couleur de base** : lecture du flou au point déplacé, saturation × `saturation`.
- **Teinte adaptative** :
  - `L` = luminance du dernier mip ;
  - mode clair : `col = lerp(col, 1, tint · (0.6 + 0.4·(1 − L)))` ;
  - mode sombre : `col = lerp(col, 0.08, tint · (0.6 + 0.4·L))` ;
  - garde-fou de contraste : luminance finale ramenée dans `[0.30, 0.92]` en clair et `[0.06, 0.42]` en sombre.
- **Fresnel** : `+ fresnel · strength · (1 − t)³`, en blanc.
- **Liseré spéculaire** : `+ specular · strength · exp(−(−d) / (0.75·scale)) · (0.35 + 0.65·saturate(dot(n, normalize(−1, −1))))`, en blanc. Un liseré bas-droit plus faible vaut 40 % de cette valeur.
- **Ombre**, à l'extérieur, `do = d(p − (0, shadowOffsetPx))` : `a = shadowOpacity · exp(−max(do, 0)² / (2·(shadowBlurPx/2)²))`.
- **Sortie prémultipliée** : `glass·cov + (0,0,0,a)·(1 − cov)`.

**Intégration au renderer (repli inclus)**
- `RenderFrame` gagne `bool glass` (verre disponible), `float tooltipLeft, tooltipTop, tooltipRight, tooltipBottom` (calculés par le renderer) et `bool glassBackdropScRgb`.
- `DockRenderer` possède :
  - `backdropTex_`, texture BGRA8 ou RGBA16F de la taille de la fenêtre, remplie par l'appelant ;
  - `glassTex_`, BGRA8 avec RTV et SRV, enveloppée en `ID2D1Bitmap1` par `CreateBitmapFromDxgiSurface`.
- **À chaque image, si `glass`** : `GlassRenderer::render`, avec deux formes (le Dock, avec ombre `shadowOpacity` ; l'infobulle si elle est visible, de force `tooltipGlassStrength` et d'opacité ∝ fondu), puis `DrawBitmap(glassTex_)` sous les icônes.
- **Sinon** : verre dépoli Direct2D (dégradé, liseré, ombre) sur la forme continue.
- Le texte de l'infobulle est toujours dessiné en Direct2D.
- `renderToBgra` téléverse le fond fourni dans `backdropTex_` et force `glass = true`. On teste ainsi le vrai pipeline hors écran.

**Tests ajoutés (`tests/test_render.cpp`, WARP)**

```cpp
TEST_CASE(render_glass_refracts_near_edge) {
    // Fond à rayures verticales noires/blanches de 8 px : sans réfraction, le pixel juste sous le bord haut
    // et le pixel du même x au centre du fond ont la même couleur de rayure floutée ; avec réfraction, ils diffèrent.
    md::DockRenderer r; REQUIRE(r.initOffscreen());
    auto f = sampleFrame(false, 2); md::Metrics m; m.glassBlur = 0.5;
    auto stripes = stripedWallpaper(900, 220, 8);
    m.glassRefraction = 0; auto flat = r.renderToBgra(f, m, L"", 900, 220, stripes);
    m.glassRefraction = 1.5; auto bent = r.renderToBgra(f, m, L"", 900, 220, stripes);
    int y = int(f.bgTop) + 6, diff = 0;
    for (int x = int(f.bgLeft) + 60; x < int(f.bgRight) - 60; ++x)
        diff += std::abs(int(flat[(size_t(y) * 900 + x) * 4]) - int(bent[(size_t(y) * 900 + x) * 4]));
    CHECK(diff > 2000);
}
TEST_CASE(render_glass_readable_on_white_and_black) {
    md::DockRenderer r; REQUIRE(r.initOffscreen()); md::Metrics m;
    for (bool dark : {false, true})
        for (int v : {0, 255}) {
            auto f = sampleFrame(dark, 2);
            auto img = r.renderToBgra(f, m, L"", 900, 220, flatWallpaper(900, 220, v, v, v));
            // Verre « profond » (hors biseau, sans icône) : à côté du séparateur, à mi-hauteur.
            int glassY = int((f.bgTop + f.bgBottom) / 2), x = int(f.icons[3].cx) + 6;
            double lum = img[(size_t(glassY) * 900 + x) * 4 + 1] / 255.0;
            if (dark) { CHECK(lum >= 0.05); CHECK(lum <= 0.45); }
            else { CHECK(lum >= 0.28); CHECK(lum <= 0.95); }
            const auto& ic = f.icons[1];   // le point contraste avec le verre (écart ≥ 60)
            int dot = img[(size_t(ic.indicatorY) * 900 + size_t(ic.cx)) * 4 + 1];
            CHECK(std::abs(dot - int(lum * 255)) >= 60);
        }
}
TEST_CASE(render_glass_hdr_backdrop_not_blown_out) {
    // Fond scRGB à 3.0 (blanc SDR à 240 nits) avec sdrWhiteScale = 3 : même rendu qu'un fond SDR blanc (écart ≤ 8).
    md::DockRenderer r; REQUIRE(r.initOffscreen()); md::Metrics m;
    auto f = sampleFrame(false, 2);
    auto sdr = r.renderToBgra(f, m, L"", 900, 220, flatWallpaper(900, 220, 255, 255, 255));
    auto hdr = r.renderToBgraScRgb(f, m, L"", 900, 220, 3.0f /*valeur*/, 3.0f /*sdrWhiteScale*/);
    int x = int(f.icons[3].cx) + 6, y = int((f.bgTop + f.bgBottom) / 2);   // à côté du séparateur
    CHECK(std::abs(int(sdr[(size_t(y) * 900 + x) * 4 + 1]) - int(hdr[(size_t(y) * 900 + x) * 4 + 1])) <= 8);
}
TEST_CASE(render_glass_shadow_outside_only) {
    md::DockRenderer r; REQUIRE(r.initOffscreen()); md::Metrics m;
    auto f = sampleFrame(false, 2);
    auto img = r.renderToBgra(f, m, L"", 900, 220, flatWallpaper(900, 220, 230, 230, 230));
    int below = img[(size_t(f.bgBottom) + 4) * 900 * 4 + 450 * 4 + 1];
    CHECK(below < 230);              // ombre sous le Dock
    CHECK(below > 150);              // douce
}
```

`renderToBgraScRgb` (réservé aux tests) remplit `backdropTex_` en RGBA16F avec une valeur uniforme et règle `glassBackdropScRgb` et `sdrWhiteScale`. Les fixtures gagnent `stripedWallpaper`.

- [ ] **Step 1 : tests.** **Step 2 : constater l'échec** (`GlassRenderer` absent, la réfraction n'a aucun effet).
- [ ] **Step 3 : implémenter** les shaders, la compilation dans `build.ps1`, `GlassRenderer` et l'intégration.
- [ ] **Step 4 : tests verts.** Puis captures `--snapshot` sur 3 fonds : `--wallpaper` d'un fond coloré, du blanc et du noir. Attendu à l'œil : verre translucide, bords qui déforment le fond, liseré fin, ombre douce.
- [ ] **Step 5 : commit** `feat(glass): verre Liquid Glass (flou, réfraction, Fresnel, liseré, teinte adaptative, ombre)`.

### Task 7 : Capture de l'arrière-plan (Desktop Duplication)

**Files :** Create `src/glass/capture_policy.h|.cpp`, `tests/test_capture_policy.cpp`, `src/glass/backdrop_capture.h|.cpp` ; Modify `build.ps1` (`capture_policy.cpp` dans les sources logiques), `src/app/main.cpp` (option `--capture-test`).

**Interfaces pures (`capture_policy.h`) :**

```cpp
namespace md {
struct IRect { long left = 0, top = 0, right = 0, bottom = 0; };
bool intersects(const IRect& a, const IRect& b);              // rectangles vides : false
// Région de l'écran virtuel → coordonnées de la sortie (desktopCoords), bornée ; vide si hors sortie.
IRect toOutputRect(const IRect& screen, const IRect& outputDesktop);
bool anyIntersects(const IRect& region, const IRect* rects, std::size_t n);
// Rotation DXGI (1 = IDENTITY, 0 = UNSPECIFIED) : seules ces deux valeurs sont prises en charge.
bool rotationSupported(int dxgiRotation);
// SDRWhiteLevel de DISPLAYCONFIG (1000 = 80 nits) → facteur scRGB du blanc SDR ; 1 si invalide.
float sdrWhiteScale(unsigned sdrWhiteLevel);
class CaptureBackoff {                                         // délais de reprise : 250, 500, 1000, 2000, 2000… ms
public:
    unsigned nextDelayMs();
    void reset();
};
}
```

**Tests (`tests/test_capture_policy.cpp`)**

```cpp
TEST_CASE(capture_region_to_output) {
    md::IRect out = md::toOutputRect({100, 900, 1000, 1080}, {0, 0, 1920, 1080});
    CHECK_EQ(out.left, 100); CHECK_EQ(out.bottom, 1080);
    md::IRect second = md::toOutputRect({3900, 1400, 4000, 1628}, {3840, 548, 5760, 1628});
    CHECK_EQ(second.left, 60); CHECK_EQ(second.top, 852);
    md::IRect none = md::toOutputRect({0, 0, 10, 10}, {3840, 548, 5760, 1628});
    CHECK(none.right <= none.left);
}
TEST_CASE(capture_dirty_rect_filter) {
    md::IRect region{0, 900, 1920, 1080};
    md::IRect rects[2] = {{0, 0, 100, 100}, {500, 1000, 600, 1050}};
    CHECK(md::anyIntersects(region, rects, 2));
    CHECK(!md::anyIntersects(region, rects, 1));
    CHECK(!md::intersects({0, 0, 0, 0}, region));
}
TEST_CASE(capture_rotated_output_unsupported) {
    CHECK(md::rotationSupported(0)); CHECK(md::rotationSupported(1));
    CHECK(!md::rotationSupported(2)); CHECK(!md::rotationSupported(4));
}
TEST_CASE(capture_sdr_white_scale) {
    CHECK_NEAR(md::sdrWhiteScale(1000), 1.0, 1e-6);
    CHECK_NEAR(md::sdrWhiteScale(3000), 3.0, 1e-6);
    CHECK_NEAR(md::sdrWhiteScale(0), 1.0, 1e-6);
}
TEST_CASE(capture_backoff_grows_and_resets) {
    md::CaptureBackoff b;
    CHECK_EQ(b.nextDelayMs(), 250u); CHECK_EQ(b.nextDelayMs(), 500u); CHECK_EQ(b.nextDelayMs(), 1000u);
    CHECK_EQ(b.nextDelayMs(), 2000u); CHECK_EQ(b.nextDelayMs(), 2000u);
    b.reset(); CHECK_EQ(b.nextDelayMs(), 250u);
}
```

**`BackdropCapture` (Win32/D3D11) :**

```cpp
class BackdropCapture {
public:
    enum class Status { Off, Running, Unavailable };
    // notify reçoit notifyMsg quand une nouvelle image couvre la région (au plus un message en attente).
    bool start(HWND notify, UINT notifyMsg, HMONITOR monitor, IRect regionScreen);
    void setRegion(IRect regionScreen);          // thread UI ; provoque une copie complète
    void stop();
    Status status() const;
    // Thread UI : copie la dernière image dans dst (texture du device UI, même taille que la région).
    // scRgb vaut true si le format est RGBA16F. false si rien de neuf ou indisponible.
    bool takeLatest(ID3D11Device* uiDevice, ID3D11DeviceContext* ctx, ID3D11Texture2D* dst, bool& scRgb, float& sdrWhite);
    static Microsoft::WRL::ComPtr<IDXGIAdapter1> adapterFor(HMONITOR monitor);   // carte qui pilote l'écran
};
```

**Thread de capture**
- **Initialisation** :
  - device D3D11 sur `adapterFor(monitor)` ;
  - `IDXGIOutput5::DuplicateOutput1` avec `{B8G8R8A8_UNORM, R16G16B16A16_FLOAT}`, ou à défaut `IDXGIOutput1::DuplicateOutput` ;
  - rotation non prise en charge → `Unavailable` ;
  - niveau de blanc SDR lu via `DisplayConfigGetDeviceInfo(DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL)` sur le chemin de cet écran.
- **Texture partagée** : de la taille de la région, au format de la duplication, avec `D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX`. Elle est recréée, avec un numéro de génération, si la taille ou le format change. Le handle NT est conservé jusqu'à `stop`.
- **Boucle `AcquireNextFrame(100 ms)`**, avec `CaptureBackoff` en cas d'échec :
  - `WAIT_TIMEOUT` : on recommence ;
  - `DXGI_ERROR_ACCESS_LOST` : on libère la duplication et on la recrée après le délai ;
  - `E_ACCESSDENIED` (bureau sécurisé) ou autre échec : `Unavailable`, puis nouvel essai après le délai ;
  - `LastPresentTime == 0` (souris seule) : on relâche la frame et on recommence ;
  - sinon, si la région a changé, si c'est la première image, ou si `GetFrameMoveRects` / `GetFrameDirtyRects` touchent la région : `AcquireSync(0)`, `CopySubresourceRegion`, `ReleaseSync(0)`, `Flush`, puis `PostMessage(notify)` si aucun message n'est en attente.
  - Dans tous les cas, `ReleaseFrame`.
  - Un succès remet `CaptureBackoff` à zéro.
- **`takeLatest`** :
  - ouvre la texture partagée sur le device UI (`ID3D11Device1::OpenSharedResource1`, rouvert si la génération change) ;
  - `AcquireSync(0, 5 ms)`, `CopyResource(dst)`, `ReleaseSync(0)` ;
  - efface le drapeau « message en attente ».
- **`stop`** : événement d'arrêt, puis `join`.

- [ ] **Step 1 : tests de `capture_policy`.** **Step 2 : constater l'échec.** **Step 3 : implémenter** `capture_policy` puis `BackdropCapture`. **Step 4 : tests verts**, puis vérification réelle avec un petit outil de diagnostic : l'option `MacDock.exe --capture-test out.png` démarre la capture de la région du Dock (bas de l'écran principal, hauteur de la fenêtre du Dock), attend une image et l'écrit. Attendu : un PNG du bas de l'écran, ni noir ni vide. Le Dock en cours d'exécution y apparaît encore ; l'exclusion arrive à la Task 8.
- [ ] **Step 5 : commit** `feat(glass): capture de l'arrière-plan par Desktop Duplication`.

### Task 8 : Intégration du verre dans le Dock réel

**Files :** Modify `src/render/dock_renderer.h|.cpp` (`init` sur la carte de l'écran), `src/app/dock_window.h|.cpp`, `src/app/main.cpp`, `README.md`.

**Comportement**
- **Exclusion** : à la création de la fenêtre, si `settings.glass`, appeler `SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE)`. En cas d'échec, le verre est désactivé (journal), pour éviter une boucle où le Dock se capture lui-même. Si `glass` passe à `false` à chaud, on applique `WDA_NONE`, on arrête la capture et on passe au repli.
- **Device** : `DockRenderer::init` crée le device D3D11 sur `BackdropCapture::adapterFor(moniteur du Dock)`, ce qui rend possible la texture partagée, puis WARP en dernier recours. Avec WARP, la capture n'est pas démarrée.
- **Démarrage et mises à jour de la capture**
  - Démarrée après `reposition()`, avec comme région le rectangle écran de la fenêtre du Dock.
  - `setRegion` est appelé après chaque `reposition()`.
  - `WM_DISPLAYCHANGE` ou `WM_DPICHANGED` : `reposition()` puis redémarrage de la capture.
- **Nouvelle image** : `WM_APP_BACKDROP` (`WM_APP+7`) appelle `capture_.takeLatest(renderer_.device(), …, renderer_.backdropTexture(), …)` puis `requestFrame()`. Le premier `takeLatest` réussi bascule `frame.glass = true`. Si `status()` passe à `Unavailable`, `frame.glass = false` (repli), sans écran noir.
- **Périphérique perdu** : arrêt de la capture, `renderer_.init()`, redémarrage de la capture.
- **Repos** : aucune image n'est rendue tant que ni le modèle, ni la souris, ni l'arrière-plan sous le Dock ne changent. Le thread de capture filtre déjà par région.
- **Performance** : la passe de verre est limitée au rectangle des formes, élargi de la marge d'ombre (scissor), et non à toute la fenêtre.
- **Compteurs `[perf]` en mode trace** : images de capture reçues, images de capture rendues, temps GPU moyen de la passe de verre (requêtes `D3D11_QUERY_TIMESTAMP`).
- **`README.md`** : nouveau réglage `glass`, absence du Dock dans les captures d'écran quand le verre est actif, grille des icônes personnalisées (toile 1024 avec la forme à 824), outils de calibration.

**Vérifications** (pas de test unitaire : code système ; ruling de la Task 8, consigné dans le registre)
- [ ] **Step 1 : implémenter.**
- [ ] **Step 2 : suite de tests verte** (`./build.ps1 -Target tests -Run`) et construction Release (`-Target all -Config Release`).
- [ ] **Step 3 : vérification réelle** avec `MacDock.exe --trace-windows` pendant 60 s, écran immobile puis vidéo ailleurs à l'écran. Attendu dans le journal :
  - `[perf] 0 images` sur 5 s au repos ;
  - aucun rendu pendant la vidéo hors du Dock ;
  - processeur de `MacDock.exe` sous 0,5 % au repos, mesuré par `Get-Process MacDock | % CPU` à 10 s d'intervalle.
- [ ] **Step 4 : `--capture-test`** pour vérifier que le Dock est absent de la capture.
- [ ] **Step 5 : commit** `feat(app): Liquid Glass en direct sur l'arrière-plan réel, repli et reprise`.

### Task 9 : Calibration finale et vérifications

**Files :** Modify `src/config/metrics.h` (valeurs ajustées, si nécessaire), `docs/superpowers/specs/2026-10-06-macos-dock-design.md` (sections 3.3 et 3.4 : valeurs retenues et écarts assumés), `README.md`.

- [ ] **Step 1 : captures comparatives.** Faire `--snapshot` au repos et au survol, en clair et en sombre, sur 3 fonds (photo colorée, blanc, noir). Les comparer à l'œil avec des captures publiques de Tahoe aux points mesurés : rayon concentrique, pas, point, hauteur du fond, intensité du verre. Ajuster dans `metrics.h` les valeurs encore manifestement fausses. Chaque changement de défaut passe par la migration : ajouter l'ancienne valeur v2 à la table si elle a été écrite dans des fichiers. Tant que le plan 2 n'est pas livré, les défauts v2 n'ont été écrits que par le développement ; c'est un ruling.
- [ ] **Step 2 : mettre à jour la spec** (valeurs de départ réelles, écarts assumés repris de l'en-tête de ce plan) et le README.
- [ ] **Step 3 : suite verte et construction Release.**
- [ ] **Step 4 : liste des vérifications manuelles pour l'utilisateur**, dans le message final :
  - verre et réfraction sur son fond d'écran ;
  - fenêtre déplacée sous le Dock : le verre suit ;
  - UAC (bureau sécurisé) : repli puis reprise ;
  - changement de résolution ;
  - HDR activé et désactivé ;
  - mode clair et sombre ;
  - `glass: false` ;
  - superposition de calibration avec une capture Tahoe.
- [ ] **Step 5 : commit** `chore: calibration des mesures et documentation du plan 2`.

---

## Suite

- **Plan 3 – Interactions avancées** (inchangé) : glisser-déposer, « poof », menus contextuels en verre, piles, miniatures, badges et progression, masquage automatique, positions gauche et droite, multi-écran, plein écran, Corbeille vide ou pleine. La capture suivra alors l'écran du Dock.
- **Sous-projets 2 à 6** : barre de menus, animations de fenêtres (génie), feux tricolores, thème global, écran « Apps ».
