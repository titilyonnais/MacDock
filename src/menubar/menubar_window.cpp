#include "menubar_window.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <shlobj.h>
#define SECURITY_WIN32
#include <security.h>

#include <algorithm>
#include <cmath>
#include <memory>

#include "../calib/png_io.h"
#include "../config/config_store.h"
#include "../core/diag.h"
#include "../sound/sound_play.h"
#include "../core/log.h"
#include "../core/strings.h"
#include "../shell/shell_actions.h"
#include "../tracker/app_identity.h"
#include "bar_color.h"
#include "bar_screens.h"
#include "clock_format.h"
#include "foreground_rules.h"
#include "shortcut.h"
#include "status_brightness.h"
#include "status_network.h"
#include "status_power.h"
#include "status_winrt.h"

namespace md {

MenuBarApp* MenuBarApp::self_ = nullptr;

namespace {

constexpr wchar_t kClassName[] = L"MacMenuBarWindow";   // fenêtre de contrôle (trouvée par le lanceur et --quit)
constexpr wchar_t kScreenClass[] = L"MacMenuBarScreen";  // une barre par écran
constexpr UINT WM_APP_APPBAR = WM_APP + 1;
constexpr UINT WM_APP_SAMPLE = WM_APP + 2;
constexpr UINT WM_APP_UIA_TITLES = WM_APP + 3;   // lParam : UiaTitles* (à libérer)
constexpr UINT WM_APP_STATUS = WM_APP + 4;       // lParam : StatusSnapshot* (à libérer)
constexpr UINT WM_APP_SCREENS = WM_APP + 7;      // DPI d'un écran changé : barres refaites
constexpr UINT WM_APP_TRAY = WM_APP + 6;         // lParam : ipc::Message* (à libérer) ; wParam 1 : connexion
constexpr UINT WM_APP_VOLUME = WM_APP + 5;       // Core Audio : wParam 1 = sortie par défaut changée
constexpr UINT WM_APP_ZOOM_MENU = WM_APP + 8;    // pastille verte survolée : wParam la fenêtre, lParam l'ancrage
constexpr int kBrightnessJob = 1;                // curseur de luminosité glissé : seule la dernière valeur part
constexpr DWORD kUiaItemsWaitMs = 2500;           // lecture d'un menu à son ouverture

struct UiaTitles {
    unsigned generation = 0;
    HWND window = nullptr;
    std::vector<RawMenuItem> titles;
};
constexpr UINT_PTR kClockTimer = 0x434C;        // "CL"
constexpr UINT_PTR kSampleTimeout = 0x5354;     // "ST" : pas d'image utilisable de la capture
constexpr UINT_PTR kShotRevealTimer = 0x5352;   // "SR" : sécurité, si le Dock ne rend jamais la main
constexpr UINT_PTR kResampleTimer = 0x5253;     // "RS" : diaporama de fonds d'écran
constexpr UINT_PTR kResampleSoon = 0x5253 + 1;  // après un changement de fond (transition de Windows)
constexpr UINT_PTR kConfigTimer = 0x4346;       // "CF"
constexpr UINT_PTR kFullscreenTimer = 0x4653;   // "FS"
constexpr UINT_PTR kRecentTimer = 0x5243;       // "RC" : écriture différée des apps récentes
constexpr UINT_PTR kVisibilityTimer = 0x5649;   // "VI"
constexpr UINT_PTR kTrayLayoutTimer = 0x544C;   // "TL" : rafale de messages du mod regroupée
constexpr UINT_PTR kTrayPruneTimer = 0x5450;    // "TP" : icônes d'apps fermées
constexpr UINT_PTR kHudTimer = 0x4855;          // "HU" : fondu de la pastille du volume et de la luminosité
// "FG" : Windows n'annonce pas toujours le premier plan (fenêtre active fermée, activation par un autre processus).
constexpr UINT_PTR kForegroundTimer = 0x4647;
constexpr UINT_PTR kDesktopFocusTimer = 0x4446;   // "DF" : bureau au premier plan sans clic, décidé une fois la fermeture finie
constexpr UINT kDesktopFocusDelayMs = 150;
// Touches de volume reprises (ctl_) ; Maj+Alt : pas fin, comme Maj+Option sur macOS.
constexpr int kHotVolUp = 1, kHotVolDown = 2, kHotMute = 3, kHotVolUpFine = 4, kHotVolDownFine = 5;

double hudNow() { return double(GetTickCount64()) / 1000.0; }
constexpr int kCmdBarSettings = 1, kCmdBarAutohide = 2, kCmdBarQuit = 3;

double nowSeconds() {
    static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return double(t.QuadPart) / double(freq.QuadPart);
}

WindowId toId(HWND h) { return static_cast<WindowId>(reinterpret_cast<std::uintptr_t>(h)); }
HWND toHwnd(WindowId id) { return reinterpret_cast<HWND>(static_cast<std::uintptr_t>(id)); }

std::wstring processExe(DWORD pid) {
    std::wstring out;
    if (HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        wchar_t path[MAX_PATH];
        DWORD n = MAX_PATH;
        if (QueryFullProcessImageNameW(p, 0, path, &n)) out.assign(path, n);
        CloseHandle(p);
    }
    return out;
}

std::wstring fileName(const std::wstring& path) {
    auto slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::wstring userDisplayName() {
    wchar_t name[256];
    ULONG n = 256;
    if (GetUserNameExW(NameDisplay, name, &n) && n > 1) return name;
    DWORD m = 256;
    if (GetUserNameW(name, &m)) return name;
    return {};
}

bool fileTime(const std::wstring& path, FILETIME& out) {
    WIN32_FILE_ATTRIBUTE_DATA d{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &d)) return false;
    out = d.ftLastWriteTime;
    return true;
}

} // namespace

bool MenuBarApp::systemDarkMode() {
    DWORD value = 1, size = sizeof value;
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return value == 0;
}

// ---- Réglages ----

void MenuBarApp::loadSettings(bool initial) {
    const std::wstring path = dataDir_ + L"\\menubar.json";
    auto f = loadJsonFile(path);
    if (!initial && (f.wasInvalid || f.unreadable)) {
        log::warn(L"menubar.json ignoré (invalide ou illisible) : réglages actuels conservés");
    } else {
        settings_ = menuBarSettingsFromJson(f.value);
    }
    // Fichier absent : on l'écrit avec les valeurs par défaut (pas en --snapshot, qui ne touche à rien).
    if (!f.fromFile && !f.wasInvalid && !f.unreadable && ctl_) saveJsonFileAtomic(path, menuBarSettingsToJson(settings_));
    fileTime(path, settingsTime_);
    auto m = loadJsonFile(dataDir_ + L"\\dock-metrics.json");
    // Migré en mémoire : le Dock, lancé en même temps, n'a peut-être pas encore réécrit le fichier.
    if (m.fromFile && !m.wasInvalid) glassMetrics_ = metricsFromJson(migrateMetricsJson(m.value));
}

void MenuBarApp::checkSettingsFile() {
    FILETIME t{};
    if (!fileTime(dataDir_ + L"\\menubar.json", t) || CompareFileTime(&t, &settingsTime_) == 0) return;
    log::info(L"menubar.json modifié : rechargement");
    loadSettings(false);
    applySettings();
}

void MenuBarApp::applySettings() {
    if (ctl_) registerVolumeKeys();   // après le démarrage seulement (fenêtre de contrôle prête)
    lights_.attach(lights_.target(), settings_.trafficLights);
    if (ctl_) styler_.setEnabled(settings_.macWindows, appsDarkMode());
    for (auto& s : screens_) syncAppBar(*s);
    repositionAll();
}

void MenuBarApp::loadLogo() {
    UINT w = 0, h = 0;
    auto px = readPng(dataDir_ + L"\\menubar-logo.png", w, h);
    logo_ = {};
    if (!px.empty()) {
        logo_.w = w;
        logo_.h = h;
        logo_.bgra = std::move(px);
        log::info(L"Barre : logo personnalisé %ux%u", w, h);
    }
    for (auto& s : screens_) s->renderer.setLogo(logo_);
}

// ---- Écrans ----

namespace {
BOOL CALLBACK collectMonitor(HMONITOR mon, HDC, LPRECT, LPARAM lp) {
    reinterpret_cast<std::vector<HMONITOR>*>(lp)->push_back(mon);
    return TRUE;
}
} // namespace

MenuBarApp::Screen* MenuBarApp::screenOf(HWND hwnd) {
    return reinterpret_cast<Screen*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));   // posé après la création, retiré avant la destruction
}

bool MenuBarApp::createScreen(Screen& s) {
    s.hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, kScreenClass,
                             L"MacMenuBar", WS_POPUP, s.rect.left, s.rect.top, 100, 24, nullptr, nullptr, instance_, nullptr);
    if (!s.hwnd) {
        log::error(L"Barre : CreateWindowEx a échoué (%lu)", GetLastError());
        return false;
    }
    SetWindowLongPtrW(s.hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&s));
    if (!s.renderer.init(s.hwnd)) {
        log::error(L"Barre : initialisation graphique impossible");
        SetWindowLongPtrW(s.hwnd, GWLP_USERDATA, 0);
        DestroyWindow(s.hwnd);
        s.hwnd = nullptr;
        return false;
    }
    s.renderer.setLogo(logo_);
    s.visibility.setTimings({0, 0.5, 0.25, 0.25});
    s.darkText = !systemDarkMode();
    if (!settings_.autohide) registerAppBar(s);
    return true;
}

void MenuBarApp::destroyScreen(Screen& s) {
    if (s.sampler.running()) s.sampler.stop();
    if (s.hwnd) SetWindowLongPtrW(s.hwnd, GWLP_USERDATA, 0);   // ses derniers messages ne la touchent plus
    removeAppBar(s);
    if (s.hwnd) DestroyWindow(s.hwnd);
    s.hwnd = nullptr;
}

void MenuBarApp::rebuildScreens() {
    // Le menu ouvert lit sa barre : on attend sa fermeture. Imbriqué (message traité pendant un appel qui suit) :
    // on refait après.
    if (!screensGate_.tryBegin(menuOpen_ || menuSession_)) return;
    std::vector<HMONITOR> mons;
    EnumDisplayMonitors(nullptr, nullptr, collectMonitor, reinterpret_cast<LPARAM>(&mons));
    std::vector<ScreenInfo> infos;
    for (HMONITOR mon : mons) {
        MONITORINFO mi{sizeof mi};
        if (!GetMonitorInfoW(mon, &mi)) continue;
        UINT dx = 96, dy = 96;
        GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dx, &dy);
        infos.push_back({mi.rcMonitor, dx, (mi.dwFlags & MONITORINFOF_PRIMARY) != 0});
    }
    infos = orderScreens(std::move(infos));
    std::vector<RECT> existing;
    for (const auto& s : screens_) existing.push_back(s->rect);
    const ScreenPlan plan = planScreens(existing, infos);   // une barre déjà sur un écran est gardée : pas de clignotement
    // Les nouvelles barres sont créées à part : pendant CreateWindowEx et l'inscription de la zone réservée, des
    // messages sont traités et screens_ reste entier.
    std::vector<std::unique_ptr<Screen>> fresh(infos.size());
    for (std::size_t i = 0; i < infos.size(); ++i) {
        if (plan.keep[i] >= 0) continue;
        auto s = std::make_unique<Screen>();
        s->rect = infos[i].rect;
        if (createScreen(*s)) fresh[i] = std::move(s);
    }
    // Échange d'un bloc, sans appel à Windows ; les barres débranchées sont détruites une fois hors de screens_.
    std::vector<std::unique_ptr<Screen>> next, dropped;
    for (std::size_t i = 0; i < infos.size(); ++i) {
        auto s = plan.keep[i] >= 0 ? std::move(screens_[std::size_t(plan.keep[i])]) : std::move(fresh[i]);
        if (!s) continue;
        s->monitor = MonitorFromRect(&infos[i].rect, MONITOR_DEFAULTTONEAREST);
        s->dpi = infos[i].dpi;
        s->primary = infos[i].primary;
        next.push_back(std::move(s));
    }
    for (std::size_t i : plan.drop) dropped.push_back(std::move(screens_[i]));
    screens_ = std::move(next);
    for (auto& old : dropped) destroyScreen(*old);
    if (screens_.empty()) {
        screensGate_.end();
        log::error(L"Barre : aucun écran utilisable, arrêt (le lanceur relancera la barre)");
        exitCode_ = 2;
        PostQuitMessage(2);
        return;
    }
    log::info(L"Barre : %zu écran(s)", screens_.size());
    activeScreen_ = std::min(activeScreen_, screens_.size() - 1);
    repositionAll();
    updateActiveScreen();
    startSamples();
    stepVisibilityAll();   // masquage automatique : les barres neuves se cachent aussi
    if (screensGate_.end()) PostMessageW(ctl_, WM_APP_SCREENS, 0, 0);   // écrans changés entre-temps
}

void MenuBarApp::updateActiveScreen() {
    if (screens_.empty()) return;
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    if (fg) GetWindowThreadProcessId(fg, &pid);
    if (pid == GetCurrentProcessId()) return;   // nos menus : l'écran actif ne change pas
    std::vector<ScreenInfo> infos;
    for (auto& s : screens_) infos.push_back({s->rect, s->dpi, s->primary});
    RECT rc{};
    const RECT* window = nullptr;
    wchar_t cls[64] = {};
    if (fg) GetClassNameW(fg, cls, 64);
    const bool desktop = wcscmp(cls, L"Progman") == 0 || wcscmp(cls, L"WorkerW") == 0;
    if (fg && !desktop && !IsIconic(fg) && GetWindowRect(fg, &rc)) window = &rc;
    POINT pt{};
    GetCursorPos(&pt);
    const std::size_t a = activeScreen(infos, window, pt);
    if (a == activeScreen_) return;
    activeScreen_ = a;
    if (trace_) log::info(L"[trace] barre : écran actif %zu", a);
    render();
}

// ---- Placement ----

void MenuBarApp::registerAppBar(Screen& s) {
    APPBARDATA abd{};
    abd.cbSize = sizeof abd;
    abd.hWnd = s.hwnd;
    abd.uCallbackMessage = WM_APP_APPBAR;
    s.appBar = SHAppBarMessage(ABM_NEW, &abd) != FALSE;
}

void MenuBarApp::removeAppBar(Screen& s) {
    if (!s.appBar) return;
    APPBARDATA abd{};
    abd.cbSize = sizeof abd;
    abd.hWnd = s.hwnd;
    SHAppBarMessage(ABM_REMOVE, &abd);
    s.appBar = false;
}

void MenuBarApp::syncAppBar(Screen& s) {
    if (!s.hwnd) return;
    const bool want = !settings_.autohide;
    if (want == s.appBar) return;
    if (want) registerAppBar(s);
    else removeAppBar(s);
}

void MenuBarApp::reposition(Screen& s) {
    if (s.monitor) {
        UINT dx = 96, dy = 96;
        if (SUCCEEDED(GetDpiForMonitor(s.monitor, MDT_EFFECTIVE_DPI, &dx, &dy))) s.dpi = dx;
    }
    s.scale = float(s.dpi) / 96.0f;
    s.heightPx = int(std::lround(settings_.metrics.height * s.scale));
    s.font = s.renderer.setFont(settings_.font, float(settings_.metrics.fontSize) * s.scale);
    if (s.hwnd && s.appBar) {
        APPBARDATA abd{};
        abd.cbSize = sizeof abd;
        abd.hWnd = s.hwnd;
        abd.uEdge = ABE_TOP;
        abd.rc = {s.rect.left, s.rect.top, s.rect.right, s.rect.top + s.heightPx};
        SHAppBarMessage(ABM_QUERYPOS, &abd);
        abd.rc.bottom = abd.rc.top + s.heightPx;
        SHAppBarMessage(ABM_SETPOS, &abd);
    }
    const int width = s.rect.right - s.rect.left;
    if (s.hwnd) {
        SetWindowPos(s.hwnd, HWND_TOPMOST, s.rect.left, s.rect.top - s.yOffsetPx, width, s.heightPx,
                     SWP_NOACTIVATE | (s.visible ? SWP_SHOWWINDOW : 0));
        s.renderer.resize(UINT(width), UINT(s.heightPx));
    }
    if (trace_) log::info(L"[trace] barre : écran (%ld, %ld) %d x %d px (échelle %.2f), zone réservée %s, police %s",
                          s.rect.left, s.rect.top, width, s.heightPx, s.scale, s.appBar ? L"oui" : L"non", s.font.c_str());
}

void MenuBarApp::repositionAll() {
    for (auto& s : screens_) reposition(*s);
    relayout();
    render();
}

// ---- App active ----

namespace {
constexpr ULONGLONG kNoMenuBarMemoryMs = 5 * 60 * 1000;

BOOL CALLBACK collectChildClass(HWND h, LPARAM lp) {
    auto* out = reinterpret_cast<std::vector<std::wstring>*>(lp);
    wchar_t cls[128] = {};
    GetClassNameW(h, cls, 128);
    out->push_back(cls);
    return out->size() < 500;
}

std::vector<std::wstring> childClasses(HWND top) {
    std::vector<std::wstring> out;
    EnumChildWindows(top, collectChildClass, reinterpret_cast<LPARAM>(&out));
    return out;
}
} // namespace

namespace {
bool isDesktopClass(const wchar_t* cls) { return wcscmp(cls, L"Progman") == 0 || wcscmp(cls, L"WorkerW") == 0; }

// Clic sur le bureau : un bouton de la souris est enfoncé et le curseur est sur le bureau.
bool clickedOnDesktop() {
    const SHORT down = GetAsyncKeyState(VK_LBUTTON) | GetAsyncKeyState(VK_RBUTTON) | GetAsyncKeyState(VK_MBUTTON);
    if (!(down & 0x8000)) return false;
    POINT pt{};
    GetCursorPos(&pt);
    HWND under = WindowFromPoint(pt);
    HWND root = under ? GetAncestor(under, GA_ROOT) : nullptr;
    wchar_t cls[64] = {};
    if (root) GetClassNameW(root, cls, 64);
    return isDesktopClass(cls);
}

// La fenêtre d'app visible la plus haute (ordre Z : la dernière utilisée), hors `except` : ni réduite, ni masquée
// (autre bureau virtuel compris).
HWND topAppWindow(HWND except) {
    for (HWND h = GetTopWindow(nullptr); h; h = GetWindow(h, GW_HWNDNEXT)) {
        if (h == except || !isDockEligibleWindow(h) || IsIconic(h)) continue;
        DWORD cloaked = 0;
        if (SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof cloaked)) && cloaked) continue;
        return h;
    }
    return nullptr;
}
} // namespace

void MenuBarApp::decideDesktopFocus() {
    KillTimer(ctl_, kDesktopFocusTimer);
    const HWND previous = desktopPrevious_;
    desktopPrevious_ = nullptr;
    const HWND fg = GetForegroundWindow();
    wchar_t cls[64] = {};
    if (fg) GetClassNameW(fg, cls, 64);
    if (!isDesktopClass(cls)) return;   // une fenêtre a pris la main entre-temps : déjà traitée
    DesktopFocusContext c;
    if (previous) {   // sans fenêtre d'avant (démarrage) : le bureau tel quel
        DWORD pid = 0;
        GetWindowThreadProcessId(previous, &pid);
        DWORD cloaked = 0;
        const bool hidden = !IsWindow(previous) || !IsWindowVisible(previous) || pid == GetCurrentProcessId() ||
                            (SUCCEEDED(DwmGetWindowAttribute(previous, DWMWA_CLOAKED, &cloaked, sizeof cloaked)) &&
                             (cloaked & (DWM_CLOAKED_APP | DWM_CLOAKED_INHERITED)));
        c.previousMinimized = !hidden && IsIconic(previous);
        c.previousGone = hidden;
    }
    const HWND next = topAppWindow(previous);
    c.otherWindowVisible = next != nullptr;
    switch (desktopFocus(c)) {
        case DesktopFocus::ActivateNext:
            if (trace_) log::info(L"[trace] barre : bureau sans clic après %p, la main passe à %p", previous, next);
            forceForeground(next);   // son premier plan arrive ensuite par le suivi des fenêtres
            return;
        case DesktopFocus::KeepPrevious:
            if (trace_) log::info(L"[trace] barre : seule fenêtre réduite, « %s » reste active", active_.name.c_str());
            return;
        case DesktopFocus::ShowExplorer:
            desktopDecided_ = true;
            onForeground(fg);
            return;
    }
}

void MenuBarApp::onForeground(HWND h) {
    if (!h) return;
    const HWND previous = lastForeground_;
    lastForeground_ = h;
    const HWND rootWindow = GetAncestor(h, GA_ROOT) ? GetAncestor(h, GA_ROOT) : h;
    lights_.attach(rootWindow, settings_.trafficLights);   // il décide
    if (settings_.macWindows) styler_.apply(rootWindow, readInfo(rootWindow), appsDarkMode(), effectiveDpi(rootWindow));
    // L'app est celle de la fenêtre propriétaire racine ; la cible des commandes est la fenêtre réellement au
    // premier plan (un dialogue « Enregistrer sous » reçoit Ctrl+V, pas sa fenêtre principale désactivée).
    HWND root = GetAncestor(h, GA_ROOTOWNER);
    if (!root) root = h;
    HWND top = GetAncestor(h, GA_ROOT);
    if (!top) top = h;
    wchar_t cls[256] = {};
    GetClassNameW(root, cls, 256);
    DWORD pid = 0;
    GetWindowThreadProcessId(root, &pid);
    const std::wstring exe = processExe(pid);
    const ForegroundKind kind = classifyForeground(cls, fileName(exe), pid == GetCurrentProcessId());
    if (trace_) log::info(L"[trace] barre : premier plan %p (%s, %s) → %s", h, cls, fileName(exe).c_str(),
                          kind == ForegroundKind::Ignore ? L"ignoré" : kind == ForegroundKind::Explorer ? L"Explorateur" : L"app");
    if (kind == ForegroundKind::Ignore) return;
    if (!(kind == ForegroundKind::Explorer && isDesktopClass(cls))) {
        KillTimer(ctl_, kDesktopFocusTimer);
        desktopPrevious_ = nullptr;
        desktopDecided_ = false;
    } else if (!desktopDecided_) {
        if (clickedOnDesktop()) {
            desktopDecided_ = true;   // clic sur le bureau : le Finder, comme sur Mac
        } else {
            // Windows donne la main au bureau quand la fenêtre active se ferme ou se réduit : décidé une fois la
            // transition finie ; d'ici là, l'app d'avant reste affichée.
            if (!desktopPrevious_ && previous) desktopPrevious_ = GetAncestor(previous, GA_ROOT) ? GetAncestor(previous, GA_ROOT) : previous;
            SetTimer(ctl_, kDesktopFocusTimer, kDesktopFocusDelayMs, nullptr);
            return;
        }
    }
    Active a;
    if (kind == ForegroundKind::Explorer) {
        a.name = L"Explorateur";
        a.explorer = true;
        a.desktop = wcscmp(cls, L"Progman") == 0 || wcscmp(cls, L"WorkerW") == 0;
        a.exePath = exe;
        auto id = identifyWindow(root);
        a.appId = id ? id->appId : makeAppId(L"", exe);
    } else {
        auto id = identifyWindow(root);
        if (!id && root != h) id = identifyWindow(h);
        if (!id) return;
        a.name = id->displayName.empty() ? fileName(id->exePath) : id->displayName;
        a.appId = id->appId;
        a.exePath = id->exePath;
        a.launch = id->launch.empty() ? id->exePath : id->launch;
    }
    if (!a.explorer) readRealMenus(top, root, a);
    if (!a.explorer && a.source == MenuSource::Generic) {
        if (active_.source == MenuSource::Uia && active_.menuOwner == top) {   // même fenêtre : titres déjà lus
            a.source = active_.source;
            a.real = active_.real;
            a.menuOwner = top;
        } else {
            wchar_t topCls[256] = {};
            GetClassNameW(top, topCls, 256);
            if (shouldProbeUia(topCls, childClasses(top)) && !knownWithoutMenuBar(top)) requestUiaTitles(top);
            else uiaWindow_ = nullptr;
        }
    } else {
        uiaWindow_ = nullptr;
        ++uiaLatest_;   // une demande en cours ne s'appliquera plus
    }
    target_ = keepTarget(target_, top, kind, a.appId);
    const bool changed = a.name != active_.name || a.explorer != active_.explorer || a.desktop != active_.desktop ||
                         a.menuOwner != active_.menuOwner || a.real.size() != active_.real.size() ||
                         !std::equal(a.real.begin(), a.real.end(), active_.real.begin(),
                                     [](const RawMenuItem& x, const RawMenuItem& y) { return x.text == y.text; });
    if (changed && !a.explorer && !a.launch.empty() && a.name != active_.name) {
        pushRecent(recent_.apps, {a.name, a.launch});
        recentDirty_ = true;
        if (ctl_) SetTimer(ctl_, kRecentTimer, 5000, nullptr);
    }
    active_ = a;
    checkFullscreen();
    updateActiveScreen();
    if (!changed) return;
    if (trace_) log::info(L"[trace] barre : app active « %s » (%s), %zu vrais menus", a.name.c_str(), a.appId.c_str(), a.real.size());
    relayout();
    render();
}

namespace {
void disableAll(std::vector<RawMenuItem>& items) {
    for (auto& it : items) {
        it.enabled = false;
        disableAll(it.children);
    }
}

// Loupe : Spotlight du Dock (message enregistré « MacDockSpotlight ») ; false si le Dock ne tourne pas.
bool askDockForSpotlight() {
    HWND dock = FindWindowW(L"MacDockWindow", nullptr);
    if (!dock) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(dock, &pid);
    AllowSetForegroundWindow(pid);   // le panneau doit prendre le clavier
    return PostMessageW(dock, RegisterWindowMessageW(L"MacDockSpotlight"), 0, 0) != FALSE;
}
} // namespace

// Barre de menus Win32 de la fenêtre au premier plan, ou de sa fenêtre principale quand un dialogue est devant
// (menus alors grisés, comme une app macOS pendant une feuille modale).
void MenuBarApp::readRealMenus(HWND top, HWND root, Active& a) {
    HWND owner = top;
    HMENU bar = GetMenu(top);
    if (!bar || !IsMenu(bar)) {
        owner = root;
        bar = root != top ? GetMenu(root) : nullptr;
    }
    if (!bar || !IsMenu(bar)) return;
    auto real = readWin32Menu(bar);
    if (!hasReadableEntries(real)) return;   // menus vides (owner-draw sans texte) : génériques
    if (!IsWindowEnabled(owner))
        for (auto& title : real) disableAll(title.children);
    a.menuOwner = owner;
    a.source = MenuSource::Win32;
    a.real = std::move(real);
}

bool MenuBarApp::syncRealTitles() {
    if (active_.source != MenuSource::Win32) return false;
    HWND owner = active_.menuOwner;
    HMENU bar = IsWindow(owner) ? GetMenu(owner) : nullptr;
    if (!bar || !IsMenu(bar)) return false;
    if (!syncWin32Titles(bar, active_.real)) return false;
    if (trace_) log::info(L"[trace] barre : l'app a changé sa barre de menus (%zu titres)", active_.real.size());
    return true;
}

void MenuBarApp::refreshRealMenu(int real) {
    if (active_.source == MenuSource::Generic || real < 0 || std::size_t(real) >= active_.real.size()) return;
    HWND owner = active_.menuOwner;
    if (active_.source == MenuSource::Uia) {
        RawMenuItem& title = active_.real[std::size_t(real)];
        // Attente bornée sans traiter les messages : rien ne change sous le menu qui va s'ouvrir.
        if (uia_.busy()) {   // une lecture précédente n'a pas fini : ne pas figer la barre une fois de plus
            log::warn(L"Barre : UI Automation occupé, menu « %s » non relu", title.text.c_str());
            return;
        }
        auto out = std::make_shared<std::optional<std::vector<RawMenuItem>>>();
        const int position = title.position;
        menuOpen_ = true;   // pendant l'attente, un message envoyé ne doit pas changer menus_
        const bool read = uia_.call([out, owner, position](UiaMenus& uia) { *out = uia.items(owner, position); }, kUiaItemsWaitMs);
        menuOpen_ = false;
        if (read && *out)
            title.children = std::move(**out);
        else
            log::warn(L"Barre : menu « %s » de l'app illisible par UI Automation", title.text.c_str());
        return;
    }
    HMENU bar = IsWindow(owner) ? GetMenu(owner) : nullptr;
    if (!bar || !IsMenu(bar)) return;
    RawMenuItem& title = active_.real[std::size_t(real)];
    if (!refreshWin32Popup(owner, bar, title.position))
        log::warn(L"Barre : l'app ne prépare pas son menu « %s » à temps", title.text.c_str());
    HMENU sub = GetSubMenu(bar, title.position);
    if (!sub) return;
    title.children = readWin32Menu(sub);
    if (!IsWindowEnabled(owner)) disableAll(title.children);
}

bool MenuBarApp::knownWithoutMenuBar(HWND window) {
    const ULONGLONG now = GetTickCount64();
    std::erase_if(noMenuBar_, [&](const auto& e) { return now - e.second > kNoMenuBarMemoryMs || !IsWindow(e.first); });
    return noMenuBar_.contains(window);
}

void MenuBarApp::requestUiaTitles(HWND window) {
    if (window == uiaWindow_) return;   // demande déjà en cours pour cette fenêtre
    uiaWindow_ = window;
    const unsigned generation = ++uiaLatest_;
    HWND bar = ctl_;
    uia_.post([this, window, generation, bar](UiaMenus& uia) {
        if (generation != uiaLatest_.load()) return;   // l'utilisateur est déjà passé à autre chose
        auto* r = new UiaTitles{generation, window, uia.titles(window)};
        if (!PostMessageW(bar, WM_APP_UIA_TITLES, 0, reinterpret_cast<LPARAM>(r))) delete r;
    });
}

void MenuBarApp::onUiaTitles(LPARAM lp) {
    std::unique_ptr<UiaTitles> r(reinterpret_cast<UiaTitles*>(lp));
    if (r->generation != uiaLatest_.load() || r->window != uiaWindow_ || active_.source != MenuSource::Generic ||
        active_.explorer)
        return;
    if (trace_) log::info(L"[trace] barre : %zu menus lus par UI Automation", r->titles.size());
    if (r->titles.empty()) {
        noMenuBar_[r->window] = GetTickCount64();
        return;
    }
    active_.source = MenuSource::Uia;
    active_.real = std::move(r->titles);
    active_.menuOwner = r->window;
    relayout();
    render();
}

// ---- Éléments récents ----

void MenuBarApp::loadRecent() {
    auto f = loadJsonFile(dataDir_ + L"\\menubar-recent.json");
    if (f.fromFile && !f.wasInvalid) recent_ = recentFromJson(f.value);
}

void MenuBarApp::saveRecent() {
    if (ctl_) KillTimer(ctl_, kRecentTimer);
    if (!recentDirty_ || !ctl_) return;
    recentDirty_ = false;
    if (!saveJsonFileAtomic(dataDir_ + L"\\menubar-recent.json", recentToJson(recent_)))
        log::warn(L"Barre : apps récentes non enregistrées");
}

std::vector<RecentEntry> MenuBarApp::withIcons(std::vector<RecentEntry> list, bool documents, float scale) const {
    const int px = int(std::lround(kMenuIconSize * scale));
    for (auto& e : list) {
        // Document : par son extension seulement (un raccourci vers un partage hors ligne bloquerait la barre).
        if (documents) {
            e.icon = icons_.extensionIcon(e.name, px);
            continue;
        }
        e.icon = icons_.fileIcon(e.target, px);
        if (!e.icon) e.icon = icons_.get(e.target, e.target, px);   // app empaquetée : shell:AppsFolder\AUMID
    }
    return list;
}

std::vector<HWND> MenuBarApp::appWindows() const {
    std::vector<HWND> out;
    for (WindowId id : model_.windowsOf(active_.appId))
        if (IsWindow(toHwnd(id))) out.push_back(toHwnd(id));
    return out;
}

BarContext MenuBarApp::context(bool recentDocs, float scale) const {
    BarContext c;
    c.appName = active_.name.empty() ? L"Explorateur" : active_.name;
    if (const auto slash = active_.exePath.find_last_of(L"\\/"); !active_.exePath.empty())
        c.exe = slash == std::wstring::npos ? active_.exePath : active_.exePath.substr(slash + 1);
    c.userName = userDisplayName();
    c.explorer = active_.explorer || active_.name.empty();
    c.desktop = active_.desktop || active_.name.empty();
    if (!c.desktop)
        for (WindowId id : model_.windowsOf(active_.appId)) c.windows.push_back({id, model_.titleOf(id)});
    c.activeWindow = toId(target_.window);
    if (const HWND t = target_.window; t && IsWindow(t)) {   // Déplacer et redimensionner (menu Fenêtre)
        c.targetResizable = (GetWindowLongPtrW(t, GWL_STYLE) & WS_THICKFRAME) != 0;
        c.targetIconic = IsIconic(t) != FALSE;
    }
    c.source = active_.source;
    c.real = active_.real;
    c.menuOwner = toId(active_.menuOwner);
    c.recentApps = withIcons(recent_.apps, false, scale);
    PWSTR folder = nullptr;
    if (recentDocs && SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Recent, KF_FLAG_DONT_VERIFY, nullptr, &folder)))
        c.recentDocs = withIcons(recentDocuments(folder, kRecentMax, recent_.clearedAt), true, scale);
    CoTaskMemFree(folder);
    return c;
}

// ---- Mise en page et rendu ----

void MenuBarApp::relayout() {
    // Pendant un menu, MenuWindow lit le modèle ouvert : menus_ ne doit pas changer (heure, app, réglages…).
    if (menuOpen_) {
        layoutPending_ = true;
        return;
    }
    layoutPending_ = false;
    menus_ = buildBarMenus(context());
    updateClock();
    status_ = statusItems(statusState());
    trayLaid_.clear();
    if (settings_.showAppIcons)
        for (const TrayIcon* t : tray_.visible()) {
            TrayShown shown{t->e, std::make_shared<const std::vector<std::uint8_t>>(t->e.bgra)};
            shown.e.bgra.clear();
            trayLaid_.push_back(std::move(shown));
        }
    for (auto& s : screens_) layoutScreen(*s);
}

void MenuBarApp::layoutScreen(Screen& s) {
    const MenuBarMetrics& m = settings_.metrics;
    const double pad = m.titlePadding;
    s.layoutIn = {};
    s.layoutIn.barWidth = double(s.rect.right - s.rect.left) / s.scale;
    s.layoutIn.leftMargin = m.leftMargin;
    s.layoutIn.rightMargin = m.rightMargin;
    for (const auto& menu : menus_.menus) {
        if (menu.logo) s.layoutIn.leftWidths.push_back(m.logoSize + 2 * pad);
        else s.layoutIn.leftWidths.push_back(std::ceil(s.renderer.measure(menu.title, menu.bold) / s.scale) + 2 * pad);
    }
    for (const auto& item : status_)
        s.layoutIn.rightWidths.push_back(item.kind == StatusKind::Clock
                                             ? std::ceil(s.renderer.measure(clock_, false) / s.scale) + 2 * pad
                                             : m.statusWidth);
    // Trop d'icônes d'apps : celles de gauche cèdent la place au logo et au nom de l'app (comme sur macOS).
    const std::size_t fit = trayFit(s.layoutIn, trayLaid_.size(), m.statusWidth);
    s.trayFirst = trayLaid_.size() - fit;
    s.layoutIn.rightWidths.insert(s.layoutIn.rightWidths.begin(), fit, m.statusWidth);
    s.layout = layoutBar(s.layoutIn);
}

std::size_t MenuBarApp::trayShown(const Screen& s) const {
    return trayLaid_.size() > s.trayFirst ? trayLaid_.size() - s.trayFirst : 0;
}

StatusState MenuBarApp::statusState() {
    StatusState s;
    s.snap = snap_;
    s.volume = audio_.volume();
    s.audio = s.volume >= 0;
    s.volume = std::max(s.volume, 0.0f);
    s.muted = s.audio && audio_.muted();
    s.settings = settings_;
    s.clock = clock_;
    SYSTEMTIME now;
    GetLocalTime(&now);
    s.year = now.wYear;
    s.month = now.wMonth;
    s.day = now.wDay;
    s.weekday = now.wDayOfWeek;
    return s;
}

void MenuBarApp::onStatus(LPARAM snapshot) {
    std::unique_ptr<StatusSnapshot> p(reinterpret_cast<StatusSnapshot*>(snapshot));
    if (!p) return;
    snap_ = std::move(*p);
    updateStatusItems();
}

void MenuBarApp::updateStatusItems() {
    auto items = statusItems(statusState());
    bool sameKinds = items.size() == status_.size(), changed = !sameKinds;
    for (std::size_t i = 0; sameKinds && i < items.size(); ++i) {
        sameKinds = items[i].kind == status_[i].kind;
        changed = changed || items[i].glyph != status_[i].glyph || std::fabs(items[i].level - status_[i].level) > 1e-3f ||
                  items[i].alt != status_[i].alt;
    }
    if (!changed) return;
    if (sameKinds) status_ = std::move(items);   // mêmes cases : pas de nouvelle mise en page
    else relayout();                             // icône apparue ou disparue (pendant un menu : à sa fermeture)
    render();
}

BarFrame MenuBarApp::frame(const Screen& s) const {
    BarFrame f;
    f.scale = s.scale;
    f.darkText = s.darkText;
    f.metrics = settings_.metrics;
    const bool active = activeScreen_ < screens_.size() && screens_[activeScreen_].get() == &s;
    f.opacity = active || screens_.size() < 2 ? 1.0f : 0.6f;   // barres des autres écrans atténuées
    const int highlight = &s == menuScreen_ ? highlight_ : -1;
    for (std::size_t i = 0; i < s.layout.leftVisible && i < menus_.menus.size(); ++i) {
        BarDrawItem it;
        it.text = menus_.menus[i].title;
        it.bold = menus_.menus[i].bold;
        it.logo = menus_.menus[i].logo;
        it.x = float(s.layout.leftX[i]) * s.scale;
        it.width = float(s.layoutIn.leftWidths[i]) * s.scale;
        it.highlighted = int(i) == highlight;
        f.items.push_back(std::move(it));
    }
    const std::size_t trayN = trayShown(s);
    for (std::size_t k = 0; k < trayN && k < s.layout.rightX.size(); ++k) {
        BarDrawItem it;
        const TrayShown& t = trayLaid_[s.trayFirst + k];
        it.image = t.image;
        it.imageW = t.e.w;
        it.imageH = t.e.h;
        it.x = float(s.layout.rightX[k]) * s.scale;
        it.width = float(s.layoutIn.rightWidths[k]) * s.scale;
        f.items.push_back(std::move(it));
    }
    for (std::size_t j = 0; trayN + j < s.layout.rightX.size() && j < status_.size(); ++j) {
        BarDrawItem it;
        if (status_[j].kind == StatusKind::Clock) it.text = clock_;
        it.glyph = status_[j].glyph;
        it.level = status_[j].level;
        it.alt = status_[j].alt;
        it.x = float(s.layout.rightX[trayN + j]) * s.scale;
        it.width = float(s.layoutIn.rightWidths[trayN + j]) * s.scale;
        it.highlighted = highlight == int(s.layout.leftVisible + j);
        f.items.push_back(std::move(it));
    }
    return f;
}

void MenuBarApp::render() {
    for (std::size_t i = 0; i < screens_.size(); ++i) render(*screens_[i]);
}

void MenuBarApp::render(Screen& s) {
    if (!s.hwnd) return;
    if (s.renderer.render(frame(s))) {
        s.renderFailures = 0;
        return;
    }
    log::warn(L"Barre : rendu impossible (0x%08lX)", static_cast<unsigned long>(s.renderer.lastError()));
    if (isDeviceLost(s.renderer.lastError()) || ++s.renderFailures >= 3) recoverDevice(s);
}

void MenuBarApp::recoverDevice(Screen& s) {
    if (menuOpen_) return;   // le menu ouvert utilise encore le device : on réessaiera au prochain rendu
    log::warn(L"Barre : device graphique perdu, recréation");
    s.renderer.reset();
    s.renderFailures = 0;
    if (!s.renderer.init(s.hwnd)) {
        log::error(L"Barre : recréation du device impossible, arrêt (le lanceur relancera la barre)");
        exitCode_ = 3;
        PostQuitMessage(3);
        return;
    }
    s.renderer.setLogo(logo_);
    reposition(s);   // surface à la bonne taille, police, mise en page, puis rendu
    layoutScreen(s);
    render(s);
}

void MenuBarApp::afterMenu() {
    menuOpen_ = false;
    menuSession_ = false;
    highlight_ = -1;
    menuScreen_ = nullptr;
    if (screensGate_.pending) {
        rebuildScreens();   // mise en page et rendu compris
        return;
    }
    if (layoutPending_) relayout();
    render();
}

void MenuBarApp::restoreTargetFocus() {
    HWND t = target_.window;
    if (!t || !IsWindow(t) || GetAncestor(GetForegroundWindow(), GA_ROOT) == GetAncestor(t, GA_ROOT)) return;
    SetForegroundWindow(t);   // notre menu vient d'avoir le premier plan : permis, sans frappe Alt synthétique
}

void MenuBarApp::updateClock() {
    SYSTEMTIME t;
    GetLocalTime(&t);
    clock_ = formatClock(t, settings_.clock);
}

void MenuBarApp::scheduleClock() {
    SYSTEMTIME t;
    GetLocalTime(&t);
    UINT ms = settings_.clock.seconds ? UINT(1000 - t.wMilliseconds) : UINT((60 - t.wSecond) * 1000 - t.wMilliseconds);
    SetTimer(ctl_, kClockTimer, ms + 20, nullptr);
}

// ---- Couleur du texte ----

void MenuBarApp::startSample(Screen& s) {
    if (menuOpen_ || shotReveal_ || hud_.visible() || s.sampler.running() || !s.visible || !s.hwnd) return;
    // La barre est exclue de la capture le temps de l'échantillon : on mesure le fond, pas son texte.
    SetWindowDisplayAffinity(s.hwnd, WDA_EXCLUDEFROMCAPTURE);
    RECT strip{s.rect.left, s.rect.top, s.rect.right, s.rect.top + s.heightPx};
    if (!s.sampler.start(s.hwnd, WM_APP_SAMPLE, s.monitor, strip)) {
        finishSample(s, std::nullopt);
        return;
    }
    SetTimer(s.hwnd, kSampleTimeout, 2000, nullptr);
}

void MenuBarApp::startSamples() {
    for (std::size_t i = 0; i < screens_.size(); ++i) startSample(*screens_[i]);
}

void MenuBarApp::abortSamples() {
    for (auto& s : screens_) {
        if (!s->sampler.running()) continue;
        s->sampler.stop();
        KillTimer(s->hwnd, kSampleTimeout);
        SetWindowDisplayAffinity(s->hwnd, WDA_NONE);   // la barre réapparaît dans les captures d'écran
    }
}

void MenuBarApp::onScreenshotReveal(bool on) {
    if (on) {
        shotReveal_ = true;
        abortSamples();
        lights_.setCaptureVisible(true);
        MenuWindow::setCaptureVisible(true);   // un menu ouvert de la barre (Centre de contrôle…) est photographié
        hud_.setCaptureVisible(true);           // la pastille du volume aussi
        SetTimer(ctl_, kShotRevealTimer, 3000, nullptr);   // le Dock rend la main en moins d'une seconde
    } else if (shotReveal_) {
        shotReveal_ = false;
        KillTimer(ctl_, kShotRevealTimer);
        lights_.setCaptureVisible(false);
        MenuWindow::setCaptureVisible(false);
        hud_.setCaptureVisible(false);
    }
    if (trace_ || diagnosticCapture()) log::info(L"[trace] capture d'écran : pastilles %s", on ? L"visibles" : L"exclues de nouveau");
}

void MenuBarApp::stopSamples() {   // une seule duplication d'un écran par processus : le menu capture à son tour
    hideHud();
    for (auto& s : screens_)
        if (s->sampler.running()) finishSample(*s, std::nullopt);
}

// ---- Pastille du volume et de la luminosité ----

void MenuBarApp::registerVolumeKeys() {
    const bool want = settings_.hud;
    if (!want) hideHud();   // même si les touches n'avaient pas pu être prises
    if (want == volumeKeys_) return;
    for (int id : {kHotVolUp, kHotVolDown, kHotMute, kHotVolUpFine, kHotVolDownFine}) UnregisterHotKey(ctl_, id);
    volumeKeys_ = false;
    if (!want) {
        hideHud();
        log::info(L"HUD : désactivé, touches de volume rendues à Windows");
        return;
    }
    const bool ok = RegisterHotKey(ctl_, kHotVolUp, 0, VK_VOLUME_UP) && RegisterHotKey(ctl_, kHotVolDown, 0, VK_VOLUME_DOWN) &&
                    RegisterHotKey(ctl_, kHotMute, 0, VK_VOLUME_MUTE);
    if (!ok) {
        log::warn(L"HUD : touches de volume déjà prises (%lu) ; Windows les garde, la pastille suit le volume", GetLastError());
        for (int id : {kHotVolUp, kHotVolDown, kHotMute}) UnregisterHotKey(ctl_, id);
        return;
    }
    RegisterHotKey(ctl_, kHotVolUpFine, MOD_SHIFT | MOD_ALT, VK_VOLUME_UP);   // facultatifs
    RegisterHotKey(ctl_, kHotVolDownFine, MOD_SHIFT | MOD_ALT, VK_VOLUME_DOWN);
    volumeKeys_ = true;
    log::info(L"HUD : touches de volume reprises");
}

HudContent MenuBarApp::volumeContent() {
    HudContent c;
    c.level = audio_.volume();
    c.muted = c.level >= 0 && audio_.muted();
    if (outputName_.empty())
        for (const AudioOutput& o : audio_.outputs())
            if (o.isDefault) outputName_ = o.name;
    c.detail = outputName_;
    return c;
}

void MenuBarApp::onVolumeKey(int id) {
    if (!volumeKeys_ || id < kHotVolUp || id > kHotVolDownFine) return;
    const float v = audio_.volume();
    if (v < 0) return;   // aucune sortie audio : rien à régler (Windows non plus)
    if (id == kHotMute) {
        audio_.setMuted(!audio_.muted());
    } else {
        const bool up = id == kHotVolUp || id == kHotVolUpFine;
        audio_.setVolume(volumeStep(v, up ? 1 : -1, id == kHotVolUpFine || id == kHotVolDownFine));   // enlève la sourdine
        if (settings_.volumeFeedback) playSystemSound(SystemSound::Volume);   // au nouveau volume, comme sur macOS
    }
    updateStatusItems();
    if (!menuOpen_ && !menuSession_) showHud(volumeContent());
}

void MenuBarApp::showHud(const HudContent& c) {
    POINT pt{};
    GetCursorPos(&pt);
    const Screen* at = nullptr;
    for (const auto& s : screens_)
        if (PtInRect(&s->rect, pt)) at = s.get();
    if (!at && !screens_.empty()) at = screens_.front().get();
    if (!at) return;
    // Sous la barre visible ; barre masquée (masquage automatique, plein écran) : sous le haut de l'écran.
    const int barBottom = hudBarBottom(at->rect.top, at->heightPx, at->yOffsetPx);
    for (auto& s : screens_)   // une seule duplication d'écran par processus : la pastille capture à son tour
        if (s->sampler.running()) {
            finishSample(*s, std::nullopt);
            hudStoppedSample_ = true;   // refait quand la pastille se cache
        }
    if (!hud_.show(menuEnv(*at), at->monitor, hudPlace(at->rect, barBottom, at->scale), c)) return;
    hudFade_.show(hudNow());
    SetTimer(ctl_, kHudTimer, UINT(HudFade::kHold * 1000), nullptr);   // rien à animer avant le fondu
}

void MenuBarApp::stepHud() {
    const float o = hudFade_.opacity(hudNow());
    if (o <= 0) {
        hideHud();
        return;
    }
    SetTimer(ctl_, kHudTimer, 16, nullptr);   // fondu (ou fin du maintien à quelques ms près) : toutes les 16 ms
    if (o >= 1) return;
    hud_.setOpacity(o);
}

void MenuBarApp::hideHud() {
    if (ctl_) KillTimer(ctl_, kHudTimer);
    hudFade_.reset();
    if (hud_.visible()) hud_.hide();
    if (hudStoppedSample_ && ctl_) {   // couleur du texte : relevé repris (après un menu s'il s'en ouvre un)
        hudStoppedSample_ = false;
        SetTimer(ctl_, kResampleSoon, 800, nullptr);
    }
}

void MenuBarApp::onSample(Screen& s) {
    if (!s.sampler.running()) return;
    if (s.sampler.failed()) {
        finishSample(s, std::nullopt);
        return;
    }
    if (auto lum = s.sampler.take(s.renderer.device())) finishSample(s, lum);
}

void MenuBarApp::finishSample(Screen& s, std::optional<double> luminance) {
    s.sampler.stop();
    KillTimer(s.hwnd, kSampleTimeout);
    SetWindowDisplayAffinity(s.hwnd, WDA_NONE);   // la barre réapparaît dans les captures d'écran
    s.lastSample = nowSeconds();
    const bool before = s.darkText;
    if (luminance) s.darkText = chooseDarkText(*luminance, s.darkText);
    else s.darkText = !systemDarkMode();   // repli : le thème de Windows
    if (trace_) {
        if (luminance) log::info(L"[trace] barre : luminance du fond %.3f → texte %s", *luminance, s.darkText ? L"foncé" : L"clair");
        else log::info(L"[trace] barre : fond non mesuré → texte %s (thème)", s.darkText ? L"foncé" : L"clair");
    }
    if (before != s.darkText) render(s);
}

// ---- Plein écran et masquage ----

HWND MenuBarApp::detectFullscreen(const Screen& s) const {
    // La plus haute fenêtre de cet écran, qu'elle ait le clavier ou non : la barre reste cachée sur une vidéo en
    // plein écran quand on travaille sur l'autre écran, comme sur Mac.
    return fullscreenWindowOn(s.monitor, s.rect);
}

void MenuBarApp::checkFullscreen() {
    for (std::size_t i = 0; i < screens_.size(); ++i) {
        Screen& s = *screens_[i];
        const HWND window = detectFullscreen(s);
        s.fullscreenWatch.watch(window, [this] { checkFullscreen(); });   // sortie vue tout de suite
        if (window != s.fullscreenWindow) {   // coins carrés le temps du plein écran, arrondis de nouveau à la sortie
            for (HWND h : {s.fullscreenWindow, window})
                if (h && IsWindow(h) && settings_.macWindows) styler_.apply(h, readInfo(h), appsDarkMode(), effectiveDpi(h));
            s.fullscreenWindow = window;
        }
        const bool fs = window != nullptr;
        if (fs == s.fullscreen) continue;
        s.fullscreen = fs;
        if (trace_) log::info(L"[trace] barre : plein écran %s sur l'écran %zu", fs ? L"oui" : L"non", i);
        stepVisibility(s);
    }
}

void MenuBarApp::stepVisibility(Screen& s) {
    if (!s.hwnd) return;
    POINT pt{};
    GetCursorPos(&pt);
    VisibilityInputs in;
    in.autohide = settings_.autohide;
    in.fullscreen = s.fullscreen;
    in.cursorAtEdge = cursorAtTopEdge(s.rect, pt);   // un écran placé au-dessus ne fait pas apparaître celle-ci
    in.cursorInDock = cursorInBar(s.rect, s.heightPx - s.yOffsetPx, pt);
    in.menuOpen = menuOpen_ && menuScreen_ == &s;
    const bool animating = s.visibility.update(in, nowSeconds());
    const int offset = int(std::lround((1.0 - s.visibility.shown()) * s.heightPx));
    const bool show = !s.visibility.hidden();
    if (offset != s.yOffsetPx || show != s.visible) {
        s.yOffsetPx = offset;
        s.visible = show;
        SetWindowPos(s.hwnd, HWND_TOPMOST, s.rect.left, s.rect.top - s.yOffsetPx, 0, 0,
                     SWP_NOACTIVATE | SWP_NOSIZE | (show ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    }
    // Tant que le curseur peut faire apparaître la barre ou qu'elle s'anime, on la suit de près.
    const bool watch = animating || settings_.autohide || s.fullscreen;
    if (watch && !s.visibilityTimer) SetTimer(s.hwnd, kVisibilityTimer, 16, nullptr);
    if (!watch && s.visibilityTimer) KillTimer(s.hwnd, kVisibilityTimer);
    s.visibilityTimer = watch;
}

void MenuBarApp::stepVisibilityAll() {
    for (std::size_t i = 0; i < screens_.size(); ++i) stepVisibility(*screens_[i]);
}

// ---- Menus et actions ----

MenuWindow::Env MenuBarApp::menuEnv(const Screen& s) const {
    MenuWindow::Env env;
    env.instance = instance_;
    env.device = s.renderer.device();
    env.dark = systemDarkMode();
    env.glass = true;
    env.scale = s.scale;
    env.font = s.font;
    env.metrics = glassMetrics_;
    env.trace = trace_;
    return env;
}

void MenuBarApp::onPress(Screen& s, POINT client, bool doubleClick) {
    const BarHit hit = hitTestBar(s.layout, s.layoutIn, double(client.x) / s.scale);
    const std::size_t trayN = trayShown(s);
    if (hit.kind == BarHit::Kind::Left) {
        openMenu(s, hit.index);
    } else if (hit.kind == BarHit::Kind::Right && hit.index < trayN) {
        trayClickAt(s, hit.index, doubleClick ? 2 : 0);   // double-clic : relayé à l'app (certaines s'ouvrent ainsi)
    } else if (hit.kind == BarHit::Kind::Right && hit.index - trayN < status_.size()) {
        const std::size_t j = hit.index - trayN;
        const StatusKind k = status_[j].kind;
        if (opensMenu(k)) {
            openMenu(s, s.layout.leftVisible + j);
        } else if (k == StatusKind::Search && askDockForSpotlight()) {
            // Spotlight du Dock
        } else {   // recherche de Windows (Win+S) si le Dock ne tourne pas ; horloge : centre de notifications (Win+N)
            auto inputs = shortcutInputs(*parseShortcut(k == StatusKind::Search ? L"Win+S" : L"Win+N"));
            SendInput(UINT(inputs.size()), inputs.data(), sizeof(INPUT));
        }
    }
}

void MenuBarApp::openSettingsFile() {
    if (openMacDockSettings(L"menubar")) return;   // sinon (app absente) : le fichier dans le Bloc-notes
    ShellExecuteW(nullptr, L"open", L"notepad.exe", (L"\"" + dataDir_ + L"\\menubar.json\"").c_str(), nullptr, SW_SHOWNORMAL);
}

void MenuBarApp::onRightClick(Screen& s, POINT client) {
    const BarHit hit = hitTestBar(s.layout, s.layoutIn, double(client.x) / s.scale);
    if (hit.kind == BarHit::Kind::Right && hit.index < trayShown(s)) {
        trayClickAt(s, hit.index, 1);   // menu de l'app
        return;
    }
    if (hit.kind != BarHit::Kind::None) return;
    MenuModel m;
    m.items.push_back({kCmdBarSettings, L"Réglages de la barre des menus…"});
    MenuItem autohide{kCmdBarAutohide, L"Masquer automatiquement la barre des menus"};
    autohide.checked = settings_.autohide;
    m.items.push_back(autohide);
    m.items.push_back({});
    m.items.push_back({kCmdBarQuit, L"Quitter la barre des menus"});
    RECT win{};
    GetWindowRect(s.hwnd, &win);
    POINT anchor{client.x + win.left, win.bottom + LONG(std::lround(s.scale))};
    stopSamples();
    menuSession_ = true;
    menuOpen_ = true;
    menuScreen_ = &s;
    ReleaseCapture();
    const int r = MenuWindow::track(menuEnv(s), m, anchor, MenuWindow::Side::Below);
    afterMenu();   // s n'est plus utilisée : les écrans ont pu changer
    if (r <= 0) restoreTargetFocus();
    switch (r) {
        case kCmdBarSettings: openSettingsFile(); break;
        case kCmdBarAutohide:
            checkSettingsFile();   // un changement fait dans l'app Réglages depuis la dernière relecture n'est pas écrasé
            settings_.autohide = !settings_.autohide;
            saveJsonFileAtomic(dataDir_ + L"\\menubar.json", menuBarSettingsToJson(settings_));
            fileTime(dataDir_ + L"\\menubar.json", settingsTime_);
            applySettings();
            stepVisibilityAll();
            break;
        case kCmdBarQuit: PostMessageW(ctl_, WM_CLOSE, 0, 0); break;
        default: break;
    }
}

void MenuBarApp::openMenu(Screen& s, std::size_t index) {
    stopSamples();
    menuSession_ = true;   // jusqu'à afterMenu : les écrans ne sont pas refaits sous le menu
    menuScreen_ = &s;
    int current = int(index), previous = -1;
    const BarTarget target = target_;   // la cible au moment de l'ouverture, quoi qu'il arrive pendant le menu
    for (;;) {
        menuOpen_ = false;
        const int left = int(s.layout.leftVisible), n = left + int(status_.size());
        const auto hasMenu = [&](int i) { return i < left || opensMenu(status_[std::size_t(i - left)].kind); };
        if (previous >= 0 && current >= 0 && current < n && !hasMenu(current)) {
            // Flèches du clavier : la recherche et l'horloge n'ont pas de menu, on passe au suivant dans le même sens.
            const int dir = (previous + 1) % n == current ? 1 : -1;
            for (int k = 0; k < n && !hasMenu(current); ++k) current = ((current + dir) % n + n) % n;
        }
        if (current >= left && current < n) {   // icône d'état
            if (!hasMenu(current)) break;
            menuOpen_ = true;
            highlight_ = current;
            render(s);
            StatusCommand chosen;
            const int r = trackStatus(s, std::size_t(current - left), barLink(s, current), chosen);
            if (auto next = menuSwitchTarget(r)) {
                previous = current;
                current = *next;
                continue;
            }
            afterMenu();   // s n'est plus utilisée : les écrans ont pu changer
            target_ = target;
            if (chosen.first != StatusAction::None) runStatus(chosen);
            else restoreTargetFocus();
            return;
        }
        if (current >= 0 && std::size_t(current) < menus_.menus.size() && syncRealTitles()) {
            // L'app a changé sa barre (document ouvert, MDI) : le titre cliqué est retrouvé par son texte.
            const std::wstring title = menus_.menus[std::size_t(current)].title;
            relayout();
            render();
            current = -1;
            for (std::size_t i = 0; i < menus_.menus.size(); ++i)
                if (menus_.menus[i].title == title) current = int(i);
            if (current < 0) break;
        }
        if (current >= 0 && std::size_t(current) < menus_.menus.size()) refreshRealMenu(menus_.menus[std::size_t(current)].real);
        menus_ = buildBarMenus(context(true, s.scale));   // liste des fenêtres, vrais menus et documents récents à jour
        menuOpen_ = true;
        if (current < 0 || std::size_t(current) >= s.layout.leftVisible || std::size_t(current) >= menus_.menus.size()) break;
        // Copies : MenuWindow lit le modèle pendant toute sa boucle modale.
        const MenuModel model = menus_.menus[std::size_t(current)].model;
        const std::map<int, MenuAction> actions = menus_.actions;
        highlight_ = current;
        render(s);
        const MenuWindow::BarLink link = barLink(s, current);
        POINT anchor{link.titles[std::size_t(current)].left, link.titles[std::size_t(current)].bottom + LONG(std::lround(s.scale))};
        ReleaseCapture();   // sinon l'appui sur le titre garde la souris : glisser-relâcher dans le menu ne marcherait pas
        if (trace_) log::info(L"[trace] barre : menu « %s » ouvert", menus_.menus[std::size_t(current)].title.c_str());
        const int r = MenuWindow::track(menuEnv(s), model, anchor, MenuWindow::Side::Below, &link);
        if (auto next = menuSwitchTarget(r)) {
            previous = current;
            current = *next;
            continue;
        }
        afterMenu();   // s n'est plus utilisée : les écrans ont pu changer
        target_ = target;
        if (r > 0) {
            if (auto it = actions.find(r); it != actions.end()) execute(it->second);
        } else {
            restoreTargetFocus();
        }
        return;
    }
    afterMenu();
}

void MenuBarApp::onZoomMenu(HWND window, POINT anchor) {
    if (menuOpen_ || menuSession_ || !window || !IsWindow(window) || IsIconic(window) || IsHungAppWindow(window)) return;
    const HMONITOR mon = MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST);
    Screen* s = nullptr;
    for (auto& x : screens_)
        if (x->monitor == mon) s = x.get();
    for (auto& x : screens_)
        if (!s && x->primary) s = x.get();
    if (!s && !screens_.empty()) s = screens_.front().get();
    if (!s) return;
    MenuWindow::Env env = menuEnv(*s);
    UINT dpiX = 96, dpiY = 96;   // écran de la pastille (sans barre : l'échelle d'une autre barre ne convient pas)
    if (SUCCEEDED(GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) env.scale = float(dpiX) / 96.0f;
    ZoomMenuContext c;
    c.resizable = (GetWindowLongPtrW(window, GWL_STYLE) & WS_THICKFRAME) != 0;
    c.zoomed = IsZoomed(window) != FALSE;
    c.option = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    const BarMenus menu = buildZoomMenu(c);   // copie gardée : MenuWindow le lit pendant toute sa boucle
    const HWND before = GetForegroundWindow();
    stopSamples();
    menuOpen_ = true;   // le device de la barre sert au menu ; la barre elle-même ne se montre pas (menuScreen_ nul)
    menuSession_ = true;
    if (trace_) log::info(L"[trace] barre : menu de la pastille verte (fenêtre %p, ⌥ %d)", static_cast<void*>(window), int(c.option));
    const int r = MenuWindow::track(env, menu.menus.front().model, anchor, MenuWindow::Side::BelowLeft);
    afterMenu();
    lights_.zoomMenuClosed(window);   // pointeur resté sur la pastille : pas de nouveau menu
    const auto it = r > 0 ? menu.actions.find(r) : menu.actions.end();
    if (it == menu.actions.end()) {   // rien de choisi : le premier plan d'avant revient
        if (before && IsWindow(before) && GetForegroundWindow() != before) SetForegroundWindow(before);
        return;
    }
    ActionContext ctx;
    ctx.target.window = window;
    const bool done = runAction(it->second, ctx, sys_);
    if (done && IsWindow(window) && !IsIconic(window)) SetForegroundWindow(window);   // la fenêtre rangée passe devant
    if (trace_)
        log::info(L"[trace] barre : pastille verte, action %d (%s) %s", int(it->second.kind), it->second.arg.c_str(),
                  done ? L"exécutée" : L"sans effet");
}

void MenuBarApp::onTray(WPARAM wp, LPARAM lp) {
    std::unique_ptr<ipc::Message> m(reinterpret_cast<ipc::Message*>(lp));
    if (wp == 1) {
        tray_.clear();   // le mod (re)connecté renvoie toute sa liste ; parti, ses icônes ne sont plus à jour
    } else if (m) {
        if (auto e = ipc::parseTrayUpdate(*m)) tray_.update(*e);
        else if (auto r = ipc::parseTrayRemove(*m)) tray_.remove(*r);
        else return;
    }
    SetTimer(ctl_, kTrayLayoutTimer, 50, nullptr);   // une rafale (connexion) : une seule mise en page
}

void MenuBarApp::trayClickAt(Screen& s, std::size_t k, int button) {
    if (k >= trayShown(s) || k >= s.layout.rightX.size()) return;
    const ipc::TrayIconEvent& e = trayLaid_[s.trayFirst + k].e;
    HWND app = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(e.hwnd));
    if (!IsWindow(app)) {   // app fermée sans retirer son icône
        tray_.remove(e);
        SetTimer(ctl_, kTrayLayoutTimer, 50, nullptr);
        return;
    }
    RECT win{};
    GetWindowRect(s.hwnd, &win);
    const POINT anchor{win.left + LONG(std::lround((s.layout.rightX[k] + s.layoutIn.rightWidths[k] / 2) * s.scale)), win.bottom};
    DWORD pid = 0;
    GetWindowThreadProcessId(app, &pid);
    AllowSetForegroundWindow(pid);   // son menu ou sa fenêtre s'ouvre devant
    for (const TrayPost& p : trayClick(e, button, anchor)) PostMessageW(app, p.msg, p.wp, p.lp);
    if (trace_) log::info(L"[trace] barre : clic %s sur l'icône « %s »", button == 1 ? L"droit" : button == 2 ? L"double" : L"gauche", e.tip.c_str());
}

MenuWindow::BarLink MenuBarApp::barLink(const Screen& s, int current) const {
    RECT win{};
    GetWindowRect(s.hwnd, &win);
    MenuWindow::BarLink link;
    const auto box = [&](double x, double w) {
        const LONG l = win.left + LONG(std::lround(x * s.scale));
        return RECT{l, win.top, l + LONG(std::lround(w * s.scale)), win.bottom};
    };
    for (std::size_t i = 0; i < s.layout.leftVisible; ++i) link.titles.push_back(box(s.layout.leftX[i], s.layoutIn.leftWidths[i]));
    const std::size_t trayN = trayShown(s);   // icônes d'apps : leur menu est celui de l'app, hors de la barre
    for (std::size_t j = 0; j < status_.size() && trayN + j < s.layout.rightX.size(); ++j)   // sans menu : rectangle vide
        link.titles.push_back(opensMenu(status_[j].kind) ? box(s.layout.rightX[trayN + j], s.layoutIn.rightWidths[trayN + j])
                                                          : RECT{});
    link.current = current;
    return link;
}

int MenuBarApp::trackStatus(Screen& s, std::size_t j, const MenuWindow::BarLink& link, StatusCommand& chosen) {
    const StatusKind kind = status_[j].kind;
    StatusState opened = statusState();
    if (kind == StatusKind::Sound) opened.outputs = audio_.outputs();   // relues à la prochaine ouverture
    const StatusMenu menu = statusMenu(kind, opened);
    const auto actionOf = [&menu](int id) {
        auto it = menu.actions.find(id);
        return it == menu.actions.end() ? StatusAction::None : it->second.first;
    };
    MenuWindow::Live live;
    live.slider = [this, actionOf](int id, double v) {
        if (actionOf(id) == StatusAction::Volume) audio_.setVolume(float(v));
        else if (actionOf(id) == StatusAction::Brightness) {
            brightnessGate_.noteOwnChange(hudNow());   // l'avis qui suivra ne montre pas la pastille
            hub_.post([v] { setBrightness(v); }, kBrightnessJob);
        }
    };
    live.toggle = [this, actionOf](int id, bool on) {
        if (actionOf(id) == StatusAction::WifiPower) hub_.post([on] { setRadio(false, on); });
    };
    live.tile = [this, &menu, &chosen](int id, int k) {
        auto it = menu.tiles.find(id);
        if (it == menu.tiles.end() || k < 0 || std::size_t(k) >= it->second.size()) return false;
        const StatusCommand& c = it->second[std::size_t(k)];
        if (c.first == StatusAction::WifiPower) {
            const bool on = !snap_.network.wifiOn;
            hub_.post([on] { setRadio(false, on); });
            return false;
        }
        if (c.first == StatusAction::Bluetooth) {
            const bool on = !snap_.radios.btOn;
            hub_.post([on] { setRadio(true, on); });
            return false;
        }
        chosen = c;   // réglages, recopie de l'écran : le menu se ferme d'abord
        return true;
    };
    live.media = [this](int, int button) { hub_.post([button] { mediaCommand(button); }); };
    live.refresh = [this, kind, &opened](MenuModel& m) {   // mêmes lignes, donc mêmes actions par identifiant
        m = refreshStatusMenu(kind, opened, statusState());
        return true;
    };
    hub_.refreshNow();
    const RECT& box = link.titles[std::size_t(link.current)];
    POINT anchor{box.left, box.bottom + LONG(std::lround(s.scale))};
    ReleaseCapture();
    if (trace_) log::info(L"[trace] barre : menu d'état %d ouvert", int(kind));
    const int r = MenuWindow::track(menuEnv(s), menu.model, anchor, MenuWindow::Side::Below, &link, &live);
    if (r > 0)
        if (auto it = menu.actions.find(r); it != menu.actions.end()) chosen = it->second;
    return r;
}

void MenuBarApp::runStatus(const StatusCommand& c) {
    bool done = true;
    switch (c.first) {
        case StatusAction::Output: done = audio_.setDefault(c.second); break;
        case StatusAction::WifiConnect: hub_.post([ssid = c.second] { connectWifi(ssid); }); break;
        case StatusAction::OpenUri:
            done = INT_PTR(ShellExecuteW(nullptr, L"open", c.second.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
            break;
        case StatusAction::Shortcut:
            if (auto sc = parseShortcut(c.second)) {
                auto inputs = shortcutInputs(*sc);
                SendInput(UINT(inputs.size()), inputs.data(), sizeof(INPUT));
            }
            break;
        case StatusAction::BarSettings: openSettingsFile(); break;
        default: done = false; break;   // curseurs, interrupteurs, média : déjà faits pendant le menu
    }
    if (trace_) log::info(L"[trace] barre : action d'état %d (%s) %s", int(c.first), c.second.c_str(), done ? L"faite" : L"sans effet");
    if (!done)
        if (auto f = statusFallback(c)) runStatus(*f);   // sortie refusée : réglages Son
}

void MenuBarApp::execute(const MenuAction& a) {
    if (a.kind == ActionKind::UiaInvoke) {
        restoreTargetFocus();   // le menu de l'app se déplie dans une app au premier plan
        HWND w = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(a.window));
        uia_.post([w, path = a.path, name = a.arg, trace = trace_](UiaMenus& uia) {
            const bool done = uia.invoke(w, path, name);
            if (trace) log::info(L"[trace] barre : entrée « %s » %s", name.c_str(), done ? L"invoquée" : L"introuvable");
        });
        return;
    }
    if (a.kind == ActionKind::ClearRecent) {   // le dossier Récents de Windows n'est pas touché
        recent_.apps.clear();
        FILETIME now{};
        GetSystemTimeAsFileTime(&now);
        recent_.clearedAt = (std::uint64_t(now.dwHighDateTime) << 32) | now.dwLowDateTime;
        recentDirty_ = true;
        saveRecent();
        return;
    }
    ActionContext ctx;
    ctx.target = target_;
    ctx.appWindows = appWindows();
    ctx.hidden = &hidden_;
    ctx.exePath = active_.exePath;
    const bool done = runAction(a, ctx, sys_);
    if (trace_) log::info(L"[trace] barre : action %d (%s) %s", int(a.kind), a.arg.c_str(), done ? L"exécutée" : L"sans effet");
}

// ---- Fenêtre ----

LRESULT CALLBACK MenuBarApp::controlProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (self_ && self_->ctl_ == hwnd) return self_->handle(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK MenuBarApp::screenProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (self_)
        if (Screen* s = self_->screenOf(hwnd)) return self_->handleScreen(*s, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT MenuBarApp::handleScreen(Screen& s, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;   // le clavier reste à l'app
        case WM_LBUTTONDOWN: onPress(s, POINT{short(LOWORD(lp)), short(HIWORD(lp))}); return 0;
        case WM_LBUTTONDBLCLK: onPress(s, POINT{short(LOWORD(lp)), short(HIWORD(lp))}, true); return 0;
        case WM_RBUTTONUP: onRightClick(s, POINT{short(LOWORD(lp)), short(HIWORD(lp))}); return 0;
        case WM_APP_APPBAR:
            if (wp == ABN_POSCHANGED) {
                reposition(s);
                layoutScreen(s);
                render(s);
            } else if (wp == ABN_FULLSCREENAPP) {
                checkFullscreen();
            }
            return 0;
        case WM_APP_SAMPLE: onSample(s); return 0;
        case WM_TIMER:
            if (wp == kSampleTimeout) finishSample(s, s.sampler.sawBlack() ? std::optional<double>(0.0) : std::nullopt);
            else if (wp == kVisibilityTimer) stepVisibility(s);
            return 0;
        case WM_DPICHANGED: PostMessageW(ctl_, WM_APP_SCREENS, 0, 0); return 0;   // échelle changée : barres refaites
        case WM_CLOSE: PostMessageW(ctl_, WM_CLOSE, 0, 0); return 0;
        default: break;
    }
    return DefWindowProcW(s.hwnd, msg, wp, lp);
}

LRESULT MenuBarApp::handle(UINT msg, WPARAM wp, LPARAM lp) {
    if (tracker_.handleMessage(msg, wp, lp)) return 0;
    if (shotRevealMsg_ && msg == shotRevealMsg_) {
        onScreenshotReveal(wp != 0);
        return 1;
    }
    if (taskbarCreated_ && msg == taskbarCreated_) {
        // Sans réenregistrement, la bande de la barre n'est plus réservée : les fenêtres agrandies passent dessous et
        // leur barre de titre devient impossible à saisir.
        log::info(L"Barre : Explorateur redémarré, réenregistrement des zones réservées");
        for (auto& s : screens_) {
            removeAppBar(*s);
            syncAppBar(*s);
        }
        repositionAll();
        return 0;
    }
    switch (msg) {
        case WM_APP_UIA_TITLES: onUiaTitles(lp); return 0;
        case WM_APP_ZOOM_MENU:
            onZoomMenu(reinterpret_cast<HWND>(wp), POINT{short(LOWORD(lp)), short(HIWORD(lp))});
            return 0;
        case WM_APP_STATUS: onStatus(lp); return 0;
        case WM_APP_TRAY: onTray(wp, lp); return 0;
        case WM_APP_VOLUME:
            if (wp == 1) {   // nouvelle sortie par défaut : on la suit
                audio_.watch(ctl_, WM_APP_VOLUME);
                outputName_.clear();
                if (hud_.visible() && audio_.volume() < 0) hideHud();   // plus de sortie : la pastille serait fausse
            }
            updateStatusItems();
            // Touches non reprises : la pastille suit le volume changé ailleurs (pas sous un menu : son curseur suffit).
            if (wp == 0 && settings_.hud && !volumeKeys_ && !menuOpen_ && !menuSession_) {
                const HudContent c = volumeContent();
                if (c.level >= 0) showHud(c);
            }
            return 0;
        case WM_HOTKEY:
            onVolumeKey(int(wp));
            return 0;
        case WM_POWERBROADCAST:
            if (wp == PBT_APMRESUMEAUTOMATIC) brightnessGate_.noteSystemChange(hudNow());   // le niveau est réappliqué
            if (wp == PBT_POWERSETTINGCHANGE && lp) {
                const auto* p = reinterpret_cast<const POWERBROADCAST_SETTING*>(lp);
                if (IsEqualGUID(p->PowerSetting, GUID_CONSOLE_DISPLAY_STATE) || IsEqualGUID(p->PowerSetting, GUID_ACDC_POWER_SOURCE))
                    brightnessGate_.noteSystemChange(hudNow());   // écran rallumé, secteur ou batterie : niveau du profil
                DWORD pct = 0;
                if (IsEqualGUID(p->PowerSetting, GUID_VIDEO_CURRENT_MONITOR_BRIGHTNESS) && p->DataLength >= sizeof(DWORD) &&
                    (memcpy(&pct, p->Data, sizeof pct), brightnessGate_.accept(hudNow(), menuOpen_ || menuSession_, int(pct))) &&
                    settings_.hud) {
                    HudContent c;
                    c.kind = HudKind::Brightness;
                    c.level = float(std::min<DWORD>(pct, 100)) / 100.0f;
                    showHud(c);
                }
            }
            return TRUE;
        case WM_DISPLAYCHANGE:   // envoyé (pas posté) : il peut arriver pendant un appel à Windows, on refait après
            PostMessageW(ctl_, WM_APP_SCREENS, 0, 0);
            return 0;
        case WM_APP_SCREENS:
            hideHud();   // sa place (écran, échelle) n'est plus sûre
            rebuildScreens();
            SetTimer(ctl_, kResampleSoon, 800, nullptr);
            return 0;
        case WM_TIMER:
            switch (wp) {
                case kClockTimer: {
                    const std::wstring before = clock_;
                    updateClock();
                    if (clock_.size() != before.size()) relayout();   // largeur changée (« 9:59 » → « 10:00 »)
                    render();
                    scheduleClock();
                    break;
                }
                case kResampleTimer: startSamples(); break;
                case kResampleSoon:   // fond ou clair/sombre changé : barre rééchantillonnée, fenêtres recolorées
                    KillTimer(ctl_, kResampleSoon);
                    startSamples();
                    if (settings_.macWindows) {
                        styler_.applyAll(appsDarkMode());
                        lights_.attach(lights_.target(), settings_.trafficLights);   // couleur sous les pastilles remesurée
                    }
                    break;
                case kConfigTimer: checkSettingsFile(); break;
                case kForegroundTimer:   // premier plan changé sans annonce : rattrapé
                    if (HWND fg = GetForegroundWindow(); fg && fg != lastForeground_) {
                        DWORD pid = 0;   // nos menus déroulants prennent le premier plan : jamais traités ici
                        GetWindowThreadProcessId(fg, &pid);
                        if (pid != GetCurrentProcessId()) onForeground(fg);
                    }
                    break;
                case kDesktopFocusTimer: decideDesktopFocus(); break;
                case kRecentTimer: saveRecent(); break;
                case kFullscreenTimer:
                    checkFullscreen();
                    updateActiveScreen();   // une fenêtre déplacée vers un autre écran
                    break;
                case kTrayLayoutTimer:
                    KillTimer(ctl_, kTrayLayoutTimer);
                    relayout();
                    render();
                    break;
                case kHudTimer: stepHud(); break;
                case kShotRevealTimer: onScreenshotReveal(false); break;
                case kTrayPruneTimer:
                    if (tray_.prune([](std::uint64_t h) { return IsWindow(reinterpret_cast<HWND>(static_cast<std::uintptr_t>(h))) != FALSE; }))
                        SetTimer(ctl_, kTrayLayoutTimer, 50, nullptr);
                    break;
                default: break;
            }
            return 0;
        case WM_SETTINGCHANGE:
            if (wp == SPI_SETDESKWALLPAPER ||
                (lp && CompareStringOrdinal(reinterpret_cast<const wchar_t*>(lp), -1, L"ImmersiveColorSet", -1, TRUE) == CSTR_EQUAL))
                SetTimer(ctl_, kResampleSoon, 800, nullptr);   // après la transition du fond
            return 0;
        case WM_TIMECHANGE:
            updateClock();
            relayout();
            render();
            scheduleClock();
            return 0;
        case WM_ENDSESSION:
            if (wp)
                for (auto& s : screens_) removeAppBar(*s);
            return 0;
        case WM_CLOSE:
            for (auto& s : screens_) removeAppBar(*s);
            PostQuitMessage(0);
            return 0;
        default: break;
    }
    return DefWindowProcW(ctl_, msg, wp, lp);
}

int MenuBarApp::runSnapshot(const Options& options) {
    auto owned = std::make_unique<Screen>();   // barre de l'écran principal, sans fenêtre
    Screen& s = *owned;
    if (!s.renderer.initOffscreen()) return 2;
    s.renderer.setLogo(logo_);
    s.monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(s.monitor, &mi);
    s.rect = mi.rcMonitor;
    s.primary = true;
    UINT dpiX = 96, dpiY = 96;
    GetDpiForMonitor(s.monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    s.dpi = dpiX;
    s.scale = float(dpiX) / 96.0f;
    s.heightPx = int(std::lround(settings_.metrics.height * s.scale));
    s.font = s.renderer.setFont(settings_.font, float(settings_.metrics.fontSize) * s.scale);
    screens_.push_back(std::move(owned));
    const UINT W = UINT(s.rect.right - s.rect.left), H = UINT(s.heightPx);
    const bool dark = options.dark.value_or(systemDarkMode());

    // Fond : le fond d'écran donné (mis à la taille de l'écran, bande du haut), sinon un dégradé selon le thème.
    std::vector<std::uint8_t> bg(std::size_t(W) * H * 4);
    UINT ww = 0, wh = 0;
    auto wall = options.wallpaper.empty() ? std::vector<std::uint8_t>() : readPng(options.wallpaper, ww, wh);
    if (!wall.empty()) {
        const UINT mh = UINT(s.rect.bottom - s.rect.top);
        auto scaled = resizeBgra(wall, ww, wh, W, mh);
        std::copy(scaled.begin(), scaled.begin() + std::ptrdiff_t(bg.size()), bg.begin());
    } else {
        for (UINT y = 0; y < H; ++y)
            for (UINT x = 0; x < W; ++x) {
                const float t = float(x) / float(W);
                std::uint8_t* p = &bg[(std::size_t(y) * W + x) * 4];
                if (dark) { p[0] = std::uint8_t(90 + 40 * t); p[1] = std::uint8_t(40 + 20 * t); p[2] = std::uint8_t(30); }
                else { p[0] = std::uint8_t(235); p[1] = std::uint8_t(215 - 20 * t); p[2] = std::uint8_t(190 + 30 * t); }
                p[3] = 255;
            }
    }
    const double lum = stripLuminance(bg.data(), int(W), int(H), int(W * 4));
    s.darkText = chooseDarkText(lum, false);
    active_.name = options.app.empty() ? L"Notes" : options.app;
    active_.explorer = active_.name == L"Explorateur";
    audio_.init();
    snap_.network = readNetwork();   // les radios et la lecture en cours (WinRT, fil MTA) ne servent pas ici
    snap_.battery = readBattery();
    relayout();
    menuScreen_ = &s;
    highlight_ = options.open;
    std::vector<std::uint8_t> out;
    if (!s.renderer.renderToImage(frame(s), bg, W, H, out) || !writePng(options.snapshot, out.data(), W, H)) {
        log::error(L"Barre : --snapshot impossible");
        return 1;
    }
    log::info(L"Barre --snapshot : %ux%u, luminance %.3f, texte %s, police %s, %zu titres visibles sur %zu", W, H, lum,
              s.darkText ? L"foncé" : L"clair", s.font.c_str(), s.layout.leftVisible, menus_.menus.size());
    return 0;
}

int MenuBarApp::run(HINSTANCE instance, const Options& options) {
    self_ = this;
    instance_ = instance;
    trace_ = options.trace;
    dataDir_ = appDataDir();
    log::init(dataDir_ + L"\\logs\\menubar");
    loadSettings(true);
    loadLogo();
    if (!options.snapshot.empty()) return runSnapshot(options);

    log::info(L"MacMenuBar démarre");
    sys_ = realSystemActions();
    WNDCLASSEXW control{sizeof control};
    control.lpfnWndProc = controlProc;
    control.hInstance = instance;
    control.lpszClassName = kClassName;
    RegisterClassExW(&control);
    WNDCLASSEXW bar{sizeof bar};
    bar.style = CS_DBLCLKS;   // double-clic sur une icône d'app
    bar.lpfnWndProc = screenProc;
    bar.hInstance = instance;
    bar.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    bar.lpszClassName = kScreenClass;
    RegisterClassExW(&bar);
    // Fenêtre de contrôle cachée, de premier niveau : elle reçoit les messages diffusés (écrans, réglages, heure).
    ctl_ = CreateWindowExW(WS_EX_TOOLWINDOW, kClassName, L"MacMenuBar", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    if (!ctl_) {
        log::error(L"Barre : CreateWindowEx a échoué (%lu)", GetLastError());
        return 2;
    }
    loadSettings(false);   // écrit menubar.json s'il manque (la barre tourne maintenant)
    shotRevealMsg_ = RegisterWindowMessageW(L"MacDockScreenshotReveal");
    taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");
    ChangeWindowMessageFilterEx(ctl_, taskbarCreated_, MSGFLT_ALLOW, nullptr);
    lights_.create(instance);
    lights_.setZoomMenuSink(ctl_, WM_APP_ZOOM_MENU);
    rebuildScreens();
    if (screens_.empty()) {
        DestroyWindow(ctl_);
        return exitCode_ ? exitCode_ : 2;
    }

    WindowTracker::Events ev;
    ev.opened = [this](HWND h, const AppIdentity& id) { model_.windowOpened(toId(h), id); };
    ev.closed = [this](HWND h) { model_.windowClosed(toId(h)); };
    ev.minimized = [this](HWND h, bool m) { model_.windowMinimized(toId(h), m); };
    ev.titleChanged = [this](HWND h, const std::wstring& t) { model_.windowTitle(toId(h), t); };
    ev.foreground = [this](HWND h) { onForeground(h); };   // bureau et dialogues compris
    ev.flashed = [](HWND) {};
    tracker_.start(ctl_, ev);
    uia_.start();   // sinon : menus Win32 et génériques seulement
    audio_.init();   // sinon : pas d'icône du son
    audio_.watch(ctl_, WM_APP_VOLUME);
    registerVolumeKeys();
    brightnessNotify_ = RegisterPowerSettingNotification(ctl_, &GUID_VIDEO_CURRENT_MONITOR_BRIGHTNESS, DEVICE_NOTIFY_WINDOW_HANDLE);
    displayNotify_ = RegisterPowerSettingNotification(ctl_, &GUID_CONSOLE_DISPLAY_STATE, DEVICE_NOTIFY_WINDOW_HANDLE);
    powerNotify_ = RegisterPowerSettingNotification(ctl_, &GUID_ACDC_POWER_SOURCE, DEVICE_NOTIFY_WINDOW_HANDLE);
    if (!hub_.start(ctl_, WM_APP_STATUS)) log::warn(L"Barre : relevés d'état indisponibles");
    trayPipe_.setConnectionHandler([ctl = ctl_](bool) { PostMessageW(ctl, WM_APP_TRAY, 1, 0); });
    trayPipe_.start(L"\\\\.\\pipe\\MacMenuBar", [ctl = ctl_](const ipc::Message& m) {
        if (m.type != ipc::MsgType::TrayUpdate && m.type != ipc::MsgType::TrayRemove) return;
        auto* copy = new ipc::Message(m);
        if (!PostMessageW(ctl, WM_APP_TRAY, 0, reinterpret_cast<LPARAM>(copy))) delete copy;
    });
    SetTimer(ctl_, kTrayPruneTimer, 5000, nullptr);
    loadRecent();
    styler_.setEnabled(settings_.macWindows, appsDarkMode());   // fenêtres déjà ouvertes
    onForeground(GetForegroundWindow());
    if (active_.name.empty()) {   // rien d'identifiable au premier plan : le bureau
        active_.name = L"Explorateur";
        active_.explorer = active_.desktop = true;
        relayout();
    }
    updateActiveScreen();

    render();
    scheduleClock();
    startSamples();
    SetTimer(ctl_, kResampleTimer, 60000, nullptr);
    SetTimer(ctl_, kConfigTimer, 400, nullptr);   // l'app Réglages : changements appliqués presque tout de suite
    SetTimer(ctl_, kFullscreenTimer, 1000, nullptr);
    SetTimer(ctl_, kForegroundTimer, 500, nullptr);
    checkFullscreen();
    stepVisibilityAll();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    log::info(L"MacMenuBar s'arrête");
    hideHud();
    for (int id : {kHotVolUp, kHotVolDown, kHotMute, kHotVolUpFine, kHotVolDownFine}) UnregisterHotKey(ctl_, id);
    for (HPOWERNOTIFY n : {brightnessNotify_, displayNotify_, powerNotify_})
        if (n) UnregisterPowerSettingNotification(n);
    tracker_.stop();
    uia_.stop();
    hub_.stop();
    trayPipe_.stop();
    audio_.unwatch();
    saveRecent();
    for (MSG m; PeekMessageW(&m, nullptr, WM_APP_UIA_TITLES, WM_APP_UIA_TITLES, PM_REMOVE);)   // résultats en attente
        delete reinterpret_cast<UiaTitles*>(m.lParam);
    for (MSG m; PeekMessageW(&m, nullptr, WM_APP_STATUS, WM_APP_STATUS, PM_REMOVE);)
        delete reinterpret_cast<StatusSnapshot*>(m.lParam);
    for (MSG m; PeekMessageW(&m, nullptr, WM_APP_TRAY, WM_APP_TRAY, PM_REMOVE);)
        delete reinterpret_cast<ipc::Message*>(m.lParam);
    lights_.destroy();
    styler_.restoreAll();   // fenêtres des autres apps rendues à l'apparence de Windows
    for (auto& s : screens_) destroyScreen(*s);   // zones réservées rendues
    screens_.clear();
    DestroyWindow(ctl_);
    return exitCode_;
}

} // namespace md
