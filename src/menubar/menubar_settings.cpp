#include "menubar_settings.h"

#include <algorithm>
#include <cmath>

#include "../config/metrics.h"
#include "../core/strings.h"

namespace md {
namespace {

bool readBool(const json::Value& v, const char* key, bool def) {
    auto* f = v.find(key);
    return f ? f->asBool(def) : def;
}

double readBounded(const json::Value& v, const char* key, double def, double lo, double hi) {
    auto* f = v.find(key);
    double d = f ? f->asNumber(def) : def;
    if (!std::isfinite(d)) d = def;
    return std::clamp(d, lo, hi);
}

} // namespace

MenuBarSettings menuBarSettingsFromJson(const json::Value& v) {
    MenuBarSettings s;
    if (!v.isObject()) return s;
    s.autohide = readBool(v, "autohide", s.autohide);
    s.showSound = readBool(v, "showSound", s.showSound);
    s.showNetwork = readBool(v, "showNetwork", s.showNetwork);
    s.showBattery = readBool(v, "showBattery", s.showBattery);
    s.showSearch = readBool(v, "showSearch", s.showSearch);
    s.showInput = readBool(v, "showInput", s.showInput);
    s.hud = readBool(v, "hud", s.hud);
    s.volumeFeedback = readBool(v, "volumeFeedback", s.volumeFeedback);
    s.showAppIcons = readBool(v, "showAppIcons", s.showAppIcons);
    s.macWindows = readBool(v, "macWindows", s.macWindows);
    s.fillOnMaximize = readBool(v, "fillOnMaximize", s.fillOnMaximize);
    if (auto* f = v.find("font")) s.font = fromUtf8(f->asString(""));
    if (auto* c = v.find("clock"); c && c->isObject()) {
        s.clock.weekday = readBool(*c, "weekday", s.clock.weekday);
        s.clock.date = readBool(*c, "date", s.clock.date);
        s.clock.seconds = readBool(*c, "seconds", s.clock.seconds);
        s.clock.hour24 = readBool(*c, "hour24", s.clock.hour24);
    }
    if (auto* m = v.find("metrics"); m && m->isObject()) {
        MenuBarMetrics& t = s.metrics;
        t.height = readBounded(*m, "height", t.height, 16, 48);
        t.fontSize = readBounded(*m, "fontSize", t.fontSize, 9, 24);
        t.leftMargin = readBounded(*m, "leftMargin", t.leftMargin, 0, 60);
        t.titlePadding = readBounded(*m, "titlePadding", t.titlePadding, 0, 60);
        t.logoSize = readBounded(*m, "logoSize", t.logoSize, 6, 40);
        t.highlightHeight = readBounded(*m, "highlightHeight", t.highlightHeight, 0, 48);
        t.highlightRadius = readBounded(*m, "highlightRadius", t.highlightRadius, 0, 24);
        t.statusWidth = readBounded(*m, "statusWidth", t.statusWidth, 10, 60);
        t.rightMargin = readBounded(*m, "rightMargin", t.rightMargin, 0, 60);
        t.statusIconSize = readBounded(*m, "statusIconSize", t.statusIconSize, 8, 32);
        t.textBaseline = readBounded(*m, "textBaseline", t.textBaseline, 0, 48);
        t.clockGap = readBounded(*m, "clockGap", t.clockGap, 0, 60);
    }
    return s;
}

json::Value menuBarSettingsToJson(const MenuBarSettings& s) {
    json::Value v;
    v.set("version", kMenuBarSettingsVersion);
    v.set("autohide", s.autohide);
    v.set("font", toUtf8(s.font));
    json::Value c;
    c.set("weekday", s.clock.weekday);
    c.set("date", s.clock.date);
    c.set("seconds", s.clock.seconds);
    c.set("hour24", s.clock.hour24);
    v.set("clock", c);
    v.set("showSound", s.showSound);
    v.set("showNetwork", s.showNetwork);
    v.set("showBattery", s.showBattery);
    v.set("showSearch", s.showSearch);
    v.set("showInput", s.showInput);
    v.set("hud", s.hud);
    v.set("volumeFeedback", s.volumeFeedback);
    v.set("showAppIcons", s.showAppIcons);
    v.set("macWindows", s.macWindows);
    v.set("fillOnMaximize", s.fillOnMaximize);
    const MenuBarMetrics& t = s.metrics;
    json::Value m;
    m.set("height", t.height);
    m.set("fontSize", t.fontSize);
    m.set("leftMargin", t.leftMargin);
    m.set("titlePadding", t.titlePadding);
    m.set("logoSize", t.logoSize);
    m.set("highlightHeight", t.highlightHeight);
    m.set("highlightRadius", t.highlightRadius);
    m.set("statusWidth", t.statusWidth);
    m.set("rightMargin", t.rightMargin);
    m.set("statusIconSize", t.statusIconSize);
    m.set("textBaseline", t.textBaseline);
    m.set("clockGap", t.clockGap);
    v.set("metrics", m);
    return v;
}

json::Value migrateMenuBarSettingsJson(const json::Value& v) {
    if (!v.isObject() || jsonVersion(v) >= kMenuBarSettingsVersion) return v;
    struct Changed { int before; const char* key; double oldDefault; double newDefault; };
    static const Changed kChanged[] = {
        // v2 : barre mesurée sur macOS 26 (plan 64).
        {2, "statusWidth", 30, 36},
        {2, "rightMargin", 10, 8.4},
    };
    const int from = jsonVersion(v);
    json::Value out = v;
    if (const json::Value* metrics = v.find("metrics"); metrics && metrics->isObject()) {
        json::Value m = *metrics;
        for (const Changed& c : kChanged) {
            const json::Value* f = m.find(c.key);
            if (from >= c.before || !f || !f->isNumber() || std::fabs(f->asNumber(0) - c.oldDefault) >= 1e-9) continue;
            m.set(c.key, c.newDefault);   // défaut de l'époque : suit le nouveau
        }
        out.set("metrics", std::move(m));
    }
    out.set("version", kMenuBarSettingsVersion);
    return out;
}

} // namespace md
