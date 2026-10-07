// Barre de menus : Éléments récents (apps récentes, documents du dossier Récents, menu du système).
#include <windows.h>

#include <filesystem>

#include "minitest.h"
#include "../src/core/json.h"
#include "../src/menubar/app_menus.h"
#include "../src/menubar/recent_items.h"

namespace {

std::uint64_t fileTime(int daysAgo) {
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    const std::uint64_t t = (std::uint64_t(now.dwHighDateTime) << 32) | now.dwLowDateTime;
    return t - std::uint64_t(daysAgo) * 24 * 3600 * 10'000'000ull;
}

void touch(const std::filesystem::path& p, std::uint64_t time) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    FILETIME ft{DWORD(time), DWORD(time >> 32)};
    SetFileTime(h, nullptr, nullptr, &ft);
    CloseHandle(h);
}

} // namespace

TEST_CASE(recent_apps_mru_dedupes_and_caps) {
    std::vector<md::RecentEntry> list;
    for (int i = 0; i < 12; ++i) md::pushRecent(list, {L"App " + std::to_wstring(i), L"C:\\a\\" + std::to_wstring(i) + L".exe"});
    REQUIRE(list.size() == 10);
    CHECK(list[0].name == L"App 11");   // la plus récente en tête
    CHECK(list[9].name == L"App 2");
    md::pushRecent(list, {L"App 5", L"C:\\A\\5.EXE"});   // déjà présente (casse ignorée) : remonte en tête
    CHECK_EQ(list.size(), std::size_t(10));
    CHECK(list[0].name == L"App 5");
    CHECK(list[1].name == L"App 11");
    md::pushRecent(list, {L"Sans cible", L""});   // rien à relancer : ignorée
    CHECK(list[0].name == L"App 5");
}

TEST_CASE(recent_documents_sorted_and_cleared) {
    const auto dir = std::filesystem::temp_directory_path() / L"macdock-test-recent";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / L"AutomaticDestinations");
    touch(dir / L"vieux.pdf.lnk", fileTime(30));
    touch(dir / L"a.txt.lnk", fileTime(2));
    touch(dir / L"b.docx.lnk", fileTime(1));
    touch(dir / L"Téléchargements.lnk", fileTime(0));   // dossier : pas un document
    touch(dir / L"notes.md", fileTime(0));              // pas un raccourci

    auto docs = md::recentDocuments(dir.wstring(), 10, 0);
    REQUIRE(docs.size() == 3);
    CHECK(docs[0].name == L"b.docx");
    CHECK(docs[1].name == L"a.txt");
    CHECK(docs[2].name == L"vieux.pdf");
    CHECK(docs[0].target == (dir / L"b.docx.lnk").wstring());

    docs = md::recentDocuments(dir.wstring(), 10, fileTime(10));   // effacé il y a 10 jours
    CHECK_EQ(docs.size(), std::size_t(2));
    docs = md::recentDocuments(dir.wstring(), 1, 0);
    CHECK_EQ(docs.size(), std::size_t(1));
    CHECK(md::recentDocuments((dir / L"absent").wstring(), 10, 0).empty());
    std::filesystem::remove_all(dir);
}

TEST_CASE(recent_state_roundtrip) {
    md::RecentState s;
    s.apps = {{L"Bloc-notes", L"C:\\Windows\\notepad.exe"}, {L"Calculatrice", L"shell:AppsFolder\\Microsoft.WindowsCalculator_8wekyb3d8bbwe!App"}};
    s.clearedAt = 133'712'345'678'901'234ull;   // au-delà de la précision d'un double
    auto back = md::recentFromJson(*md::json::parse(md::json::serialize(md::recentToJson(s))));
    REQUIRE(back.apps.size() == 2);
    CHECK(back.apps[1].name == L"Calculatrice");
    CHECK(back.apps[1].target == s.apps[1].target);
    CHECK_EQ(back.clearedAt, s.clearedAt);
    auto empty = md::recentFromJson(md::json::Value{});
    CHECK(empty.apps.empty());
    CHECK_EQ(empty.clearedAt, std::uint64_t(0));
}

TEST_CASE(menus_logo_recent_items) {
    md::BarContext c;
    c.appName = L"Bloc-notes";
    c.recentApps = {{L"Bloc-notes", L"C:\\Windows\\notepad.exe"}, {L"Paint", L"C:\\Windows\\mspaint.exe"}};
    c.recentDocs = {{L"a.txt", L"C:\\r\\a.txt.lnk"}};
    auto m = md::buildBarMenus(c);
    const md::MenuItem* recent = nullptr;
    for (const auto& it : m.menus[0].model.items)
        if (it.text == L"Éléments récents") recent = &it;
    REQUIRE(recent != nullptr);
    const auto& s = recent->submenu;
    REQUIRE(s.size() == 8);
    CHECK(s[0].text == L"Applications");
    CHECK(!s[0].enabled);
    CHECK(s[1].text == L"Bloc-notes");
    CHECK(m.actions.at(s[2].id).kind == md::ActionKind::LaunchApp);
    CHECK(m.actions.at(s[2].id).arg == L"C:\\Windows\\mspaint.exe");
    CHECK(s[3].separator());
    CHECK(s[4].text == L"Documents");
    CHECK(m.actions.at(s[5].id).kind == md::ActionKind::OpenUri);
    CHECK(s[6].separator());
    CHECK(s[7].text == L"Effacer le menu");
    CHECK(s[7].enabled);
    CHECK(m.actions.at(s[7].id).kind == md::ActionKind::ClearRecent);

    c.recentApps.clear();
    c.recentDocs.clear();
    m = md::buildBarMenus(c);
    for (const auto& it : m.menus[0].model.items)
        if (it.text == L"Éléments récents") CHECK(!it.submenu.back().enabled);   // rien à effacer
}
