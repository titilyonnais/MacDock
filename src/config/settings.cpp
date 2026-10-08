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

constexpr const char* kCornerKeys[4] = {"topLeft", "topRight", "bottomLeft", "bottomRight"};   // ordre de Corner

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
        case StackView::List: return "list";
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
    const std::wstring effect = readString(v, "minimizeEffect");
    if (effect == L"scale") s.minimizeEffect = MinimizeEffect::Scale;
    else if (effect == L"windows") s.minimizeEffect = MinimizeEffect::Windows;
    s.font = readString(v, "font");
    s.screen = readString(v, "screen");
    const std::wstring hotkey = toLower(readString(v, "spotlightHotkey"));
    if (hotkey == L"alt+space" || hotkey == L"ctrl+space" || hotkey == L"off") s.spotlightHotkey = hotkey;
    const std::wstring mission = toLower(readString(v, "missionControlHotkey"));
    if (mission == L"ctrl+alt+up" || mission == L"ctrl+up" || mission == L"f3" || mission == L"off")
        s.missionControlHotkey = mission;
    const std::wstring switcher = toLower(readString(v, "appSwitcherHotkey"));
    if (switcher == L"alt+tab" || switcher == L"off") s.appSwitcherHotkey = switcher;
    const std::wstring expose = toLower(readString(v, "appExposeHotkey"));
    if (expose == L"ctrl+alt+down" || expose == L"ctrl+down" || expose == L"off") s.appExposeHotkey = expose;
    s.screenshots = readBool(v, "screenshots", s.screenshots);
    s.sounds = readBool(v, "sounds", s.sounds);
    if (auto* corners = v.find("hotCorners"); corners && corners->isObject())
        for (int c = 0; c < 4; ++c)   // valeur inconnue ou mauvais type : le coin garde son défaut
            if (auto a = parseHotCornerAction(readString(*corners, kCornerKeys[c]))) s.hotCorners[std::size_t(c)] = *a;
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
                else if (view == L"list") e.stackView = StackView::List;
                if (sort == L"name") e.stackSort = StackSort::Name;
                else if (sort == L"modified") e.stackSort = StackSort::Modified;
                else if (sort == L"kind") e.stackSort = StackSort::Kind;
                if (readString(p, "display") == L"folder") e.stackDisplay = StackDisplay::Folder;
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
    v.set("minimizeEffect", s.minimizeEffect == MinimizeEffect::Scale     ? "scale"
                            : s.minimizeEffect == MinimizeEffect::Windows ? "windows"
                                                                          : "genie");
    v.set("font", toUtf8(s.font));
    if (!s.screen.empty()) v.set("screen", toUtf8(s.screen));
    v.set("spotlightHotkey", toUtf8(s.spotlightHotkey));
    v.set("missionControlHotkey", toUtf8(s.missionControlHotkey));
    v.set("appSwitcherHotkey", toUtf8(s.appSwitcherHotkey));
    v.set("appExposeHotkey", toUtf8(s.appExposeHotkey));
    v.set("screenshots", s.screenshots);
    v.set("sounds", s.sounds);
    json::Value corners = json::Object{};
    for (int c = 0; c < 4; ++c) corners.set(kCornerKeys[c], toUtf8(hotCornerName(s.hotCorners[std::size_t(c)])));
    v.set("hotCorners", corners);
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
            e.set("display", p.stackDisplay == StackDisplay::Folder ? "folder" : "stack");
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
