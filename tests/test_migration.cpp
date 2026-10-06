#include "minitest.h"
#include "../src/config/metrics.h"
#include "../src/config/settings.h"

TEST_CASE(metrics_migrate_v1_updates_old_defaults) {
    auto v = md::migrateMetricsJson(*md::json::parse(
        R"({"iconGap":6,"dockCornerRadius":24,"separatorLengthRatio":0.72,"tooltipGap":12,"indicatorInset":3})"));
    auto m = md::metricsFromJson(v);
    CHECK_NEAR(m.iconGap, 4, 1e-9);
    CHECK_NEAR(m.dockCornerRadius, 0, 1e-9);
    CHECK_NEAR(m.separatorLengthRatio, 0.80, 1e-9);
    CHECK_NEAR(m.tooltipGap, 10, 1e-9);
    CHECK(v.find("indicatorInset") == nullptr);
    CHECK_EQ(md::jsonVersion(v), 2);
}

TEST_CASE(metrics_migrate_v1_keeps_user_values) {
    auto m = md::metricsFromJson(md::migrateMetricsJson(*md::json::parse(R"({"dockCornerRadius":30,"iconGap":9})")));
    CHECK_NEAR(m.dockCornerRadius, 30, 1e-9);
    CHECK_NEAR(m.iconGap, 9, 1e-9);
}

TEST_CASE(metrics_v2_is_not_migrated) {
    auto m = md::metricsFromJson(md::migrateMetricsJson(*md::json::parse(R"({"version":2,"iconGap":6})")));
    CHECK_NEAR(m.iconGap, 6, 1e-9);
}

TEST_CASE(settings_migrate_v1_large_size) {
    CHECK_NEAR(md::settingsFromJson(md::migrateSettingsJson(*md::json::parse(R"({"largeSize":128})"))).largeSize, 80, 1e-9);
    CHECK_NEAR(md::settingsFromJson(md::migrateSettingsJson(*md::json::parse(R"({"largeSize":100})"))).largeSize, 100, 1e-9);
    CHECK_NEAR(md::settingsFromJson(md::migrateSettingsJson(*md::json::parse(R"({"version":2,"largeSize":128})"))).largeSize,
               128, 1e-9);
}

TEST_CASE(settings_glass_defaults_on_and_roundtrips) {
    CHECK(md::settingsFromJson(*md::json::parse("{}")).glass);
    md::Settings s;
    s.glass = false;
    CHECK(!md::settingsFromJson(md::settingsToJson(s)).glass);
}
