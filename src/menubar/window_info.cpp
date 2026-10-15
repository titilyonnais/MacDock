#include "window_info.h"

#include <dwmapi.h>
#include <shellscalingapi.h>

#include <cstdint>
#include <optional>

#include "../core/own_lights.h"

namespace md {

namespace {
// Processus d'intégrité plus élevée que la nôtre (ou illisible) : UIPI refuserait nos changements.
bool higherIntegrity(DWORD pid) {
    auto level = [](HANDLE process) -> std::optional<DWORD> {
        HANDLE token = nullptr;
        if (!OpenProcessToken(process, TOKEN_QUERY, &token)) return std::nullopt;
        std::uint8_t buf[64] = {};
        DWORD size = 0;
        std::optional<DWORD> out;
        if (GetTokenInformation(token, TokenIntegrityLevel, buf, sizeof buf, &size)) {
            auto* tml = reinterpret_cast<TOKEN_MANDATORY_LABEL*>(buf);
            out = *GetSidSubAuthority(tml->Label.Sid, *GetSidSubAuthorityCount(tml->Label.Sid) - 1);
        }
        CloseHandle(token);
        return out;
    };
    const auto mine = level(GetCurrentProcess());
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return true;
    const auto theirs = level(p);
    CloseHandle(p);
    if (!theirs) return true;   // jeton illisible : un processus élevé
    return mine && *theirs > *mine;
}
} // namespace

// DPI réel de l'écran de la fenêtre (une app non consciente du DPI répond 96 à GetDpiForWindow).
UINT effectiveDpi(HWND h) {
    UINT x = 96, y = 96;
    if (FAILED(GetDpiForMonitor(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), MDT_EFFECTIVE_DPI, &x, &y))) return 96;
    return x;
}

WindowInfo readInfo(HWND h) {
    WindowInfo w;
    w.style = LONG(GetWindowLongPtrW(h, GWL_STYLE));
    w.exStyle = LONG(GetWindowLongPtrW(h, GWL_EXSTYLE));
    w.classStyle = UINT(GetClassLongPtrW(h, GCL_STYLE));
    wchar_t cls[128] = {};
    GetClassNameW(h, cls, 128);
    w.className = cls;
    if (FAILED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &w.frame, sizeof w.frame))) GetWindowRect(h, &w.frame);
    RECT c{};
    GetClientRect(h, &c);
    POINT o{0, 0};
    ClientToScreen(h, &o);
    w.client = RECT{o.x, o.y, o.x + c.right, o.y + c.bottom};
    w.zoomed = IsZoomed(h) != FALSE;
    w.iconic = IsIconic(h) != FALSE;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    w.ownProcess = pid == GetCurrentProcessId();
    w.elevated = !w.ownProcess && higherIntegrity(pid);
    w.ownLights = drawsOwnLights(h);   // Réglages MacDock, Forcer à quitter : leur cadre à elles
    MONITORINFO mi{sizeof mi};
    if (GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi)) w.monitor = mi.rcMonitor;
    return w;
}

} // namespace md
