// HUD du volume et de la luminosité : pas, fondu, place, garde, rendu hors écran.
#include <windows.h>
#include <objbase.h>

#include <cstdlib>
#include <d3d11.h>
#include <wrl/client.h>

namespace md {
using Com_ID3D11Device = Microsoft::WRL::ComPtr<ID3D11Device>;
}

#include "minitest.h"
#include "../src/hud/hud_logic.h"
#include "../src/popup/popup_glass.h"
#include "../src/hud/hud_window.h"
#include "../src/theme/wallpaper_art.h"

TEST_CASE(hud_volume_step_grid) {
    CHECK_NEAR(md::volumeStep(0.5f, 1, false), 0.5625, 1e-6);
    CHECK_NEAR(md::volumeStep(0.3f, 1, false), 0.3125, 1e-6);    // aligné sur la grille
    CHECK_NEAR(md::volumeStep(0.3f, -1, false), 0.25, 1e-6);
    CHECK_NEAR(md::volumeStep(0.25f, -1, false), 0.1875, 1e-6);
    CHECK_NEAR(md::volumeStep(0.49999f, 1, false), 0.5625, 1e-6);   // presque sur la grille : compté dessus
    CHECK_NEAR(md::volumeStep(0.5f, 1, true), 0.515625, 1e-6);
    CHECK_NEAR(md::volumeStep(1.0f, 1, false), 1.0, 1e-6);
    CHECK_NEAR(md::volumeStep(0.0f, -1, false), 0.0, 1e-6);
    CHECK_NEAR(md::volumeStep(-1.0f, 1, false), 0.0625, 1e-6);   // valeur inconnue : bornée d'abord
}

TEST_CASE(hud_fade_hold_and_revive) {
    md::HudFade f;
    CHECK(!f.visible(0));
    f.show(10);
    CHECK_NEAR(f.opacity(11.4), 1.0, 1e-6);
    CHECK_NEAR(f.opacity(11.625), 0.5, 1e-3);
    CHECK(!f.visible(11.76));
    f.show(11.6);   // ravivée pendant le fondu
    CHECK_NEAR(f.opacity(12.0), 1.0, 1e-6);
    f.reset();
    CHECK(!f.visible(12.0));
}

TEST_CASE(hud_place_top_right_under_bar) {
    const RECT mon{0, 0, 1920, 1080};
    auto p = md::hudPlace(mon, 24, 1.0f);
    CHECK(p.w == 280);
    CHECK(p.h == 64);
    CHECK(p.x == 1920 - 12 - 280);
    CHECK(p.y == 24 + 8);
    const RECT second{1920, 0, 1920 + 2560, 1440};
    p = md::hudPlace(second, 36, 1.5f);
    CHECK(p.w == 420);
    CHECK(p.x == 1920 + 2560 - 18 - 420);
    CHECK(p.y == 36 + 12);
}

TEST_CASE(hud_brightness_gate) {
    md::BrightnessGate g;
    CHECK(!g.accept(0, false, 50));   // premier avis : la valeur courante, envoyée à l'enregistrement
    CHECK(g.accept(5, false, 60));
    CHECK(!g.accept(5.5, false, 60));   // même niveau (écran rallumé, rappel de Windows) : rien
    CHECK(!g.accept(6, true, 70));      // menu ouvert : le curseur est sous les yeux
    CHECK(!g.accept(6.5, false, 70));   // le niveau vu sous le menu est retenu
    g.noteOwnChange(10);
    CHECK(!g.accept(10.5, false, 80));
    CHECK(g.accept(11.1, false, 90));
    g.noteSystemChange(20);   // sortie de veille, secteur ou batterie, écran rallumé
    CHECK(!g.accept(21.5, false, 40));
    CHECK(g.accept(22.5, false, 45));
}

TEST_CASE(hud_bar_bottom) {
    CHECK(md::hudBarBottom(0, 24, 0) == 24);     // barre visible
    CHECK(md::hudBarBottom(0, 24, 24) == 0);     // barre masquée : sous le haut de l'écran
    CHECK(md::hudBarBottom(100, 36, 12) == 124); // à moitié sortie
    CHECK(md::hudBarBottom(0, 24, 40) == 0);
}

TEST_CASE(hud_texture_device_check) {
    md::Com_ID3D11Device a, b;
    const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    REQUIRE(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, &a, nullptr, nullptr)));
    REQUIRE(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, &b, nullptr, nullptr)));
    D3D11_TEXTURE2D_DESC d{};
    d.Width = d.Height = 4;
    d.MipLevels = d.ArraySize = 1;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> t;
    REQUIRE(SUCCEEDED(a->CreateTexture2D(&d, nullptr, &t)));
    CHECK(md::onDevice(t.Get(), a.Get()));
    CHECK(!md::onDevice(t.Get(), b.Get()));   // écran voisin : autre device, texture à recréer
    CHECK(!md::onDevice(nullptr, a.Get()));
}

TEST_CASE(hud_snapshot_draws_panel) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    md::HudContent c;
    c.level = 0.5f;
    c.detail = L"Haut-parleurs";
    auto img = md::hudSnapshot(c, true, 800, 300);
    REQUIRE(img.w == 800);
    REQUIRE(img.px.size() == std::size_t(800) * 300 * 4);
    // Le panneau (en haut à droite, sous une barre de 24 pt) change le fond Tahoe ; ailleurs, le fond est intact.
    const md::BgraImage wall = md::macWallpaper(800, 300, true);
    auto diff = [&](int x, int y) {
        const std::size_t i = (std::size_t(y) * 800 + x) * 4;
        return std::abs(int(img.px[i]) - int(wall.px[i])) + std::abs(int(img.px[i + 1]) - int(wall.px[i + 1])) +
               std::abs(int(img.px[i + 2]) - int(wall.px[i + 2]));
    };
    CHECK(diff(800 - 12 - 270, 24 + 8 + 4) > 20);   // coin du panneau, hors texte et jauge
    CHECK(diff(100, 250) < 4);
    auto empty = md::hudSnapshot(c, false, 0, 0);
    CHECK(empty.px.empty());
}
