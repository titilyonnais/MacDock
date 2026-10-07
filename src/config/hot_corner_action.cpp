#include "hot_corner_action.h"

#include "../core/strings.h"

namespace md {

namespace {
struct Named {
    HotCornerAction action;
    const wchar_t* name;
};
constexpr Named kNames[] = {
    {HotCornerAction::Off, L"off"},
    {HotCornerAction::MissionControl, L"missionControl"},
    {HotCornerAction::Desktop, L"desktop"},
    {HotCornerAction::Apps, L"apps"},
    {HotCornerAction::NotificationCenter, L"notificationCenter"},
    {HotCornerAction::LockScreen, L"lockScreen"},
    {HotCornerAction::DisplaySleep, L"displaySleep"},
    {HotCornerAction::ScreenSaver, L"screenSaver"},
};
} // namespace

std::optional<HotCornerAction> parseHotCornerAction(const std::wstring& text) {
    const std::wstring t = toLower(text);
    for (const Named& n : kNames)
        if (t == toLower(n.name)) return n.action;
    return std::nullopt;
}

std::wstring hotCornerName(HotCornerAction action) {
    for (const Named& n : kNames)
        if (n.action == action) return n.name;
    return L"off";
}

} // namespace md
