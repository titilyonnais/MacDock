#include "foreground_rules.h"

#include <string>

#include "../core/strings.h"

namespace md {

ForegroundKind classifyForeground(std::wstring_view className, std::wstring_view exeName, bool ownProcess) {
    if (ownProcess) return ForegroundKind::Ignore;
    const std::wstring exe = toLower(exeName);
    for (const wchar_t* ignored : {L"macdock.exe", L"macmenubar.exe", L"macdocklauncher.exe",
                                   L"startmenuexperiencehost.exe", L"searchhost.exe", L"searchapp.exe",
                                   L"shellexperiencehost.exe", L"shellhost.exe", L"lockapp.exe", L"textinputhost.exe"})
        if (exe == ignored) return ForegroundKind::Ignore;
    for (const wchar_t* ignored : {L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd", L"XamlExplorerHostIslandWindow",
                                   L"MultitaskingViewFrame", L"TaskSwitcherWnd", L"ForegroundStaging",
                                   L"NotifyIconOverflowWindow", L"TopLevelWindowForOverflowXamlIsland", L"#32768"})
        if (className == ignored) return ForegroundKind::Ignore;
    for (const wchar_t* explorer : {L"Progman", L"WorkerW", L"CabinetWClass", L"ExploreWClass"})
        if (className == explorer) return ForegroundKind::Explorer;
    return ForegroundKind::App;
}

DesktopFocus desktopFocus(const DesktopFocusContext& c) {
    if (c.clickedDesktop) return DesktopFocus::ShowExplorer;
    if (c.previousGone || c.previousMinimized) {
        if (c.otherWindowVisible) return DesktopFocus::ActivateNext;
        return c.previousMinimized ? DesktopFocus::KeepPrevious : DesktopFocus::ShowExplorer;
    }
    return DesktopFocus::ShowExplorer;
}

} // namespace md
