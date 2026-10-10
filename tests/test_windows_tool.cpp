// Captures de l'Outil Capture de Windows (⊞⇧S, Impr. écran) : MacDock se montre le temps de la capture (plan 63).
#include <windows.h>

#include "minitest.h"
#include "../src/interact/hot_corners.h"
#include "../src/screenshot/windows_tool.h"

TEST_CASE(windows_tool_keys) {
    // Impr. écran, avec ou sans modificateurs, et ⊞⇧S lancent l'Outil Capture ; ni la répétition ni le relâchement.
    CHECK(md::windowsShotKey(VK_SNAPSHOT, true, false, false, false, false));
    CHECK(md::windowsShotKey(VK_SNAPSHOT, true, false, false, true, false));    // ⊞Impr. écran
    CHECK(md::windowsShotKey(VK_SNAPSHOT, true, false, false, false, true));
    CHECK(!md::windowsShotKey(VK_SNAPSHOT, false, false, true, false, false));  // relâchement
    CHECK(!md::windowsShotKey(VK_SNAPSHOT, true, true, true, false, false));    // répétition
    CHECK(md::windowsShotKey('S', true, false, false, true, true));              // ⊞⇧S
    CHECK(!md::windowsShotKey('S', true, false, false, true, false));            // ⊞S : recherche de Windows
    CHECK(!md::windowsShotKey('S', true, false, false, false, true));            // ⇧S : une lettre
    CHECK(!md::windowsShotKey('S', false, false, true, true, true));
    CHECK(!md::windowsShotKey('S', true, true, true, true, true));
    CHECK(!md::windowsShotKey('A', true, false, false, true, true));
    // Certains claviers n'envoient d'Impr. écran que le relâchement : il compte alors comme l'appui.
    CHECK(md::windowsShotKey(VK_SNAPSHOT, false, false, false, false, false));
    CHECK(!md::windowsShotKey('S', false, false, false, true, true));
}

TEST_CASE(windows_tool_process_names) {
    CHECK(md::isCaptureToolProcess(L"SnippingTool.exe"));
    CHECK(md::isCaptureToolProcess(L"snippingtool.EXE"));
    CHECK(md::isCaptureToolProcess(L"ScreenClippingHost.exe"));   // Windows 10 et premiers Windows 11
    CHECK(md::isCaptureToolProcess(L"ScreenSketch.exe"));         // Capture d'écran et croquis
    CHECK(!md::isCaptureToolProcess(L"explorer.exe"));
    CHECK(!md::isCaptureToolProcess(L""));
}

TEST_CASE(windows_tool_reveal_without_the_tool) {
    // Outil jamais vu devant (capture immédiate, ou outil inconnu) : MacDock reste visible 3 s après la touche.
    md::ToolReveal r;
    CHECK(!r.keep(0, false));
    r.start(1000);
    CHECK(r.active());
    CHECK(r.keep(1250, false));
    CHECK(r.keep(3999, false));
    CHECK(!r.keep(4000, false));
    CHECK(!r.active());
}

TEST_CASE(windows_tool_reveal_follows_the_tool) {
    // Outil devant : visible tant qu'il l'est (l'utilisateur prend son temps), puis 500 ms après son départ.
    md::ToolReveal r;
    r.start(0);
    CHECK(r.keep(250, true));
    CHECK(r.keep(10000, true));
    CHECK(r.keep(10250, false));
    CHECK(r.keep(10499, false));
    CHECK(!r.keep(10500, false));
    // Jamais plus de 2 min.
    r.start(0);
    CHECK(r.keep(119999, true));
    CHECK(!r.keep(120000, true));
}

TEST_CASE(windows_tool_reveal_tool_gone_early) {
    // Outil vu puis parti tôt (Échap tout de suite) : fin 500 ms après son départ, sans attendre 3 s.
    md::ToolReveal r;
    r.start(0);
    CHECK(r.keep(200, true));
    CHECK(r.keep(400, false));
    CHECK(!r.keep(700, false));
}

TEST_CASE(windows_tool_reveal_new_key_extends) {
    md::ToolReveal r;
    r.start(0);
    CHECK(r.keep(2000, false));
    r.start(2500);   // nouvel appui pendant la révélation
    CHECK(r.keep(5000, false));
    CHECK(!r.keep(5500, false));
}

TEST_CASE(windows_tool_session_lowers_and_freezes) {
    // Retour du 10 octobre : avec ⊞⇧S, le Dock et la barre restaient devant le voile de l'outil, et le Dock suivait
    // encore la souris. Pendant la capture : sous le voile, et pointeur ignoré ; ensuite, comme avant.
    const md::ToolSessionPlacement during = md::placementDuringToolSession(true);
    CHECK(!during.topmost);
    CHECK(during.pointerIgnored);
    const md::ToolSessionPlacement normal = md::placementDuringToolSession(false);
    CHECK(normal.topmost);
    CHECK(!normal.pointerIgnored);
}

TEST_CASE(windows_tool_session_disarms_hot_corners) {
    // Relecture du plan 69 : sous le voile, le Dock ignore le pointeur ; un coin atteint pendant la sélection ne doit pas
    // partir à la fin de la session, quand le Dock reprend le pointeur resté au coin.
    md::HotCornerTracker corners;
    const POINT at{0, 0};
    CHECK(!corners.update(md::Corner::TopLeft, at, md::placementDuringToolSession(true).hotCornersBlocked).has_value());
    CHECK(!corners.update(md::Corner::TopLeft, at, md::placementDuringToolSession(false).hotCornersBlocked).has_value());
    CHECK(!md::placementDuringToolSession(false).hotCornersBlocked);
}

TEST_CASE(windows_tool_lowered_only_under_the_veil) {
    // Relecture du plan 69 : abaissés et sans pointeur pendant tout la révélation, le Dock et la barre restaient
    // insensibles tant que l'éditeur de l'outil était devant (jusqu'à 2 min). Seulement sous le voile, désormais.
    md::ToolReveal r;
    r.start(0, true);                      // ⊞⇧S : voile attendu
    CHECK(r.lowered());                    // tout de suite, avant qu'il apparaisse
    CHECK(r.keep(250, false, false));
    CHECK(r.lowered());                    // en l'attendant
    CHECK(r.keep(500, true, true));
    CHECK(r.lowered());                    // voile devant
    CHECK(r.keep(5000, true, false));      // sélection faite : l'éditeur de l'outil est devant
    CHECK(!r.lowered());                   // Dock et barre de nouveau devant tout, pointeur repris
    CHECK(r.active());                     // toujours visibles aux captures
    r.start(6000, true);                   // nouvelle ⊞⇧S depuis l'éditeur
    CHECK(r.lowered());
}

TEST_CASE(windows_tool_no_veil_no_lowering) {
    md::ToolReveal r;
    r.start(0, false);                     // ⊞Impr. écran : capture directe, sans voile
    CHECK(!r.lowered());
    CHECK(r.keep(1000, false, false));
    CHECK(!r.lowered());
}

TEST_CASE(windows_tool_veil_never_seen) {
    md::ToolReveal r;
    r.start(0, true);
    CHECK(r.keep(1000, false, false));
    CHECK(r.lowered());
    CHECK(r.keep(md::ToolReveal::kVeilWaitMs, false, false));   // voile jamais venu : on ne l'attend plus
    CHECK(!r.lowered());
}

TEST_CASE(windows_tool_veil_keys) {
    CHECK(md::toolShowsVeil('S', true, false));            // ⊞⇧S
    CHECK(md::toolShowsVeil(VK_SNAPSHOT, false, false));   // Impr. écran seul : Windows 11 ouvre l'outil
    CHECK(!md::toolShowsVeil(VK_SNAPSHOT, true, false));   // ⊞Impr. écran : enregistrée directement
    CHECK(!md::toolShowsVeil(VK_SNAPSHOT, false, true));   // Alt+Impr. écran : fenêtre copiée
}

TEST_CASE(windows_tool_bar_layer_follows_the_veil_only) {
    // Relecture du plan 69 : ⊞⇧3 faisait passer la barre sous les fenêtres « toujours au premier plan », puis devant
    // Apps et Mission Control ouverts. Elle ne change de couche que pour le voile de l'outil.
    using md::ShotReveal;
    CHECK(!md::barLayerChange(false, ShotReveal::Visible).change);     // ⊞⇧3 : la barre reste à sa place
    CHECK(!md::barLayerChange(false, ShotReveal::Off).change);         // fin d'une ⊞⇧3 : rien à remonter
    const md::BarLayerChange down = md::barLayerChange(false, ShotReveal::UnderVeil);
    CHECK(down.change);
    CHECK(!down.topmost);
    CHECK(!md::barLayerChange(true, ShotReveal::UnderVeil).change);    // relance : déjà dessous
    const md::BarLayerChange up = md::barLayerChange(true, ShotReveal::Visible);   // voile parti (éditeur devant)
    CHECK(up.change);
    CHECK(up.topmost);
    const md::BarLayerChange end = md::barLayerChange(true, ShotReveal::Off);
    CHECK(end.change);
    CHECK(end.topmost);
}
