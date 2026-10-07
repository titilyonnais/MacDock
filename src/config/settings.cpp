#include "settings.h"

#include <algorithm>

#include "../core/strings.h"

namespace md {
namespace {

bool readBool(const json::Value& v, const char* key, bool def) {
    auto* f = v.find(key);
    return f ? f->asBool(def) : def;
}

double readNumber(const json::Value& v, const char* key, double def) {
    auto* f = v.find(key);
    return f ? f->asNumber(def) : def;
}

std::wstring readString(const json::Value& v, const char* key) {
    auto* f = v.find(key);
    return f ? fromUtf8(f->asString("")) : std::wstring();
}

const char* positionName(DockPosition p) {
    switch (p) {
        case DockPosition::Left: return "left";
        case DockPosition::Right: return "right";
        default: return "bottom";
    }
}

const char* viewName(StackView v) {
    switch (v) {
        case StackView::Fan: return "fan";
        case StackView::Grid: return "grid";
        default: return "auto";
    }
}

const char* sortName(StackSort s) {
    switch (s) {
        case StackSort::Name: return "name";
        case StackSort::Modified: return "modified";
        case StackSort::Kind: return "kind";
        default: return "dateAdded";
    }
}

const char* kindName(PinKind k) {
    switch (k) {
        case PinKind::AppsButton: return "apps";
        case PinKind::Stack: return "stack";
        default: return "app";
    }
}

} // namespace

Settings settingsFromJson(const json::Value& v) {
    Settings s;
    std::wstring pos = readString(v, "position");
    if (pos == L"left") s.position = DockPosition::Left;
    else if (pos == L"right") s.position = DockPosition::Right;
    s.autohide = readBool(v, "autohide", s.autohide);
    s.magnification = readBool(v, "magnification", s.magnification);
    s.showRecents = readBool(v, "showRecents", s.showRecents);
    s.tahoeStrictIcons = readBool(v, "tahoeStrictIcons", s.tahoeStrictIcons);
    s.tileSize = std::clamp(readNumber(v, "tileSize", s.tileSize), 16.0, 128.0);
    s.largeSize = std::clamp(readNumber(v, "largeSize", s.largeSize), s.tileSize, 128.0);
    s.glass = readBool(v, "glass", s.glass);
    s.font = readString(v, "font");
    s.screen = readString(v, "screen");
    s.pinnedInitialized = readBool(v, "pinnedInitialized", false);
    if (auto* pins = v.find("pinned")) {
        for (auto& p : pins->asArray()) {
            if (!p.isObject()) continue;
            PinnedEntry e;
            std::wstring kind = readString(p, "kind");
            if (kind == L"apps") e.kind = PinKind::AppsButton;
            else if (kind == L"stack") e.kind = PinKind::Stack;
            e.appId = readString(p, "appId");
            e.launch = readString(p, "launch");
            e.name = readString(p, "name");
            e.exePath = readString(p, "exePath");
            if (e.kind == PinKind::Stack) {
                std::wstring view = readString(p, "view"), sort = readString(p, "sort");
                if (view == L"fan") e.stackView = StackView::Fan;
                else if (view == L"grid") e.stackView = StackView::Grid;
                if (sort == L"name") e.stackSort = StackSort::Name;
                else if (sort == L"modified") e.stackSort = StackSort::Modified;
                else if (sort == L"kind") e.stackSort = StackSort::Kind;
            }
            if (e.kind == PinKind::App && e.appId.empty()) continue;
            if (e.kind == PinKind::Stack && e.launch.empty()) continue;
            s.pinned.push_back(std::move(e));
        }
    }
    return s;
}

json::Value settingsToJson(const Settings& s) {
    json::Value v = json::Object{};
    v.set("version", kSettingsVersion);
    v.set("position", positionName(s.position));
    v.set("autohide", s.autohide);
    v.set("magnification", s.magnification);
    v.set("showRecents", s.showRecents);
    v.set("tahoeStrictIcons", s.tahoeStrictIcons);
    v.set("tileSize", s.tileSize);
    v.set("largeSize", s.largeSize);
    v.set("glass", s.glass);
    v.set("font", toUtf8(s.font));
    if (!s.screen.empty()) v.set("screen", toUtf8(s.screen));
    v.set("pinnedInitialized", s.pinnedInitialized);
    json::Value pins = json::Array{};
    for (auto& p : s.pinned) {
        json::Value e = json::Object{};
        e.set("kind", kindName(p.kind));
        e.set("appId", toUtf8(p.appId));
        e.set("launch", toUtf8(p.launch));
        e.set("name", toUtf8(p.name));
        if (!p.exePath.empty()) e.set("exePath", toUtf8(p.exePath));
        if (p.kind == PinKind::Stack) {
            e.set("view", viewName(p.stackView));
            e.set("sort", sortName(p.stackSort));
        }
        pins.push(std::move(e));
    }
    v.set("pinned", std::move(pins));
    return v;
}

json::Value migrateSettingsJson(const json::Value& v) {
    auto* ver = v.find("version");
    if (!v.isObject() || (ver && ver->asNumber(1) >= kSettingsVersion)) return v;
    json::Value out = v;
    if (auto* large = v.find("largeSize"); large && large->isNumber() && large->asNumber(0) == 128)
        out.set("largeSize", 80);   // ancien défaut du plan 1, jugé trop gros
    out.set("version", kSettingsVersion);
    return out;
}

} // namespace md
