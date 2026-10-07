#include <windows.h>

#include "minitest.h"
#include "test_helpers.h"
#include "../src/config/config_store.h"
#include "../src/config/metrics.h"
#include "../src/config/settings.h"

TEST_CASE(settings_defaults_when_empty) {
    auto s = md::settingsFromJson(md::json::Object{});
    CHECK(s.position == md::DockPosition::Bottom);
    CHECK_NEAR(s.tileSize, 48, 1e-9);
    CHECK(s.magnification);
    CHECK(!s.pinnedInitialized);
}

TEST_CASE(settings_clamps_and_ignores_wrong_types) {
    auto v = md::json::parse(R"({"tileSize":500,"largeSize":10,"magnification":"yes","position":"left"})");
    auto s = md::settingsFromJson(*v);
    CHECK_NEAR(s.tileSize, 128, 1e-9);
    CHECK_NEAR(s.largeSize, 128, 1e-9);
    CHECK(s.magnification);
    CHECK(s.position == md::DockPosition::Left);
}

TEST_CASE(settings_roundtrip_with_pins) {
    md::Settings s;
    s.pinnedInitialized = true;
    s.autohide = true;
    s.pinned.push_back({md::PinKind::App, L"c:\\windows\\explorer.exe", L"C:\\Windows\\explorer.exe", L"Explorateur"});
    s.pinned.push_back({md::PinKind::Stack, L"", L"C:\\Users\\x\\Downloads", L"Téléchargements"});
    auto back = md::settingsFromJson(md::settingsToJson(s));
    CHECK_EQ(back.pinned.size(), size_t(2));
    CHECK(back.pinned[1].kind == md::PinKind::Stack);
    CHECK(back.pinned[1].name == L"Téléchargements");
    CHECK(back.pinned[0].appId == L"c:\\windows\\explorer.exe");
    CHECK(back.autohide);
    CHECK(back.pinnedInitialized);
}

TEST_CASE(settings_screen_roundtrip) {
    md::Settings s;
    s.screen = L"\\\\.\\DISPLAY2";
    auto back = md::settingsFromJson(md::settingsToJson(s));
    CHECK(back.screen == L"\\\\.\\DISPLAY2");
    CHECK(md::settingsFromJson(md::json::Object{}).screen.empty());
}

TEST_CASE(settings_stack_options_roundtrip) {
    md::Settings s;
    md::PinnedEntry e{md::PinKind::Stack, L"", L"C:\\D", L"D"};
    e.stackView = md::StackView::Grid;
    e.stackSort = md::StackSort::Name;
    s.pinned.push_back(e);
    s.pinned.push_back({md::PinKind::Stack, L"", L"C:\\E", L"E"});
    auto back = md::settingsFromJson(md::settingsToJson(s));
    REQUIRE(back.pinned.size() == 2);
    CHECK(back.pinned[0].stackView == md::StackView::Grid);
    CHECK(back.pinned[0].stackSort == md::StackSort::Name);
    CHECK(back.pinned[1].stackView == md::StackView::Auto);
    CHECK(back.pinned[1].stackSort == md::StackSort::DateAdded);
}

TEST_CASE(metrics_partial_override) {
    auto m = md::metricsFromJson(*md::json::parse(R"({"dockCornerRadius":30,"unknown":1})"));
    CHECK_NEAR(m.dockCornerRadius, 30, 1e-9);
    CHECK_NEAR(m.iconGap, 4, 1e-9);
}

TEST_CASE(metrics_roundtrip) {
    md::Metrics m;
    m.launchBouncePeriod = 0.7;
    auto back = md::metricsFromJson(md::metricsToJson(m));
    CHECK_NEAR(back.launchBouncePeriod, 0.7, 1e-9);
    CHECK_NEAR(back.shadowBlur, m.shadowBlur, 1e-9);
}

TEST_CASE(config_load_invalid_json_falls_back_and_backs_up) {
    std::wstring p = md::testTempDir() + L"\\settings.json";
    md::testWriteFile(p, "{\"tileSize\": 4");
    auto r = md::loadJsonFile(p);
    CHECK(r.wasInvalid);
    CHECK(r.value.isObject());
    CHECK(md::testFileExists(p + L".bak"));
}

TEST_CASE(config_missing_file_is_not_invalid) {
    auto r = md::loadJsonFile(md::testTempDir() + L"\\absent.json");
    CHECK(!r.fromFile);
    CHECK(!r.wasInvalid);
    CHECK(r.value.isObject());
}

TEST_CASE(config_atomic_save_then_load) {
    std::wstring p = md::testTempDir() + L"\\m.json";
    md::json::Value o = md::json::Object{};
    o.set("a", 2.0);
    CHECK(md::saveJsonFileAtomic(p, o));
    auto r = md::loadJsonFile(p);
    CHECK(r.fromFile);
    CHECK(!r.wasInvalid);
    CHECK_NEAR(r.value.find("a")->asNumber(0), 2, 1e-9);
}

TEST_CASE(metrics_absurd_values_are_clamped) {
    auto m = md::metricsFromJson(*md::json::parse(
        R"({"iconGap":-48,"magnifyDamping":0,"magnifyStiffness":-5,"dockPadding":-3,"launchBouncePeriod":0,"magnifyRangeTiles":-1})"));
    CHECK(m.iconGap >= 0);
    CHECK(m.magnifyDamping > 0);
    CHECK(m.magnifyStiffness > 0);
    CHECK(m.dockPadding >= 0);
    CHECK(m.launchBouncePeriod > 0);
    CHECK(m.magnifyRangeTiles > 0);
    auto big = md::metricsFromJson(*md::json::parse(R"({"magnifyStiffness":1e12})"));
    CHECK(big.magnifyStiffness <= 5000);
}

TEST_CASE(config_default_pins_only_when_file_absent) {
    md::LoadResult absent;
    CHECK(md::shouldImportDefaultPins(absent, md::Settings{}));
    md::LoadResult invalid;
    invalid.wasInvalid = true;
    CHECK(!md::shouldImportDefaultPins(invalid, md::Settings{}));
    md::LoadResult unreadable;
    unreadable.unreadable = true;
    CHECK(!md::shouldImportDefaultPins(unreadable, md::Settings{}));
    md::LoadResult ok;
    ok.fromFile = true;
    md::Settings initialized;
    initialized.pinnedInitialized = true;
    CHECK(!md::shouldImportDefaultPins(ok, initialized));
    CHECK(md::shouldImportDefaultPins(ok, md::Settings{}));   // fichier valide mais jamais initialisé
}

TEST_CASE(config_locked_file_is_unreadable_not_absent) {
    std::wstring p = md::testTempDir() + L"\\locked.json";
    md::testWriteFile(p, "{}");
    HANDLE lock = CreateFileW(p.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);   // aucun partage
    auto r = md::loadJsonFile(p);
    CloseHandle(lock);
    CHECK(r.unreadable);
    CHECK(!r.fromFile);
}

TEST_CASE(settings_stack_display_roundtrip) {
    md::Settings s;
    md::PinnedEntry e{md::PinKind::Stack, L"", L"C:\\D", L"D"};
    e.stackDisplay = md::StackDisplay::Folder;
    s.pinned.push_back(e);
    s.pinned.push_back({md::PinKind::Stack, L"", L"C:\\E", L"E"});
    s.pinned[1].stackView = md::StackView::List;
    auto back = md::settingsFromJson(md::settingsToJson(s));
    REQUIRE(back.pinned.size() == 2);
    CHECK(back.pinned[1].stackView == md::StackView::List);
    CHECK(back.pinned[0].stackDisplay == md::StackDisplay::Folder);
    CHECK(back.pinned[1].stackDisplay == md::StackDisplay::Stack);   // par défaut, comme sur macOS
}

TEST_CASE(settings_minimize_effect) {
    auto s = md::settingsFromJson(*md::json::parse(R"({"minimizeEffect":"scale"})"));
    CHECK(s.minimizeEffect == md::MinimizeEffect::Scale);
    CHECK(md::settingsFromJson(*md::json::parse(R"({"minimizeEffect":"zoom"})")).minimizeEffect == md::MinimizeEffect::Genie);
    CHECK(md::settingsFromJson(*md::json::parse("{}")).minimizeEffect == md::MinimizeEffect::Genie);
    s.minimizeEffect = md::MinimizeEffect::Windows;
    CHECK(md::settingsFromJson(md::settingsToJson(s)).minimizeEffect == md::MinimizeEffect::Windows);
}

TEST_CASE(settings_spotlight_hotkey) {
    md::Settings s = md::settingsFromJson(*md::json::parse("{\"spotlightHotkey\":\"ctrl+space\"}"));
    CHECK(s.spotlightHotkey == L"ctrl+space");
    CHECK(md::settingsFromJson(*md::json::parse("{}")).spotlightHotkey == L"alt+space");
    CHECK(md::settingsFromJson(*md::json::parse("{\"spotlightHotkey\":\"bizarre\"}")).spotlightHotkey == L"alt+space");
    auto back = md::settingsFromJson(md::settingsToJson(s));
    CHECK(back.spotlightHotkey == L"ctrl+space");
}

TEST_CASE(settings_spotlight_hotkey_off_and_case) {
    CHECK(md::settingsFromJson(*md::json::parse("{\"spotlightHotkey\":\"off\"}")).spotlightHotkey == L"off");
    CHECK(md::settingsFromJson(*md::json::parse("{\"spotlightHotkey\":\"Ctrl+Space\"}")).spotlightHotkey == L"ctrl+space");
}

TEST_CASE(settings_mission_hotkey) {
    CHECK(md::settingsFromJson(*md::json::parse("{}")).missionControlHotkey == L"ctrl+alt+up");
    CHECK(md::settingsFromJson(*md::json::parse("{\"missionControlHotkey\":\"F3\"}")).missionControlHotkey == L"f3");
    CHECK(md::settingsFromJson(*md::json::parse("{\"missionControlHotkey\":\"off\"}")).missionControlHotkey == L"off");
    CHECK(md::settingsFromJson(*md::json::parse("{\"missionControlHotkey\":\"bizarre\"}")).missionControlHotkey == L"ctrl+alt+up");
    md::Settings s;
    s.missionControlHotkey = L"ctrl+up";
    CHECK(md::settingsFromJson(md::settingsToJson(s)).missionControlHotkey == L"ctrl+up");
}

TEST_CASE(settings_switcher_hotkey) {
    CHECK(md::settingsFromJson(*md::json::parse("{}")).appSwitcherHotkey == L"alt+tab");
    CHECK(md::settingsFromJson(*md::json::parse("{\"appSwitcherHotkey\":\"OFF\"}")).appSwitcherHotkey == L"off");
    CHECK(md::settingsFromJson(*md::json::parse("{\"appSwitcherHotkey\":\"bizarre\"}")).appSwitcherHotkey == L"alt+tab");
    md::Settings s;
    s.appSwitcherHotkey = L"off";
    CHECK(md::settingsFromJson(md::settingsToJson(s)).appSwitcherHotkey == L"off");
}

TEST_CASE(settings_hot_corners) {
    md::Settings d;
    CHECK(d.hotCorners[int(md::Corner::BottomRight)] == md::HotCornerAction::Desktop);
    CHECK(d.hotCorners[int(md::Corner::TopLeft)] == md::HotCornerAction::Off);
    auto v = md::json::parse(R"({"hotCorners": {"topLeft": "missionControl", "bottomRight": "nope", "topRight": 3}})");
    REQUIRE(v.has_value());
    auto s = md::settingsFromJson(*v);
    CHECK(s.hotCorners[int(md::Corner::TopLeft)] == md::HotCornerAction::MissionControl);
    CHECK(s.hotCorners[int(md::Corner::BottomRight)] == md::HotCornerAction::Desktop);   // inconnu : défaut
    CHECK(s.hotCorners[int(md::Corner::TopRight)] == md::HotCornerAction::Off);
    auto back = md::settingsFromJson(md::settingsToJson(s));
    CHECK(back.hotCorners == s.hotCorners);
}
