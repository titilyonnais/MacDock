#include <algorithm>

#include "minitest.h"
#include "../src/model/app_model.h"

static md::AppIdentity idOf(const wchar_t* exe, const wchar_t* aumid = L"") {
    md::AppIdentity a;
    a.exePath = exe;
    a.aumid = aumid;
    a.appId = md::makeAppId(aumid, exe);
    a.displayName = L"X";
    a.launch = exe;
    return a;
}

static std::vector<std::wstring> keys(const md::AppModel& m) {
    std::vector<std::wstring> k;
    for (auto& i : m.items()) k.push_back(i.key);
    return k;
}

TEST_CASE(model_groups_windows_by_app_id) {
    md::AppModel m;
    m.setShowRecents(false);
    m.windowOpened(1, idOf(L"C:\\A\\a.exe"));
    m.windowOpened(2, idOf(L"c:\\a\\A.EXE"));
    auto it = m.items();
    CHECK_EQ(std::count_if(it.begin(), it.end(), [](auto& i) { return i.kind == md::ItemKind::App; }), 1);
    CHECK_EQ(m.windowsOf(md::makeAppId(L"", L"C:\\A\\a.exe")).size(), size_t(2));
}

TEST_CASE(model_store_app_identity_is_aumid) {
    CHECK(md::makeAppId(L"Microsoft.WindowsCalculator_8wekyb3d8bbwe!App",
                        L"C:\\Windows\\System32\\ApplicationFrameHost.exe") ==
          L"Microsoft.WindowsCalculator_8wekyb3d8bbwe!App");
}

TEST_CASE(model_order_without_recents) {
    md::AppModel m;
    m.setShowRecents(false);
    m.loadPinned({{md::PinKind::App, L"c:\\e.exe", L"c:\\e.exe", L"E"},
                  {md::PinKind::AppsButton, L"", L"", L"Apps"},
                  {md::PinKind::Stack, L"", L"C:\\D", L"D"}});
    m.windowOpened(5, idOf(L"C:\\z.exe"));
    m.windowOpened(6, idOf(L"C:\\e.exe"));
    m.windowMinimized(6, true);
    CHECK((keys(m) == std::vector<std::wstring>{L"app:c:\\e.exe", L"apps", L"app:c:\\z.exe", L"sep:1",
                                                L"stack:C:\\D", L"win:6", L"trash"}));
    auto items = m.items();
    CHECK(items[0].pinned);
    CHECK(items[0].running);
    CHECK(!items[2].pinned);
}

TEST_CASE(model_recents_section) {
    md::AppModel m;
    m.setShowRecents(true);
    m.windowOpened(1, idOf(L"C:\\r1.exe"));
    m.windowClosed(1);
    m.windowOpened(2, idOf(L"C:\\r2.exe"));
    auto k = keys(m);
    CHECK((k == std::vector<std::wstring>{L"sep:1", L"app:c:\\r2.exe", L"app:c:\\r1.exe", L"sep:2", L"trash"}));
    CHECK(m.items()[2].recent);
    CHECK(!m.items()[2].running);
}

TEST_CASE(model_recents_capped_at_three) {
    md::AppModel m;
    m.setShowRecents(true);
    for (int i = 0; i < 6; ++i) {
        std::wstring p = L"C:\\a" + std::to_wstring(i) + L".exe";
        m.windowOpened(i + 1, idOf(p.c_str()));
        m.windowClosed(i + 1);
    }
    int recents = 0;
    for (auto& i : m.items()) recents += i.recent;
    CHECK_EQ(recents, 3);
}

TEST_CASE(model_pinned_app_closing_does_not_become_recent) {
    md::AppModel m;
    m.setShowRecents(true);
    m.loadPinned({{md::PinKind::App, L"c:\\p.exe", L"c:\\p.exe", L"P"}});
    m.windowOpened(1, idOf(L"C:\\p.exe"));
    m.windowClosed(1);
    for (auto& i : m.items()) CHECK(!i.recent);
}

TEST_CASE(model_reopened_recent_leaves_recents) {
    md::AppModel m;
    m.setShowRecents(true);
    m.windowOpened(1, idOf(L"C:\\r.exe"));
    m.windowClosed(1);
    m.windowOpened(2, idOf(L"C:\\r.exe"));
    int count = 0;
    for (auto& i : m.items()) count += i.appId == L"c:\\r.exe";
    CHECK_EQ(count, 1);
}

TEST_CASE(model_pin_unpin_move) {
    md::AppModel m;
    m.setShowRecents(false);
    m.windowOpened(1, idOf(L"C:\\a.exe"));
    CHECK(m.pin(md::makeAppId(L"", L"C:\\a.exe"), 0));
    CHECK(m.items()[0].pinned);
    m.loadPinned({{md::PinKind::App, L"x", L"x", L"X"}, {md::PinKind::App, L"y", L"y", L"Y"}});
    CHECK(m.movePinned(0, 1));
    CHECK(m.pinnedEntries()[0].appId == L"y");
    CHECK(m.unpin(L"app:y"));
    CHECK_EQ(m.pinnedEntries().size(), size_t(1));
    CHECK(!m.unpin(L"trash"));
    CHECK(!m.movePinned(0, 9));
}

TEST_CASE(model_revision_changes_only_on_change) {
    md::AppModel m;
    auto r0 = m.revision();
    m.windowClosed(999);
    CHECK_EQ(m.revision(), r0);
    m.windowOpened(1, idOf(L"C:\\a.exe"));
    CHECK(m.revision() != r0);
}

TEST_CASE(model_close_unknown_and_double_close_are_noops) {
    md::AppModel m;
    m.windowOpened(1, idOf(L"C:\\a.exe"));
    m.windowClosed(1);
    m.windowClosed(1);
    m.windowMinimized(42, true);
    m.windowOpened(1, idOf(L"C:\\a.exe"));
    m.windowOpened(1, idOf(L"C:\\a.exe"));
    CHECK_EQ(m.windowsOf(L"c:\\a.exe").size(), size_t(1));
}

TEST_CASE(model_closing_minimized_window_removes_tile) {
    md::AppModel m;
    m.setShowRecents(false);
    m.windowOpened(3, idOf(L"C:\\a.exe"));
    m.windowMinimized(3, true);
    m.windowClosed(3);
    for (auto& i : m.items()) CHECK(i.kind != md::ItemKind::MinimizedWindow);
}

TEST_CASE(model_titles) {
    md::AppModel m;
    m.windowOpened(3, idOf(L"C:\\a.exe"));
    m.windowTitle(3, L"Doc");
    CHECK(m.titleOf(3) == L"Doc");
    CHECK(m.titleOf(4).empty());
}

TEST_CASE(model_window_matches_pin_by_exe_path) {
    md::AppModel m;
    m.setShowRecents(false);
    md::PinnedEntry pin{md::PinKind::App, L"Chrome", L"C:\\Chrome\\chrome.lnk", L"Chrome"};
    pin.exePath = L"C:\\Chrome\\chrome.exe";
    m.loadPinned({pin});
    // La fenêtre n'expose pas d'AUMID : son appId est le chemin de l'exe.
    m.windowOpened(1, idOf(L"c:\\chrome\\CHROME.exe"));
    auto items = m.items();
    CHECK_EQ(items.size(), size_t(3));   // épingle, séparateur, corbeille
    CHECK(items[0].running);
    CHECK_EQ(m.windowsOf(L"Chrome").size(), size_t(1));
}

TEST_CASE(settings_roundtrip_pin_exe_path) {
    md::Settings s;
    md::PinnedEntry p{md::PinKind::App, L"Chrome", L"x.lnk", L"Chrome"};
    p.exePath = L"C:\\c.exe";
    s.pinned.push_back(p);
    CHECK(md::settingsFromJson(md::settingsToJson(s)).pinned[0].exePath == L"C:\\c.exe");
}

TEST_CASE(model_app_of_window) {
    md::AppModel m;
    m.windowOpened(7, idOf(L"C:\\a.exe"));
    CHECK(m.appOfWindow(7) == L"c:\\a.exe");
    CHECK(m.appOfWindow(8).empty());
}

TEST_CASE(model_pin_keeps_exe_path) {
    md::AppModel m;
    md::AppIdentity a;
    a.exePath = L"C:\\Tools\\x.exe";
    a.appId = md::makeAppId(L"", a.exePath);
    a.displayName = L"X";
    a.launch = L"C:\\Tools\\x.lnk";
    m.windowOpened(1, a);
    REQUIRE(m.pin(a.appId, 0));
    auto pins = m.pinnedEntries();
    REQUIRE(pins.size() == 1u);
    CHECK(pins[0].exePath == L"C:\\Tools\\x.exe");   // sans lui, les fenêtres ne se rattachent plus après relance
    CHECK(pins[0].launch == L"C:\\Tools\\x.lnk");
}

TEST_CASE(model_hidden_app_windows_not_listed) {
    // « Masquer » réduit toutes les fenêtres de l'app sans créer de miniatures dans le Dock.
    md::AppModel m;
    m.setShowRecents(false);
    m.windowOpened(3, idOf(L"C:\\a.exe"));
    m.windowOpened(4, idOf(L"C:\\a.exe"));
    m.setHidden(L"c:\\a.exe", true);
    m.windowMinimized(3, true);
    m.windowMinimized(4, true);
    CHECK((keys(m) == std::vector<std::wstring>{L"app:c:\\a.exe", L"sep:1", L"trash"}));
    // Restaurer une fenêtre (clic sur l'app) met fin au masquage : les réductions suivantes redeviennent des tuiles.
    m.windowMinimized(3, false);
    m.windowMinimized(3, true);
    CHECK((keys(m) == std::vector<std::wstring>{L"app:c:\\a.exe", L"sep:1", L"win:4", L"win:3", L"trash"}));
}

TEST_CASE(model_identity_of_closed_pin_has_exe_path) {
    // Menu d'une épingle fermée : « Ouvrir à la connexion » et « Afficher dans l'Explorateur » ont besoin de l'exe.
    md::AppModel m;
    md::PinnedEntry pin{md::PinKind::App, L"Chrome", L"chrome.lnk", L"Chrome"};
    pin.exePath = L"C:/Chrome/chrome.exe";
    m.loadPinned({pin});
    auto id = m.identityOf(L"Chrome");
    REQUIRE(id.has_value());
    CHECK(id->exePath == L"C:/Chrome/chrome.exe");
}

TEST_CASE(model_trash_full_changes_revision_and_item) {
    md::AppModel m;
    CHECK(!m.items().back().trashFull);
    auto r = m.revision();
    m.setTrashFull(true);
    CHECK(m.revision() != r);
    CHECK(m.items().back().kind == md::ItemKind::Trash);
    CHECK(m.items().back().trashFull);
    r = m.revision();
    m.setTrashFull(true);   // sans changement : pas de nouveau rendu
    CHECK(m.revision() == r);
}

TEST_CASE(model_stack_options_update) {
    md::AppModel m;
    m.loadPinned({{md::PinKind::Stack, L"", L"C:\\D", L"D"}});
    auto rev = m.revision();
    CHECK(m.setStackOptions(L"stack:C:\\D", md::StackView::Fan, md::StackSort::Kind));
    CHECK(m.revision() != rev);
    CHECK(m.pinnedEntries()[0].stackView == md::StackView::Fan);
    CHECK(m.pinnedEntries()[0].stackSort == md::StackSort::Kind);
    CHECK(!m.setStackOptions(L"stack:C:\\X", md::StackView::Fan, md::StackSort::Kind));
}

TEST_CASE(model_stack_preview_changes_revision) {
    md::AppModel m;
    m.loadPinned({{md::PinKind::Stack, L"", L"C:\\D", L"D"}});
    auto rev = m.revision();
    CHECK(m.setStackPreview(L"stack:C:\\D", {md::FileRef{L"C:\\D\\a", 1}, md::FileRef{L"C:\\D\\b", 1}}));
    CHECK(m.revision() != rev);
    rev = m.revision();
    CHECK(!m.setStackPreview(L"stack:C:\\D", {md::FileRef{L"C:\\D\\a", 1}, md::FileRef{L"C:\\D\\b", 1}}));   // inchangé : pas de redessin
    CHECK(m.revision() == rev);
    const md::DockItem* stack = nullptr;
    auto items = m.items();
    for (auto& it : items)
        if (it.kind == md::ItemKind::Stack) stack = &it;
    REQUIRE(stack != nullptr);
    CHECK(stack->stackPreview.size() == 2);
    CHECK(stack->stackDisplay == md::StackDisplay::Stack);
    CHECK(m.setStackDisplay(L"stack:C:\\D", md::StackDisplay::Folder));
    CHECK(m.pinnedEntries()[0].stackDisplay == md::StackDisplay::Folder);
    CHECK(!m.setStackPreview(L"stack:C:\\X", {}));
}

TEST_CASE(model_stack_preview_tracks_modification) {
    // Même chemin, contenu nouveau (téléchargement terminé, capture réenregistrée) : l'icône doit suivre.
    md::AppModel m;
    m.loadPinned({{md::PinKind::Stack, L"", L"C:\\D", L"D"}});
    CHECK(m.setStackPreview(L"stack:C:\\D", {md::FileRef{L"C:\\D\\photo.jpg", 100}}));
    CHECK(m.setStackPreview(L"stack:C:\\D", {md::FileRef{L"C:\\D\\photo.jpg", 200}}));
}

TEST_CASE(model_app_click_follows_macos) {
    // Clic sur l'icône d'une app, comme sur macOS : jamais de réduction ; les fenêtres réduites restent au Dock tant
    // qu'une fenêtre est visible ; toutes réduites : la dernière réduite revient.
    md::AppModel m;
    m.setShowRecents(false);
    const auto a = idOf(L"C:\\A\\a.exe");
    CHECK(m.clickActionFor(a.appId).kind == md::AppClick::Kind::Launch);   // pas lancée
    m.windowOpened(1, a);
    m.windowOpened(2, a);
    m.windowOpened(3, a);
    m.windowMinimized(2, true);
    auto c = m.clickActionFor(a.appId);
    CHECK(c.kind == md::AppClick::Kind::Front);   // une fenêtre visible : l'app passe devant
    CHECK(c.windows == std::vector<md::WindowId>({1, 3}));   // sans rouvrir la réduite
    m.windowMinimized(3, true);
    m.windowMinimized(1, true);
    c = m.clickActionFor(a.appId);
    CHECK(c.kind == md::AppClick::Kind::Restore);
    CHECK_EQ(c.windows.size(), std::size_t(1));
    CHECK_EQ(c.windows[0], md::WindowId(1));   // la plus récemment réduite
    m.setHidden(a.appId, true);
    c = m.clickActionFor(a.appId);   // app masquée : tout revient
    CHECK(c.kind == md::AppClick::Kind::Unhide);
    CHECK_EQ(c.windows.size(), std::size_t(3));
}
