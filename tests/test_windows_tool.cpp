// Captures de l'Outil Capture de Windows (⊞⇧S, Impr. écran) : MacDock se montre le temps de la capture (plan 63).
#include <windows.h>

#include "minitest.h"
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
