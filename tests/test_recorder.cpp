// Enregistreur de ⊞⇧5 : encodage H.264 dans un MP4 par Media Foundation, avec une source d'images synthétique (le test
// ne capture jamais l'écran) ; fichier écrit dans le dossier temporaire puis effacé.
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "minitest.h"
#include "../src/screenshot/screen_recorder.h"

TEST_CASE(recorder_writes_a_playable_mp4) {
    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    const std::wstring path = std::wstring(tmp) + L"macdock-recorder-test.mp4";
    DeleteFileW(path.c_str());
    int frames = 0;
    md::ScreenRecorder rec;
    // Image n : dégradé qui glisse (l'encodeur reçoit des images différentes).
    const bool started = rec.start(RECT{0, 0, 160, 90}, path, 30, [&](std::uint8_t* bgra, int w, int h) {
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                std::uint8_t* p = bgra + (std::size_t(y) * w + x) * 4;
                p[0] = std::uint8_t((x + frames * 4) & 0xFF);
                p[1] = std::uint8_t(y * 2);
                p[2] = 128;
                p[3] = 255;
            }
        ++frames;
    });
    CHECK(started);
    if (!started) return;
    Sleep(500);   // une quinzaine d'images
    CHECK(rec.recording());
    md::BgraImage last;
    CHECK(rec.stop(&last));
    CHECK(!rec.recording());
    CHECK(frames >= 5);
    CHECK(last.w == 160 && last.h == 90 && last.px.size() == 160 * 90 * 4);   // dernière image, pour la vignette
    std::ifstream f(path, std::ios::binary);
    std::vector<char> head(12);
    f.read(head.data(), 12);
    CHECK(f.gcount() == 12 && std::string(head.data() + 4, 4) == "ftyp");   // conteneur MP4
    f.seekg(0, std::ios::end);
    CHECK(f.tellg() > 1000);
    f.close();
    DeleteFileW(path.c_str());
}

TEST_CASE(recorder_refuses_an_unwritable_path) {
    md::ScreenRecorder rec;
    const bool started = rec.start(RECT{0, 0, 64, 64}, L"Z:\\dossier\\inexistant\\x.mp4", 30, [](std::uint8_t*, int, int) {});
    CHECK(!started && !rec.recording());
}
