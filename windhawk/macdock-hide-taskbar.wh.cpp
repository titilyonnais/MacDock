// ==WindhawkMod==
// @id              macdock-hide-taskbar
// @name            MacDock - Hide Taskbar
// @description     Hides the Windows taskbar while MacDock is running, and restores it automatically if the Dock stops
// @version         1.2.0
// @author          MacDock
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -luser32 -lshell32 -lgdi32
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
- Notification area icons of your apps are relayed to the MacMenuBar menu bar over the
  `\\.\pipe\MacMenuBar` named pipe, so they appear in the menu bar like macOS menu extras.
  Explorer keeps handling them as usual: the mod only observes.
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

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

// --- Protocole : doit rester identique à src/ipc/protocol.h (version 1) ---
namespace proto {
constexpr uint32_t kMagic = 0x4B43444D;  // "MDCK"
constexpr uint16_t kVersion = 1;
constexpr uint32_t kMaxPayload = 64 * 1024;
constexpr size_t kHeaderSize = 12;
constexpr uint16_t kHeartbeat = 1;
constexpr uint16_t kGoodbye = 5;
constexpr uint16_t kTrayUpdate = 6;
constexpr uint16_t kTrayRemove = 7;
}  // namespace proto

namespace {

constexpr wchar_t kPipeName[] = L"\\\\.\\pipe\\MacDock";
constexpr wchar_t kBarPipeName[] = L"\\\\.\\pipe\\MacMenuBar";

// --- Zone de notification : messages WM_COPYDATA reçus par Shell_TrayWnd (fonctions pures, testées) ---

constexpr DWORD kTraySignature = 0x34753423;
constexpr int kTrayIconPx = 32;
constexpr size_t kTrayTipMax = 127;

// NOTIFYICONDATAW tel qu'explorer le reçoit : handles sur 32 bits, quelle que soit l'architecture de l'app.
struct NotifyIconData32 {
    DWORD cbSize;
    DWORD hWnd;
    UINT uID;
    UINT uFlags;
    UINT uCallbackMessage;
    DWORD hIcon;
    WCHAR szTip[128];
    DWORD dwState;
    DWORD dwStateMask;
    WCHAR szInfo[256];
    UINT uVersion;   // union avec uTimeout
    WCHAR szInfoTitle[64];
    DWORD dwInfoFlags;
    GUID guidItem;
    DWORD hBalloonIcon;
};

struct TrayCopyData {
    DWORD signature;
    DWORD message;   // NIM_*
    NotifyIconData32 nid;
};

struct TrayRecord {
    uint64_t hwnd = 0;
    uint32_t uid = 0, callback = 0, version = 0, flags = 0;
    bool hidden = false, hiddenSet = false;   // hiddenSet : NIF_STATE avec NIS_HIDDEN dans le masque
    std::wstring tip;
    uint8_t guid[16] = {};
    uint32_t hicon = 0;   // handle 32 bits du message, valable pendant son traitement seulement
};

enum class TrayOp { None, Add, Modify, Delete, SetVersion };

// Jamais d'exception ni de lecture hors du tampon : ce code tourne dans explorer.
TrayOp parseTrayCopyData(const COPYDATASTRUCT* cds, TrayRecord& out) {
    constexpr size_t kMin = offsetof(TrayCopyData, nid) + offsetof(NotifyIconData32, szTip);
    if (!cds || cds->dwData != 1 || !cds->lpData || cds->cbData < kMin) return TrayOp::None;
    TrayCopyData d{};   // un NOTIFYICONDATA court laisse les champs suivants à zéro
    memcpy(&d, cds->lpData, std::min<size_t>(cds->cbData, sizeof d));
    if (d.signature != kTraySignature) return TrayOp::None;
    const NotifyIconData32& n = d.nid;
    out = TrayRecord{};
    out.hwnd = uint64_t(uintptr_t(HWND(LONG_PTR(LONG(n.hWnd)))));   // handle 32 bits étendu avec son signe
    out.uid = n.uID;
    out.flags = n.uFlags;
    out.callback = n.uCallbackMessage;
    out.hicon = n.hIcon;
    size_t len = 0;
    while (len < kTrayTipMax && n.szTip[len]) ++len;
    out.tip.assign(n.szTip, len);
    if ((n.uFlags & NIF_STATE) && (n.dwStateMask & NIS_HIDDEN)) {
        out.hiddenSet = true;
        out.hidden = (n.dwState & NIS_HIDDEN) != 0;
    }
    if (n.uFlags & NIF_GUID) memcpy(out.guid, &n.guidItem, 16);
    out.version = n.uVersion;
    switch (d.message) {
        case NIM_ADD: return TrayOp::Add;
        case NIM_MODIFY: return TrayOp::Modify;
        case NIM_DELETE: return TrayOp::Delete;
        case NIM_SETVERSION: return TrayOp::SetVersion;
        default: return TrayOp::None;
    }
}

struct TrayEntry {
    TrayRecord r;
    std::vector<uint8_t> bgra;   // kTrayIconPx x kTrayIconPx, prémultiplié (vide : pas d'icône)
};

struct TrayChange {
    bool changed = false, removed = false;
    TrayRecord key;
};

bool hasGuid(const TrayRecord& r) {
    for (uint8_t b : r.guid)
        if (b) return true;
    return false;
}

bool sameTray(const TrayRecord& a, const TrayRecord& b) {
    if (hasGuid(a) || hasGuid(b)) return memcmp(a.guid, b.guid, 16) == 0;
    return a.hwnd == b.hwnd && a.uid == b.uid;
}

// Applique un message à la liste. render(hicon) lit l'icône pendant que le message est traité (l'app peut la
// détruire juste après).
TrayChange mergeTray(std::vector<TrayEntry>& list, TrayOp op, const TrayRecord& in,
                     const std::function<std::vector<uint8_t>(uint32_t)>& render) {
    TrayChange ch;
    ch.key = in;
    auto it = std::find_if(list.begin(), list.end(), [&](const TrayEntry& e) { return sameTray(e.r, in); });
    if (op == TrayOp::Delete) {
        if (it == list.end()) return ch;
        ch.key = it->r;
        list.erase(it);
        ch.changed = ch.removed = true;
        return ch;
    }
    if (op == TrayOp::Add && it == list.end()) {
        TrayEntry e;
        e.r = in;
        e.r.version = 0;
        if (in.flags & NIF_ICON) e.bgra = render(in.hicon);
        list.push_back(std::move(e));
        ch.changed = true;
        return ch;
    }
    if (it == list.end()) return ch;   // modification d'une icône inconnue
    TrayRecord& r = it->r;
    if (op == TrayOp::SetVersion) {
        r.version = in.version;
    } else {   // NIM_MODIFY, ou NIM_ADD d'une icône déjà connue : seuls les champs annoncés changent
        if (in.flags & NIF_MESSAGE) r.callback = in.callback;
        if (in.flags & NIF_ICON) it->bgra = render(in.hicon);
        if (in.flags & NIF_TIP) r.tip = in.tip;
        if (in.hiddenSet) r.hidden = in.hidden;
        r.flags |= in.flags & (NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_GUID);
    }
    ch.key = r;
    ch.changed = true;
    return ch;
}

template <class T>
void putLE(std::vector<uint8_t>& out, T v) {
    uint8_t bytes[sizeof(T)];
    memcpy(bytes, &v, sizeof(T));
    out.insert(out.end(), bytes, bytes + sizeof(T));
}

std::vector<uint8_t> frame(uint16_t type, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> out;
    putLE(out, proto::kMagic);
    putLE(out, proto::kVersion);
    putLE(out, type);
    putLE(out, uint32_t(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

// Même charge utile que md::ipc::makeTrayUpdate.
std::vector<uint8_t> encodeTrayUpdate(const TrayEntry& e) {
    const TrayRecord& r = e.r;
    std::vector<uint8_t> p;
    putLE(p, r.hwnd);
    putLE(p, r.uid);
    putLE(p, r.callback);
    putLE(p, r.version);
    putLE(p, r.flags);
    putLE(p, uint8_t(r.hidden ? 1 : 0));
    p.insert(p.end(), r.guid, r.guid + 16);
    const size_t n = std::min(r.tip.size(), kTrayTipMax);
    putLE(p, uint16_t(n));
    for (size_t i = 0; i < n; ++i) putLE(p, uint16_t(r.tip[i]));
    const bool image = e.bgra.size() == size_t(kTrayIconPx) * kTrayIconPx * 4;
    putLE(p, uint16_t(image ? kTrayIconPx : 0));
    putLE(p, uint16_t(image ? kTrayIconPx : 0));
    if (image) p.insert(p.end(), e.bgra.begin(), e.bgra.end());
    return frame(proto::kTrayUpdate, p);
}

std::vector<uint8_t> encodeTrayRemove(const TrayRecord& r) {
    std::vector<uint8_t> p;
    putLE(p, r.hwnd);
    putLE(p, r.uid);
    p.insert(p.end(), r.guid, r.guid + 16);
    return frame(proto::kTrayRemove, p);
}

// Icône en BGRA prémultiplié px x px (vide si l'icône est invalide). Les icônes sans alpha prennent leur masque.
std::vector<uint8_t> renderIcon(HICON icon, int px) {
    std::vector<uint8_t> out;
    if (!icon) return out;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = px;
    bi.bmiHeader.biHeight = -px;   // de haut en bas
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC dc = CreateCompatibleDC(nullptr);
    void* bits = nullptr;
    HBITMAP bmp = dc ? CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
    if (bmp && bits) {
        HGDIOBJ old = SelectObject(dc, bmp);
        const size_t n = size_t(px) * px * 4;
        memset(bits, 0, n);
        if (DrawIconEx(dc, 0, 0, icon, px, px, 0, nullptr, DI_NORMAL)) {
            GdiFlush();
            const uint8_t* b = static_cast<const uint8_t*>(bits);
            out.assign(b, b + n);
            bool alpha = false;
            for (size_t i = 3; i < n && !alpha; i += 4) alpha = out[i] != 0;
            if (!alpha) {   // icône à masque : opaque là où le masque est noir
                memset(bits, 0, n);
                DrawIconEx(dc, 0, 0, icon, px, px, 0, nullptr, DI_MASK);
                GdiFlush();
                for (size_t i = 0; i < n; i += 4) {
                    const bool transparent = b[i] > 0x7F;
                    if (transparent) out[i] = out[i + 1] = out[i + 2] = out[i + 3] = 0;
                    else out[i + 3] = 0xFF;
                }
            }
        }
        SelectObject(dc, old);
    }
    if (bmp) DeleteObject(bmp);
    if (dc) DeleteDC(dc);
    return out;
}

std::atomic<bool> g_dockAlive{false};
// Vrai seulement dans le processus explorer qui porte la barre des tâches (pas les explorer secondaires).
std::atomic<bool> g_isShell{false};
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
    if (g_isShell && g_dockAlive && cmd != SW_HIDE && isTrayWindow(hwnd)) cmd = SW_HIDE;
    return ShowWindow_orig(hwnd, cmd);
}

BOOL WINAPI SetWindowPos_hook(HWND hwnd, HWND after, int x, int y, int cx, int cy, UINT flags) {
    if (g_isShell && g_dockAlive && (flags & SWP_SHOWWINDOW) && isTrayWindow(hwnd)) {
        flags &= ~SWP_SHOWWINDOW;
        flags |= SWP_HIDEWINDOW;
    }
    return SetWindowPos_orig(hwnd, after, x, y, cx, cy, flags);
}

bool ownedByThisProcess(HWND h) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    return pid == GetCurrentProcessId();
}

// Barres des tâches de CE processus uniquement.
template <class F>
void forEachTrayWindow(F f) {
    HWND main = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (main && ownedByThisProcess(main)) f(main);
    HWND h = nullptr;
    while ((h = FindWindowExW(nullptr, h, L"Shell_SecondaryTrayWnd", nullptr)) != nullptr)
        if (ownedByThisProcess(h)) f(h);
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
        // On n'oublie l'état d'origine qu'une fois sa restauration vérifiée (sinon : nouvel essai plus tard).
        if ((appBarState() & ABS_AUTOHIDE) == (UINT(original) & ABS_AUTOHIDE)) Wh_DeleteValue(L"originalState");
        else Wh_Log(L"Restauration du masquage automatique non confirmée : nouvel essai");
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

// Attend que la barre des tâches existe. Retourne false si elle appartient à un autre processus
// (explorer secondaire : le mod n'y fait rien) ou si le mod s'arrête.
bool waitForOwnTaskbar() {
    for (;;) {
        HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
        if (tray) return ownedByThisProcess(tray);
        if (WaitForSingleObject(g_stopEvent, 500) == WAIT_OBJECT_0) return false;
    }
}

// --- Relais vers la barre de menus ---
// Le crochet (fil de la barre des tâches) met la liste à jour et note les changements ; le fil du pipe les envoie.

CRITICAL_SECTION g_trayLock;
std::vector<TrayEntry> g_tray;
std::vector<TrayChange> g_trayQueue;   // à envoyer ; vidée à chaque connexion (la liste entière part alors)
HANDLE g_trayEvent = nullptr;          // changements en attente
HHOOK g_trayHook = nullptr;
HANDLE g_barThread = nullptr;
std::atomic<int> g_hookBusy{0};   // appels du crochet en cours : attendus avant de décharger le mod
constexpr size_t kTrayQueueMax = 512;

bool isMainTray(HWND hwnd) {
    wchar_t cls[32];
    return hwnd && GetClassNameW(hwnd, cls, 32) && wcscmp(cls, L"Shell_TrayWnd") == 0;
}

void onTrayCopyData(const COPYDATASTRUCT* cds) {
    TrayRecord rec;
    const TrayOp op = parseTrayCopyData(cds, rec);
    if (op == TrayOp::None) return;
    EnterCriticalSection(&g_trayLock);
    try {
        TrayChange ch = mergeTray(g_tray, op, rec, [](uint32_t h) {
            return renderIcon(HICON(LONG_PTR(LONG(h))), kTrayIconPx);
        });
        if (ch.changed) {
            if (g_trayQueue.size() >= kTrayQueueMax) g_trayQueue.clear();   // barre absente : la connexion renverra tout
            g_trayQueue.push_back(std::move(ch));
        }
    } catch (...) {   // mémoire épuisée : on n'emporte pas explorer
    }
    LeaveCriticalSection(&g_trayLock);
    if (g_trayEvent) SetEvent(g_trayEvent);
}

LRESULT CALLBACK trayHookProc(int code, WPARAM wp, LPARAM lp) {
    ++g_hookBusy;
    if (code == HC_ACTION && lp && g_trayHook) {
        const auto* c = reinterpret_cast<const CWPSTRUCT*>(lp);
        if (c->message == WM_COPYDATA && isMainTray(c->hwnd)) onTrayCopyData(reinterpret_cast<const COPYDATASTRUCT*>(c->lParam));
    }
    const LRESULT r = CallNextHookEx(nullptr, code, wp, lp);
    --g_hookBusy;
    return r;
}

// Trames à envoyer : toute la liste (connexion), sinon les changements notés.
std::vector<std::vector<uint8_t>> takeTrayFrames(bool all) {
    std::vector<std::vector<uint8_t>> out;
    EnterCriticalSection(&g_trayLock);
    try {
        if (all) {
            std::erase_if(g_tray, [](const TrayEntry& e) { return !IsWindow(HWND(uintptr_t(e.r.hwnd))); });
            for (const auto& e : g_tray) out.push_back(encodeTrayUpdate(e));
        } else {
            for (const auto& ch : g_trayQueue) {
                if (ch.removed) {
                    out.push_back(encodeTrayRemove(ch.key));
                    continue;
                }
                auto it = std::find_if(g_tray.begin(), g_tray.end(), [&](const TrayEntry& e) { return sameTray(e.r, ch.key); });
                if (it != g_tray.end()) out.push_back(encodeTrayUpdate(*it));
            }
        }
        g_trayQueue.clear();
    } catch (...) {
        out.clear();
    }
    LeaveCriticalSection(&g_trayLock);
    return out;
}

bool writeFrames(HANDLE pipe, HANDLE writeEvent, const std::vector<std::vector<uint8_t>>& frames) {
    for (const auto& f : frames) {
        OVERLAPPED ov{};
        ov.hEvent = writeEvent;
        ResetEvent(writeEvent);
        if (!WriteFile(pipe, f.data(), DWORD(f.size()), nullptr, &ov) && GetLastError() != ERROR_IO_PENDING) return false;
        HANDLE waits[2] = {writeEvent, g_stopEvent};
        if (WaitForMultipleObjects(2, waits, FALSE, 3000) != WAIT_OBJECT_0) {   // barre figée ou arrêt
            CancelIoEx(pipe, &ov);
            DWORD dummy;
            GetOverlappedResult(pipe, &ov, &dummy, TRUE);
            return false;
        }
        DWORD done = 0;
        if (!GetOverlappedResult(pipe, &ov, &done, FALSE) || done != f.size()) return false;
    }
    return true;
}

// Flux de la barre : battements de cœur ignorés ; false à l'au revoir ou sur un flux invalide.
bool barFramesOk(std::vector<uint8_t>& buf) {
    while (buf.size() >= proto::kHeaderSize) {
        uint32_t magic, len;
        uint16_t version, type;
        memcpy(&magic, buf.data(), 4);
        memcpy(&version, buf.data() + 4, 2);
        memcpy(&type, buf.data() + 6, 2);
        memcpy(&len, buf.data() + 8, 4);
        if (magic != proto::kMagic || version != proto::kVersion || len > proto::kMaxPayload) return false;
        if (buf.size() < proto::kHeaderSize + len) return true;
        buf.erase(buf.begin(), buf.begin() + std::ptrdiff_t(proto::kHeaderSize + len));
        if (type == proto::kGoodbye) return false;
    }
    return true;
}

bool installTrayHook() {
    HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!tray || !ownedByThisProcess(tray)) return false;
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&trayHookProc), &self);
    g_trayHook = SetWindowsHookExW(WH_CALLWNDPROC, trayHookProc, self, GetWindowThreadProcessId(tray, nullptr));
    if (!g_trayHook) {
        Wh_Log(L"Crochet de la zone de notification impossible (%lu)", GetLastError());
        return false;
    }
    // Les apps déjà lancées redéclarent leurs icônes, comme après un redémarrage d'explorer.
    PostMessageW(HWND_BROADCAST, RegisterWindowMessageW(L"TaskbarCreated"), 0, 0);
    Wh_Log(L"Zone de notification relayée vers la barre de menus");
    return true;
}

DWORD WINAPI barPipeThread(LPVOID) {
    if (!waitForOwnTaskbar() || !installTrayHook()) return 0;
    HANDLE readEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE writeEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    while (WaitForSingleObject(g_stopEvent, 0) != WAIT_OBJECT_0) {
        HANDLE pipe = CreateFileW(kBarPipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                  FILE_FLAG_OVERLAPPED, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) {   // barre absente : nouvel essai dans 2 s
            if (WaitForSingleObject(g_stopEvent, 2000) == WAIT_OBJECT_0) break;
            continue;
        }
        bool ok = writeFrames(pipe, writeEvent, takeTrayFrames(true));
        std::vector<uint8_t> frames;
        uint8_t buf[512];
        while (ok) {
            OVERLAPPED ov{};
            ov.hEvent = readEvent;
            ResetEvent(readEvent);
            if (!ReadFile(pipe, buf, sizeof buf, nullptr, &ov) && GetLastError() != ERROR_IO_PENDING) break;
            bool readDone = false;
            while (ok && !readDone) {
                HANDLE waits[3] = {readEvent, g_trayEvent, g_stopEvent};
                const DWORD r = WaitForMultipleObjects(3, waits, FALSE, INFINITE);
                if (r == WAIT_OBJECT_0) readDone = true;
                else if (r == WAIT_OBJECT_0 + 1) ok = writeFrames(pipe, writeEvent, takeTrayFrames(false));
                else ok = false;
            }
            if (!readDone) {
                CancelIoEx(pipe, &ov);
                DWORD dummy;
                GetOverlappedResult(pipe, &ov, &dummy, TRUE);
                break;
            }
            DWORD got = 0;
            if (!GetOverlappedResult(pipe, &ov, &got, FALSE)) break;
            frames.insert(frames.end(), buf, buf + got);
            ok = barFramesOk(frames);
        }
        CloseHandle(pipe);
    }
    CloseHandle(readEvent);
    CloseHandle(writeEvent);
    return 0;
}

DWORD WINAPI pipeThread(LPVOID) {
    if (!waitForOwnTaskbar()) {
        Wh_Log(L"Processus explorer secondaire : le mod reste inactif");
        return 0;
    }
    g_isShell = true;
    // Un état d'origine resté en mémoire : explorer ou la session s'est arrêté pendant que la barre était cachée.
    if (Wh_GetIntValue(L"originalState", -1) >= 0) restoreTaskbar();

    HANDLE readEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    while (WaitForSingleObject(g_stopEvent, 0) != WAIT_OBJECT_0) {
        HANDLE pipe = CreateFileW(kPipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                  FILE_FLAG_OVERLAPPED, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) {
            setDockAlive(false);
            // Restauration précédente non confirmée : on réessaie tant que le Dock est absent.
            if (Wh_GetIntValue(L"originalState", -1) >= 0) restoreTaskbar();
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
    InitializeCriticalSection(&g_trayLock);
    loadSettings();
    // La restauration éventuelle se fait dans le thread du pipe, une fois la barre de CE processus créée.
    Wh_SetFunctionHook((void*)ShowWindow, (void*)ShowWindow_hook, (void**)&ShowWindow_orig);
    Wh_SetFunctionHook((void*)SetWindowPos, (void*)SetWindowPos_hook, (void**)&SetWindowPos_orig);
    return TRUE;
}

void Wh_ModAfterInit() {
    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_thread = CreateThread(nullptr, 0, pipeThread, nullptr, 0, nullptr);
    g_trayEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_barThread = CreateThread(nullptr, 0, barPipeThread, nullptr, 0, nullptr);
}

void Wh_ModBeforeUninit() {
    if (g_trayHook) {
        HHOOK hook = g_trayHook;
        g_trayHook = nullptr;
        UnhookWindowsHookEx(hook);
        for (int i = 0; i < 100 && g_hookBusy > 0; ++i) Sleep(10);   // un message en cours sur le fil de la barre
    }
    if (g_stopEvent) SetEvent(g_stopEvent);
    if (g_barThread) {
        WaitForSingleObject(g_barThread, 5000);
        CloseHandle(g_barThread);
        g_barThread = nullptr;
    }
    if (g_thread) {
        WaitForSingleObject(g_thread, 5000);
        CloseHandle(g_thread);
        g_thread = nullptr;
    }
    if (g_isShell) {
        setDockAlive(false);
        if (Wh_GetIntValue(L"originalState", -1) >= 0) restoreTaskbar();
    }
}

void Wh_ModUninit() {
    if (g_stopEvent) {
        CloseHandle(g_stopEvent);
        g_stopEvent = nullptr;
    }
    if (g_trayEvent) {
        CloseHandle(g_trayEvent);
        g_trayEvent = nullptr;
    }
    DeleteCriticalSection(&g_trayLock);
    DeleteCriticalSection(&g_stateLock);
}

void Wh_ModSettingsChanged() {
    loadSettings();
}
