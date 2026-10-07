// Effet génie : géométrie des bandes, aperçu hors écran, coupure de l'animation de Windows.
#include "minitest.h"
#include <windows.h>
#include <objbase.h>

#include <algorithm>
#include <cmath>
#include <string>

#include "../src/anim/genie.h"
#include "../src/anim/genie_preview.h"
#include "../src/app/min_animate.h"
#include "../src/calib/png_io.h"

namespace {
const RECT kWin{100, 100, 900, 700};   // 800 x 600
const RECT kTile{1500, 1040, 1548, 1076};
const SIZE kSrc{800, 600};

bool same(const RECT& a, const RECT& b) { return EqualRect(&a, &b) != FALSE; }

bool contiguous(const std::vector<md::GenieSlice>& s, bool vertical) {
    for (std::size_t i = 1; i < s.size(); ++i) {
        if (vertical ? s[i].dst.top != s[i - 1].dst.bottom : false) return false;
        if (s[i].src.top < s[i - 1].src.top && vertical) return false;
    }
    return true;
}
} // namespace

TEST_CASE(genie_start_is_window) {
    auto s = md::minimizeFrame(md::MinimizeEffect::Genie, kSrc, kWin, kTile, md::DockPosition::Bottom, 0.0);
    REQUIRE(s.size() == 48);
    CHECK_EQ(s.front().dst.top, kWin.top);
    CHECK_EQ(s.back().dst.bottom, kWin.bottom);
    CHECK_EQ(s.front().src.top, 0L);
    CHECK_EQ(s.back().src.bottom, 600L);
    for (auto& x : s) {
        CHECK_EQ(x.dst.left, kWin.left);
        CHECK_EQ(x.dst.right, kWin.right);
        CHECK_EQ(x.src.left, 0L);
        CHECK_EQ(x.src.right, 800L);
    }
    CHECK(contiguous(s, true));
}

TEST_CASE(genie_end_is_tile) {
    auto s = md::minimizeFrame(md::MinimizeEffect::Genie, kSrc, kWin, kTile, md::DockPosition::Bottom, 1.0);
    REQUIRE(!s.empty());
    CHECK_EQ(s.front().dst.top, kTile.top);
    CHECK_EQ(s.back().dst.bottom, kTile.bottom);
    for (auto& x : s) {
        CHECK(x.dst.left >= kTile.left - 1 && x.dst.right <= kTile.right + 1);
        CHECK(x.dst.bottom >= x.dst.top);
    }
}

TEST_CASE(genie_middle_narrows_toward_dock) {
    auto s = md::minimizeFrame(md::MinimizeEffect::Genie, kSrc, kWin, kTile, md::DockPosition::Bottom, 0.5);
    REQUIRE(s.size() > 2);
    CHECK(contiguous(s, true));
    LONG prev = s.front().dst.right - s.front().dst.left;
    for (auto& x : s) {   // plus étroit en allant vers le Dock
        const LONG w = x.dst.right - x.dst.left;
        CHECK(w <= prev + 1);
        prev = w;
    }
    CHECK(s.back().dst.right - s.back().dst.left < 800);
}

TEST_CASE(genie_left_dock_uses_columns) {
    const RECT win{400, 100, 1000, 500};       // 600 x 400
    const RECT tile{8, 300, 56, 336};          // Dock à gauche
    auto s = md::minimizeFrame(md::MinimizeEffect::Genie, SIZE{600, 400}, win, tile, md::DockPosition::Left, 0.0);
    REQUIRE(!s.empty());
    for (auto& x : s) {   // colonnes : hauteur complète
        CHECK_EQ(x.src.top, 0L);
        CHECK_EQ(x.src.bottom, 400L);
        CHECK_EQ(x.dst.top, win.top);
        CHECK_EQ(x.dst.bottom, win.bottom);
    }
    // La première bande est la plus éloignée du Dock (bord droit de la fenêtre).
    CHECK_EQ(s.front().src.right, 600L);
    CHECK_EQ(s.front().dst.right, win.right);
    CHECK_EQ(s.back().dst.left, win.left);
    auto e = md::minimizeFrame(md::MinimizeEffect::Genie, SIZE{600, 400}, win, tile, md::DockPosition::Left, 1.0);
    for (auto& x : e) CHECK(x.dst.top >= tile.top - 1 && x.dst.bottom <= tile.bottom + 1 && x.dst.left >= tile.left - 1);
}

TEST_CASE(genie_right_dock_end_in_tile) {
    const RECT win{100, 100, 700, 500};
    const RECT tile{1860, 300, 1908, 336};
    auto e = md::minimizeFrame(md::MinimizeEffect::Genie, SIZE{600, 400}, win, tile, md::DockPosition::Right, 1.0);
    REQUIRE(!e.empty());
    CHECK_EQ(e.front().src.left, 0L);   // la colonne la plus éloignée du Dock (bord gauche) d'abord
    for (auto& x : e) CHECK(x.dst.left >= tile.left - 1 && x.dst.right <= tile.right + 1);
}

TEST_CASE(genie_scale_and_windows) {
    auto s = md::minimizeFrame(md::MinimizeEffect::Scale, kSrc, kWin, kTile, md::DockPosition::Bottom, 0.5);
    REQUIRE(s.size() == 1);
    CHECK(s[0].dst.left > kWin.left && s[0].dst.left < kTile.left);
    CHECK_EQ(s[0].src.right, 800L);
    auto e = md::minimizeFrame(md::MinimizeEffect::Scale, kSrc, kWin, kTile, md::DockPosition::Bottom, 1.0);
    CHECK(same(e[0].dst, kTile));
    CHECK(md::minimizeFrame(md::MinimizeEffect::Windows, kSrc, kWin, kTile, md::DockPosition::Bottom, 0.5).empty());
}

TEST_CASE(genie_degenerate_inputs) {
    CHECK(md::minimizeFrame(md::MinimizeEffect::Genie, SIZE{0, 0}, kWin, kTile, md::DockPosition::Bottom, 0.5).empty());
    auto tiny = md::minimizeFrame(md::MinimizeEffect::Genie, SIZE{800, 3}, kWin, kTile, md::DockPosition::Bottom, 0.5);
    CHECK(tiny.size() == 3);   // pas plus de bandes que de lignes
    for (auto& x : tiny) CHECK(x.src.bottom > x.src.top);
    // Case au-dessus de la fenêtre (Dock en bas mais fenêtre plus bas que la case) : pas de division par zéro.
    auto odd = md::minimizeFrame(md::MinimizeEffect::Genie, kSrc, RECT{100, 1040, 900, 1060}, kTile, md::DockPosition::Bottom, 0.5);
    for (auto& x : odd) CHECK(x.dst.right >= x.dst.left && x.dst.bottom >= x.dst.top);
}

TEST_CASE(genie_durations) {   // Golden Gate : animations ~12 % plus courtes que Tahoe
    CHECK_NEAR(md::minimizeDuration(md::MinimizeEffect::Genie, false), 0.48, 1e-9);
    CHECK_NEAR(md::minimizeDuration(md::MinimizeEffect::Scale, false), 0.26, 1e-9);
    CHECK_NEAR(md::minimizeDuration(md::MinimizeEffect::Genie, true), 0.48 * 8, 1e-9);
    CHECK_EQ(md::minimizeDuration(md::MinimizeEffect::Windows, false), 0.0);
}

TEST_CASE(genie_restored_rect) {
    WINDOWPLACEMENT wp{sizeof wp};
    wp.rcNormalPosition = RECT{100, 50, 900, 650};
    const RECT monitor{0, 0, 1920, 1080}, work{0, 48, 1920, 1000};   // barre de menus en haut
    RECT r = md::restoredRect(wp, work, monitor, false, SIZE{800, 600});
    CHECK(same(r, RECT{100, 98, 900, 698}));   // décalé de la zone de travail
    r = md::restoredRect(wp, work, monitor, true, SIZE{800, 600});   // fenêtre outil : coordonnées écran
    CHECK(same(r, wp.rcNormalPosition));
    wp.flags = WPF_RESTORETOMAXIMIZED;   // agrandie : la zone de travail et ses bordures invisibles
    r = md::restoredRect(wp, work, monitor, false, SIZE{1936, 968});
    CHECK(same(r, RECT{-8, 40, 1928, 1008}));
}

TEST_CASE(genie_preview_draws_inside_tile_at_end) {
    md::BgraImage src = md::syntheticWindow(200, 150);
    md::BgraImage dst{400, 300, std::vector<std::uint8_t>(400 * 300 * 4, 0)};
    const RECT win{20, 20, 220, 170}, tile{300, 250, 340, 280};
    md::drawSlices(src, md::minimizeFrame(md::MinimizeEffect::Genie, SIZE{200, 150}, win, tile, md::DockPosition::Bottom, 1.0), dst);
    int inside = 0, outside = 0;
    for (int y = 0; y < 300; ++y)
        for (int x = 0; x < 400; ++x)
            if (dst.px[(y * 400 + x) * 4 + 3]) ((x >= 299 && x <= 340 && y >= 249 && y <= 280) ? inside : outside)++;
    CHECK(inside > 0);
    CHECK_EQ(outside, 0);
}

TEST_CASE(genie_preview_start_copies_window) {
    md::BgraImage src = md::syntheticWindow(200, 150);
    md::BgraImage dst{400, 300, std::vector<std::uint8_t>(400 * 300 * 4, 0)};
    md::drawSlices(src, md::minimizeFrame(md::MinimizeEffect::Genie, SIZE{200, 150}, RECT{20, 20, 220, 170}, RECT{300, 250, 340, 280},
                                          md::DockPosition::Bottom, 0.0), dst);
    for (int y : {0, 75, 149})
        for (int x : {0, 100, 199})
            CHECK(std::equal(&src.px[(y * 200 + x) * 4], &src.px[(y * 200 + x) * 4] + 4, &dst.px[((y + 20) * 400 + x + 20) * 4]));
}

TEST_CASE(genie_sheet_size) {   // MACDOCK_DUMP=dossier : planches pour un contrôle à l'œil
    wchar_t dump[MAX_PATH] = {};
    const bool write = GetEnvironmentVariableW(L"MACDOCK_DUMP", dump, MAX_PATH) != 0;
    if (write) CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);   // WIC
    const std::pair<md::DockPosition, const wchar_t*> edges[] = {
        {md::DockPosition::Bottom, L"bottom"}, {md::DockPosition::Left, L"left"}, {md::DockPosition::Right, L"right"}};
    for (auto [edge, name] : edges) {
        auto sheet = md::genieSheet(md::MinimizeEffect::Genie, edge);
        CHECK_EQ(sheet.w, 1920);
        CHECK_EQ(sheet.h, 800);
        CHECK(sheet.px.size() == std::size_t(1920 * 800 * 4));
        if (write) CHECK(md::writePng(std::wstring(dump) + L"\\genie-" + name + L".png", sheet.px.data(), UINT(sheet.w), UINT(sheet.h)));
    }
    if (write) CoUninitialize();
}

TEST_CASE(min_animate_guard_never_takes_own_zero) {
    bool live = false;   // un Dock précédent a déjà coupé l'animation
    int sets = 0;
    md::MinAnimateApi api{[&] { return std::optional<bool>(live); }, [&](bool on) { live = on; ++sets; return true; },
                          [] { return std::optional<bool>(true); }};   // préférence de l'utilisateur : animée
    md::MinAnimateGuard g(api);
    g.apply(md::MinimizeEffect::Genie);
    CHECK(!live);
    CHECK(g.suppressed());
    g.restore();
    CHECK(live);   // revient à la préférence, pas au 0 trouvé
}

TEST_CASE(min_animate_guard_switches) {
    bool live = true;
    int sets = 0;
    md::MinAnimateApi api{[&] { return std::optional<bool>(live); }, [&](bool on) { live = on; ++sets; return true; },
                          [] { return std::optional<bool>(); }};   // registre illisible : défaut 1
    md::MinAnimateGuard g(api);
    g.apply(md::MinimizeEffect::Scale);
    CHECK(!live);
    g.apply(md::MinimizeEffect::Scale);   // déjà coupée : aucun nouvel appel
    CHECK_EQ(sets, 1);
    g.apply(md::MinimizeEffect::Windows);
    CHECK(live);
    CHECK(!g.suppressed());
    g.restore();   // rien à rendre
    CHECK_EQ(sets, 2);
}

TEST_CASE(min_animate_guard_user_without_animation) {
    bool live = false;
    int sets = 0;
    md::MinAnimateApi api{[&] { return std::optional<bool>(live); }, [&](bool on) { live = on; ++sets; return true; },
                          [] { return std::optional<bool>(false); }};   // l'utilisateur a coupé les animations
    md::MinAnimateGuard g(api);
    g.apply(md::MinimizeEffect::Genie);
    g.restore();
    CHECK(!live);
    CHECK_EQ(sets, 0);   // rien n'a été touché
}

TEST_CASE(genie_reacts_only_to_live_minimize) {   // relecture n° 1 : pas d'animation pour une fenêtre déjà réduite
    md::GenieRun idle;
    CHECK(md::genieOnMinimize(idle, 7, true, true) == md::GenieReact::Start);
    CHECK(md::genieOnMinimize(idle, 7, true, false) == md::GenieReact::Nothing);   // découverte au lancement
    CHECK(md::genieOnMinimize(idle, 7, false, true) == md::GenieReact::Nothing);
}

TEST_CASE(genie_cancels_when_restored_elsewhere) {   // relecture n° 5 : aussi pendant une restauration animée
    md::GenieRun minimizing{true, 7, false}, restoring{true, 7, true};
    CHECK(md::genieOnMinimize(minimizing, 7, false, true) == md::GenieReact::Cancel);
    CHECK(md::genieOnMinimize(restoring, 7, false, true) == md::GenieReact::Cancel);
    CHECK(md::genieOnMinimize(restoring, 8, false, true) == md::GenieReact::Nothing);
}

TEST_CASE(genie_interrupted_restore_still_restores) {   // relecture n° 2
    CHECK(md::genieMustRestoreFirst(md::GenieRun{true, 7, true}));
    CHECK(!md::genieMustRestoreFirst(md::GenieRun{true, 7, false}));
    CHECK(!md::genieMustRestoreFirst(md::GenieRun{}));
}

TEST_CASE(genie_start_rect_prefers_last_seen) {   // relecture n° 3 : fenêtre ancrée (Snap)
    WINDOWPLACEMENT wp{sizeof wp};
    wp.rcNormalPosition = RECT{300, 200, 1100, 800};   // place flottante d'avant l'ancrage
    const RECT monitor{0, 0, 1920, 1080}, work{0, 0, 1920, 1032};
    const RECT snapped{-7, 0, 967, 1039};
    RECT r = md::genieStartRect(snapped, wp, work, monitor, false, SIZE{});
    CHECK(same(r, snapped));
    r = md::genieStartRect(std::nullopt, wp, work, monitor, false, SIZE{});
    CHECK(same(r, wp.rcNormalPosition));
    r = md::genieStartRect(RECT{0, 0, 0, 0}, wp, work, monitor, false, SIZE{});   // vide : ignoré
    CHECK(same(r, wp.rcNormalPosition));
}

TEST_CASE(genie_slice_count_fine) {
    // Repli par miniatures DWM : chaque bande coûte un appel à DWM (128 bandes = 4 ms par image), plafond 128.
    CHECK(md::genieSliceCount(400) == 100);
    CHECK(md::genieSliceCount(20) == 16);      // petite fenêtre : un minimum
    CHECK(md::genieSliceCount(4000) == 128);   // très grande : plafonné
}

TEST_CASE(genie_strips_tile_without_gaps) {
    // Bandes jointives à l'écran à tout instant : pas de ligne vide ni de chevauchement entre deux bandes.
    const RECT from{100, 100, 1100, 900}, to{1500, 1400, 1564, 1464};
    for (double t : {0.1, 0.3, 0.5, 0.7, 0.9}) {
        auto s = md::minimizeFrame(md::MinimizeEffect::Genie, SIZE{1000, 800}, from, to, md::DockPosition::Bottom, t, 400);
        for (std::size_t i = 1; i < s.size(); ++i) CHECK(s[i].dst.top == s[i - 1].dst.bottom);
    }
}

namespace {
bool approx(float a, double b) { return std::abs(double(a) - b) < 0.01; }
} // namespace

TEST_CASE(genie_mesh_start_is_window) {
    auto m = md::genieMesh(md::MinimizeEffect::Genie, kSrc, kWin, kTile, md::DockPosition::Bottom, 0.0, 64);
    REQUIRE(m.size() == 2 * 65);
    CHECK(approx(m.front().x, kWin.left) && approx(m.front().y, kWin.top));   // haut gauche
    CHECK(approx(m[1].x, kWin.right) && approx(m[1].y, kWin.top));            // haut droite
    CHECK(approx(m[m.size() - 2].x, kWin.left) && approx(m.back().y, kWin.bottom));
    CHECK(approx(m.front().u, 0) && approx(m.front().v, 0) && approx(m.back().u, 1) && approx(m.back().v, 1));
    for (std::size_t i = 0; i < m.size(); i += 2) {   // à t = 0 : rectangle exact, lignes de la source dans l'ordre
        CHECK(approx(m[i].x, kWin.left) && approx(m[i + 1].x, kWin.right));
        CHECK(approx(m[i].v, double(i / 2) / 64));
    }
}

TEST_CASE(genie_mesh_end_in_tile_and_subpixel) {
    auto m = md::genieMesh(md::MinimizeEffect::Genie, kSrc, kWin, kTile, md::DockPosition::Bottom, 1.0, 64);
    REQUIRE(!m.empty());
    for (auto& p : m) CHECK(p.x >= kTile.left - 0.5f && p.x <= kTile.right + 0.5f && p.y >= kTile.top - 0.5f && p.y <= kTile.bottom + 0.5f);
    // Milieu de l'animation : des positions sous le pixel (pas d'arrondi, bords sans marches).
    auto mid = md::genieMesh(md::MinimizeEffect::Genie, kSrc, kWin, kTile, md::DockPosition::Bottom, 0.5, 64);
    bool fractional = false;
    for (auto& p : mid) fractional |= std::abs(p.x - std::round(p.x)) > 0.01f;
    CHECK(fractional);
    for (std::size_t i = 2; i < mid.size(); i += 2) {   // lignes de haut en bas, plus étroites vers le Dock
        CHECK(mid[i].y >= mid[i - 2].y);
        CHECK(mid[i + 1].x - mid[i].x <= mid[i - 1].x - mid[i - 2].x + 0.01f);
    }
}

TEST_CASE(genie_mesh_side_docks) {
    const RECT win{400, 100, 1000, 500};
    const RECT tile{8, 300, 56, 336};
    auto m = md::genieMesh(md::MinimizeEffect::Genie, SIZE{600, 400}, win, tile, md::DockPosition::Left, 0.0, 10);
    REQUIRE(m.size() == 22);
    // Première ligne : la plus éloignée du Dock, le bord droit (colonne u = 1), de haut (v = 0) en bas (v = 1).
    CHECK(approx(m[0].x, win.right) && approx(m[0].y, win.top) && approx(m[0].u, 1) && approx(m[0].v, 0));
    CHECK(approx(m[1].x, win.right) && approx(m[1].y, win.bottom) && approx(m[1].u, 1) && approx(m[1].v, 1));
    CHECK(approx(m[20].x, win.left) && approx(m[20].u, 0));
    auto r = md::genieMesh(md::MinimizeEffect::Genie, SIZE{600, 400}, RECT{100, 100, 700, 500}, RECT{1860, 300, 1908, 336},
                           md::DockPosition::Right, 0.0, 10);
    CHECK(approx(r[0].x, 100) && approx(r[0].u, 0) && approx(r[0].y, 100) && approx(r[1].y, 500) && approx(r[1].v, 1));
}

TEST_CASE(genie_mesh_scale_and_windows) {
    auto s = md::genieMesh(md::MinimizeEffect::Scale, kSrc, kWin, kTile, md::DockPosition::Bottom, 1.0, 64);
    REQUIRE(s.size() == 4);   // une seule rangée
    CHECK(approx(s[0].x, kTile.left) && approx(s[0].y, kTile.top) && approx(s[3].x, kTile.right) && approx(s[3].y, kTile.bottom));
    CHECK(md::genieMesh(md::MinimizeEffect::Windows, kSrc, kWin, kTile, md::DockPosition::Bottom, 0.5, 64).empty());
    CHECK(md::genieMesh(md::MinimizeEffect::Genie, SIZE{0, 0}, kWin, kTile, md::DockPosition::Bottom, 0.5, 64).empty());
}

TEST_CASE(genie_strip_target_covers_first_then_grows) {
    // Mesuré à 165 Hz : la fenêtre réduite disparaît aussitôt ; enregistrer 48 miniatures (20 ms) et lancer la capture
    // (12 ms) avant d'afficher quoi que ce soit laissait un trou de 60 ms, puis la fenêtre réapparaissait (éclair).
    // Au départ elle n'est pas déformée : une seule miniature la reproduit exactement, tout de suite.
    CHECK_EQ(md::genieStripTarget(128, true, 0), 1);
    CHECK_EQ(md::genieStripTarget(128, false, 0), 1);
    // Attente du GPU, fenêtre immobile : rien de plus. Mesuré : ajouter des miniatures pendant l'attente (appels
    // synchrones à DWM) retardait la première image GPU de 100 à 140 ms.
    CHECK_EQ(md::genieStripTarget(128, true, 1), 1);
    CHECK_EQ(md::genieStripTarget(128, false, 9), 128);    // en mouvement sans GPU : toutes les bandes
    CHECK_EQ(md::genieStripTarget(1, true, 1), 1);         // effet « échelle » : une seule
}

TEST_CASE(genie_settling_restore_is_not_cancelled) {   // fin d'ouverture : la fenêtre est restaurée sous l'image
    md::GenieRun settling{true, 7, true};
    settling.settling = true;
    CHECK(md::genieOnMinimize(settling, 7, false, true) == md::GenieReact::Nothing);   // c'est notre restauration
    CHECK(md::genieOnMinimize(settling, 8, false, true) == md::GenieReact::Nothing);
    md::GenieRun running{true, 7, true};
    CHECK(md::genieOnMinimize(running, 7, false, true) == md::GenieReact::Cancel);     // restaurée ailleurs en route
    CHECK(!md::genieMustRestoreFirst(settling));   // déjà restaurée : rien à refaire avant une nouvelle animation
}

TEST_CASE(genie_waits_for_gpu_before_moving) {   // pas de raccord bandes → GPU en plein mouvement
    CHECK(md::genieWaitStep(false, 0.0) == md::GenieWait::Hold);        // capture pas encore là : la fenêtre reste en place
    CHECK(md::genieWaitStep(false, 0.149) == md::GenieWait::Hold);
    CHECK(md::genieWaitStep(true, 0.05) == md::GenieWait::Go);          // image GPU prête : l'animation démarre
    CHECK(md::genieWaitStep(false, 0.15) == md::GenieWait::GoStrips);   // trop long : les bandes, en pleine qualité
    CHECK(md::genieWaitStep(true, 0.3) == md::GenieWait::Go);
}

TEST_CASE(genie_visible_rect_drops_invisible_borders) {
    // Mesuré (Windows 11) : fenêtre 800 x 600, partie visible et miniature DWM 778 x 589 (11 px invisibles à gauche,
    // à droite et en bas, 0 en haut). Le génie doit partir de la partie visible, sinon l'image est étirée et décalée.
    const RECT window{-3000, 100, -2200, 700};
    const RECT v = md::genieVisibleRect(window, SIZE{778, 589});
    CHECK_EQ(v.left, -2989L);
    CHECK_EQ(v.top, 100L);
    CHECK_EQ(v.right, -2989L + 778);
    CHECK_EQ(v.bottom, 689L);
    const RECT exact{10, 20, 788, 609};   // déjà la partie visible : inchangé
    CHECK(EqualRect(&exact, &static_cast<const RECT&>(md::genieVisibleRect(exact, SIZE{778, 589}))));
    // Écart trop grand (autre taille qu'avant la réduction) : rien à corriger, on garde le rectangle.
    const RECT other{0, 0, 1600, 1200};
    const RECT o = md::genieVisibleRect(other, SIZE{778, 589});
    CHECK(EqualRect(&other, &o));
}

TEST_CASE(genie_minimize_click_is_confirmed_on_release) {
    // Appui sur « réduire » : capture et couverture préparées ; relâché sur place, la réduction va suivre (la
    // couverture s'affiche avant que Windows retire la fenêtre). Sinon, l'annonce tombe. La fenêtre sous le pointeur au
    // relâchement ne compte pas : mesuré, l'app a parfois déjà réduit quand le Dock le traite (Brave dessous).
    CHECK(md::genieMinimizeConfirmed(POINT{100, 20}, POINT{100, 20}));
    CHECK(md::genieMinimizeConfirmed(POINT{100, 20}, POINT{103, 23}));    // tremblement de la main
    CHECK(!md::genieMinimizeConfirmed(POINT{100, 20}, POINT{140, 20}));   // glissé hors du bouton
    CHECK(!md::genieMinimizeConfirmed(POINT{100, 20}, POINT{100, 26}));
}

TEST_CASE(genie_host_covers_both_screens) {
    // La fenêtre des bandes couvre l'écran de la fenêtre et celui du Dock : posée d'avance à sa place définitive,
    // elle n'a plus à bouger au départ (un déplacement et une miniature changent rarement dans la même image).
    const RECT left{0, 0, 3840, 2160}, right{3840, 0, 7680, 2160};
    const RECT same = md::genieHostBox(left, left);
    CHECK(EqualRect(&same, &left));
    const RECT both = md::genieHostBox(right, left);
    CHECK_EQ(both.left, 0L);
    CHECK_EQ(both.right, 7680L);
    CHECK_EQ(both.bottom, 2160L);
}

TEST_CASE(genie_gpu_box_reuses_armed_box_when_it_fits) {
    // La surface GPU posée à l'appui (fenêtre + Dock) sert telle quelle si elle contient l'animation : ni
    // redimensionnement ni déplacement au départ. Sinon, le cadre serré de l'animation.
    const RECT from{900, 300, 2500, 1300}, to{1800, 2000, 1900, 2100};
    const RECT tight = md::genieGpuBox(from, to, nullptr);
    CHECK_EQ(tight.left, 898L);
    CHECK_EQ(tight.top, 298L);
    CHECK_EQ(tight.right, 2502L);
    CHECK_EQ(tight.bottom, 2102L);
    const RECT armed{0, 0, 3840, 2160};
    const RECT reused = md::genieGpuBox(from, to, &armed);
    CHECK(EqualRect(&reused, &armed));
    const RECT narrow{800, 200, 2600, 1400};   // ne contient pas la case : le cadre serré
    const RECT fallback = md::genieGpuBox(from, to, &narrow);
    CHECK(EqualRect(&fallback, &tight));
    const RECT empty{};
    const RECT none = md::genieGpuBox(from, to, &empty);
    CHECK(EqualRect(&none, &tight));
}

TEST_CASE(genie_minimize_button_from_dwm_caption_bounds) {
    // Mesuré (Windows 11, 200 %) : fenêtre en 900,300 ; DWMWA_CAPTION_BUTTON_BOUNDS = 1296,0,1588,57 (repère de la
    // fenêtre) : réduire, agrandir, fermer, un tiers chacun. WM_NCHITTEST, lui, répond « barre de titre » au milieu
    // du vrai bouton « réduire » (ancienne géométrie) : il ne sert qu'aux apps qui dessinent leurs propres boutons.
    const RECT window{900, 300, 2500, 1300}, bounds{1296, 0, 1588, 57};
    CHECK(md::genieOnMinimizeButton(POINT{2259, 328}, window, bounds, true));
    CHECK(md::genieOnMinimizeButton(POINT{2197, 301}, window, bounds, true));
    CHECK(!md::genieOnMinimizeButton(POINT{2300, 328}, window, bounds, true));    // agrandir
    CHECK(!md::genieOnMinimizeButton(POINT{2259, 360}, window, bounds, true));    // sous la barre
    CHECK(!md::genieOnMinimizeButton(POINT{2150, 328}, window, bounds, true));    // barre de titre
    CHECK(!md::genieOnMinimizeButton(POINT{2259, 328}, window, bounds, false));   // pas de bouton réduire
    CHECK(!md::genieOnMinimizeButton(POINT{2259, 328}, window, RECT{1932, 0, 1932, 57}, true));   // boutons de l'app
}
