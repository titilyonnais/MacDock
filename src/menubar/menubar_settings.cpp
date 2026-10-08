#include "menubar_settings.h"

#include <algorithm>
#include <cmath>

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
    s.hud = readBool(v, "hud", s.hud);
    s.showAppIcons = readBool(v, "showAppIcons", s.showAppIcons);
    s.macWindows = readBool(v, "macWindows", s.macWindows);
    if (auto* side = v.find("trafficLightsSide")) s.lightsAlwaysLeft = side->asString("left") == "left";
    if (auto* f = v.find("font")) s.font = fromUtf8(f->asString(""));
    if (auto* t = v.find("trafficLights")) {
        const std::string mode = t->asString("");
        if (mode == "all") s.trafficLights = LightsMode::All;
        else if (mode == "off") s.trafficLights = LightsMode::Off;
    }
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
    v.set("hud", s.hud);
    v.set("showAppIcons", s.showAppIcons);
    v.set("macWindows", s.macWindows);
    v.set("trafficLightsSide", s.lightsAlwaysLeft ? "left" : "auto");
    v.set("trafficLights", s.trafficLights == LightsMode::All ? "all" : s.trafficLights == LightsMode::Off ? "off" : "standard");
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
    v.set("metrics", m);
    return v;
}

} // namespace md
