// Thème macOS : dessin des curseurs, fichiers .cur/.ani, fond d'écran, application et rétablissement.
#include <string>

#include "minitest.h"
#include "../src/theme/cursor_art.h"
#include "../src/theme/cursor_file.h"
#include "../src/theme/vector_art.h"

namespace {
const std::uint8_t* px(const md::BgraImage& im, int x, int y) { return &im.px[(std::size_t(y) * im.w + x) * 4]; }
}

TEST_CASE(theme_rasterize_fill_outline_clear) {
    md::Layer square{{md::Poly{{{8, 8}, {24, 8}, {24, 24}, {8, 24}}}}, 0xFF000000, 0xFFFFFFFF, 2.0};
    auto im = md::rasterize({square}, 32, 1.0, 0.0);
    REQUIRE(im.w == 32 && im.h == 32);
    CHECK(px(im, 16, 16)[3] == 255 && px(im, 16, 16)[0] < 10);     // cœur noir
    CHECK(px(im, 7, 16)[3] == 255 && px(im, 7, 16)[0] > 245);      // bordure blanche
    CHECK(px(im, 2, 2)[3] == 0);                                    // loin : transparent
}

TEST_CASE(theme_arrow_cursor) {
    auto f = md::cursorFrames(md::CursorKind::Arrow, 64);
    REQUIRE(f.size() == 1);
    CHECK(f[0].image.w == 64);
    CHECK(f[0].hotspot.x == 6 && f[0].hotspot.y == 4);              // pointe (3, 2) × 2
    const std::uint8_t* tip = px(f[0].image, 10, 16);                // dans le corps de la flèche
    CHECK(tip[3] == 255 && tip[0] < 30);
    CHECK(px(f[0].image, 60, 4)[3] == 0);
}

TEST_CASE(theme_cursor_kinds) {
    CHECK(md::cursorFrames(md::CursorKind::Wait, 32).size() == 12);
    CHECK(md::cursorFrames(md::CursorKind::AppStarting, 32).size() == 12);
    auto ns = md::cursorFrames(md::CursorKind::SizeNS, 32);
    REQUIRE(ns.size() == 1);
    CHECK(ns[0].hotspot.x == 16 && ns[0].hotspot.y == 16);
    CHECK(px(ns[0].image, 16, 16)[3] == 255);                        // tige au centre
    CHECK(px(ns[0].image, 4, 16)[3] == 0);                           // rien sur les côtés
    auto we = md::cursorFrames(md::CursorKind::SizeWE, 32);
    CHECK(px(we[0].image, 4, 16)[3] > 0);                            // tournée de 90°
    CHECK(std::wstring(md::cursorRegistryName(md::CursorKind::SizeNWSE)) == L"SizeNWSE");
    CHECK(std::wstring(md::cursorRegistryName(md::CursorKind::No)) == L"No");
}

TEST_CASE(theme_encode_cur) {
    std::vector<md::CursorFrame> sizes;
    for (int s : {32, 64}) sizes.push_back(md::cursorFrames(md::CursorKind::Arrow, s)[0]);
    auto cur = md::encodeCur(sizes);
    REQUIRE(cur.size() > 6 + 2 * 16);
    auto u16 = [&](std::size_t o) { return unsigned(cur[o] | (cur[o + 1] << 8)); };
    auto u32 = [&](std::size_t o) { return std::uint32_t(u16(o) | (u16(o + 2) << 16)); };
    CHECK(u16(0) == 0 && u16(2) == 2 && u16(4) == 2);                 // ICONDIR : curseur, 2 images
    CHECK(cur[6] == 32 && cur[7] == 32);                              // 1re entrée 32 × 32
    CHECK(u16(10) == 3 && u16(12) == 2);                              // point actif (3, 2)
    const std::uint32_t size0 = u32(14), off0 = u32(18);
    CHECK(size0 == 40 + 32 * 32 * 4 + 32 * 4);                        // DIB + masque ET (4 octets par ligne)
    CHECK(u32(off0) == 40 && u32(off0 + 4) == 32 && u32(off0 + 8) == 64);   // hauteur doublée
    CHECK(u16(off0 + 14) == 32);                                       // 32 bits
    CHECK(cur[6 + 16] == 64 && u16(6 + 16 + 4) == 6);                  // 2e entrée 64, point actif x = 6
    CHECK(off0 + size0 == u32(6 + 16 + 12));                           // images jointives
}

TEST_CASE(theme_encode_ani) {
    std::vector<std::vector<std::uint8_t>> frames;
    for (auto& f : md::cursorFrames(md::CursorKind::Wait, 32)) frames.push_back(md::encodeCur({f}));
    auto ani = md::encodeAni(frames, 5);
    REQUIRE(ani.size() > 12);
    CHECK(std::string(ani.begin(), ani.begin() + 4) == "RIFF");
    CHECK(std::string(ani.begin() + 8, ani.begin() + 12) == "ACON");
    CHECK(std::string(ani.begin() + 12, ani.begin() + 16) == "anih");
    auto u32 = [&](std::size_t o) { return std::uint32_t(ani[o] | (ani[o + 1] << 8) | (ani[o + 2] << 16) | (ani[o + 3] << 24)); };
    CHECK(u32(4) == ani.size() - 8);
    CHECK(u32(16) == 36 && u32(20) == 36 && u32(24) == 12 && u32(28) == 12);   // anih : taille, images, pas
    CHECK(u32(48) == 5 && u32(52) == 1);                                       // cadence, AF_ICON
    CHECK(std::string(ani.begin() + 56, ani.begin() + 60) == "LIST");
    CHECK(std::string(ani.begin() + 64, ani.begin() + 68) == "fram");
}

TEST_CASE(theme_windows_loads_our_cursors) {   // fichiers temporaires seulement : aucun curseur du système changé
    wchar_t dir[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, dir);
    const std::wstring cur = std::wstring(dir) + L"macdock-test-arrow.cur", ani = std::wstring(dir) + L"macdock-test-wait.ani";
    std::vector<md::CursorFrame> sizes;
    for (int s : {32, 48, 64}) sizes.push_back(md::cursorFrames(md::CursorKind::Arrow, s)[0]);
    const auto curBytes = md::encodeCur(sizes);
    std::vector<std::vector<std::uint8_t>> frames;
    for (auto& f : md::cursorFrames(md::CursorKind::Wait, 32)) frames.push_back(md::encodeCur({f}));
    const auto aniBytes = md::encodeAni(frames, 5);
    auto write = [](const std::wstring& p, const std::vector<std::uint8_t>& b) {
        HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        DWORD n = 0;
        const bool ok = h != INVALID_HANDLE_VALUE && WriteFile(h, b.data(), DWORD(b.size()), &n, nullptr) && n == b.size();
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        return ok;
    };
    REQUIRE(write(cur, curBytes));
    REQUIRE(write(ani, aniBytes));
    HCURSOR c = HCURSOR(LoadImageW(nullptr, cur.c_str(), IMAGE_CURSOR, 48, 48, LR_LOADFROMFILE));
    REQUIRE(c != nullptr);
    ICONINFO info{};
    REQUIRE(GetIconInfo(c, &info));
    CHECK(info.xHotspot == 5 && info.yHotspot == 3);   // pointe (3, 2) × 1,5
    if (info.hbmColor) DeleteObject(info.hbmColor);
    if (info.hbmMask) DeleteObject(info.hbmMask);
    DestroyCursor(c);
    HCURSOR a = LoadCursorFromFileW(ani.c_str());
    CHECK(a != nullptr);
    if (a) DestroyCursor(a);
    DeleteFileW(cur.c_str());
    DeleteFileW(ani.c_str());
}
