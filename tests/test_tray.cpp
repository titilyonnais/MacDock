// Barre de menus : icônes des autres apps (zone de notification relayée par le mod Windhawk).
#include <windows.h>
#include <shellapi.h>
#include <objbase.h>

#include <set>

#include "minitest.h"
#include "../src/core/json.h"
#include "../src/ipc/protocol.h"
#include "../src/menubar/bar_renderer.h"
#include "../src/menubar/menubar_settings.h"
#include "../src/menubar/tray_model.h"

namespace {

md::ipc::TrayIconEvent icon(std::uint64_t hwnd, std::uint32_t uid, const wchar_t* tip = L"App") {
    md::ipc::TrayIconEvent e;
    e.hwnd = hwnd;
    e.uid = uid;
    e.callback = WM_APP + 9;
    e.flags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    e.tip = tip;
    e.w = e.h = 2;
    e.bgra.assign(16, 0xFF);
    return e;
}

std::vector<std::wstring> tips(const md::TrayModel& m) {
    std::vector<std::wstring> out;
    for (const auto* i : m.visible()) out.push_back(i->e.tip);
    return out;
}

} // namespace

TEST_CASE(tray_protocol_roundtrip) {
    auto e = icon(0x1234567890ull, 7, L"Discord — en ligne");
    e.version = 4;
    e.hidden = true;
    e.guid[0] = 0xAB;
    e.guid[15] = 0xCD;
    auto m = md::ipc::makeTrayUpdate(e);
    CHECK(m.type == md::ipc::MsgType::TrayUpdate);
    // En passant par le flux : en-tête et longueur comprises.
    md::ipc::Decoder d;
    auto bytes = md::ipc::encode(m);
    d.feed(bytes.data(), bytes.size());
    auto got = d.next();
    REQUIRE(got.has_value());
    auto back = md::ipc::parseTrayUpdate(*got);
    REQUIRE(back.has_value());
    CHECK_EQ(back->hwnd, e.hwnd);
    CHECK_EQ(back->uid, 7u);
    CHECK_EQ(back->callback, e.callback);
    CHECK_EQ(back->version, 4u);
    CHECK_EQ(back->flags, e.flags);
    CHECK(back->hidden);
    CHECK(back->tip == e.tip);
    CHECK(back->guid[0] == 0xAB && back->guid[15] == 0xCD);
    CHECK(back->w == 2 && back->h == 2);
    CHECK(back->bgra == e.bgra);

    auto r = md::ipc::makeTrayRemove(e.hwnd, 7, e.guid);
    auto rb = md::ipc::parseTrayRemove(r);
    REQUIRE(rb.has_value());
    CHECK_EQ(rb->hwnd, e.hwnd);
    CHECK_EQ(rb->uid, 7u);
    CHECK(rb->guid[0] == 0xAB);
    CHECK(!md::ipc::parseTrayUpdate(r).has_value());   // mauvais type
}

TEST_CASE(tray_protocol_rejects_bad_sizes) {
    auto m = md::ipc::makeTrayUpdate(icon(1, 1));
    auto cut = m;
    cut.payload.resize(cut.payload.size() - 1);   // image tronquée
    CHECK(!md::ipc::parseTrayUpdate(cut).has_value());
    auto big = icon(1, 1);
    big.w = big.h = 200;   // plus que 64 × 64 : refusé à l'envoi comme à la lecture
    big.bgra.assign(size_t(200) * 200 * 4, 0);
    auto mb = md::ipc::makeTrayUpdate(big);
    CHECK(!md::ipc::parseTrayUpdate(mb).has_value());
    md::ipc::Message tiny{md::ipc::MsgType::TrayUpdate, {1, 2, 3}};
    CHECK(!md::ipc::parseTrayUpdate(tiny).has_value());
    auto longTip = icon(1, 1);
    longTip.tip.assign(300, L'x');   // texte borné à 128 caractères, comme NOTIFYICONDATA
    auto lt = md::ipc::parseTrayUpdate(md::ipc::makeTrayUpdate(longTip));
    REQUIRE(lt.has_value());
    CHECK(lt->tip.size() == 127);
}

TEST_CASE(tray_model_update_remove_order) {
    md::TrayModel m;
    m.update(icon(10, 1, L"A"));
    m.update(icon(20, 1, L"B"));
    m.update(icon(10, 2, L"C"));
    CHECK(tips(m) == (std::vector<std::wstring>{L"C", L"B", L"A"}));   // la plus récente à gauche
    m.update(icon(20, 1, L"B2"));                                          // modification : même place
    CHECK(tips(m) == (std::vector<std::wstring>{L"C", L"B2", L"A"}));
    auto hidden = icon(10, 1, L"A");
    hidden.hidden = true;
    m.update(hidden);
    CHECK(tips(m) == (std::vector<std::wstring>{L"C", L"B2"}));
    m.remove(icon(10, 2));
    CHECK(tips(m) == (std::vector<std::wstring>{L"B2"}));
    auto g1 = icon(30, 1, L"G");   // identifiée par GUID : la clé ignore hwnd et uid
    g1.guid[3] = 9;
    m.update(g1);
    auto g2 = icon(31, 5, L"G2");
    g2.guid[3] = 9;
    m.update(g2);
    CHECK(tips(m) == (std::vector<std::wstring>{L"G2", L"B2"}));
}

TEST_CASE(tray_model_reset_on_reconnect) {
    md::TrayModel m;
    m.update(icon(10, 1));
    m.update(icon(11, 1));
    m.clear();   // nouvelle connexion du mod : il renvoie toute la liste
    CHECK(m.visible().empty());
    m.update(icon(11, 1, L"X"));
    CHECK(tips(m) == (std::vector<std::wstring>{L"X"}));
}

TEST_CASE(tray_model_prunes_dead_windows) {
    md::TrayModel m;
    m.update(icon(10, 1, L"vivante"));
    m.update(icon(66, 1, L"morte"));
    const std::set<std::uint64_t> alive{10};
    CHECK(m.prune([&](std::uint64_t h) { return alive.count(h) != 0; }));
    CHECK(tips(m) == (std::vector<std::wstring>{L"vivante"}));
    CHECK(!m.prune([&](std::uint64_t h) { return alive.count(h) != 0; }));
}

TEST_CASE(tray_click_versions) {
    auto e = icon(10, 7);
    e.version = 3;
    auto left = md::trayClick(e, 0, POINT{100, 12});
    REQUIRE(left.size() == 2);
    CHECK(left[0].msg == e.callback);
    CHECK(left[0].wp == 7);
    CHECK(left[0].lp == WM_LBUTTONDOWN);
    CHECK(left[1].lp == WM_LBUTTONUP);
    auto right = md::trayClick(e, 1, POINT{100, 12});
    REQUIRE(right.size() == 2);
    CHECK(right[1].lp == WM_RBUTTONUP);

    e.version = 4;
    left = md::trayClick(e, 0, POINT{100, 12});
    REQUIRE(left.size() == 3);
    CHECK(left[0].wp == MAKEWPARAM(100, 12));
    CHECK(LOWORD(left[0].lp) == WM_LBUTTONDOWN);
    CHECK(HIWORD(left[0].lp) == 7);
    CHECK(LOWORD(left[2].lp) == NIN_SELECT);
    right = md::trayClick(e, 1, POINT{100, 12});
    REQUIRE(right.size() == 3);
    CHECK(LOWORD(right[2].lp) == WM_CONTEXTMENU);

    auto dbl = md::trayClick(e, 2, POINT{100, 12});   // double-clic gauche (apps qui s'ouvrent ainsi)
    REQUIRE(!dbl.empty());
    CHECK(LOWORD(dbl[0].lp) == WM_LBUTTONDBLCLK);
    e.version = 3;
    dbl = md::trayClick(e, 2, POINT{100, 12});
    REQUIRE(!dbl.empty());
    CHECK(dbl[0].lp == WM_LBUTTONDBLCLK);
    CHECK(dbl[0].wp == 7);

    e.flags &= ~NIF_MESSAGE;   // pas de message de rappel : rien à poster
    CHECK(md::trayClick(e, 0, POINT{0, 0}).empty());
}

TEST_CASE(bar_renderer_draws_tray_image) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    {
        md::BarRenderer r;
        REQUIRE(r.initOffscreen());
        r.setFont(L"", 13);
        md::BarFrame f;
        md::BarDrawItem icon;
        auto px = std::make_shared<std::vector<std::uint8_t>>(32 * 32 * 4);
        for (std::size_t i = 0; i < px->size(); i += 4) (*px)[i] = (*px)[i + 3] = 0xFF;   // bleu opaque
        icon.image = px;
        icon.imageW = icon.imageH = 32;
        icon.x = 100;
        icon.width = 30;
        f.items.push_back(icon);
        const UINT w = 200, h = 24;
        std::vector<std::uint8_t> bg(size_t(w) * h * 4, 0), out;
        REQUIRE(r.renderToImage(f, bg, w, h, out));
        int inside = 0, outside = 0;
        for (UINT y = 0; y < h; ++y)
            for (UINT x = 0; x < w; ++x) {
                const std::uint8_t* p = &out[(size_t(y) * w + x) * 4];
                const bool lit = p[3] > 60;
                (x >= 100 && x < 130 ? inside : outside) += lit;
                if (lit && x >= 100 && x < 130) CHECK(p[0] > 200 && p[2] < 40);   // couleurs gardées
            }
        CHECK(inside >= 16 * 16 - 32);   // 16 pt à l'échelle 1
        CHECK_EQ(outside, 0);
    }
    CoUninitialize();
}

TEST_CASE(menubar_settings_app_icons_roundtrip) {
    md::MenuBarSettings s;
    CHECK(s.showAppIcons);
    s.showAppIcons = false;
    auto back = md::menuBarSettingsFromJson(*md::json::parse(md::json::serialize(md::menuBarSettingsToJson(s))));
    CHECK(!back.showAppIcons);
}
