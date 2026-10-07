// Mod Windhawk : relais de la zone de notification. Le mod est compilé ici avec un faux windhawk_api.h ; seules ses
// fonctions pures sont appelées (aucun crochet posé, aucun message diffusé).
#include "../windhawk/macdock-hide-taskbar.wh.cpp"

#include "minitest.h"
#include "../src/ipc/protocol.h"

namespace {

TrayCopyData trayData(DWORD message, UINT uid, UINT flags) {
    TrayCopyData d{};
    d.signature = kTraySignature;
    d.message = message;
    d.nid.cbSize = sizeof(NotifyIconData32);
    d.nid.hWnd = 0x00012345;
    d.nid.uID = uid;
    d.nid.uFlags = flags;
    d.nid.uCallbackMessage = WM_APP + 3;
    d.nid.hIcon = 0x0000ABCD;
    wcscpy_s(d.nid.szTip, L"Discord");
    return d;
}

COPYDATASTRUCT cds(TrayCopyData& d, DWORD size = sizeof(TrayCopyData)) {
    return COPYDATASTRUCT{1, size, &d};
}

std::vector<std::uint8_t> fakeImage(std::uint32_t) { return std::vector<std::uint8_t>(kTrayIconPx * kTrayIconPx * 4, 0x80); }

} // namespace

TEST_CASE(tray_mod_parses_copydata) {
    auto add = trayData(NIM_ADD, 4, NIF_MESSAGE | NIF_ICON | NIF_TIP);
    auto c = cds(add);
    TrayRecord r;
    CHECK(parseTrayCopyData(&c, r) == TrayOp::Add);
    CHECK_EQ(r.hwnd, 0x12345ull);
    CHECK_EQ(r.uid, 4u);
    CHECK_EQ(r.callback, UINT(WM_APP + 3));
    CHECK_EQ(r.hicon, 0xABCDu);
    CHECK(r.tip == L"Discord");

    auto hide = trayData(NIM_MODIFY, 4, NIF_STATE);
    hide.nid.dwState = hide.nid.dwStateMask = NIS_HIDDEN;
    c = cds(hide);
    CHECK(parseTrayCopyData(&c, r) == TrayOp::Modify);
    CHECK(r.hidden && r.hiddenSet);

    auto ver = trayData(NIM_SETVERSION, 4, 0);
    ver.nid.uVersion = NOTIFYICON_VERSION_4;
    c = cds(ver);
    CHECK(parseTrayCopyData(&c, r) == TrayOp::SetVersion);
    CHECK_EQ(r.version, 4u);

    auto del = trayData(NIM_DELETE, 4, 0);
    c = cds(del);
    CHECK(parseTrayCopyData(&c, r) == TrayOp::Delete);

    auto bad = trayData(NIM_ADD, 4, NIF_TIP);
    bad.signature = 0x12345678;   // pas un message de la zone de notification
    c = cds(bad);
    CHECK(parseTrayCopyData(&c, r) == TrayOp::None);
    c = cds(add, 20);   // trop court
    CHECK(parseTrayCopyData(&c, r) == TrayOp::None);
    COPYDATASTRUCT other{2, sizeof add, &add};   // autre usage de WM_COPYDATA (AppBar…)
    CHECK(parseTrayCopyData(&other, r) == TrayOp::None);
    CHECK(parseTrayCopyData(nullptr, r) == TrayOp::None);

    auto noZero = trayData(NIM_ADD, 5, NIF_TIP);
    for (auto& ch : noZero.nid.szTip) ch = L'x';   // texte sans zéro final
    c = cds(noZero);
    CHECK(parseTrayCopyData(&c, r) == TrayOp::Add);
    CHECK(r.tip.size() == 127);

    // NOTIFYICONDATA court (ancienne app) : les champs absents restent vides.
    auto shortData = trayData(NIM_ADD, 6, NIF_TIP | NIF_GUID);
    c = cds(shortData, DWORD(offsetof(TrayCopyData, nid) + offsetof(NotifyIconData32, dwState)));
    CHECK(parseTrayCopyData(&c, r) == TrayOp::Add);
    CHECK(r.guid[0] == 0);
}

TEST_CASE(tray_mod_merges_records) {
    std::vector<TrayEntry> list;
    TrayRecord a;
    a.hwnd = 1;
    a.uid = 1;
    a.flags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    a.callback = 77;
    a.tip = L"A";
    auto ch = mergeTray(list, TrayOp::Add, a, fakeImage);
    REQUIRE(list.size() == 1);
    CHECK(!ch.removed);
    CHECK(list[0].bgra.size() == size_t(kTrayIconPx) * kTrayIconPx * 4);

    TrayRecord tip;   // modification partielle : seul le texte change
    tip.hwnd = 1;
    tip.uid = 1;
    tip.flags = NIF_TIP;
    tip.tip = L"A2";
    mergeTray(list, TrayOp::Modify, tip, fakeImage);
    CHECK(list[0].r.tip == L"A2");
    CHECK_EQ(list[0].r.callback, 77u);
    CHECK((list[0].r.flags & NIF_MESSAGE) != 0);

    TrayRecord unknown = tip;
    unknown.uid = 9;
    CHECK(!mergeTray(list, TrayOp::Modify, unknown, fakeImage).changed);   // inconnue : ignorée
    CHECK(list.size() == 1);

    ch = mergeTray(list, TrayOp::Delete, tip, fakeImage);
    CHECK(ch.removed);
    CHECK(list.empty());
}

TEST_CASE(tray_mod_frames_parse_in_bar) {
    std::vector<TrayEntry> list;
    TrayRecord a;
    a.hwnd = 0x12345;
    a.uid = 3;
    a.flags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    a.callback = 99;
    a.tip = L"Steam";
    mergeTray(list, TrayOp::Add, a, fakeImage);
    a.version = 4;   // NIM_ADD ne porte pas la version (union avec uTimeout) : NIM_SETVERSION la donne
    mergeTray(list, TrayOp::SetVersion, a, fakeImage);
    md::ipc::Decoder d;
    auto bytes = encodeTrayUpdate(list[0]);
    auto rm = encodeTrayRemove(list[0].r);
    bytes.insert(bytes.end(), rm.begin(), rm.end());
    d.feed(bytes.data(), bytes.size());
    auto m1 = d.next();
    REQUIRE(m1.has_value());
    auto up = md::ipc::parseTrayUpdate(*m1);
    REQUIRE(up.has_value());
    CHECK_EQ(up->hwnd, 0x12345ull);
    CHECK_EQ(up->uid, 3u);
    CHECK_EQ(up->callback, 99u);
    CHECK_EQ(up->version, 4u);
    CHECK(up->tip == L"Steam");
    CHECK(up->w == kTrayIconPx && up->h == kTrayIconPx);
    auto m2 = d.next();
    REQUIRE(m2.has_value());
    auto gone = md::ipc::parseTrayRemove(*m2);
    REQUIRE(gone.has_value());
    CHECK_EQ(gone->uid, 3u);
}

TEST_CASE(tray_mod_renders_icon) {   // icône partagée du système : rien n'est modifié
    auto px = renderIcon(LoadIconW(nullptr, IDI_INFORMATION), kTrayIconPx);
    REQUIRE(px.size() == size_t(kTrayIconPx) * kTrayIconPx * 4);
    int opaque = 0;
    for (size_t i = 3; i < px.size(); i += 4) opaque += px[i] > 128;
    CHECK(opaque > 100);
    CHECK(opaque < kTrayIconPx * kTrayIconPx);   // le disque n'emplit pas les coins
    CHECK(renderIcon(nullptr, kTrayIconPx).empty());
}
