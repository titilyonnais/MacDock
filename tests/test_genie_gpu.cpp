// Génie sur le GPU : maillage texturé (mipmaps, MSAA), rendu hors écran sur WARP.
#include "minitest.h"
#include <windows.h>
#include <objbase.h>

#include <algorithm>
#include <cstdlib>

#include "../src/anim/genie.h"
#include "../src/anim/genie_preview.h"
#include "../src/app/genie_gpu.h"
#include "../src/calib/png_io.h"

#include <string>

namespace {

md::BgraImage solid(int w, int h, std::uint8_t b, std::uint8_t g, std::uint8_t r) {
    md::BgraImage img{w, h, std::vector<std::uint8_t>(std::size_t(w) * h * 4)};
    for (std::size_t i = 0; i < img.px.size(); i += 4) {
        img.px[i] = b;
        img.px[i + 1] = g;
        img.px[i + 2] = r;
        img.px[i + 3] = 255;
    }
    return img;
}

const std::uint8_t* at(const std::vector<std::uint8_t>& px, int w, int x, int y) { return px.data() + (std::size_t(y) * w + x) * 4; }

} // namespace

TEST_CASE(genie_gpu_start_reproduces_window) {
    const md::BgraImage src = md::syntheticWindow(96, 64);
    const RECT win{10, 20, 106, 84};
    const auto mesh = md::genieMesh(md::MinimizeEffect::Genie, SIZE{96, 64}, win, RECT{40, 150, 56, 166},
                                    md::DockPosition::Bottom, 0.0, 32);
    const auto out = md::genieRenderToBgra(src, mesh, 128, 180, POINT{0, 0});
    REQUIRE(out.size() == std::size_t(128) * 180 * 4);
    int worst = 0;
    for (int y = 1; y < 63; ++y)       // intérieur : la fenêtre à l'identique (1:1, sans flou)
        for (int x = 1; x < 95; ++x)
            for (int c = 0; c < 4; ++c)
                worst = std::max(worst, std::abs(int(at(out, 128, win.left + x, win.top + y)[c]) - int(at(src.px, 96, x, y)[c])));
    CHECK(worst <= 3);
    CHECK_EQ(int(at(out, 128, 5, 5)[3]), 0);       // hors du maillage : transparent
    CHECK_EQ(int(at(out, 128, 60, 120)[3]), 0);
}

TEST_CASE(genie_gpu_origin_offsets_screen) {
    // Le maillage est en pixels écran ; la cible commence à origin.
    const md::BgraImage src = solid(40, 30, 0, 0, 255);
    const RECT win{1000, 500, 1040, 530};
    const auto mesh = md::genieMesh(md::MinimizeEffect::Scale, SIZE{40, 30}, win, RECT{1000, 600, 1010, 610},
                                    md::DockPosition::Bottom, 0.0, 1);
    const auto out = md::genieRenderToBgra(src, mesh, 64, 64, POINT{990, 490});
    REQUIRE(!out.empty());
    CHECK(at(out, 64, 30, 25)[2] > 240 && at(out, 64, 30, 25)[3] > 240);   // (1020, 515) : dans la fenêtre
    CHECK_EQ(int(at(out, 64, 5, 5)[3]), 0);
}

TEST_CASE(genie_gpu_shrunk_source_keeps_colour) {
    // Fenêtre rouge réduite à 1/8 : mipmaps prémultipliées, la couleur ne fonce pas et ne scintille pas.
    const md::BgraImage src = solid(256, 192, 0, 0, 255);
    const auto mesh = md::genieMesh(md::MinimizeEffect::Scale, SIZE{256, 192}, RECT{0, 0, 256, 192}, RECT{8, 8, 40, 32},
                                    md::DockPosition::Bottom, 1.0, 1);
    const auto out = md::genieRenderToBgra(src, mesh, 64, 64, POINT{0, 0});
    REQUIRE(!out.empty());
    const auto* p = at(out, 64, 24, 20);
    CHECK(p[2] > 245 && p[1] < 10 && p[0] < 10 && p[3] > 245);
    // Bord du maillage anticrénelé : un pixel partiellement couvert (MSAA) existe sur un bord en biais.
    const auto bent = md::genieMesh(md::MinimizeEffect::Genie, SIZE{256, 192}, RECT{0, 0, 256, 192}, RECT{100, 300, 132, 324},
                                    md::DockPosition::Bottom, 0.4, 64);
    const auto edge = md::genieRenderToBgra(src, bent, 256, 330, POINT{0, 0});
    bool partial = false;
    for (std::size_t i = 3; i < edge.size(); i += 4) partial |= edge[i] > 20 && edge[i] < 235;
    CHECK(partial);
}

TEST_CASE(genie_gpu_sheet) {   // MACDOCK_DUMP=dossier : images du génie GPU pour un contrôle à l'œil
    const md::BgraImage src = md::syntheticWindow(480, 320);
    const RECT win{80, 40, 560, 360}, tile{300, 560, 348, 608};
    wchar_t dump[MAX_PATH] = {};
    const bool write = GetEnvironmentVariableW(L"MACDOCK_DUMP", dump, MAX_PATH) != 0;
    if (write) CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);   // WIC
    for (double t : {0.0, 0.25, 0.5, 0.75}) {
        const auto mesh = md::genieMesh(md::MinimizeEffect::Genie, SIZE{480, 320}, win, tile, md::DockPosition::Bottom, t, 192);
        const auto out = md::genieRenderToBgra(src, mesh, 640, 640, POINT{0, 0});
        CHECK(!out.empty());
        if (write && !out.empty())
            CHECK(md::writePng(std::wstring(dump) + L"\\genie-gpu-" + std::to_wstring(int(t * 100)) + L".png",
                         out.data(), 640, 640));
    }
    if (write) CoUninitialize();
}
