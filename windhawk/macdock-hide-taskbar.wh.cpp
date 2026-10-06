// ==WindhawkMod==
// @id              macdock-hide-taskbar
// @name            MacDock - Hide Taskbar
// @description     Hides the Windows taskbar while MacDock is running, and restores it automatically if the Dock stops
// @version         1.0.0
// @author          MacDock
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -luser32 -lshell32
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# MacDock - Hide Taskbar

Companion mod of **MacDock** (a macOS-style Dock for Windows).

- While `MacDock.exe` is running, the Windows taskbar is hidden on every monitor and its
  reserved screen area is released.
- The Dock sends a heartbeat every second over the `\\.\pipe\MacDock` named pipe.
  If no heartbeat arrives for `restoreDelaySeconds`, the taskbar comes back automatically.
- Disabling this mod restores the taskbar immediately.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- restoreDelaySeconds: 5
  $name: Restore delay (seconds)
  $description: How long without a heartbeat from MacDock before the taskbar is shown again
*/
// ==/WindhawkModSettings==

#include <windows.h>
#include <shellapi.h>
#include <windhawk_api.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

// --- Protocole : doit rester identique à src/ipc/protocol.h (version 1) ---
namespace proto {
constexpr uint32_t kMagic = 0x4B43444D;  // "MDCK"
constexpr uint16_t kVersion = 1;
constexpr uint32_t kMaxPayload = 64 * 1024;
constexpr size_t kHeaderSize = 12;
constexpr uint16_t kHeartbeat = 1;
constexpr uint16_t kGoodbye = 5;
}  // namespace proto

namespace {

constexpr wchar_t kPipeName[] = L"\\\\.\\pipe\\MacDock";

std::atomic<bool> g_dockAlive{false};
std::atomic<int> g_restoreDelayMs{5000};
HANDLE g_stopEvent = nullptr;
HANDLE g_thread = nullptr;
CRITICAL_SECTION g_stateLock;

using ShowWindow_t = BOOL(WINAPI*)(HWND, int);
using SetWindowPos_t = BOOL(WINAPI*)(HWND, HWND, int, int, int, int, UINT);
ShowWindow_t ShowWindow_orig = nullptr;
SetWindowPos_t SetWindowPos_orig = nullptr;

bool isTrayWindow(HWND hwnd) {
    wchar_t cls[32];
    if (!hwnd || !GetClassNameW(hwnd, cls, 32)) return false;
    return wcscmp(cls, L"Shell_TrayWnd") == 0 || wcscmp(cls, L"Shell_SecondaryTrayWnd") == 0;
}

BOOL WINAPI ShowWindow_hook(HWND hwnd, int cmd) {
    if (g_dockAlive && cmd != SW_HIDE && isTrayWindow(hwnd)) cmd = SW_HIDE;
    return ShowWindow_orig(hwnd, cmd);
}

BOOL WINAPI SetWindowPos_hook(HWND hwnd, HWND after, int x, int y, int cx, int cy, UINT flags) {
    if (g_dockAlive && (flags & SWP_SHOWWINDOW) && isTrayWindow(hwnd)) {
        flags &= ~SWP_SHOWWINDOW;
        flags |= SWP_HIDEWINDOW;
    }
    return SetWindowPos_orig(hwnd, after, x, y, cx, cy, flags);
}

template <class F>
void forEachTrayWindow(F f) {
    if (HWND main = FindWindowW(L"Shell_TrayWnd", nullptr)) f(main);
    HWND h = nullptr;
    while ((h = FindWindowExW(nullptr, h, L"Shell_SecondaryTrayWnd", nullptr)) != nullptr) f(h);
}

UINT appBarState() {
    APPBARDATA abd{};
    abd.cbSize = sizeof abd;
    return UINT(SHAppBarMessage(ABM_GETSTATE, &abd));
}

void setAppBarState(UINT state) {
    APPBARDATA abd{};
    abd.cbSize = sizeof abd;
    abd.hWnd = FindWindowW(L"Shell_TrayWnd", nullptr);
    abd.lParam = state;
    SHAppBarMessage(ABM_SETSTATE, &abd);
}

// L'état d'origine est mémorisé dans le stockage Windhawk pour survivre à un plantage d'explorer.
void hideTaskbar() {
    if (Wh_GetIntValue(L"originalState", -1) < 0) Wh_SetIntValue(L"originalState", int(appBarState()));
    UINT original = UINT(Wh_GetIntValue(L"originalState", 0));
    setAppBarState(ABS_AUTOHIDE | (original & ABS_ALWAYSONTOP));
    forEachTrayWindow([](HWND h) { ShowWindowAsync(h, SW_HIDE); });
    Wh_Log(L"Barre des tâches cachée");
}

void restoreTaskbar() {
    int original = Wh_GetIntValue(L"originalState", -1);
    if (original >= 0) {
        setAppBarState(UINT(original));
        Wh_DeleteValue(L"originalState");
    }
    forEachTrayWindow([](HWND h) { ShowWindowAsync(h, SW_SHOWNA); });
    Wh_Log(L"Barre des tâches rétablie");
}

void setDockAlive(bool alive) {
    EnterCriticalSection(&g_stateLock);
    if (g_dockAlive.exchange(alive) != alive) {
        if (alive) hideTaskbar();
        else restoreTaskbar();
    } else if (alive) {
        // Explorer peut réafficher la barre (changement d'écran, etc.) : on la recache.
        forEachTrayWindow([](HWND h) {
            if (IsWindowVisible(h)) ShowWindowAsync(h, SW_HIDE);
        });
    }
    LeaveCriticalSection(&g_stateLock);
}

// Retourne false si le flux est invalide ou si le Dock a dit au revoir.
bool consumeFrames(std::vector<uint8_t>& buf) {
    while (buf.size() >= proto::kHeaderSize) {
        uint32_t magic, len;
        uint16_t version, type;
        memcpy(&magic, buf.data(), 4);
        memcpy(&version, buf.data() + 4, 2);
        memcpy(&type, buf.data() + 6, 2);
        memcpy(&len, buf.data() + 8, 4);
        if (magic != proto::kMagic || version != proto::kVersion || len > proto::kMaxPayload) {
            Wh_Log(L"Flux invalide (version %u)", version);
            return false;
        }
        if (buf.size() < proto::kHeaderSize + len) return true;
        buf.erase(buf.begin(), buf.begin() + proto::kHeaderSize + len);
        if (type == proto::kHeartbeat) setDockAlive(true);
        else if (type == proto::kGoodbye) return false;
    }
    return true;
}

DWORD WINAPI pipeThread(LPVOID) {
    HANDLE readEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    while (WaitForSingleObject(g_stopEvent, 0) != WAIT_OBJECT_0) {
        HANDLE pipe = CreateFileW(kPipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                  FILE_FLAG_OVERLAPPED, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) {
            setDockAlive(false);
            if (WaitForSingleObject(g_stopEvent, 1000) == WAIT_OBJECT_0) break;
            continue;
        }
        std::vector<uint8_t> frames;
        uint8_t buf[1024];
        for (;;) {
            OVERLAPPED ov{};
            ov.hEvent = readEvent;
            ResetEvent(readEvent);
            if (!ReadFile(pipe, buf, sizeof buf, nullptr, &ov) && GetLastError() != ERROR_IO_PENDING) {
                setDockAlive(false);
                break;
            }
            HANDLE waits[2] = {readEvent, g_stopEvent};
            DWORD r = WaitForMultipleObjects(2, waits, FALSE, DWORD(g_restoreDelayMs.load()));
            if (r != WAIT_OBJECT_0) {
                CancelIoEx(pipe, &ov);
                DWORD dummy;
                GetOverlappedResult(pipe, &ov, &dummy, TRUE);
                if (r == WAIT_TIMEOUT) {
                    Wh_Log(L"Aucun battement de cœur depuis %d ms", g_restoreDelayMs.load());
                    setDockAlive(false);
                }
                break;
            }
            DWORD got = 0;
            if (!GetOverlappedResult(pipe, &ov, &got, FALSE)) {
                setDockAlive(false);
                break;
            }
            frames.insert(frames.end(), buf, buf + got);
            if (!consumeFrames(frames)) {
                setDockAlive(false);
                break;
            }
        }
        CloseHandle(pipe);
    }
    CloseHandle(readEvent);
    return 0;
}

void loadSettings() {
    int seconds = Wh_GetIntSetting(L"restoreDelaySeconds");
    if (seconds < 2) seconds = 2;
    if (seconds > 60) seconds = 60;
    g_restoreDelayMs = seconds * 1000;
}

}  // namespace

BOOL Wh_ModInit() {
    InitializeCriticalSection(&g_stateLock);
    loadSettings();
    // Un état d'origine resté en mémoire signifie qu'explorer s'est arrêté pendant que la barre était cachée.
    if (Wh_GetIntValue(L"originalState", -1) >= 0) restoreTaskbar();
    Wh_SetFunctionHook((void*)ShowWindow, (void*)ShowWindow_hook, (void**)&ShowWindow_orig);
    Wh_SetFunctionHook((void*)SetWindowPos, (void*)SetWindowPos_hook, (void**)&SetWindowPos_orig);
    return TRUE;
}

void Wh_ModAfterInit() {
    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_thread = CreateThread(nullptr, 0, pipeThread, nullptr, 0, nullptr);
}

void Wh_ModBeforeUninit() {
    if (g_stopEvent) SetEvent(g_stopEvent);
    if (g_thread) {
        WaitForSingleObject(g_thread, 5000);
        CloseHandle(g_thread);
        g_thread = nullptr;
    }
    setDockAlive(false);
}

void Wh_ModUninit() {
    if (g_stopEvent) {
        CloseHandle(g_stopEvent);
        g_stopEvent = nullptr;
    }
    DeleteCriticalSection(&g_stateLock);
}

void Wh_ModSettingsChanged() {
    loadSettings();
}
