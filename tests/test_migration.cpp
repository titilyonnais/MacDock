#include "minitest.h"
#include "../src/config/metrics.h"
#include "../src/config/settings.h"
#include "../src/icons/squircle.h"

TEST_CASE(metrics_migrate_v1_updates_old_defaults) {
    auto v = md::migrateMetricsJson(*md::json::parse(
        R"({"iconGap":6,"dockCornerRadius":24,"separatorLengthRatio":0.72,"tooltipGap":12,"indicatorInset":3})"));
    auto m = md::metricsFromJson(v);
    CHECK_NEAR(m.iconGap, 2.3, 1e-9);   // 6 → 4 (v2) → 2,3 (v7, plan 61)
    CHECK_NEAR(m.dockCornerRadius, 0, 1e-9);
    CHECK_NEAR(m.separatorLengthRatio, 0.95, 1e-9);   // 0,72 → 0,80 (v2) → 0,95 (v7)
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
    CHECK_NEAR(m.glassSpecular, 0, 1e-9);   // v8 : aucun contour blanc (retour du 10 octobre)
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
    CHECK_EQ(md::jsonVersion(v), md::kMetricsVersion);
    const md::Metrics fresh = md::metricsFromJson(md::json::Value(md::json::Object{}));   // défauts : ceux de Tahoe
    CHECK_NEAR(fresh.autohideShowSeconds, 0.45, 1e-9);
    CHECK_NEAR(fresh.poofSeconds, 0.35, 1e-9);
}

TEST_CASE(metrics_migrate_v5_tahoe_icon_corner) {   // plan 60 : rayon des icônes mesuré sur macOS 26
    // Le fichier de l'utilisateur garde le défaut de Big Sur (0,225, écrit 0.22500000000000001) : il prend celui de Tahoe.
    auto v = md::migrateMetricsJson(*md::json::parse(R"({"version":5,"iconCornerRatio":0.22500000000000001})"));
    CHECK_NEAR(md::metricsFromJson(v).iconCornerRatio, 0.259, 1e-9);
    CHECK_EQ(md::jsonVersion(v), md::kMetricsVersion);
    // Un rayon choisi reste.
    auto kept = md::metricsFromJson(md::migrateMetricsJson(*md::json::parse(R"({"version":5,"iconCornerRatio":0.24})")));
    CHECK_NEAR(kept.iconCornerRatio, 0.24, 1e-9);
    const md::Metrics fresh;
    CHECK_NEAR(fresh.iconCornerRatio, 0.259, 1e-9);
    CHECK_NEAR(md::kIconCornerRatio, 0.259, 1e-9);   // forme par défaut (masques, plaque grise, icônes de MacDock)
}

TEST_CASE(metrics_migrate_v6_tahoe_dock_geometry) {   // plan 61 : proportions du Dock mesurées sur macOS 26
    auto v = md::migrateMetricsJson(*md::json::parse(
        R"({"version":6,"dockPadding":8,"iconGap":4,"separatorMargin":7,"separatorLengthRatio":0.8,)"
        R"("indicatorCenterFromBottom":4,"dockScreenMargin":5,"indicatorDiameter":4})"));
    auto m = md::metricsFromJson(v);
    CHECK_NEAR(m.dockPadding, 7.1, 1e-9);
    CHECK_NEAR(m.iconGap, 2.3, 1e-9);
    CHECK_NEAR(m.separatorMargin, 9.9, 1e-9);
    CHECK_NEAR(m.separatorLengthRatio, 0.95, 1e-9);
    CHECK_NEAR(m.indicatorCenterFromBottom, 4.7, 1e-9);
    CHECK_NEAR(m.dockScreenMargin, 3.9, 1e-9);
    CHECK_NEAR(m.indicatorDiameter, 4, 1e-9);   // inchangé
    CHECK_EQ(md::jsonVersion(v), md::kMetricsVersion);
    // Des valeurs choisies restent.
    auto kept = md::metricsFromJson(md::migrateMetricsJson(*md::json::parse(R"({"version":6,"dockPadding":10,"iconGap":6})")));
    CHECK_NEAR(kept.dockPadding, 10, 1e-9);
    CHECK_NEAR(kept.iconGap, 6, 1e-9);
    const md::Metrics fresh;
    CHECK_NEAR(fresh.dockPadding, 7.1, 1e-9);
    CHECK_NEAR(fresh.iconGap, 2.3, 1e-9);
    CHECK_NEAR(fresh.separatorLengthRatio, 0.95, 1e-9);
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
    CHECK_NEAR(defaults.glassSpecular, 0, 1e-9);   // v8 : aucun contour blanc
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

TEST_CASE(metrics_migrate_v8_no_white_rim) {
    // Retour du 10 octobre : le Liquid Glass de macOS 26 n'a pas de contour blanc. Le reflet du liseré et le liseré clair
    // passent à 0, même une valeur choisie avant (0,6 chez l'utilisateur) ; les autres réglages du verre restent.
    auto v = md::migrateMetricsJson(*md::json::parse(
        R"({"version":7,"glassSpecular":0.6,"borderOpacity":0.55,"glassFresnel":0.04,"glassTintDark":0.3})"));
    auto m = md::metricsFromJson(v);
    CHECK_NEAR(m.glassSpecular, 0, 1e-9);
    CHECK_NEAR(m.borderOpacity, 0, 1e-9);
    CHECK_NEAR(m.glassFresnel, 0.04, 1e-9);
    CHECK_NEAR(m.glassTintDark, 0.3, 1e-9);
    CHECK_EQ(md::jsonVersion(v), 8);
    const md::Metrics defaults;
    CHECK_NEAR(defaults.borderOpacity, 0, 1e-9);
    CHECK_NEAR(defaults.glassSpecular, 0, 1e-9);
    // Un fichier déjà en v8 garde ses valeurs (on peut remettre un liseré à la main).
    auto kept = md::metricsFromJson(md::migrateMetricsJson(*md::json::parse(R"({"version":8,"glassSpecular":0.3})")));
    CHECK_NEAR(kept.glassSpecular, 0.3, 1e-9);
}
