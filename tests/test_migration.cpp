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
    CHECK_EQ(md::jsonVersion(v), md::kMetricsVersion);
}

TEST_CASE(metrics_migrate_v1_keeps_user_values) {
    auto m = md::metricsFromJson(md::migrateMetricsJson(*md::json::parse(R"({"dockCornerRadius":30,"iconGap":9})")));
    CHECK_NEAR(m.dockCornerRadius, 30, 1e-9);
    CHECK_NEAR(m.iconGap, 9, 1e-9);
}

TEST_CASE(metrics_v2_keeps_v1_values) {   // les changements de la v1 ne s'appliquent plus à un fichier v2
    auto m = md::metricsFromJson(md::migrateMetricsJson(*md::json::parse(R"({"version":2,"iconGap":6})")));
    CHECK_NEAR(m.iconGap, 6, 1e-9);
}

TEST_CASE(metrics_migrate_v2_golden_gate_glass) {
    // Golden Gate : un fichier v2 suit les défauts successifs (v3 puis v4) ; les valeurs choisies restent.
    auto v = md::migrateMetricsJson(*md::json::parse(
        R"({"version":2,"glassTintLight":0.22,"glassTintDark":0.3,"glassSpecular":0.55,"iconGap":6,)"
        R"("autohideShowSeconds":0.45,"autohideHideSeconds":0.45,"poofSeconds":0.35})"));
    auto m = md::metricsFromJson(v);
    CHECK_NEAR(m.autohideShowSeconds, 0.45, 1e-9);   // v3 les raccourcissait (Golden Gate), v5 rend celles de Tahoe
    CHECK_NEAR(m.autohideHideSeconds, 0.45, 1e-9);
    CHECK_NEAR(m.poofSeconds, 0.35, 1e-9);
    CHECK_NEAR(m.glassTintLight, 0.10, 1e-9);
    CHECK_NEAR(m.glassTintDark, 0.22, 1e-9);
    CHECK_NEAR(m.glassSpecular, 0.75, 1e-9);
    CHECK_NEAR(m.iconGap, 6, 1e-9);
    CHECK_EQ(md::jsonVersion(v), md::kMetricsVersion);
    auto kept = md::metricsFromJson(md::migrateMetricsJson(*md::json::parse(R"({"version":2,"glassTintLight":0.5})")));
    CHECK_NEAR(kept.glassTintLight, 0.5, 1e-9);
}

TEST_CASE(metrics_migrate_v4_tahoe_durations) {
    // v5 (plan 44) : la cible redevient macOS 26 Tahoe. Les durées encore à leur défaut Golden Gate (~12 % plus courtes)
    // reprennent celles de Tahoe ; une durée choisie par l'utilisateur reste ; le verre (recalé sur des captures) aussi.
    auto v = md::migrateMetricsJson(*md::json::parse(
        R"({"version":4,"autohideShowSeconds":0.40,"autohideHideSeconds":0.6,"poofSeconds":0.31,"glassTintLight":0.10})"));
    auto m = md::metricsFromJson(v);
    CHECK_NEAR(m.autohideShowSeconds, 0.45, 1e-9);
    CHECK_NEAR(m.autohideHideSeconds, 0.6, 1e-9);
    CHECK_NEAR(m.poofSeconds, 0.35, 1e-9);
    CHECK_NEAR(m.glassTintLight, 0.10, 1e-9);
    CHECK_EQ(md::jsonVersion(v), 5);
    const md::Metrics fresh = md::metricsFromJson(md::json::Value(md::json::Object{}));   // défauts : ceux de Tahoe
    CHECK_NEAR(fresh.autohideShowSeconds, 0.45, 1e-9);
    CHECK_NEAR(fresh.poofSeconds, 0.35, 1e-9);
}

TEST_CASE(metrics_migrate_chain_keeps_value_chosen_before) {
    // 0,30 choisi en v2 (défaut d'alors : 0,22) est une valeur de l'utilisateur, même si c'est le défaut de la v3.
    auto m = md::metricsFromJson(md::migrateMetricsJson(*md::json::parse(R"({"version":2,"glassTintLight":0.3})")));
    CHECK_NEAR(m.glassTintLight, 0.30, 1e-9);
}

TEST_CASE(metrics_migrate_v3_glass_from_real_dock) {
    // v4 : verre recalé sur des captures du Dock réel (liseré fin, voile léger, fond saturé, ombre à peine visible).
    auto m = md::metricsFromJson(md::migrateMetricsJson(*md::json::parse(
        R"({"version":3,"glassBevel":9,"glassFresnel":0.18,"glassTintLight":0.3,"glassSaturation":1.15,"shadowOpacity":0.22})")));
    CHECK_NEAR(m.glassBevel, 4, 1e-9);
    CHECK_NEAR(m.glassFresnel, 0.04, 1e-9);
    CHECK_NEAR(m.glassTintLight, 0.10, 1e-9);
    CHECK_NEAR(m.glassSaturation, 1.50, 1e-9);
    CHECK_NEAR(m.shadowOpacity, 0.07, 1e-9);
    const md::Metrics defaults;
    CHECK_NEAR(defaults.glassTintLight, 0.10, 1e-9);
    CHECK_NEAR(defaults.glassSpecular, 0.75, 1e-9);
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

TEST_CASE(metrics_json_complete_detects_missing_keys) {
    md::Metrics m;
    CHECK(md::metricsJsonComplete(md::metricsToJson(m)));
    // Fichier migré de la v1 : il ne contient pas les mesures ajoutées par la v2 (verre, grille d'icône…).
    auto migrated = md::migrateMetricsJson(*md::json::parse(R"({"iconGap":6,"shadowOpacity":0.3})"));
    CHECK(!md::metricsJsonComplete(migrated));
    // Réécrit complet, il garde les personnalisations.
    auto full = md::metricsToJson(md::metricsFromJson(migrated));
    CHECK(md::metricsJsonComplete(full));
    CHECK(full.find("glassBlur") != nullptr);
    CHECK_NEAR(md::metricsFromJson(full).shadowOpacity, 0.3, 1e-9);
}
