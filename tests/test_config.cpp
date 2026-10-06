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

TEST_CASE(metrics_partial_override) {
    auto m = md::metricsFromJson(*md::json::parse(R"({"dockCornerRadius":30,"unknown":1})"));
    CHECK_NEAR(m.dockCornerRadius, 30, 1e-9);
    CHECK_NEAR(m.iconGap, 6, 1e-9);
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
