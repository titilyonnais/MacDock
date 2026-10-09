#include "screens.h"

#include <windows.h>

namespace md {

std::vector<std::wstring> screenLabels(const std::vector<ScreenChoice>& screens) {
    std::vector<std::wstring> out;
    std::map<std::wstring, int> seen;
    for (std::size_t i = 0; i < screens.size(); ++i) {
        const ScreenChoice& s = screens[i];
        std::wstring label = s.name.empty() ? L"Écran " + std::to_wstring(i + 1) : s.name;
        if (!s.name.empty()) {
            int same = 0;
            for (const ScreenChoice& o : screens) same += o.name == s.name;
            if (same > 1) label += L" (" + std::to_wstring(++seen[s.name]) + L")";
        }
        if (s.width > 0 && s.height > 0) label += L" — " + std::to_wstring(s.width) + L" × " + std::to_wstring(s.height);
        if (s.primary) label += L" (principal)";
        out.push_back(std::move(label));
    }
    return out;
}

std::map<std::wstring, std::wstring> monitorNames() {
    std::map<std::wstring, std::wstring> out;
    UINT32 pathCount = 0, modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) return out;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) != ERROR_SUCCESS)
        return out;
    paths.resize(pathCount);
    for (const DISPLAYCONFIG_PATH_INFO& p : paths) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        source.header.size = sizeof source;
        source.header.adapterId = p.sourceInfo.adapterId;
        source.header.id = p.sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS) continue;
        DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
        target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        target.header.size = sizeof target;
        target.header.adapterId = p.targetInfo.adapterId;
        target.header.id = p.targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&target.header) != ERROR_SUCCESS) continue;
        const auto tech = p.targetInfo.outputTechnology;
        const bool builtIn = tech == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL ||
                             tech == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED ||
                             tech == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EMBEDDED;
        const std::wstring name = builtIn ? L"Écran intégré" : std::wstring(target.monitorFriendlyDeviceName);
        if (!name.empty()) out.emplace(source.viewGdiDeviceName, name);   // écrans dupliqués : le premier
    }
    return out;
}

} // namespace md
