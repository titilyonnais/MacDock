#include "dock_window.h"

#include <dcomp.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <shldisp.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <map>

#include "../anim/genie.h"
#include "../apps/apps_window.h"
#include "../calib/image_diff.h"
#include "../calib/png_io.h"
#include "../config/config_store.h"
#include "../core/diag.h"
#include "../quicklook/quicklook_logic.h"
#include "../quicklook/quicklook_shell.h"
#include "../screenshot/screen_grab.h"
#include "../sound/sound_play.h"
#include "../core/log.h"
#include "../core/strings.h"
#include "../popup/menu_window.h"
#include "../popup/stack_window.h"
#include "../stack/stack_icon.h"
#include "../stack/stack_list.h"
#include "../theme/theme_system.h"
#include "../shell/default_pins.h"
#include "../shell/shell_actions.h"
#include "../tracker/app_identity.h"
#include "../mission/mission_view.h"
#include "../spotlight/spotlight_window.h"
#include "dock_menus.h"
#include "drop_target.h"
#include "thumbnails.h"
#include "visibility.h"

namespace md {

DockApp* DockApp::self_ = nullptr;

namespace {
constexpr wchar_t kClassName[] = L"MacDockWindow";
constexpr UINT WM_APP_IPC_FLASH = WM_APP + 1;
constexpr UINT WM_APP_CONFIG = WM_APP + 2;
constexpr UINT WM_APP_APPBAR = WM_APP + 3;
constexpr UINT WM_APP_MOUSE = WM_APP + 4;
constexpr UINT WM_APP_WAKE = WM_APP + 5;
constexpr UINT WM_APP_PING = WM_APP + 6;
constexpr UINT WM_APP_BACKDROP = WM_APP + 7;
constexpr UINT WM_APP_TRASH = WM_APP + 8;
constexpr UINT WM_APP_DROP = WM_APP + 9;
constexpr UINT WM_APP_STACKS = WM_APP + 10;
constexpr UINT WM_APP_THEME = WM_APP + 11;   // lParam : ThemeResult de themeJob_
constexpr double kGenieSettleSeconds = 0.08;   // fin d'ouverture : dernière image gardée par-dessus la fenêtre
constexpr UINT WM_APP_SWITCHKEY = WM_APP + 13;   // wParam : kHotSwitch… (frappe prise par le crochet clavier)
constexpr UINT WM_APP_CORNER = WM_APP + 12;  // wParam : HotCornerAction (lancée hors du suivi du pointeur)
constexpr UINT WM_APP_QUICKLOOK = WM_APP + 30;      // wParam : fenêtre au premier plan (Espace dans une vue Shell)
constexpr UINT WM_APP_QUICKLOOK_KEY = WM_APP + 31;  // wParam : touche pendant l'aperçu (Espace, Échap, Entrée)
constexpr UINT WM_APP_SHOT = WM_APP + 32;        // wParam : 1 écran entier (⊞⇧3), 2 viseur (⊞⇧4) ; lParam : ⌃
constexpr UINT WM_APP_SHOT_KEY = WM_APP + 33;    // wParam : ShotSessionKey (Échap, Espace pendant le viseur)
constexpr UINT WM_APP_SHOT_SAVED = WM_APP + 34;  // wParam : écrit ; lParam : std::wstring* (chemin, à libérer)
constexpr UINT_PTR kShotResumeTimer = 0x5352;    // "SR" : capture du verre reprise, Dock de nouveau exclu
constexpr UINT_PTR kRecordTimer = 0x5245;        // "RE" : durée de l'enregistrement dans la pastille
constexpr ULONG_PTR kCommandReplay = 0x4D44434B;   // "MDCK" : frappes envoyées par la touche ⌘ (le crochet les laisse)
constexpr UINT_PTR kQuickLookTimer = 0x514C;        // "QL" : l'aperçu suit la sélection de l'Explorateur
constexpr ULONG_PTR kQuickLookReplay = 0x4D44514C;   // Espace rejoué pour l'Explorateur : le crochet le laisse passer
constexpr UINT WM_APP_BUTTON = WM_APP + 14;  // wParam : 1 appui, 0 relâchement ; lParam : point écran (crochet)
constexpr UINT_PTR kArmTimer = 0x414D;       // "AM" : réduction annoncée qui ne vient pas
constexpr UINT_PTR kTransitionTimer = 0x5447;   // "TG" : animations de Windows rendues aux fenêtres libérées
constexpr UINT_PTR kWarmTimer = 0x574D;      // "WM" : case survolée assez longtemps : capture préparée
constexpr UINT_PTR kCornerTimer = 0x4352;    // "CR" : action de coin différée
constexpr UINT_PTR kStacksTimer = 0x5354;   // "ST" : regroupe les avis d'un dossier de pile (téléchargement…)
constexpr UINT_PTR kConfigTimer = 0x4346;   // "CF"
constexpr UINT_PTR kTrashTimer = 0x5442;    // "TB"
constexpr UINT_PTR kVisibilityTimer = 0x5649;   // "VI" : fin du délai de masquage
constexpr UINT_PTR kFullscreenTimer = 0x4653;   // "FS" : vérification périodique du plein écran

BOOL CALLBACK collectMonitor(HMONITOR mon, HDC, LPRECT, LPARAM lp) {
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof mi;
    if (GetMonitorInfoW(mon, &mi))
        reinterpret_cast<std::vector<MonitorInfo>*>(lp)->push_back(
            {mi.szDevice, mi.rcMonitor, (mi.dwFlags & MONITORINFOF_PRIMARY) != 0});
    return TRUE;
}

std::vector<MonitorInfo> enumMonitors() {
    std::vector<MonitorInfo> out;
    EnumDisplayMonitors(nullptr, nullptr, collectMonitor, reinterpret_cast<LPARAM>(&out));
    return out;
}
// Raccourcis de calibration (Ctrl+Alt+Maj) : superposition, opacité + et −.
constexpr int kHotOverlay = 1, kHotOpacityUp = 2, kHotOpacityDown = 3, kHotSpotlight = 4, kHotMission = 5;
// Sélecteur d'apps : Alt+Tab, Alt+Maj+Tab, puis le temps d'une session Alt+Échap, Alt+←, Alt+→, Alt+Q, Alt+H.
constexpr int kHotSwitch = 6, kHotSwitchBack = 7, kHotSwitchEsc = 8, kHotSwitchLeft = 9, kHotSwitchRight = 10,
              kHotSwitchQuit = 11, kHotSwitchHide = 12, kHotAppExpose = 14;
constexpr UINT_PTR kSwitchTimer = 0x5357;   // "SW" : Alt toujours enfoncé ?

double nowSeconds() {
    static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return double(t.QuadPart) / double(freq.QuadPart);
}

std::vector<HWND> toHwnds(const std::vector<WindowId>& ids) {
    std::vector<HWND> out;
    for (auto id : ids) out.push_back(reinterpret_cast<HWND>(static_cast<std::uintptr_t>(id)));
    return out;
}

WindowId toId(HWND h) { return static_cast<WindowId>(reinterpret_cast<std::uintptr_t>(h)); }
} // namespace

bool DockApp::systemDarkMode() {
    DWORD value = 1, size = sizeof value;
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return value == 0;
}

void DockApp::loadConfig(bool initial) {
    auto s = loadJsonFile(dataDir_ + L"\\settings.json");
    // Au rechargement à chaud, un fichier invalide ou illisible ne change rien : on garde l'état courant.
    if (!initial && (s.wasInvalid || s.unreadable)) {
        log::warn(L"settings.json ignoré (invalide ou illisible) : réglages actuels conservés");
    } else {
        if (s.fromFile && jsonVersion(s.value) < kSettingsVersion) {
            s.value = migrateSettingsJson(s.value);
            saveJsonFileAtomic(dataDir_ + L"\\settings.json", s.value);
            log::info(L"settings.json migré de la v1 à la v%d", kSettingsVersion);
        }
        settings_ = settingsFromJson(s.value);
        savedSettings_ = settingsToJson(settings_);
    }
    if (shouldImportDefaultPins(s, settings_)) {
        settings_.pinned = defaultPins();
        settings_.pinnedInitialized = true;
        saveSettings();
        log::info(L"Premier lancement : %zu épingles par défaut", settings_.pinned.size());
    }
    auto m = loadJsonFile(dataDir_ + L"\\dock-metrics.json");
    if (m.fromFile && !m.wasInvalid && jsonVersion(m.value) < kMetricsVersion) {
        m.value = migrateMetricsJson(m.value);
        saveJsonFileAtomic(dataDir_ + L"\\dock-metrics.json", m.value);
        log::info(L"dock-metrics.json migré vers la v%d", kMetricsVersion);
    }
    if (initial || !(m.wasInvalid || m.unreadable)) metrics_ = metricsFromJson(m.value);
    // Fichier absent ou incomplet (mesures ajoutées par une version plus récente) : on l'écrit complet.
    if (!m.wasInvalid && !m.unreadable && (!m.fromFile || !metricsJsonComplete(m.value)))
        saveJsonFileAtomic(dataDir_ + L"\\dock-metrics.json", metricsToJson(metrics_));

    // Les épingles ne sont rechargées que si le fichier a été modifié à la main.
    auto pinsJson = [](const std::vector<PinnedEntry>& pins) {
        Settings t;
        t.pinned = pins;
        return json::serialize(*settingsToJson(t).find("pinned"));
    };
    if (initial || pinsJson(model_.pinnedEntries()) != pinsJson(settings_.pinned)) model_.loadPinned(settings_.pinned);
    applySettings();
}

void DockApp::applySettings() {
    model_.setShowRecents(settings_.showRecents);
    watchStacks();   // piles ajoutées, retirées ou triées autrement (rechargement de settings.json)
    icons_.setStrictTahoe(settings_.tahoeStrictIcons);
    icons_.setGrid(metrics_.iconShapeRatio, metrics_.iconCornerRatio, metrics_.iconJailInset, metrics_.iconShadowOpacity);
    controller_.setSettings(settings_);
    controller_.setMetrics(metrics_);
    visibility_.setTimings({metrics_.autohideDelay, metrics_.autohideLeaveDelay, metrics_.autohideShowSeconds,
                            metrics_.autohideHideSeconds});
    syncAppBar();
    if (hwnd_ && !snapshot_ && settings_.screen != placedScreen_) onDisplayChanged();   // écran choisi dans l'app
    else if (hwnd_ && !snapshot_ && settings_.position != placedPosition_) reposition();   // bord changé à chaud
    if (!snapshot_) minAnimate_.apply(settings_.minimizeEffect);   // l'animation de Windows ne double pas la nôtre
    if (!snapshot_) genie_.prepare(instance_);
    updateGlass();   // réglage glass modifié à chaud
    shotKeysOn_ = settings_.screenshots && !snapshot_;
    commandKeyOn_ = settings_.altAsCommand && !snapshot_;
    if (spotlightMsg_) registerSpotlightHotkey();   // après le démarrage seulement (fenêtre prête)
    if (missionMsg_) {   // après le démarrage seulement (fenêtre prête)
        registerMissionHotkey();
        registerAppExposeHotkey();
        registerSwitcherHotkey();
    }
    requestFrame();
}

// Masquage automatique : pas de zone réservée (le Dock passe au-dessus des fenêtres, comme sur macOS).
void DockApp::syncAppBar() {
    if (!hwnd_ || snapshot_) return;
    bool want = !settings_.autohide;
    if (want == appBar_) return;
    if (want) registerAppBar();
    else removeAppBar();
    reposition();
}

HWND DockApp::detectFullscreen() const {
    // La plus haute fenêtre de l'écran du Dock, qu'elle ait le clavier ou non : une vidéo en plein écran le reste
    // quand on travaille sur un autre écran.
    return fullscreenWindowOn(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY), monitor_);
}

bool DockApp::fullscreenAt(POINT screen) const {
    QUERY_USER_NOTIFICATION_STATE q{};
    if (SUCCEEDED(SHQueryUserNotificationState(&q)) &&
        (q == QUNS_RUNNING_D3D_FULL_SCREEN || q == QUNS_BUSY || q == QUNS_PRESENTATION_MODE))
        return true;
    const HMONITOR mon = MonitorFromPoint(screen, MONITOR_DEFAULTTONULL);
    MONITORINFO mi{sizeof mi};
    return mon && GetMonitorInfoW(mon, &mi) && fullscreenWindowOn(mon, mi.rcMonitor) != nullptr;
}

void DockApp::checkFullscreen() {
    const HWND window = detectFullscreen();
    // Sa sortie du plein écran (taille, fermeture) est vue tout de suite, pas à la vérification suivante.
    fullscreenWatch_.watch(window, [this] { checkFullscreen(); });
    const bool fs = window != nullptr;
    if (fs == fullscreen_) return;
    fullscreen_ = fs;
    if (trace_) log::info(L"[trace] plein écran : %s", fs ? L"oui" : L"non");
    requestFrame();
}

// Entrées du masquage, puis décalage du Dock ; true tant que l'animation continue.
bool DockApp::stepVisibility(double now) {
    VisibilityInputs in;
    in.autohide = settings_.autohide;
    in.fullscreen = fullscreen_;
    in.cursorAtEdge = cursorAtEdge_;
    in.cursorInDock = cursorInDock_;
    in.menuOpen = menuOpen_ && !shotSession_;   // le viseur ne fait pas sortir le Dock masqué
    in.dragging = controller_.dragging();
    bool animating = visibility_.update(in, now);
    controller_.setShown(visibility_.shown());
    bool hidden = visibility_.hidden();
    if (hidden != loggedHidden_) {
        loggedHidden_ = hidden;
        if (trace_) log::info(L"[trace] Dock %s", hidden ? L"masqué" : L"visible");
    }
    if (double at = visibility_.wakeAt(); at >= 0)
        SetTimer(hwnd_, kVisibilityTimer, UINT(std::max(1.0, (at - now) * 1000 + 1)), nullptr);
    return animating;
}

void DockApp::savePinned() {
    settings_.pinned = model_.pinnedEntries();
    saveSettings();
    watchStacks();
}

void DockApp::registerAppBar() {
    APPBARDATA abd{};
    abd.cbSize = sizeof abd;
    abd.hWnd = hwnd_;
    abd.uCallbackMessage = WM_APP_APPBAR;
    appBar_ = SHAppBarMessage(ABM_NEW, &abd) != FALSE;
}

void DockApp::removeAppBar() {
    if (!appBar_) return;
    APPBARDATA abd{};
    abd.cbSize = sizeof abd;
    abd.hWnd = hwnd_;
    SHAppBarMessage(ABM_REMOVE, &abd);
    appBar_ = false;
}

HMONITOR DockApp::dockMonitor() {
    monitors_ = enumMonitors();
    // Écran enregistré s'il est branché (il revient dès qu'on le rebranche), sinon l'écran courant ; aucun
    // (« Écran principal ») : le principal.
    std::size_t i = dockMonitorIndex(monitors_, settings_.screen, screenName_);
    if (i >= monitors_.size()) return MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    if (monitors_[i].name != screenName_) {
        screenName_ = monitors_[i].name;
        log::info(L"[trace] écran du Dock : %s", screenName_.c_str());
    }
    const RECT& r = monitors_[i].rect;
    return MonitorFromPoint(POINT{(r.left + r.right) / 2, (r.top + r.bottom) / 2}, MONITOR_DEFAULTTOPRIMARY);
}

void DockApp::checkScreenPush(POINT screen) {
    auto hit = pushedMonitor(monitors_, screen, settings_.position, int(metrics_.autohideEdgePx));
    std::wstring target = hit ? monitors_[*hit].name : std::wstring();
    // Poussée sur l'écran du Dock, ou pendant un menu, une pile ou un glisser : rien à faire.
    if (target == screenName_ || menuOpen_ || controller_.dragging()) target.clear();
    std::wstring chosen = screenPush_.update(target, nowSeconds());
    if (chosen.empty()) return;
    screenName_ = chosen;
    settings_.screen = chosen;
    saveSettings();
    log::info(L"[trace] écran du Dock : %s (poussée)", chosen.c_str());
    onDisplayChanged();
}

void DockApp::onDisplayChanged() {
    cornerScreens_.clear();   // relus au prochain mouvement
    icons_.clear();
    renderer_.releaseImages();
    reposition();
    captureFailed_ = false;
    if (!renderer_.isWarp() && !rendererOnDockAdapter()) {
        // L'écran du Dock est passé sur une autre carte (station d'accueil, eGPU) : on suit.
        capture_.stop();
        glassLive_ = false;
        if (initRenderer()) {
            RECT rc;
            GetClientRect(hwnd_, &rc);
            renderer_.resize(UINT(rc.right), UINT(rc.bottom));
        }
    }
    if (capture_.status() != BackdropCapture::Status::Off) restartCapture();
    updateGlass();
}

void DockApp::reposition() {
    HMONITOR mon = dockMonitor();
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(mon, &mi);
    monitor_ = mi.rcMonitor;
    UINT dpiX = 96, dpiY = 96;
    GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    scale_ = float(dpiX) / 96.0f;

    const int reserve = int(DockController::reservePx(settings_, metrics_, scale_));
    const int thick = int(DockController::windowHeightPx(settings_, metrics_, scale_));   // épaisseur de la fenêtre
    const DockPosition edge = settings_.position;

    // Bande du bord occupée par le Dock ; la barre d'application la décale des autres barres (barre Windows…).
    RECT band = monitor_;
    if (edge == DockPosition::Left) band.right = band.left + reserve;
    else if (edge == DockPosition::Right) band.left = band.right - reserve;
    else band.top = band.bottom - reserve;
    if (appBar_) {
        APPBARDATA abd{};
        abd.cbSize = sizeof abd;
        abd.hWnd = hwnd_;
        abd.uEdge = edge == DockPosition::Left ? ABE_LEFT : edge == DockPosition::Right ? ABE_RIGHT : ABE_BOTTOM;
        abd.rc = band;
        SHAppBarMessage(ABM_QUERYPOS, &abd);
        if (edge == DockPosition::Left) abd.rc.right = abd.rc.left + reserve;
        else if (edge == DockPosition::Right) abd.rc.left = abd.rc.right - reserve;
        else abd.rc.top = abd.rc.bottom - reserve;
        SHAppBarMessage(ABM_SETPOS, &abd);
        band = abd.rc;
    }
    int width, height;
    if (edge == DockPosition::Bottom) {
        width = monitor_.right - monitor_.left;
        height = thick;
        origin_ = POINT{monitor_.left, band.bottom - height};
    } else {
        width = thick;
        height = band.bottom - band.top;   // hauteur disponible le long du bord (hors barre Windows)
        origin_ = POINT{edge == DockPosition::Left ? band.left : band.right - width, band.top};
    }
    placedPosition_ = edge;
    placedScreen_ = settings_.screen;
    if (trace_) log::info(L"[trace] zone réservée : %d px (bord %d)", appBar_ ? reserve : 0, int(edge));
    SetWindowPos(hwnd_, HWND_TOPMOST, origin_.x, origin_.y, width, height,
                 SWP_NOACTIVATE | (snapshot_ ? 0 : SWP_SHOWWINDOW));
    renderer_.resize(UINT(width), UINT(height));
    controller_.setViewport(UINT(width), UINT(height), scale_);
    if (capture_.status() != BackdropCapture::Status::Off)
        capture_.setRegion({origin_.x, origin_.y, origin_.x + width, origin_.y + height});
    requestFrame();
}

bool DockApp::initRenderer() {
    HMONITOR mon = hwnd_ ? MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY) : dockMonitor();
    auto adapter = BackdropCapture::adapterFor(mon);
    return renderer_.init(hwnd_, adapter.Get());
}

bool DockApp::rendererOnDockAdapter() {
    auto adapter = BackdropCapture::adapterFor(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY));
    DXGI_ADAPTER_DESC1 d{};
    if (!adapter || FAILED(adapter->GetDesc1(&d))) return false;
    LUID r = renderer_.adapterLuid();
    return r.LowPart == d.AdapterLuid.LowPart && r.HighPart == d.AdapterLuid.HighPart;
}

void DockApp::updateGlass() {
    if (!hwnd_ || snapshot_) return;
    bool want = settings_.glass && renderer_.glassAvailable() && !renderer_.isWarp() && !captureFailed_;
    if (want && !rendererOnDockAdapter()) {
        // La texture partagée ne passe pas d'une carte à l'autre : pas de capture (verre dépoli).
        log::warn(L"Verre : le rendu n'est pas sur la carte de l'écran du Dock ; verre dépoli");
        want = false;
    }
    if (want && !excluded_) {
        // Sans exclusion, le Dock se capturerait lui-même (boucle de rétroaction) : verre désactivé.
        excluded_ = SetWindowDisplayAffinity(hwnd_, WDA_EXCLUDEFROMCAPTURE) != FALSE;
        if (!excluded_) log::warn(L"Exclusion des captures impossible (%lu) : verre dépoli", GetLastError());
    }
    if (!want || !excluded_) {
        capture_.stop();
        glassLive_ = false;
        if (excluded_) SetWindowDisplayAffinity(hwnd_, WDA_NONE);
        excluded_ = false;
        requestFrame();
        return;
    }
    if (capture_.status() == BackdropCapture::Status::Off) restartCapture();
}

void DockApp::restartCapture() {
    capture_.stop();
    glassLive_ = false;
    capturePaused_ = false;
    if (!excluded_) return;
    RECT rc;
    GetWindowRect(hwnd_, &rc);
    capture_.start(hwnd_, WM_APP_BACKDROP, MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY),
                   {rc.left, rc.top, rc.right, rc.bottom});
    requestFrame();
}

void DockApp::pauseCapture() {
    if (capture_.status() != BackdropCapture::Status::Running) return;
    capturePaused_ = true;
    capture_.stop();
}

void DockApp::resumeCapture() {
    if (!capturePaused_) return;
    RECT rc;
    GetWindowRect(hwnd_, &rc);
    capture_.start(hwnd_, WM_APP_BACKDROP, MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY),
                   {rc.left, rc.top, rc.right, rc.bottom});
    requestFrame();
}

// ---- Captures d'écran façon macOS ----

bool DockApp::shotIgnores(HWND h) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId()) return true;   // Dock, viseur, vignette, menus
    wchar_t cls[64] = {};
    GetClassNameW(h, cls, 64);
    return wcscmp(cls, L"MacMenuBarLights") == 0;   // pastilles posées sur la fenêtre : elles en font partie
}

void DockApp::revealForCapture(bool dock, bool lights) {
    revealDock_ = dock && excluded_;
    shotPausedCapture_ = false;
    if (revealDock_) {
        // Le verre garde sa dernière image : sa capture ne doit pas voir le Dock pendant ce temps.
        shotPausedCapture_ = capture_.status() == BackdropCapture::Status::Running;
        if (shotPausedCapture_) pauseCapture();
        SetWindowDisplayAffinity(hwnd_, WDA_NONE);
    }
    revealLights_ = false;
    if (lights && shotRevealMsg_)
        if (HWND bar = FindWindowW(L"MacMenuBarWindow", nullptr)) {
            DWORD_PTR r = 0;
            revealLights_ = SendMessageTimeoutW(bar, shotRevealMsg_, 1, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 400, &r) && r == 1;
        }
    MenuWindow::setCaptureVisible(true);   // menu du Dock ouvert (capture lancée pendant sa boucle)
    SpotlightWindow::setCaptureVisible(true);
    AppsWindow::setCaptureVisible(true);
    // L'affichage change à la prochaine composition : deux images de DWM avant la copie.
    DwmFlush();
    DwmFlush();
}

void DockApp::concealAfterCapture() {
    MenuWindow::setCaptureVisible(false);
    SpotlightWindow::setCaptureVisible(false);
    AppsWindow::setCaptureVisible(false);
    if (revealDock_) {
        SetWindowDisplayAffinity(hwnd_, WDA_EXCLUDEFROMCAPTURE);
        if (shotPausedCapture_) SetTimer(hwnd_, kShotResumeTimer, 150, nullptr);
    }
    if (revealLights_)
        if (HWND bar = FindWindowW(L"MacMenuBarWindow", nullptr)) PostMessageW(bar, shotRevealMsg_, 0, 0);
    revealDock_ = revealLights_ = false;
}

namespace {
BOOL CALLBACK addShotMonitor(HMONITOR m, HDC, LPRECT, LPARAM lp) {
    reinterpret_cast<std::vector<HMONITOR>*>(lp)->push_back(m);
    return TRUE;
}
} // namespace

void DockApp::takeScreenShot(bool clipboard) {
    if (viewfinder_.active() || snapshot_) return;
    std::vector<HMONITOR> monitors;
    EnumDisplayMonitors(nullptr, nullptr, addShotMonitor, reinterpret_cast<LPARAM>(&monitors));
    // Écran principal d'abord (« … .png »), les autres ensuite (« … (2).png »…), comme sur macOS.
    std::stable_partition(monitors.begin(), monitors.end(), [](HMONITOR m) {
        MONITORINFO mi{sizeof mi};
        return GetMonitorInfoW(m, &mi) && (mi.dwFlags & MONITORINFOF_PRIMARY);
    });
    std::vector<std::pair<HMONITOR, BgraImage>> shots;
    revealForCapture(true, true);
    for (HMONITOR m : monitors) {
        MONITORINFO mi{sizeof mi};
        if (GetMonitorInfoW(m, &mi)) shots.emplace_back(m, grabScreen(mi.rcMonitor));
    }
    concealAfterCapture();
    POINT cursor;
    GetCursorPos(&cursor);
    deliverShots(std::move(shots), MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY), clipboard);
}

void DockApp::startRegionShot(bool clipboard) {
    if (viewfinder_.active() || menuOpen_ || snapshot_) return;
    shotClipboard_ = clipboard;
    shotSession_ = true;
    menuOpen_ = true;   // une fenêtre modale à la fois ; le Dock ne réagit plus au survol
    controller_.setCursor(std::nullopt);
    requestFrame();
    if (!viewfinder_.start(instance_, [this](const ShotViewfinder::Result& r) { onViewfinderDone(r); }, shotIgnores)) {
        shotSession_ = false;
        menuOpen_ = false;
    }
}

void DockApp::openShotToolbar() {
    if (recorder_.recording()) {   // ⊞⇧5 pendant un enregistrement : il s'arrête
        stopRecording();
        return;
    }
    if (viewfinder_.active() || menuOpen_ || shotToolbar_.isOpen() || snapshot_) return;
    shotSession_ = true;   // Échap ferme la barre
    if (!shotToolbar_.open(instance_, [this](int item) { onShotToolbar(item); })) shotSession_ = false;
}

void DockApp::onShotToolbar(int item) {
    shotSession_ = false;
    switch (item) {
        case kToolScreen: takeScreenShot(false); break;
        case kToolWindow:
            startRegionShot(false);
            viewfinder_.key(ShotSessionKey::ToggleWindow);   // directement en mode fenêtre
            break;
        case kToolRegion: startRegionShot(false); break;
        case kToolRecordScreen: {
            POINT cursor;
            GetCursorPos(&cursor);
            MONITORINFO mi{sizeof mi};
            GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY), &mi);
            startRecording(mi.rcMonitor);
            break;
        }
        case kToolRecordRegion:
            recordAfterViewfinder_ = true;
            startRegionShot(false);
            break;
        default: break;   // ×
    }
}

void DockApp::startRecording(const RECT& area) {
    if (recorder_.recording()) return;
    const std::wstring dir = desktopFolder();
    if (dir.empty()) return;
    SYSTEMTIME now;
    GetLocalTime(&now);
    recordingPath_ = uniqueRecordingPath(dir, recordingBaseName(now), [](const std::wstring& p) {
        return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
    });
    recordingMonitor_ = MonitorFromRect(&area, MONITOR_DEFAULTTONEAREST);
    if (!recorder_.start(area, recordingPath_, 30)) return;
    log::info(L"Enregistrement de l'écran : %s", recordingPath_.c_str());
    recPill_.show(instance_, recordingMonitor_, [this] { stopRecording(); });
    SetTimer(hwnd_, kRecordTimer, 500, nullptr);
}

void DockApp::stopRecording() {
    if (!recorder_.recording()) return;
    KillTimer(hwnd_, kRecordTimer);
    recPill_.hide();
    BgraImage last;
    const bool ok = recorder_.stop(&last);
    if (ok) log::info(L"Enregistrement terminé : %s", recordingPath_.c_str());
    else log::warn(L"Enregistrement : fichier incomplet (%s)", recordingPath_.c_str());
    if (!last.px.empty()) {
        shotThumb_.show(instance_, last, recordingPath_, recordingMonitor_);   // un clic ouvre la vidéo
        shotThumb_.fileSaved(recordingPath_, ok);
    }
}

void DockApp::onViewfinderDone(const ShotViewfinder::Result& r) {
    shotSession_ = false;
    menuOpen_ = false;
    requestFrame();
    const bool clipboard = shotClipboard_;
    shotClipboard_ = false;
    const bool record = recordAfterViewfinder_;   // ⊞⇧5, « enregistrer une zone »
    recordAfterViewfinder_ = false;
    if (record) {
        if (r.kind == ShotViewfinder::Result::Kind::Region) startRecording(r.rect);
        return;
    }
    if (r.kind == ShotViewfinder::Result::Kind::Cancel) return;
    BgraImage img;
    if (r.kind == ShotViewfinder::Result::Kind::Region) {
        revealForCapture(true, true);
        img = grabScreen(r.rect);
        concealAfterCapture();
    } else if (IsWindow(r.window)) {
        // La fenêtre seule : copie de l'écran si rien ne la recouvre (pastilles comprises, Dock exclu : on voit ce
        // qu'il cache), sinon sa propre image.
        RECT frame = windowFrameBounds(r.window);
        revealForCapture(false, true);
        if (rectOnScreens(frame, screenRects()) && windowUnobscured(r.window, frame, shotIgnores)) img = grabScreen(frame);
        concealAfterCapture();
        if (img.px.empty()) img = grabWindow(r.window, frame);
        if (!img.px.empty()) {
            roundCorners(img, windowCornerRadius(r.window));
            if (r.shadow) img = withShadow(img, windowShadowSpec(monitorScale(r.monitor)));
        }
    }
    std::vector<std::pair<HMONITOR, BgraImage>> shots;
    shots.emplace_back(r.monitor, std::move(img));
    deliverShots(std::move(shots), r.monitor, clipboard);
}

void DockApp::deliverShots(std::vector<std::pair<HMONITOR, BgraImage>> shots, HMONITOR thumbOn, bool clipboard) {
    std::erase_if(shots, [](const auto& s) { return s.second.px.empty(); });
    if (shots.empty()) {
        log::warn(L"Capture d'écran : aucune image");
        return;
    }
    if (settings_.sounds) playSystemSound(SystemSound::Screenshot);   // déclic d'appareil photo
    std::size_t main = 0;
    for (std::size_t i = 0; i < shots.size(); ++i)
        if (shots[i].first == thumbOn) main = i;
    if (clipboard) {   // ⌃ : ni fichier ni vignette, comme sur macOS
        if (!copyImageToClipboard(hwnd_, shots[main].second)) log::warn(L"Capture d'écran : presse-papiers indisponible");
        return;
    }
    const std::wstring dir = desktopFolder();
    if (dir.empty()) {
        log::warn(L"Capture d'écran : Bureau introuvable");
        return;
    }
    SYSTEMTIME now;
    GetLocalTime(&now);
    const std::wstring base = screenshotBaseName(now);
    std::vector<std::wstring> paths;
    for (std::size_t i = 0; i < shots.size(); ++i)
        paths.push_back(uniqueScreenshotPath(dir, base, int(i) + 1, [&](const std::wstring& p) {
            return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES || std::find(paths.begin(), paths.end(), p) != paths.end();
        }));
    shotThumb_.show(instance_, shots[main].second, paths[main], shots[main].first);   // tout de suite, depuis la mémoire
    std::erase_if(shotJobs_, [](std::future<void>& f) { return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready; });
    std::vector<BgraImage> images;
    for (auto& s : shots) images.push_back(std::move(s.second));
    const HWND target = hwnd_;
    shotJobs_.push_back(std::async(std::launch::async, [images = std::move(images), paths, target] {
        const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        for (std::size_t i = 0; i < images.size(); ++i) {
            const bool ok = saveScreenshotPng(images[i], paths[i]);
            auto* p = new std::wstring(paths[i]);
            if (!PostMessageW(target, WM_APP_SHOT_SAVED, ok ? 1 : 0, reinterpret_cast<LPARAM>(p))) delete p;
        }
        if (SUCCEEDED(co)) CoUninitialize();
    }));
}

void DockApp::onBackdrop() {
    auto status = capture_.status();
    if (status == BackdropCapture::Status::Unavailable || status == BackdropCapture::Status::Failed) capturePaused_ = false;
    if (capture_.status() == BackdropCapture::Status::Failed) {
        // Échec définitif : on rend le Dock aux captures d'écran ; nouvel essai au prochain changement d'affichage.
        captureFailed_ = true;
        updateGlass();
        return;
    }
    ID3D11Device* dev = renderer_.device();
    if (dev) {
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> ctx;
        dev->GetImmediateContext(&ctx);
        bool scRgb = false;
        float white = 1;
        if (capture_.takeLatest(dev, ctx.Get(), [this](UINT w, UINT h, bool hdr) { return renderer_.backdropTexture(w, h, hdr); },
                                scRgb, white)) {
            renderer_.setBackdropWhite(white);
            capturePaused_ = false;
            if (!glassLive_) log::info(L"Verre : arrière-plan réel reçu (%s)", scRgb ? L"HDR" : L"SDR");
            glassLive_ = true;
            ++capturesTaken_;
            requestFrame();
            return;
        }
    }
    // Changement d'état (capture indisponible ou reprise) : on bascule entre verre réel et repli dépoli.
    requestFrame();
}

void DockApp::setTransparent(bool transparent) {
    if (transparent == transparent_) return;
    transparent_ = transparent;
    if (trace_) log::info(L"[trace] Dock %s", transparent ? L"traversé par les clics" : L"cliquable");
    LONG_PTR ex = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    ex = transparent ? (ex | WS_EX_TRANSPARENT) : (ex & ~WS_EX_TRANSPARENT);
    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, ex);
}

void DockApp::onMouse(POINT screen) {
    POINT client{screen.x - origin_.x, screen.y - origin_.y};
    controller_.setCursor(client);   // ne marque le Dock à redessiner que si son état change
    const bool inside = controller_.pointerInside();
    warmHovered(client);
    const LONG edgePx = LONG(metrics_.autohideEdgePx);
    bool atEdge = false;
    switch (settings_.position) {
        case DockPosition::Left:
            atEdge = screen.x <= monitor_.left + edgePx && screen.y >= monitor_.top && screen.y < monitor_.bottom;
            break;
        case DockPosition::Right:
            atEdge = screen.x >= monitor_.right - 1 - edgePx && screen.y >= monitor_.top && screen.y < monitor_.bottom;
            break;
        default:
            atEdge = screen.y >= monitor_.bottom - 1 - edgePx && screen.x >= monitor_.left && screen.x < monitor_.right;
    }
    checkScreenPush(screen);
    checkHotCorner(screen);
    if (atEdge != cursorAtEdge_ || inside != cursorInDock_) {
        cursorAtEdge_ = atEdge;
        cursorInDock_ = inside;
        if (settings_.autohide) requestFrame();   // réveille la boucle : le masquage réévalue ses entrées
    }
    setTransparent(!inside);
}

// Le Dock change de forme sous un curseur immobile (révélation, icône ajoutée ou retirée, fin d'agrandissement) :
// sans cette réévaluation, il resterait traversé par les clics jusqu'au prochain mouvement de souris.
void DockApp::syncPointer() {
    const std::optional<bool> inside =
        controller_.recheckPointer(POINT{mouseX_.load() - origin_.x, mouseY_.load() - origin_.y});
    if (!inside) return;
    if (*inside != cursorInDock_) {
        cursorInDock_ = *inside;
        if (settings_.autohide) requestFrame();
    }
    setTransparent(!*inside);
}

void DockApp::checkHotCorner(POINT screen) {
    if (cornerScreens_.empty())
        for (const MonitorInfo& m : enumMonitors()) cornerScreens_.push_back(m.rect);
    const std::optional<Corner> at = cornerAt(screen, cornerScreens_);
    const HotCornerAction action = at ? settings_.hotCorners[std::size_t(*at)] : HotCornerAction::Off;
    // Glisser (bouton enfoncé), plein écran sur l'écran du coin, Alt+Tab ou vue modale : rien. Mission Control ouvert :
    // son coin le referme.
    const bool button = ((GetAsyncKeyState(VK_LBUTTON) | GetAsyncKeyState(VK_RBUTTON)) & 0x8000) != 0;
    const bool modal = menuOpen_ && !(action == HotCornerAction::MissionControl && MissionView::isOpen());
    const bool blocked = button || switch_.active() || modal || (at && fullscreenAt(screen));
    if (corners_.update(at, screen, blocked) && action != HotCornerAction::Off)
        PostMessageW(hwnd_, WM_APP_CORNER, WPARAM(action), 0);
}

void DockApp::runHotCorner(HotCornerAction action) {
    log::info(L"Coin actif : %s", hotCornerName(action).c_str());
    switch (action) {
        case HotCornerAction::MissionControl: openMissionControl(); break;
        case HotCornerAction::Apps: openApps(); break;
        case HotCornerAction::Desktop: {   // comme Win+D : tout réduit, ou tout rétabli
            Microsoft::WRL::ComPtr<IShellDispatch4> shell;
            if (SUCCEEDED(CoCreateInstance(CLSID_Shell, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shell))))
                shell->ToggleDesktop();
            break;
        }
        case HotCornerAction::NotificationCenter:
            ShellExecuteW(nullptr, L"open", L"ms-actioncenter:", nullptr, nullptr, SW_SHOWNORMAL);
            break;
        case HotCornerAction::LockScreen: LockWorkStation(); break;
        case HotCornerAction::DisplaySleep: PostMessageW(hwnd_, WM_SYSCOMMAND, SC_MONITORPOWER, 2); break;
        case HotCornerAction::ScreenSaver: {
            BOOL active = FALSE;
            SystemParametersInfoW(SPI_GETSCREENSAVEACTIVE, 0, &active, 0);
            if (!active) log::warn(L"Coin actif : aucun économiseur d'écran n'est réglé dans Windows");
            else PostMessageW(hwnd_, WM_SYSCOMMAND, SC_SCREENSAVE, 0);
            break;
        }
        case HotCornerAction::Off: break;
    }
}

void DockApp::logItemPositions(const RenderFrame& frame) {
    // Position des éléments à l'écran (essais automatisés, calibration).
    for (std::size_t i = 0; i < frame.icons.size(); ++i)
        if (const DockItem* it = controller_.itemAt(i))
            log::info(L"[trace] élément %zu %s : x=%ld y=%ld", i, it->key.c_str(), origin_.x + LONG(frame.icons[i].cx),
                      origin_.y + LONG(frame.icons[i].cy));
}

void DockApp::onPointerUp(POINT client) {
    DragOutcome o = controller_.pointerUp(client);   // avant ReleaseCapture (WM_CAPTURECHANGED annulerait le glisser)
    if (GetCapture() == hwnd_) ReleaseCapture();
    updateDragSprite();
    using Kind = DragOutcome::Kind;
    if (trace_) log::info(L"[trace] relâchement en %ld,%ld : résultat %d", client.x, client.y, int(o.kind));
    switch (o.kind) {
        case Kind::Click: onClick(o.index); break;
        case Kind::Move:
            if (model_.movePinned(o.fromPinned, o.toPinned)) savePinned();
            if (trace_) log::info(L"[trace] glisser : épingle %zu déplacée en %zu", o.fromPinned, o.toPinned);
            break;
        case Kind::Pin:
            if (model_.pin(o.appId, o.toPinned)) savePinned();
            if (trace_) log::info(L"[trace] glisser : %s épinglée en %zu", o.appId.c_str(), o.toPinned);
            break;
        case Kind::Remove:
            if (model_.unpin(o.key)) savePinned();
            if (trace_) log::info(L"[trace] glisser : %s retirée%s", o.key.c_str(), o.poof ? L" (poof)" : L"");
            if (o.poof) {
                if (settings_.sounds) playSystemSound(SystemSound::Poof);
                GetCursorPos(&poofCenter_);
                poofStart_ = nowSeconds();
            }
            break;
        default: break;
    }
    requestFrame();
}

void DockApp::updateDragSprite() {
    DragVisual v = controller_.dragVisual(icons_);
    if (!v.active || !v.image) {
        dragSprite_.hide();
        dragSpriteKey_ = {};
        return;
    }
    POINT cur;
    GetCursorPos(&cur);
    UINT px = UINT(std::lround(v.sizePx));
    auto& k = dragSpriteKey_;
    if (k.key != v.key || k.removing != v.removing || k.px != px || k.dark != dark_ || !dragSprite_.visible()) {
        UINT w = 0, h = 0;
        auto bgra = sprites_.dragSprite(*v.image, px, v.removing ? L"Supprimer" : L"", scale_, dark_,
                                        renderer_.fontName(settings_.font), w, h);
        if (bgra.empty()) return;
        k = {v.key, v.removing, px, w, h, dark_};
        dragSprite_.show(bgra, w, h, POINT{cur.x - LONG(w / 2), cur.y - LONG(px / 2)});
    } else {
        dragSprite_.move(POINT{cur.x - LONG(k.w / 2), cur.y - LONG(px / 2)});
    }
}

bool DockApp::stepPoof(double now) {
    if (poofStart_ < 0) return false;
    double t = (now - poofStart_) / std::max(0.05, metrics_.poofSeconds);
    if (t >= 1) {
        poofSprite_.hide();
        poofStart_ = -1;
        return false;
    }
    UINT px = UINT(std::lround(settings_.tileSize * 1.6 * scale_));
    auto frame = sprites_.poofFrame(t, px);
    poofSprite_.show(frame, px, px, POINT{poofCenter_.x - LONG(px / 2), poofCenter_.y - LONG(px / 2)});
    return true;
}

void DockApp::onClick(std::size_t index) {
    const DockItem* p = controller_.itemAt(index);
    if (p && p->kind == ItemKind::Stack) openStack(index);
    else if (p) activateItem(*p);
}

MenuWindow::Env DockApp::popupEnv() {
    MenuWindow::Env env;
    env.instance = instance_;
    env.device = renderer_.device();
    env.dark = dark_;
    env.glass = settings_.glass && !captureFailed_ && !renderer_.isWarp();
    env.scale = scale_;
    env.font = renderer_.fontName(settings_.font);
    env.metrics = metrics_;
    env.trace = trace_;
    return env;
}

// Entrées de la liste d'une pile qui tiennent à l'écran (le menu ne défile pas), séparateur et lien compris.
std::size_t DockApp::listCapacity(const StackWindow::Request& r) const {
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(MonitorFromPoint(r.iconCenter, MONITOR_DEFAULTTONEAREST), &mi);
    const double room = r.side == MenuWindow::Side::Above ? double(r.dockEdge - mi.rcMonitor.top)
                                                          : double(mi.rcMonitor.bottom - mi.rcMonitor.top);
    const double rows = (room / scale_ - 2 * kMenuPadding - kMenuSeparatorHeight - 16) / kMenuItemHeight - 1;
    return std::size_t(std::max(1.0, std::floor(rows)));
}

void DockApp::openStack(std::size_t index) {
    if (menuOpen_) return;   // pas de fenêtre modale dans une autre
    endSwitch(false);   // une session Alt+Tab en cours se termine sans activer
    const DockItem* p = controller_.itemAt(index);
    if (!p) return;
    const DockItem item = *p;
    RenderFrame frame = controller_.buildFrame(dark_, icons_);
    if (index >= frame.icons.size()) return;
    StackView view = StackView::Auto;
    StackSort sort = StackSort::DateAdded;
    if (auto i = model_.pinnedIndexOf(item.key)) {
        const PinnedEntry e = model_.pinnedEntries()[*i];
        view = e.stackView;
        sort = e.stackSort;
    }
    StackWindow::Request r;
    r.folder = item.launch;
    r.title = item.name;
    r.items = sortStack(listFolder(item.launch), sort);
    // Dock vertical : la grille s'ouvre à côté (l'éventail ne monte que d'un Dock en bas, comme sur macOS).
    if (view == StackView::List) r.view = StackView::List;
    else r.view = settings_.position == DockPosition::Bottom ? resolveView(view, r.items.size()) : StackView::Grid;
    const RenderIcon& icon = frame.icons[index];
    r.iconCenter = {origin_.x + LONG(std::lround(icon.cx)), origin_.y + LONG(std::lround(icon.cy))};
    switch (settings_.position) {
        case DockPosition::Left:
            r.side = MenuWindow::Side::Right;
            r.dockEdge = origin_.x + LONG(std::lround(frame.bgRight));
            break;
        case DockPosition::Right:
            r.side = MenuWindow::Side::Left;
            r.dockEdge = origin_.x + LONG(std::lround(frame.bgLeft));
            break;
        default:
            r.side = MenuWindow::Side::Above;
            r.dockEdge = origin_.y + LONG(std::lround(frame.bgTop));
    }
    r.tile = settings_.tileSize;
    if (trace_) log::info(L"[trace] pile %s : %zu éléments", item.key.c_str(), r.items.size());
    MenuWindow::Env env = popupEnv();
    controller_.setCursor(std::nullopt);   // l'agrandissement retombe pendant que la pile est ouverte
    requestFrame();
    pauseCapture();   // une seule duplication de l'écran par processus
    menuOpen_ = true;
    std::wstring chosen;
    if (r.view == StackView::List) {
        // Liste : menu en verre, icônes de la liste système (rapides), sous-dossiers en sous-menus.
        std::vector<std::wstring> paths;
        MenuModel menu = stackListMenu(r.folder, r.items,
                                       [&](const std::wstring& p) { return sortStack(listFolder(p), sort); }, paths,
                                       listCapacity(r));
        const int px = int(std::lround(kMenuIconSize * scale_));
        // 400 icônes au plus : borne le travail pour un dossier rempli de sous-dossiers pleins.
        assignListIcons(menu, paths, 400, [&](const std::wstring& path) { return icons_.fileIcon(path, px); });
        const LONG gap = LONG(std::lround(6 * scale_));
        POINT anchor = r.side == MenuWindow::Side::Right  ? POINT{r.dockEdge + gap, r.iconCenter.y}
                       : r.side == MenuWindow::Side::Left ? POINT{r.dockEdge - gap, r.iconCenter.y}
                                                          : POINT{r.iconCenter.x, r.dockEdge - gap};
        int cmd = MenuWindow::track(env, menu, anchor, r.side);
        if (cmd >= kStackListBase && std::size_t(cmd - kStackListBase) < paths.size())
            chosen = paths[std::size_t(cmd - kStackListBase)];
    } else {
        chosen = StackWindow::track(env, r);
    }
    menuOpen_ = false;
    resumeCapture();
    if (chosen == r.folder) openFolder(chosen);
    else if (!chosen.empty()) launchAsync(chosen);
    requestFrame();
}

// Écran Apps sur l'écran du Dock ; menu Démarrer si la vue ne peut pas s'ouvrir ou si le catalogue est vide.
void DockApp::openApps() {
    if (menuOpen_) return;   // second clic d'un double-clic, ou une autre fenêtre modale déjà ouverte
    endSwitch(false);   // une session Alt+Tab en cours se termine sans activer
    std::vector<AppEntry> list = apps_.get(1500);
    if (list.empty()) {
        log::warn(L"Apps : catalogue vide, ouverture du menu Démarrer");
        openStartMenu();
        apps_.refreshAsync();
        return;
    }
    AppsWindow::Request r;
    r.apps = std::move(list);
    r.monitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY);
    r.icons = appsIconStyle();
    const AppsIconStyle& st = r.icons;
    appsIcons_->setStyle(std::to_wstring(st.strict) + L"|" + std::to_wstring(st.dark) + L"|" + std::to_wstring(st.shapeRatio) +
                         L"|" + std::to_wstring(st.cornerRatio) + L"|" + std::to_wstring(st.jailInset) + L"|" +
                         std::to_wstring(st.shadowOpacity) + L"|" + st.customDir);
    r.cache = appsIcons_;
    MenuWindow::Env env = popupEnv();
    controller_.setCursor(std::nullopt);
    requestFrame();
    pauseCapture();   // une seule duplication de l'écran par processus
    menuOpen_ = true;
    const std::optional<std::wstring> chosen = AppsWindow::track(env, r);
    menuOpen_ = false;
    resumeCapture();
    if (!chosen) {
        openStartMenu();
    } else if (!chosen->empty()) {
        const AppEntry* e = nullptr;
        for (const AppEntry& a : r.apps)
            if (a.parsingName == *chosen) e = &a;
        if (e) launchAsync(launchTarget(*e));
    }
    apps_.refreshAsync();   // une app installée entre-temps sera là la prochaine fois
    requestFrame();
}

AppsIconStyle DockApp::appsIconStyle() const {
    AppsIconStyle st;
    st.strict = settings_.tahoeStrictIcons;
    st.dark = dark_;
    st.shapeRatio = metrics_.iconShapeRatio;
    st.cornerRatio = metrics_.iconCornerRatio;
    st.jailInset = metrics_.iconJailInset;
    st.shadowOpacity = metrics_.iconShadowOpacity;
    st.customDir = dataDir_ + L"\\icons";
    return st;
}

namespace {
bool copyText(HWND owner, const std::wstring& text) {   // résultat d'un calcul de Spotlight
    if (!OpenClipboard(owner)) return false;
    EmptyClipboard();
    bool ok = false;
    if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t))) {
        if (auto* p = static_cast<wchar_t*>(GlobalLock(h))) {
            std::copy(text.c_str(), text.c_str() + text.size() + 1, p);
            GlobalUnlock(h);
            ok = SetClipboardData(CF_UNICODETEXT, h) != nullptr;
        }
        if (!ok) GlobalFree(h);
    }
    CloseClipboard();
    return ok;
}
} // namespace

// Spotlight sur l'écran du curseur ; un second appui (raccourci ou loupe) le ferme.
void DockApp::openSpotlight() {
    endSwitch(false);   // une session Alt+Tab en cours se termine sans activer
    if (SpotlightWindow::isOpen()) {
        SpotlightWindow::closeOpen();
        return;
    }
    if (menuOpen_) return;   // une autre fenêtre modale est ouverte
    SpotlightWindow::Request r;
    r.apps = apps_.get(500);
    POINT pt{};
    GetCursorPos(&pt);
    r.monitor = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
    wchar_t profile[MAX_PATH] = {};
    GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
    r.profile = profile;
    r.icons = appsIconStyle();
    MenuWindow::Env env = popupEnv();
    UINT dpiX = 96, dpiY = 96;
    if (SUCCEEDED(GetDpiForMonitor(r.monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) env.scale = float(dpiX) / 96.0f;
    controller_.setCursor(std::nullopt);
    requestFrame();
    pauseCapture();   // une seule duplication de l'écran par processus
    menuOpen_ = true;
    const std::optional<SpotlightWindow::Choice> choice = SpotlightWindow::track(env, r);
    menuOpen_ = false;
    resumeCapture();
    if (choice) {
        const SpotItem& it = choice->item;
        switch (it.kind) {
            case SpotKind::Calc:
                if (!copyText(hwnd_, it.target)) log::warn(L"Spotlight : presse-papiers indisponible");
                break;
            case SpotKind::App:
                launchAsync(it.target);
                break;
            case SpotKind::File:
                if (choice->reveal) revealInExplorer(it.target);
                else launchAsync(it.target);
                break;
        }
    }
    apps_.refreshAsync();
    requestFrame();
}

// Mission Control sur tous les écrans ; un second appui le ferme.
void DockApp::openMissionControl() {
    endSwitch(false);   // une session Alt+Tab en cours se termine sans activer
    if (MissionView::isOpen()) {
        MissionView::closeOpen();
        return;
    }
    if (menuOpen_) return;   // une autre fenêtre modale est ouverte
    MissionView::Request r;
    Microsoft::WRL::ComPtr<IVirtualDesktopManager> desktops;
    CoCreateInstance(CLSID_VirtualDesktopManager, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&desktops));
    for (const DockItem& it : model_.items()) {
        if (it.kind != ItemKind::App) continue;
        for (WindowId id : it.windows) {   // visibles, non réduites, sur le bureau courant
            const HWND h = toHwnds({id}).front();
            if (!IsWindowVisible(h) || IsIconic(h)) continue;
            DWORD cloaked = 0;
            if (SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof cloaked)) && cloaked) continue;
            BOOL here = TRUE;
            if (desktops && SUCCEEDED(desktops->IsWindowOnCurrentVirtualDesktop(h, &here)) && !here) continue;
            r.windows.push_back({h, model_.titleOf(id)});
        }
    }
    MenuWindow::Env env = popupEnv();
    controller_.setCursor(std::nullopt);
    requestFrame();
    menuOpen_ = true;
    const std::optional<HWND> chosen = MissionView::track(env, r);
    menuOpen_ = false;
    if (chosen) activateApp({*chosen});
    requestFrame();
}

// Exposé d'une app : ses fenêtres ouvertes rangées, les réduites en rangée en bas ; un second appui ferme.
bool DockApp::openAppExpose(const std::wstring& appId) {
    endSwitch(false);
    if (MissionView::isOpen()) {
        MissionView::closeOpen();
        return true;
    }
    if (menuOpen_ || appId.empty()) return false;
    MissionView::Request r;
    Microsoft::WRL::ComPtr<IVirtualDesktopManager> desktops;
    CoCreateInstance(CLSID_VirtualDesktopManager, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&desktops));
    for (WindowId id : model_.windowsOf(appId)) {
        const HWND h = toHwnds({id}).front();
        if (!IsWindow(h)) continue;
        BOOL here = TRUE;
        if (desktops && SUCCEEDED(desktops->IsWindowOnCurrentVirtualDesktop(h, &here)) && !here) continue;
        if (IsIconic(h)) {
            r.minimized.push_back({h, model_.titleOf(id)});
            continue;
        }
        DWORD cloaked = 0;
        if (!IsWindowVisible(h) || (SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof cloaked)) && cloaked)) continue;
        r.windows.push_back({h, model_.titleOf(id)});
    }
    if (trace_) log::info(L"[trace] exposé %s : %zu ouverte(s), %zu réduite(s)", appId.c_str(), r.windows.size(), r.minimized.size());
    if (r.windows.empty() && r.minimized.empty()) return false;
    MenuWindow::Env env = popupEnv();
    controller_.setCursor(std::nullopt);
    requestFrame();
    menuOpen_ = true;
    const std::optional<HWND> chosen = MissionView::track(env, r);
    menuOpen_ = false;
    if (chosen) activateApp({*chosen});   // une fenêtre réduite est restaurée
    requestFrame();
    return true;
}

void DockApp::registerAppExposeHotkey() {
    if (settings_.appExposeHotkey == appExposeHotkeyOn_) return;
    UnregisterHotKey(hwnd_, kHotAppExpose);
    appExposeHotkeyOn_ = settings_.appExposeHotkey;
    const auto spec = parseAppExposeHotkey(appExposeHotkeyOn_);
    if (!spec) log::info(L"Exposé d'une app : raccourci désactivé");
    else if (!RegisterHotKey(hwnd_, kHotAppExpose, spec->mods | MOD_NOREPEAT, spec->vk))
        log::warn(L"Exposé d'une app : raccourci %s déjà pris par une autre app (%lu)", appExposeHotkeyOn_.c_str(), GetLastError());
    else log::info(L"Exposé d'une app : raccourci %s", appExposeHotkeyOn_.c_str());
}

void DockApp::registerMissionHotkey() {
    if (settings_.missionControlHotkey == missionHotkeyOn_) return;
    UnregisterHotKey(hwnd_, kHotMission);
    missionHotkeyOn_ = settings_.missionControlHotkey;
    const auto spec = parseMissionHotkey(missionHotkeyOn_);
    if (!spec) {
        log::info(L"Mission Control : raccourci désactivé");
    } else if (!RegisterHotKey(hwnd_, kHotMission, spec->mods | MOD_NOREPEAT, spec->vk)) {
        log::warn(L"Mission Control : raccourci %s déjà pris par une autre app (%lu)", missionHotkeyOn_.c_str(),
                  GetLastError());
    } else {
        log::info(L"Mission Control : raccourci %s", missionHotkeyOn_.c_str());
    }
}

void DockApp::registerSpotlightHotkey() {
    if (settings_.spotlightHotkey == spotlightHotkeyOn_) return;
    UnregisterHotKey(hwnd_, kHotSpotlight);
    spotlightHotkeyOn_ = settings_.spotlightHotkey;
    const auto spec = parseSpotlightHotkey(spotlightHotkeyOn_);
    if (!spec) {
        log::info(L"Spotlight : raccourci désactivé");
    } else if (!RegisterHotKey(hwnd_, kHotSpotlight, spec->mods | MOD_NOREPEAT, spec->vk)) {
        log::warn(L"Spotlight : raccourci %s déjà pris par une autre app (%lu) ; la loupe de la barre reste disponible",
                  spotlightHotkeyOn_.c_str(), GetLastError());
    } else {
        log::info(L"Spotlight : raccourci %s", spotlightHotkeyOn_.c_str());
    }
}

void DockApp::registerSwitcherHotkey() {
    if (settings_.appSwitcherHotkey == switcherHotkeyOn_) return;
    endSwitch(false);
    switcherHotkeyOn_ = settings_.appSwitcherHotkey;
    // Windows garde Alt+Tab pour lui (RegisterHotKey : erreur 1409) : le crochet clavier du fil de la souris le prend.
    switchKeysOn_ = parseSwitcherHotkey(switcherHotkeyOn_).has_value();
    log::info(switchKeysOn_ ? L"Sélecteur d'apps : Alt+Tab repris" : L"Sélecteur d'apps : raccourci désactivé");
}

void DockApp::switcherKey(int id) {
    if (id == kHotSwitch || id == kHotSwitchBack) {
        const bool back = id == kHotSwitchBack;
        if (switch_.active()) {   // appuis suivants : une case de plus (ou de moins)
            switch_.step(back ? -1 : 1);
            switcher_.select(switch_.selected());
            return;
        }
        if (menuOpen_) return;   // une fenêtre modale est ouverte
        std::vector<std::wstring> running;
        for (const DockItem& it : model_.items())
            if (it.kind == ItemKind::App && !model_.windowsOf(it.appId).empty()) running.push_back(it.appId);
        // L'app au premier plan en tête ; bureau ou fenêtre non suivie au premier plan : l'app la plus récente est
        // alors la « précédente », la sélection part d'elle.
        const std::wstring front = model_.appOfWindow(toId(GetAncestor(GetForegroundWindow(), GA_ROOTOWNER)));
        mru_.touch(front);
        switchApps_ = mru_.order(running);
        const bool frontFirst = !front.empty() && !switchApps_.empty() && switchApps_.front() == front;
        // Touche neutre, même sans app : Alt relâché sans autre frappe (Tab est pris par le raccourci) ouvrirait le
        // menu de l'app au premier plan.
        INPUT in[2] = {};
        in[0].type = in[1].type = INPUT_KEYBOARD;
        in[0].ki.wVk = in[1].ki.wVk = 0xE8;
        in[1].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(2, in, sizeof(INPUT));
        if (!switch_.begin(switchApps_.size(), back, nowSeconds(), frontFirst)) return;
        switchSession_ = true;   // le crochet prend aussi Échap, les flèches, Q et H
        SetTimer(hwnd_, kSwitchTimer, 15, nullptr);
        return;
    }
    if (!switch_.active()) return;
    const std::size_t sel = switch_.selected();
    switch (id) {
        case kHotSwitchEsc: endSwitch(false); break;
        case kHotSwitchLeft:
        case kHotSwitchRight:
            switch_.step(id == kHotSwitchLeft ? -1 : 1);
            switcher_.select(switch_.selected());
            break;
        case kHotSwitchQuit:   // comme « Quitter » du menu du Dock ; l'app quitte la rangée
            for (HWND h : toHwnds(model_.windowsOf(switchApps_[sel]))) PostMessageW(h, WM_CLOSE, 0, 0);
            switchApps_.erase(switchApps_.begin() + std::ptrdiff_t(sel));
            switcher_.remove(sel);
            if (switch_.removeSelected()) switcher_.select(switch_.selected());
            else endSwitch(false);
            break;
        case kHotSwitchHide:   // comme « Masquer » du menu du Dock
            model_.setHidden(switchApps_[sel], true);
            hideAll(toHwnds(model_.windowsOf(switchApps_[sel])));   // d'un coup, sans génie
            switch_.hideSelected();
            requestFrame();
            break;
        default: break;
    }
}

void DockApp::switcherTick() {
    switch (switch_.tick((GetAsyncKeyState(VK_MENU) & 0x8000) != 0, nowSeconds())) {
        case SwitchSession::Tick::Finish: endSwitch(true); break;
        case SwitchSession::Tick::ShowPanel: {
            // Apps fermées depuis l'appui : retirées de la rangée (pas de case vide), la sélection reste sur son app.
            const std::wstring chosen = switchApps_[switch_.selected()];
            for (std::size_t i = switchApps_.size(); i-- > 0;) {
                if (!model_.windowsOf(switchApps_[i]).empty()) continue;
                switch_.select(i);
                if (!switch_.removeSelected()) {
                    endSwitch(false);
                    return;
                }
                switchApps_.erase(switchApps_.begin() + std::ptrdiff_t(i));
            }
            if (auto it = std::find(switchApps_.begin(), switchApps_.end(), chosen); it != switchApps_.end())
                switch_.select(std::size_t(it - switchApps_.begin()));
            std::map<std::wstring, std::wstring> names;
            for (const DockItem& it : model_.items())
                if (it.kind == ItemKind::App) names[it.appId] = it.name;
            std::vector<SwitcherWindow::Entry> entries;
            for (const std::wstring& app : switchApps_) entries.push_back({names[app], controller_.appIcon(app, icons_)});
            POINT pt{};
            GetCursorPos(&pt);
            pauseCapture();   // une seule duplication de l'écran par processus
            switchPanel_ = switcher_.show(popupEnv(), MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY), std::move(entries),
                                          switch_.selected());
            if (!switchPanel_) resumeCapture();   // sans panneau, la session continue au clavier
            break;
        }
        case SwitchSession::Tick::Wait: break;
    }
}

void DockApp::endSwitch(bool activate) {
    KillTimer(hwnd_, kSwitchTimer);
    switchSession_ = false;
    const bool was = switch_.activates();   // une app masquée par H pendant la session reste masquée
    const std::size_t sel = switch_.selected();
    switch_.end();
    if (switchPanel_) {
        switcher_.hide();
        switchPanel_ = false;
        resumeCapture();
    }
    if (activate && was && sel < switchApps_.size()) {
        const std::wstring app = switchApps_[sel];
        const std::vector<HWND> windows = toHwnds(model_.windowsOf(app));
        std::vector<bool> iconic;
        for (HWND h : windows) iconic.push_back(IsIconic(h) != FALSE);
        const bool hidden = model_.isHidden(app);
        const SwitchActivation a = switcherActivation(hidden, iconic);
        if (hidden) model_.setHidden(app, false);
        if (a.restoreFirst) {
            restoreWindow(windows[a.windows.front()]);
        } else if (!a.windows.empty()) {
            std::vector<HWND> chosen;
            for (std::size_t i : a.windows) chosen.push_back(windows[i]);
            activateApp(chosen);
        }
    }
    switchApps_.clear();
    requestFrame();
}

// Agit sur une copie de l'élément : les fenêtres sont relues dans le modèle par appId (stable), jamais
// par index (le Dock a pu changer entre-temps, par exemple pendant un menu).
void DockApp::activateItem(const DockItem& item) {
    switch (item.kind) {
        case ItemKind::App:
            if (const AppClick click = model_.clickActionFor(item.appId); click.kind == AppClick::Kind::Restore) {
                restoreFromDock(reinterpret_cast<HWND>(static_cast<std::uintptr_t>(click.windows.front())));   // génie
            } else if (click.kind != AppClick::Kind::Launch) {
                activateApp(toHwnds(click.windows));   // devant (ou tout réaffiché si l'app était masquée)
            } else {
                std::wstring target = item.launch;
                if (target.empty())
                    if (auto id = model_.identityOf(item.appId)) target = id->launch.empty() ? id->exePath : id->launch;
                if (!target.empty() && !controller_.isBouncing(item.appId)) {   // double clic : un seul lancement
                    launchAsync(target);   // jamais sur le fil de l'interface : le Dock reste vivant
                    controller_.startLaunchBounce(item.appId);
                }
            }
            break;
        case ItemKind::AppsButton: openApps(); break;
        case ItemKind::Stack: openFolder(item.launch); break;
        case ItemKind::Trash: openRecycleBin(); break;
        case ItemKind::MinimizedWindow:
            restoreFromDock(reinterpret_cast<HWND>(static_cast<std::uintptr_t>(item.window)));
            break;
        default: break;
    }
    requestFrame();
}

void DockApp::restoreFromDock(HWND window) {
    if (genie_.active() && genie_.source() == window && genie_.restoring()) return;   // déjà en route
    if (!startGenie(window, true)) restoreWindow(window);
}

GenieRun DockApp::genieRun() const {
    GenieRun run{genie_.active(), genie_.active() ? toId(genie_.source()) : 0, genie_.restoring()};
    run.settling = run.active && genieSettleUntil_ >= 0;
    return run;
}

namespace {
// Partie visible d'une fenêtre (sans ses bordures de redimensionnement invisibles), comme sa miniature DWM.
bool visibleBounds(HWND h, RECT& r) {
    if (SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof r)) && !IsRectEmpty(&r)) return true;
    return GetWindowRect(h, &r) != FALSE;
}
} // namespace

// Capture GPU et couverture prêtes avant le relâchement : le génie part sans trou ni attente. Appelé pour le bouton
// « réduire » de Windows (crochet souris) et pour la pastille jaune de la barre (message MacDockGenieArm), qui cache
// ce bouton.
void DockApp::armGenie(HWND w, POINT pt) {
    if (snapshot_ || !w || !IsWindow(w) || IsIconic(w) || !genieWouldAnimate(w)) return;
    // Avant le relâchement, même pendant un autre génie (celui-ci le remplacera) : posée après, DWM jouerait sa
    // propre réduction sous le génie.
    holdTransitions(w);
    if (genie_.active()) return;
    RECT visible{}, dock{};
    if (!visibleBounds(w, visible) || !GetWindowRect(hwnd_, &dock)) return;
    KillTimer(hwnd_, kArmTimer);   // celui d'un appui précédent désarmerait celui-ci
    genie_.arm(instance_, w, visible, dock, pt);
    if (trace_) log::info(L"[trace] réduction annoncée %p", static_cast<void*>(w));
}

void DockApp::holdTransitions(HWND w, bool always) {
    if (snapshot_ || (!always && settings_.minimizeEffect == MinimizeEffect::Windows) || !w) return;
    if (trace_ && !transitions_.held(w)) log::info(L"[trace] animations de Windows coupées pour %p", static_cast<void*>(w));
    transitions_.hold(w, nowSeconds() + 1.5);
    SetTimer(hwnd_, kTransitionTimer, 400, nullptr);
}

bool DockApp::genieWouldAnimate(HWND w) {
    // La case n'existe qu'une fois la fenêtre réduite : avant, on demande au modèle si elle en aura une.
    return settings_.minimizeEffect != MinimizeEffect::Windows && model_.minimizesToTile(toId(w));
}

void DockApp::announceMinimize(HWND w, bool hide) {
    if (snapshot_ || !w || !IsWindow(w) || IsIconic(w)) return;
    const MinimizeAnnounce a = minimizeAnnounce(hide, settings_.minimizeEffect, model_.minimizesToTile(toId(w)));
    if (a.hideApp) {   // ses fenêtres réduites ne deviennent pas des cases ; un clic sur l'app les réaffiche
        const std::wstring app = model_.appOfWindow(toId(w));
        if (!app.empty()) model_.setHidden(app, true);
    }
    if (a.hold) holdTransitions(w, a.hideApp);
}

HeldState DockApp::heldState(HWND w) {
    HeldState s;
    s.iconic = IsIconic(w) != FALSE;
    s.appHidden = model_.isHidden(model_.appOfWindow(toId(w)));
    s.genieArmed = genie_.armed() == w;
    s.genieRunning = genie_.active() && genie_.source() == w;
    return s;
}

void DockApp::onButton(bool down, POINT pt) {
    if (snapshot_ || settings_.minimizeEffect == MinimizeEffect::Windows) return;
    HWND w = GetAncestor(WindowFromPoint(pt), GA_ROOT);
    DWORD pid = 0;
    if (w) GetWindowThreadProcessId(w, &pid);
    if (pid == GetCurrentProcessId()) w = nullptr;   // le Dock et ses fenêtres
    if (down) {
        if (trace_) log::info(L"[trace] appui en %ld,%ld sur %p", pt.x, pt.y, static_cast<void*>(w));
        if (!w || IsIconic(w)) return;   // pendant un génie aussi : la seconde réduction doit être retenue à temps
        RECT visible{}, dock{};
        if (!visibleBounds(w, visible) || !GetWindowRect(hwnd_, &dock)) return;
        // Seulement dans le coin des boutons de titre (haut à droite) : aucun aller-retour vers l'app pour les autres
        // clics.
        UINT dpiX = 96, dpiY = 96;
        GetDpiForMonitor(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
        const double s = dpiX / 96.0;
        if (pt.y - visible.top > LONG(64 * s) || visible.right - pt.x > LONG(240 * s) || pt.x < visible.left) return;
        // Boutons dessinés par DWM : leur vraie place (WM_NCHITTEST garde l'ancienne géométrie sous Windows 11).
        RECT window{}, buttons{};
        GetWindowRect(w, &window);
        DwmGetWindowAttribute(w, DWMWA_CAPTION_BUTTON_BOUNDS, &buttons, sizeof buttons);
        const bool minBox = (GetWindowLongPtrW(w, GWL_STYLE) & WS_MINIMIZEBOX) != 0;
        bool onMin = genieOnMinimizeButton(pt, window, buttons, minBox);
        DWORD_PTR hit = HTNOWHERE;
        if (!onMin && buttons.right <= buttons.left) {   // boutons dessinés par l'app (Chrome, Electron…) : on lui demande
            POINT logical = pt;   // dans son propre repère (DPI)
            PhysicalToLogicalPointForPerMonitorDPI(w, &logical);
            onMin = SendMessageTimeoutW(w, WM_NCHITTEST, 0, MAKELPARAM(logical.x, logical.y), SMTO_ABORTIFHUNG, 40, &hit) &&
                    hit == HTMINBUTTON;
        }
        if (trace_)
            log::info(L"[trace] appui sur les boutons de %p en %ld,%ld : réduire %s (zone DWM %ld..%ld, test %d)",
                      static_cast<void*>(w), pt.x, pt.y, onMin ? L"oui" : L"non", buttons.left, buttons.right, int(hit));
        if (!onMin) return;
        armGenie(w, pt);
        return;
    }
    if (trace_) {
        LARGE_INTEGER q, f;
        QueryPerformanceCounter(&q);
        QueryPerformanceFrequency(&f);
        log::info(L"[trace] relâché en %ld,%ld sur %p (annoncée %p, qpc %.1f ms)", pt.x, pt.y, static_cast<void*>(w),
                  static_cast<void*>(genie_.armed()), double(q.QuadPart) * 1000.0 / double(f.QuadPart));
    }
    if (!genie_.armed()) return;
    if (genie_.revealArmed(pt)) {   // en général déjà fait par le crochet, avant que l'app ne reçoive le relâchement
        SetTimer(hwnd_, kArmTimer, 300, nullptr);
    } else {
        genie_.disarm();
    }
}

void DockApp::warmHovered(POINT client) {
    HWND want = nullptr;
    if (auto hit = controller_.hitTest(client)) {
        if (const DockItem* it = controller_.itemAt(*hit)) {
            if (it->kind == ItemKind::MinimizedWindow) {
                want = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(it->window));
            } else if (it->kind == ItemKind::App) {
                const AppClick c = model_.clickActionFor(it->appId);
                if (c.kind == AppClick::Kind::Restore) want = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(c.windows.front()));
            }
        }
    }
    if (want == hoverWarm_) return;
    hoverWarm_ = want;
    KillTimer(hwnd_, kWarmTimer);
    if (want) SetTimer(hwnd_, kWarmTimer, 120, nullptr);   // un passage rapide sur le Dock ne lance rien
    else genie_.cool();
}

void DockApp::noteForeground() {
    HWND fg = GetForegroundWindow();
    RECT r{};
    if (fg && !IsIconic(fg) && visibleBounds(fg, r)) lastSeen_[toId(fg)] = r;
}

bool DockApp::startGenie(HWND window, bool restore) {
    if (snapshot_ || settings_.minimizeEffect == MinimizeEffect::Windows || !hwnd_) return false;
    if (genieMustRestoreFirst(genieRun())) {   // une restauration interrompue aboutit quand même
        const HWND previous = genie_.source();
        genie_.finish();
        if (IsWindow(previous)) restoreWindow(previous);
    }
    if (genieSettleUntil_ >= 0) {   // la fin d'une ouverture laisse la place
        genieSettleUntil_ = -1;
        genie_.finish();
    }
    // Case de départ (restauration) : celle affichée, agrandie ou non ; sinon celle du Dock au repos.
    std::optional<RECT> cell;
    if (auto it = shownTiles_.find(toId(window)); restore && it != shownTiles_.end()) cell = it->second;
    if (!cell) cell = controller_.restingTile(toId(window));
    if (!cell) return false;   // pas de case (app masquée du Dock)
    RECT dock{};
    GetWindowRect(hwnd_, &dock);
    OffsetRect(&*cell, dock.left, dock.top);
    WINDOWPLACEMENT wp{sizeof wp};
    if (!GetWindowPlacement(window, &wp)) return false;
    MONITORINFO mi{sizeof mi};   // réduite : l'écran de sa place d'avant
    if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &mi)) return false;
    const bool tool = (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0;
    std::optional<RECT> seen;
    if (auto it = lastSeen_.find(toId(window)); it != lastSeen_.end()) seen = it->second;
    const RECT from = genieStartRect(seen, wp, mi.rcWork, mi.rcMonitor, tool, SIZE{});
    const bool slow = GetAsyncKeyState(VK_SHIFT) < 0;   // Maj : ralenti, comme sur macOS
    holdTransitions(window);   // restauration : la fenêtre revient sans l'animation de Windows en plus du génie
    if (!genie_.start(instance_, window, from, *cell, settings_.position, settings_.minimizeEffect, restore, nowSeconds(), slow))
        return false;
    if (trace_) log::info(L"[trace] génie %s %p", restore ? L"restauration" : L"réduction", static_cast<void*>(window));
    requestFrame();   // la miniature de la case s'efface le temps de l'animation
    return true;
}

bool DockApp::stepGenie(double now) {
    if (!genie_.active()) {
        genieSettleUntil_ = -1;
        return false;
    }
    if (genieSettleUntil_ >= 0) {   // fin d'ouverture : la fenêtre restaurée s'affiche sous la dernière image
        if (now < genieSettleUntil_) return true;
        genieSettleUntil_ = -1;
        genie_.finish();
        requestFrame();
        return false;
    }
    if (genie_.step(now)) return true;
    const HWND window = genie_.source();
    if (genie_.restoring() && IsWindow(window)) {
        // La fenêtre reprend sa place sous la dernière image, gardée le temps qu'elle soit composée et redessinée :
        // ni trou d'une image entre les deux, ni contenu qui se rafraîchit à la vue.
        genieSettleUntil_ = now + kGenieSettleSeconds;
        restoreWindow(window);
        requestFrame();
        return true;
    }
    genie_.finish();
    requestFrame();
    return false;
}

void DockApp::saveSettings() {
    // Relu juste avant : ce que l'app Réglages a écrit depuis notre dernière lecture n'est pas écrasé.
    const std::wstring path = dataDir_ + L"\\settings.json";
    const json::Value now = settingsToJson(settings_);
    if (saveJsonFileAtomic(path, dockSettingsToWrite(loadJsonFile(path), savedSettings_, now))) savedSettings_ = now;
}

void DockApp::showContextMenu(std::optional<std::size_t> index) {
    if (menuOpen_) return;   // pas de fenêtre modale dans une autre
    endSwitch(false);   // une session Alt+Tab en cours se termine sans activer
    const DockItem* p = index ? controller_.itemAt(*index) : nullptr;
    MenuContext ctx;
    ctx.item = p ? *p : DockItem{ItemKind::Separator};
    ctx.settings = settings_;
    if (ctx.item.kind == ItemKind::Separator) ctx.themeApplied = themeBackupExists();
    const DockItem& item = ctx.item;
    if (item.kind == ItemKind::App) {
        std::optional<AppIdentity> id = model_.identityOf(item.appId);
        if (id) ctx.exePath = id->exePath;
        if (isPackagedApp(ctx.exePath, item.launch)) {
            // AUMID connu par la fenêtre, sinon tiré de la cible épinglée (shell:AppsFolder\<AUMID>).
            const std::wstring prefix = L"shell:appsfolder\\";
            if (id && !id->aumid.empty()) ctx.aumid = id->aumid;
            else if (toLower(item.launch).starts_with(prefix)) ctx.aumid = item.launch.substr(prefix.size());
            ctx.openAtLogin = isPackagedOpenAtLogin(item.name);
        } else {
            ctx.openAtLogin = isOpenAtLogin(ctx.exePath);
        }
        for (WindowId w : model_.windowsOf(item.appId)) ctx.windows.emplace_back(w, model_.titleOf(w));
    } else if (item.kind == ItemKind::Trash) {
        ctx.trashFull = recycleBinHasItems();
    } else if (item.kind == ItemKind::Stack) {
        if (auto i = model_.pinnedIndexOf(item.key)) {
            const PinnedEntry e = model_.pinnedEntries()[*i];
            ctx.stackView = e.stackView;
            ctx.stackSort = e.stackSort;
            ctx.stackDisplay = e.stackDisplay;
        }
    }

    // Ancrage : face à l'icône (ou au curseur, hors icône), juste au-delà du Dock, côté écran.
    RenderFrame frame = controller_.buildFrame(dark_, icons_);
    POINT cursor;
    GetCursorPos(&cursor);
    const RenderIcon* icon = index && *index < frame.icons.size() ? &frame.icons[*index] : nullptr;
    const float gap = 6 * scale_;
    const float half = icon && !icon->separator ? icon->size / 2 : 0;
    POINT anchor{};
    MenuWindow::Side side = MenuWindow::Side::Above;
    switch (settings_.position) {
        case DockPosition::Left:
            side = MenuWindow::Side::Right;
            anchor.x = origin_.x + LONG(std::lround(std::max(frame.bgRight, icon ? icon->cx + half : 0.0f) + gap));
            anchor.y = icon ? origin_.y + LONG(std::lround(icon->cy)) : cursor.y;
            break;
        case DockPosition::Right:
            side = MenuWindow::Side::Left;
            anchor.x = origin_.x + LONG(std::lround(std::min(frame.bgLeft, icon ? icon->cx - half : frame.bgLeft) - gap));
            anchor.y = icon ? origin_.y + LONG(std::lround(icon->cy)) : cursor.y;
            break;
        default:
            anchor.x = icon ? origin_.x + LONG(std::lround(icon->cx)) : cursor.x;
            anchor.y = origin_.y + LONG(std::lround(std::min(frame.bgTop, icon ? icon->cy - half : frame.bgTop) - gap));
    }

    MenuWindow::Env env = popupEnv();
    controller_.setCursor(std::nullopt);   // l'agrandissement retombe pendant le menu, comme sur macOS
    requestFrame();
    // Une seule duplication de l'écran par processus : celle du Dock cède la place à celle du menu.
    pauseCapture();
    menuOpen_ = true;
    int cmd = MenuWindow::track(env, buildDockMenu(ctx), anchor, side);
    menuOpen_ = false;
    resumeCapture();
    if (trace_) log::info(L"[trace] menu %s : commande %d", item.key.c_str(), cmd);

    if (cmd >= kCmdWindowBase && std::size_t(cmd - kCmdWindowBase) < ctx.windows.size()) {
        restoreWindow(reinterpret_cast<HWND>(static_cast<std::uintptr_t>(ctx.windows[cmd - kCmdWindowBase].first)));
        requestFrame();
        return;
    }
    switch (cmd) {
        case kCmdOpen:
            activateItem(item);
            break;
        case kCmdShowAll:   // Exposé de l'app, comme sur macOS ; rien à montrer ici : l'app passe devant
            if (!openAppExpose(item.appId)) activateItem(item);
            break;
        case kCmdKeep:
            if (model_.pinnedIndexOf(item.key)) {
                if (model_.unpin(item.key)) savePinned();
            } else {
                // Après la dernière app épinglée (les piles peuvent être n'importe où dans le fichier).
                auto entries = model_.pinnedEntries();
                std::size_t at = 0;
                for (std::size_t i = 0; i < entries.size(); ++i)
                    if (entries[i].kind != PinKind::Stack) at = i + 1;
                if (model_.pin(item.appId, at)) savePinned();
            }
            break;
        case kCmdLogin:
            if (!ctx.aumid.empty()) setPackagedOpenAtLogin(ctx.aumid, item.name, !ctx.openAtLogin);
            else setOpenAtLogin(ctx.exePath, item.name, !ctx.openAtLogin);
            break;
        case kCmdReveal:
            if (item.kind == ItemKind::Stack) openFolder(item.launch);
            else revealInExplorer(ctx.exePath);
            break;
        case kCmdHide:
            model_.setHidden(item.appId, true);
            hideAll(toHwnds(model_.windowsOf(item.appId)));   // d'un coup, sans génie
            break;
        case kCmdQuit:
            for (HWND h : toHwnds(model_.windowsOf(item.appId))) PostMessageW(h, WM_CLOSE, 0, 0);
            break;
        case kCmdAutohide:
            // Bascule de l'état affiché dans le menu (la configuration a pu être rechargée entre-temps).
            settings_.autohide = !ctx.settings.autohide;
            saveSettings();
            applySettings();
            break;
        case kCmdMagnify:
            settings_.magnification = !ctx.settings.magnification;
            saveSettings();
            applySettings();
            break;
        case kCmdPosLeft:
        case kCmdPosBottom:
        case kCmdPosRight:
            settings_.position = cmd == kCmdPosLeft ? DockPosition::Left
                                 : cmd == kCmdPosRight ? DockPosition::Right : DockPosition::Bottom;
            saveSettings();
            applySettings();   // déplace le Dock (reposition) si le bord a changé
            break;
        case kCmdSettings: {
            if (openMacDockSettings(L"dock")) break;   // sinon (app absente) : le fichier dans le Bloc-notes
            std::wstring path = L"\"" + dataDir_ + L"\\settings.json\"";
            ShellExecuteW(nullptr, L"open", L"notepad.exe", path.c_str(), nullptr, SW_SHOWNORMAL);
            break;
        }
        case kCmdTrashOpen: openRecycleBin(); break;
        case kCmdTrashEmpty:   // froissement de papier à la place du son de Windows
            if (emptyRecycleBin(hwnd_, settings_.sounds) && settings_.sounds) playSystemSound(SystemSound::EmptyTrash);
            break;
        case kCmdRemove:
            if (model_.unpin(item.key)) savePinned();
            break;
        case kCmdSortDateAdded:
        case kCmdSortName:
        case kCmdSortModified:
        case kCmdSortKind: {
            const StackSort sort = cmd == kCmdSortName       ? StackSort::Name
                                   : cmd == kCmdSortModified ? StackSort::Modified
                                   : cmd == kCmdSortKind     ? StackSort::Kind
                                                             : StackSort::DateAdded;
            if (model_.setStackOptions(item.key, ctx.stackView, sort)) savePinned();
            break;
        }
        case kCmdViewAuto:
        case kCmdViewFan:
        case kCmdViewGrid:
        case kCmdViewList: {
            const StackView view = cmd == kCmdViewFan    ? StackView::Fan
                                   : cmd == kCmdViewGrid ? StackView::Grid
                                   : cmd == kCmdViewList ? StackView::List
                                                         : StackView::Auto;
            if (model_.setStackOptions(item.key, view, ctx.stackSort)) savePinned();
            break;
        }
        case kCmdDisplayStack:
        case kCmdDisplayFolder:
            if (model_.setStackDisplay(item.key, cmd == kCmdDisplayFolder ? StackDisplay::Folder : StackDisplay::Stack))
                savePinned();
            break;
        case kCmdRestore:
            restoreFromDock(reinterpret_cast<HWND>(static_cast<std::uintptr_t>(item.window)));
            break;
        case kCmdEffectGenie:
        case kCmdEffectScale:
        case kCmdEffectWindows:
            settings_.minimizeEffect = cmd == kCmdEffectGenie   ? MinimizeEffect::Genie
                                       : cmd == kCmdEffectScale ? MinimizeEffect::Scale
                                                                : MinimizeEffect::Windows;
            saveSettings();
            applySettings();
            break;
        case kCmdThemeApply:
        case kCmdThemeWallpaper:
        case kCmdThemeRestore: {   // à la demande seulement ; plusieurs secondes en Debug : hors du fil de l'interface
            const bool apply = cmd != kCmdThemeRestore;
            const ThemeParts parts{.cursors = cmd == kCmdThemeApply};
            if (!themeJob_.start([apply, parts] { return apply ? applyMacTheme(parts) : restoreWindowsTheme(); }, hwnd_,
                                 WM_APP_THEME))
                log::info(L"Thème : une application ou un rétablissement est déjà en cours");
            break;
        }
        case kCmdCloseWindow:
            PostMessageW(reinterpret_cast<HWND>(static_cast<std::uintptr_t>(item.window)), WM_CLOSE, 0, 0);
            break;
        case kCmdStartMenu:
            openStartMenu();
            break;
        case kCmdQuitDock:
            PostMessageW(hwnd_, WM_CLOSE, 0, 0);
            break;
        default: break;
    }
    requestFrame();
}

// Corbeille vide ou pleine : avis du Shell sur le dossier Corbeille, puis requête différée.
void DockApp::watchTrash() {
    refreshTrash();
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (FAILED(SHGetSpecialFolderLocation(nullptr, CSIDL_BITBUCKET, &pidl))) return;
    SHChangeNotifyEntry entry{pidl, TRUE};
    trashNotify_ = SHChangeNotifyRegister(hwnd_, SHCNRF_ShellLevel | SHCNRF_InterruptLevel | SHCNRF_NewDelivery,
                                          SHCNE_ALLEVENTS,
                                          WM_APP_TRASH, 1, &entry);
    CoTaskMemFree(pidl);
    if (!trashNotify_) log::warn(L"Surveillance de la Corbeille impossible");
}

void DockApp::watchStacks() {
    if (!hwnd_ || snapshot_) return;
    std::vector<std::wstring> folders;
    for (const auto& e : model_.pinnedEntries())
        if (e.kind == PinKind::Stack) folders.push_back(e.launch);
    if (folders != watchedStacks_) {
        for (ULONG id : stackNotify_) SHChangeNotifyDeregister(id);
        stackNotify_.clear();
        // Seuls les dossiers réellement surveillés sont retenus : un dossier absent (lecteur pas encore monté)
        // est réessayé au prochain appel.
        watchedStacks_.clear();
        for (const auto& folder : folders) {
            PIDLIST_ABSOLUTE pidl = nullptr;
            if (FAILED(SHParseDisplayName(folder.c_str(), nullptr, &pidl, 0, nullptr))) {
                log::warn(L"Surveillance de la pile impossible : %s", folder.c_str());
                continue;
            }
            SHChangeNotifyEntry entry{pidl, FALSE};
            if (ULONG id = SHChangeNotifyRegister(hwnd_, SHCNRF_ShellLevel | SHCNRF_InterruptLevel | SHCNRF_NewDelivery,
                                                  SHCNE_ALLEVENTS, WM_APP_STACKS, 1, &entry)) {
                stackNotify_.push_back(id);
                watchedStacks_.push_back(folder);
            }
            CoTaskMemFree(pidl);
        }
    }
    refreshStacks();
}

void DockApp::refreshStacks() {
    bool changed = false;
    for (const auto& e : model_.pinnedEntries()) {
        if (e.kind != PinKind::Stack) continue;
        auto preview = stackPreview(sortStack(listFolder(e.launch), e.stackSort));
        if (trace_) log::info(L"[trace] pile %s : aperçu de %zu élément(s)%s%s", e.launch.c_str(), preview.size(),
                              preview.empty() ? L"" : L", dessus : ", preview.empty() ? L"" : preview[0].path.c_str());
        changed |= model_.setStackPreview(L"stack:" + e.launch, std::move(preview));
    }
    if (changed) requestFrame();
}

void DockApp::refreshTrash() {
    bool full = recycleBinHasItems();
    if (trace_) log::info(L"[trace] corbeille %s", full ? L"pleine" : L"vide");
    model_.setTrashFull(full);
    requestFrame();
}

// Glisser-déposer de fichiers : survol (place ouverte ou icône assombrie), puis action après le retour de
// Drop (la source, souvent l'Explorateur, attend ce retour ; une copie ou une confirmation ne doit pas la bloquer).
void DockApp::registerDropTarget() {
    if (snapshot_) return;
    DropTarget::Callbacks cb;
    cb.over = [this](const std::vector<std::wstring>& paths, POINT screen) -> DropAction {
        POINT client{screen.x - origin_.x, screen.y - origin_.y};
        DropHover h = controller_.dropOver(client, paths);
        if (trace_) {
            static int lastAction = -1;
            static long lastItem = -2;
            long item = h.item ? long(*h.item) : -1;
            if (int(h.action) != lastAction || item != lastItem)
                log::info(L"[trace] survol de dépôt en %ld,%ld : action %d, élément %ld", client.x, client.y,
                          int(h.action), item);
            lastAction = int(h.action);
            lastItem = item;
        }
        requestFrame();
        return h.action;
    };
    cb.leave = [this] {
        controller_.dropLeave();
        requestFrame();
    };
    cb.drop = [this](const std::vector<std::wstring>& paths, POINT screen) {
        POINT client{screen.x - origin_.x, screen.y - origin_.y};
        PendingDrop d;
        d.hover = controller_.dropOver(client, paths);
        if (d.hover.item)
            if (const DockItem* it = controller_.itemAt(*d.hover.item)) d.item = *it;
        d.paths = paths;
        pendingDrop_ = std::move(d);
        PostMessageW(hwnd_, WM_APP_DROP, 0, 0);
    };
    dropTarget_ = new DropTarget(hwnd_, std::move(cb));
    HRESULT hr = RegisterDragDrop(hwnd_, dropTarget_);
    log::info(L"Dépôt de fichiers : RegisterDragDrop = 0x%08lx", static_cast<unsigned long>(hr));
    if (FAILED(hr)) {
        dropTarget_->Release();
        dropTarget_ = nullptr;
    }
}

void DockApp::performDrop() {
    if (!pendingDrop_) return;
    PendingDrop d = std::move(*pendingDrop_);
    pendingDrop_.reset();
    if (trace_) log::info(L"[trace] dépôt : action %d, %zu fichier(s)", int(d.hover.action), d.paths.size());
    switch (d.hover.action) {
        case DropAction::Pin: {
            const std::wstring& path = d.paths.front();
            auto id = identifyLaunchTarget(path);
            AppIdentity app;
            if (id) app = *id;
            if (app.appId.empty()) {
                app.exePath = path;
                app.appId = makeAppId(L"", path);
            }
            if (isPinnableFile(path) && toLower(path).ends_with(L".exe"))
                if (auto described = exeDisplayName(path); !described.empty()) app.displayName = described;
            if (app.displayName.empty()) app.displayName = exeDisplayName(path);
            auto entries = model_.pinnedEntries();
            bool dup = std::any_of(entries.begin(), entries.end(),
                                   [&](auto& p) { return p.kind == PinKind::App && p.appId == app.appId; });
            if (dup) break;
            std::size_t at = std::min(d.hover.pinIndex, entries.size());
            entries.insert(entries.begin() + std::ptrdiff_t(at),
                           PinnedEntry{PinKind::App, app.appId, path, app.displayName, app.exePath});
            model_.loadPinned(entries);
            savePinned();
            log::info(L"Épinglé par dépôt : %s (%s)", app.displayName.c_str(), app.appId.c_str());
            break;
        }
        case DropAction::OpenWith: {
            auto id = model_.identityOf(d.item.appId);
            std::wstring exe = id ? id->exePath : L"";
            std::wstring aumid = id ? id->aumid : L"";
            const std::wstring prefix = L"shell:AppsFolder\\";
            if (aumid.empty() && d.item.launch.starts_with(prefix)) aumid = d.item.launch.substr(prefix.size());
            if (exe.empty() && aumid.empty()) exe = d.item.launch;
            if (openWith(exe, aumid, d.paths) && !d.item.running) controller_.startLaunchBounce(d.item.appId);
            break;
        }
        case DropAction::Recycle: recycle(d.paths, hwnd_); break;
        case DropAction::MoveInto: moveInto(d.paths, d.item.launch, hwnd_); break;
        default: break;
    }
    requestFrame();
}

void DockApp::requestFrame() {
    wakeAnimation_ = true;
    // Réveille WaitMessage, y compris depuis un message envoyé (SendMessage) qui ne le réveille pas.
    if (hwnd_ && !wakePosted_.exchange(true)) PostMessageW(hwnd_, WM_APP_WAKE, 0, 0);
}

void DockApp::renderNow() {
    const bool timing = diagnosticCapture();
    const double t0 = timing ? nowSeconds() : 0;
    RenderFrame frame = controller_.buildFrame(dark_, icons_);
    const double t1 = timing ? nowSeconds() : 0;
    if (trace_ && model_.revision() != loggedRevision_ && !controller_.dragging()) {
        loggedRevision_ = model_.revision();   // positions à jour pour les essais automatisés
        logItemPositions(frame);
    }
    const double t2 = timing ? nowSeconds() : 0;
    frame.overlay = overlay_;
    frame.overlayOpacity = overlayOpacity_;
    frame.overlayScale = scale_ / 2;   // capture Retina @2x : 2 px par point
    // Pendant un menu, la capture du Dock est suspendue : il garde sa dernière image d'arrière-plan.
    frame.glass = glassLive_ && (capture_.status() == BackdropCapture::Status::Running || capturePaused_);
    shownTiles_.clear();
    const std::uint64_t animated = genie_.active() ? toId(genie_.source()) : 0;
    for (auto& icon : frame.icons) {
        if (!icon.window) continue;
        const float h = icon.size / 2;
        shownTiles_[icon.window] = RECT{LONG(std::lround(icon.cx - h)), LONG(std::lround(icon.cy - h)),
                                        LONG(std::lround(icon.cx + h)), LONG(std::lround(icon.cy + h))};
        if (icon.window == animated) icon.opacity = 0;   // l'image animée y entre ou en sort
    }
    if (!snapshot_) thumbnails_.sync(hwnd_, frame, !visibility_.hidden());
    const double t3 = timing ? nowSeconds() : 0;
    if (renderer_.render(frame, metrics_, settings_.font)) {
        renderFailures_ = 0;
        if (timing && nowSeconds() - t0 > 0.004)
            log::info(L"[diag] image du Dock : modèle %.1f ms, positions %.1f, miniatures %.1f, rendu %.1f", (t1 - t0) * 1000,
                      (t2 - t1) * 1000, (t3 - t2) * 1000, (nowSeconds() - t3) * 1000);
        return;
    }
    log::warn(L"Rendu impossible : recréation du périphérique graphique");
    if (++renderFailures_ >= 5) {
        // Dock invisible : on quitte avec une erreur pour que le lanceur, puis le mod, prennent le relais.
        log::error(L"Échecs graphiques répétés : arrêt");
        exitCode_ = 4;
        running_ = false;
        return;
    }
    capture_.stop();
    glassLive_ = false;
    if (initRenderer()) {
        RECT rc;
        GetClientRect(hwnd_, &rc);
        renderer_.resize(UINT(rc.right), UINT(rc.bottom));
        updateGlass();
    }
    requestFrame();
}

LRESULT CALLBACK DockApp::mouseHookProc(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && (wp == WM_LBUTTONDOWN || wp == WM_LBUTTONUP) && self_) {
        const auto* info = reinterpret_cast<MSLLHOOKSTRUCT*>(lp);
        // Couverture d'une réduction annoncée montrée ici même : le relâchement n'est pas encore livré à l'app, la
        // fenêtre ne peut pas avoir disparu (traité par le fil du Dock, il arrivait après la réduction).
        if (wp == WM_LBUTTONUP) self_->genie_.revealArmed(info->pt);
        PostMessageW(self_->hwnd_, WM_APP_BUTTON, wp == WM_LBUTTONDOWN, MAKELPARAM(info->pt.x, info->pt.y));
    }
    if (code == HC_ACTION && wp == WM_MOUSEMOVE && self_) {
        auto* info = reinterpret_cast<MSLLHOOKSTRUCT*>(lp);
        self_->mouseX_ = info->pt.x;
        self_->mouseY_ = info->pt.y;
        // Un seul message en attente à la fois : les mouvements sont fusionnés. Message perdu (boucle modale qui vide
        // la file sans distribuer) : au-delà de 250 ms on en renvoie un plutôt que de laisser le Dock sourd.
        const DWORD now = GetTickCount();
        if (self_->mousePending_ && now - self_->mousePostedAt_ > 250) self_->mousePending_ = false;
        if (!self_->mousePending_.exchange(true)) {
            self_->mousePostedAt_ = now;
            if (!PostMessageW(self_->hwnd_, WM_APP_MOUSE, 0, 0))
                self_->mousePending_ = false;   // file pleine : on réessaiera au prochain mouvement
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

// Ne lit que Tab (avec Alt) et, pendant une session, Échap, flèches, Q, H ; tout le reste passe sans délai.
namespace {
// Touche ⌘ : frappes simulées, marquées (le crochet les laisse passer sans les relire).
void sendKeys(std::initializer_list<std::pair<int, bool>> keys) {   // (touche, relâchement)
    INPUT in[8] = {};
    UINT n = 0;
    for (const auto& [vk, up] : keys) {
        if (n == 8) break;
        INPUT& i = in[n++];
        i.type = INPUT_KEYBOARD;
        i.ki.wVk = WORD(vk);
        i.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
        if (vk == VK_HOME || vk == VK_END || vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN)
            i.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;   // pas les touches du pavé numérique
        i.ki.dwExtraInfo = kCommandReplay;
    }
    SendInput(n, in, sizeof(INPUT));
}

void sendChord(const Chord& c) {
    const int vk = int(c.vk);
    if (c.ctrl) sendKeys({{VK_LCONTROL, false}, {vk, false}, {vk, true}, {VK_LCONTROL, true}});
    else if (c.alt) sendKeys({{VK_LMENU, false}, {vk, false}, {vk, true}, {VK_LMENU, true}});
    else sendKeys({{vk, false}, {vk, true}});
}

// Contexte de Coup d'œil, lu dans le crochet clavier (aucun message envoyé : classes et focus seulement).
QuickLookContext quickLookContextNow() {
    QuickLookContext c;
    const HWND fg = GetForegroundWindow();
    wchar_t cls[64] = {};
    if (fg && GetClassNameW(fg, cls, 64)) c.foregroundClass = cls;
    GUITHREADINFO gti{sizeof gti};
    if (fg && GetGUIThreadInfo(GetWindowThreadProcessId(fg, nullptr), &gti) && gti.hwndFocus) {
        if (GetClassNameW(gti.hwndFocus, cls, 64)) c.focusClass = cls;
        for (HWND h = gti.hwndFocus; h && h != fg; h = GetParent(h))
            if (GetClassNameW(h, cls, 64) && _wcsicmp(cls, L"SHELLDLL_DefView") == 0) {
                c.focusInShellView = true;
                break;
            }
    }
    return c;
}
} // namespace

LRESULT CALLBACK DockApp::keyboardHookProc(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && self_) {
        const auto* k = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lp);
        const bool down = wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN;
        static bool held[256] = {};   // fil du crochet seulement : distingue la répétition d'un nouvel appui
        const unsigned vk = k->vkCode & 0xFF;
        const bool repeat = down && held[vk];
        held[vk] = down;
        // Nom tapé dans la liste des fichiers : instant de la dernière lettre (l'espace qui suit en fait partie).
        static ULONGLONG lastTyped = 0;
        if (down && typeAheadKey(vk, ((GetAsyncKeyState(VK_CONTROL) | GetAsyncKeyState(VK_MENU) | GetAsyncKeyState(VK_LWIN) |
                                       GetAsyncKeyState(VK_RWIN)) & 0x8000) != 0))
            lastTyped = GetTickCount64();
        // Touche ⌘ (option) : avant tout le reste. Nos frappes simulées passent sans être relues.
        bool commandAlt = false;   // Alt rendu à Windows pour cette frappe (pas encore dans l'état du clavier)
        if (self_->commandKeyOn_ && k->dwExtraInfo != kCommandReplay &&
            ((k->flags & LLKHF_INJECTED) == 0 || diagnosticCapture())) {
            wchar_t cls[32] = {};   // Explorateur au premier plan : raccourcis du Finder (⌘↑ parent, ⌘⌫ Corbeille)
            const HWND fg = GetForegroundWindow();
            const bool explorer = fg && GetClassNameW(fg, cls, 32) &&
                                  (_wcsicmp(cls, L"CabinetWClass") == 0 || _wcsicmp(cls, L"ExploreWClass") == 0);
            const CommandAction a = self_->commandKeys_.onKey(vk, down, (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0, explorer);
            switch (a.kind) {
                case CommandAction::Kind::Swallow: return 1;
                case CommandAction::Kind::Send:
                    sendChord(a.chord);
                    return 1;
                case CommandAction::Kind::AltThenPass:
                    sendKeys({{VK_LMENU, false}});   // le vrai Alt d'abord (Alt+Tab, Alt+F4, Alt+Entrée)
                    commandAlt = true;
                    break;
                case CommandAction::Kind::Pass: break;
            }
        }
        // ⊞↓ sur une fenêtre ni agrandie, ni ancrée, ni réduite : Windows va la réduire, le génie l'animera seul.
        // Annoncée au fil du Dock (modèle, minuterie des animations) et de façon synchrone : coupée avant la réduction.
        if (vk == VK_DOWN && down && !repeat && ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) &&
            !(GetAsyncKeyState(VK_SHIFT) & 0x8000) && !(GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
            static const auto arranged = reinterpret_cast<BOOL(WINAPI*)(HWND)>(
                reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "IsWindowArranged")));
            const HWND fg = GetAncestor(GetForegroundWindow(), GA_ROOT);
            if (fg && IsWindowVisible(fg) && !IsZoomed(fg) && !(arranged && arranged(fg)))
                SendMessageTimeoutW(self_->hwnd_, self_->willMinimizeMsg_, reinterpret_cast<WPARAM>(fg), 0,
                                    SMTO_ABORTIFHUNG | SMTO_BLOCK, 50, nullptr);   // sans réentrer dans le crochet
        }
        // Captures d'écran : viseur ouvert (Échap, Espace), puis ⊞⇧3 et ⊞⇧4 (Explorer garde ces raccourcis).
        static bool escTaken = false;   // Échap a fermé le viseur : avalée jusqu'à son relâchement
        if (vk == VK_ESCAPE && escTaken && !self_->shotSession_) {
            if (!down) escTaken = false;
            return 1;
        }
        if (self_->shotSession_) {
            const ShotSessionKey s = screenshotSessionKey(vk, down, repeat);
            if (s == ShotSessionKey::Cancel || s == ShotSessionKey::ToggleWindow)
                PostMessageW(self_->hwnd_, WM_APP_SHOT_KEY, WPARAM(s), 0);
            if (vk == VK_ESCAPE) escTaken = down;
            if (s != ShotSessionKey::Pass) return 1;
        }
        if ((vk == '3' || vk == '4' || vk == '5') && self_->shotKeysOn_) {
            static bool taken[3] = {};   // fil du crochet seulement : appui pris, relâchement avalé aussi
            bool& t = taken[vk - '3'];
            auto pressed = [](int key) { return (GetAsyncKeyState(key) & 0x8000) != 0; };
            ShotKeyEvent e;
            e.vk = vk;
            e.down = down;
            e.repeat = repeat;
            e.injected = (k->flags & LLKHF_INJECTED) != 0 && !diagnosticCapture();   // acceptées en diagnostic (essais)
            e.mods = ShotMods{pressed(VK_LWIN) || pressed(VK_RWIN), pressed(VK_SHIFT), pressed(VK_CONTROL), pressed(VK_MENU)};
            e.taken = t;
            const ShotKey a = screenshotKey(e);
            if (!down) t = false;
            if (a == ShotKey::Screen || a == ShotKey::Region || a == ShotKey::Toolbar) {
                t = true;
                // Touche neutre tout de suite : ⊞ relâchée sans autre frappe visible ouvrirait le menu Démarrer.
                INPUT in[2] = {};
                in[0].type = in[1].type = INPUT_KEYBOARD;
                in[0].ki.wVk = in[1].ki.wVk = 0xE8;
                in[1].ki.dwFlags = KEYEVENTF_KEYUP;
                SendInput(2, in, sizeof(INPUT));
                PostMessageW(self_->hwnd_, WM_APP_SHOT, a == ShotKey::Screen ? 1 : a == ShotKey::Region ? 2 : 3, e.mods.ctrl ? 1 : 0);
            }
            if (a != ShotKey::Pass) return 1;
        }
        // Coup d'œil : Espace dans la liste des fichiers ; pendant l'aperçu, Espace, Échap et Entrée lui reviennent.
        if ((vk == VK_SPACE || vk == VK_ESCAPE || vk == VK_RETURN) && k->dwExtraInfo != kQuickLookReplay) {
            // Frappes simulées ignorées (une app qui tape un texte) ; acceptées en diagnostic pour les essais.
            const bool injected = (k->flags & LLKHF_INJECTED) != 0 && !diagnosticCapture();
            const bool mods = ((GetAsyncKeyState(VK_CONTROL) | GetAsyncKeyState(VK_MENU) | GetAsyncKeyState(VK_SHIFT) |
                                GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) != 0;
            QuickLookContext c = quickLookContextNow();
            static bool spaceInName = false;   // décidé à l'appui, gardé pour le relâchement
            if (self_->quickLook_->isOpen()) {
                const QuickLookKey a = quickLookKey(VK_SPACE, down, mods, injected, c);   // même contexte que l'ouverture
                if (a != QuickLookKey::Pass) {
                    if (down && !repeat) PostMessageW(self_->hwnd_, WM_APP_QUICKLOOK_KEY, WPARAM(vk), 0);
                    // Entrée ferme l'aperçu et reste à l'Explorateur (il ouvre la sélection comme d'habitude).
                    if (vk != VK_RETURN) return 1;
                }
            } else if (vk == VK_SPACE) {
                if (down && !repeat) spaceInName = typeAheadActive(lastTyped, GetTickCount64());
                c.typeAhead = spaceInName;
                switch (quickLookKey(vk, down, mods, injected, c)) {
                    case QuickLookKey::Open:
                        if (!repeat) PostMessageW(self_->hwnd_, WM_APP_QUICKLOOK, reinterpret_cast<WPARAM>(GetForegroundWindow()), 0);
                        return 1;
                    case QuickLookKey::Swallow: return 1;
                    case QuickLookKey::Pass: break;
                }
            }
        }
        if (self_->switchKeysOn_) {
            const bool alt = (k->flags & LLKHF_ALTDOWN) != 0 || commandAlt;
            const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
            const SwitchKey a = switcherKeyAction(vk, down, alt, shift, self_->switchSession_, repeat,
                                                  (k->flags & LLKHF_INJECTED) != 0);
            if (a != SwitchKey::Pass) {
                int id = 0;
                switch (a) {
                    case SwitchKey::Next: id = kHotSwitch; break;
                    case SwitchKey::Prev: id = kHotSwitchBack; break;
                    case SwitchKey::Cancel: id = kHotSwitchEsc; break;
                    case SwitchKey::Left: id = kHotSwitchLeft; break;
                    case SwitchKey::Right: id = kHotSwitchRight; break;
                    case SwitchKey::Quit: id = kHotSwitchQuit; break;
                    case SwitchKey::Hide: id = kHotSwitchHide; break;
                    default: break;
                }
                if (id) PostMessageW(self_->hwnd_, WM_APP_SWITCHKEY, WPARAM(id), 0);
                return 1;   // avalée : ni le sélecteur de Windows, ni l'app au premier plan
            }
        }
        if (commandAlt) {   // l'app doit voir Alt avant la touche : la vraie est avalée et renvoyée derrière
            sendKeys({{int(vk), false}});
            return 1;
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

void DockApp::startMouseThread() {
    // Le hook souris bas niveau vit sur son propre thread : la boucle d'animation ne ralentit jamais la souris.
    mouseThread_ = std::thread([this] {
        mouseThreadId_ = GetCurrentThreadId();
        HHOOK hook = SetWindowsHookExW(WH_MOUSE_LL, mouseHookProc, instance_, 0);
        if (!hook) log::error(L"SetWindowsHookEx(WH_MOUSE_LL) a échoué (%lu)", GetLastError());
        HHOOK keys = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardHookProc, instance_, 0);   // Alt+Tab seulement
        if (!keys) log::error(L"SetWindowsHookEx(WH_KEYBOARD_LL) a échoué (%lu) : Alt+Tab reste à Windows", GetLastError());
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {}
        if (keys) UnhookWindowsHookEx(keys);
        if (hook) UnhookWindowsHookEx(hook);
    });
}

void DockApp::startConfigWatcher() {
    configThread_ = std::thread([this] {
        HANDLE change = FindFirstChangeNotificationW(dataDir_.c_str(), FALSE,
                                                     FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME);
        if (change == INVALID_HANDLE_VALUE) return;
        HANDLE waits[2] = {change, stopEvent_};
        while (WaitForMultipleObjects(2, waits, FALSE, INFINITE) == WAIT_OBJECT_0) {
            PostMessageW(hwnd_, WM_APP_CONFIG, 0, 0);
            FindNextChangeNotification(change);
        }
        FindCloseChangeNotification(change);
    });
}

LRESULT CALLBACK DockApp::wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (self_ && self_->hwnd_ == hwnd) return self_->handle(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT DockApp::handle(UINT msg, WPARAM wp, LPARAM lp) {
    if (tracker_.handleMessage(msg, wp, lp)) return 0;
    if (msg == missionMsg_ && missionMsg_) {
        openMissionControl();
        return 0;
    }
    if (msg == spotlightMsg_ && spotlightMsg_) {
        openSpotlight();
        return 0;
    }
    if (msg == willMinimizeMsg_ && willMinimizeMsg_) {   // envoyé (synchrone) juste avant la réduction
        announceMinimize(reinterpret_cast<HWND>(wp), lp == kAnnounceHide);
        return 0;
    }
    if (msg == genieArmMsg_ && genieArmMsg_) {   // pastille jaune enfoncée : la réduction arrive au relâchement
        armGenie(reinterpret_cast<HWND>(wp), POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
        return 0;
    }
    if (msg == taskbarCreated_ && taskbarCreated_) {
        log::info(L"Explorateur redémarré : réenregistrement");
        removeAppBar();
        if (!settings_.autohide) registerAppBar();
        reposition();
        tracker_.rescan();
        return 0;
    }
    switch (msg) {
        case WM_APP_MOUSE:
            mousePending_ = false;
            onMouse(POINT{mouseX_.load(), mouseY_.load()});
            return 0;
        case WM_APP_BUTTON:
            onButton(wp != 0, POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            return 0;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_LBUTTONDOWN:
            if (hoverWarm_) {   // clic avant la fin du survol : la capture part tout de suite
                KillTimer(hwnd_, kWarmTimer);
                if (IsIconic(hoverWarm_)) genie_.warm(instance_, hoverWarm_);
            }
            if (dockClickGate(SpotlightWindow::isOpen(), menuOpen_) == DockClick::CloseSpotlight) {
                SpotlightWindow::closeOpen();   // le Dock n'active pas : ce clic est le « clic ailleurs »
                swallowClick_ = true;
                return 0;
            }
            controller_.pointerDown(POINT{short(LOWORD(lp)), short(HIWORD(lp))});
            if (trace_) {
                auto hit = controller_.hitTest(POINT{short(LOWORD(lp)), short(HIWORD(lp))});
                const DockItem* it = hit ? controller_.itemAt(*hit) : nullptr;
                log::info(L"[trace] appui en %d,%d sur %s", int(short(LOWORD(lp))), int(short(HIWORD(lp))),
                          it ? it->key.c_str() : L"(rien)");
            }
            SetCapture(hwnd_);
            return 0;
        case WM_MOUSEMOVE:
            if (GetCapture() == hwnd_) {
                if (controller_.dragging() && (GetAsyncKeyState(VK_ESCAPE) & 0x8000)) {
                    controller_.cancelDrag();   // Échap : l'icône revient à sa place
                    ReleaseCapture();
                } else {
                    controller_.pointerMove(POINT{short(LOWORD(lp)), short(HIWORD(lp))});
                }
                updateDragSprite();
                requestFrame();
            }
            return 0;
        case WM_LBUTTONUP:
            if (swallowClick_) {
                swallowClick_ = false;
                return 0;
            }
            onPointerUp(POINT{short(LOWORD(lp)), short(HIWORD(lp))});
            return 0;
        case WM_CAPTURECHANGED:
            if (controller_.dragging()) {
                controller_.cancelDrag();
                updateDragSprite();
                requestFrame();
            }
            return 0;
        case WM_RBUTTONUP:
            if (controller_.dragging()) return 0;
            switch (dockClickGate(SpotlightWindow::isOpen(), menuOpen_)) {
                case DockClick::CloseSpotlight: SpotlightWindow::closeOpen(); return 0;
                case DockClick::Ignore: return 0;
                case DockClick::Proceed: break;
            }
            showContextMenu(controller_.hitTestAny(POINT{short(LOWORD(lp)), short(HIWORD(lp))}));
            return 0;
        case WM_APP_IPC_FLASH: {
            std::wstring app = model_.appOfWindow(WindowId(lp));
            controller_.setAttention(app, true);
            requestFrame();
            return 0;
        }
        case WM_APP_CONFIG:
            SetTimer(hwnd_, kConfigTimer, 200, nullptr);   // anti-rebond : plusieurs écritures d'affilée
            return 0;
        case WM_APP_DROP:
            performDrop();
            return 0;
        case WM_APP_TRASH: {
            LONG event = 0;
            PIDLIST_ABSOLUTE* pidls = nullptr;
            if (HANDLE lock = SHChangeNotification_Lock(HANDLE(wp), DWORD(lp), &pidls, &event))
                SHChangeNotification_Unlock(lock);
            if (trace_) log::info(L"[trace] corbeille : avis 0x%lx", static_cast<unsigned long>(event));
            SetTimer(hwnd_, kTrashTimer, 300, nullptr);   // une suppression multiple envoie une rafale d'avis
            return 0;
        }
        case WM_APP_QUICKLOOK: {   // sélection lue ici (COM, fil de l'interface en STA), puis l'aperçu
            const HWND owner = reinterpret_cast<HWND>(wp);
            std::wstring first;
            if (!quickLookBusy_) {
                quickLookBusy_ = true;
                if (quickLookWatch_.attach(owner)) first = quickLookWatch_.first();
                quickLookBusy_ = false;
            }
            if (trace_) log::info(L"[trace] coup d'œil : %s", first.empty() ? L"aucune sélection" : first.c_str());
            if (!first.empty()) {
                quickLook_->show(instance_, {first}, 0, owner);
                SetTimer(hwnd_, kQuickLookTimer, 250, nullptr);
            } else {   // rien de sélectionné : Espace rendu à l'Explorateur (il sélectionne l'élément qui a le focus)
                INPUT in[2]{};
                for (int i = 0; i < 2; ++i) {
                    in[i].type = INPUT_KEYBOARD;
                    in[i].ki.wVk = VK_SPACE;
                    in[i].ki.dwExtraInfo = kQuickLookReplay;
                }
                in[1].ki.dwFlags = KEYEVENTF_KEYUP;
                SendInput(2, in, sizeof(INPUT));
            }
            return 0;
        }
        case WM_APP_SHOT:   // touche neutre déjà envoyée par le crochet
            if (wp == 1) takeScreenShot(lp != 0);
            else if (wp == 2) startRegionShot(lp != 0);
            else openShotToolbar();
            return 0;
        case WM_APP_SHOT_KEY:
            if (shotToolbar_.isOpen()) {   // Échap ferme la barre de ⊞⇧5
                if (ShotSessionKey(wp) == ShotSessionKey::Cancel) {
                    shotToolbar_.close();
                    shotSession_ = false;
                }
                return 0;
            }
            viewfinder_.key(ShotSessionKey(wp));
            return 0;
        case WM_APP_SHOT_SAVED: {
            std::unique_ptr<std::wstring> path(reinterpret_cast<std::wstring*>(lp));
            if (!path) return 0;
            shotThumb_.fileSaved(*path, wp != 0);
            if (wp) log::info(L"Capture d'écran : %s", path->c_str());
            else log::warn(L"Capture d'écran : écriture impossible (%s)", path->c_str());
            return 0;
        }
        case WM_APP_QUICKLOOK_KEY:   // Espace, Échap, ou Entrée (passée aussi à l'Explorateur) : fermeture
            quickLook_->close();
            quickLookWatch_.reset();
            return 0;
        case WM_APP_SWITCHKEY:
            if (switch_.active() || int(wp) == kHotSwitch || int(wp) == kHotSwitchBack) switcherKey(int(wp));
            return 0;
        case WM_APP_CORNER:
            if (const unsigned delay = hotCornerDelayMs(HotCornerAction(wp))) {
                pendingCorner_ = HotCornerAction(wp);
                SetTimer(hwnd_, kCornerTimer, delay, nullptr);
            } else {
                runHotCorner(HotCornerAction(wp));
            }
            return 0;
        case WM_APP_THEME: {   // le résultat est aussi dans le journal
            const ThemeResult r = ThemeJob::take(lp);
            if (!r.ok) MessageBoxW(hwnd_, r.message.c_str(), L"Thème macOS", MB_OK | MB_ICONWARNING);
            return 0;
        }
        case WM_APP_STACKS: {
            LONG event = 0;
            PIDLIST_ABSOLUTE* pidls = nullptr;
            if (HANDLE lock = SHChangeNotification_Lock(HANDLE(wp), DWORD(lp), &pidls, &event))
                SHChangeNotification_Unlock(lock);
            // Un téléchargement envoie une rafale d'avis : regroupés 400 ms, mais l'icône suit au moins toutes les 2 s.
            const double now = nowSeconds();
            if (stacksFirstEvent_ < 0) stacksFirstEvent_ = now;
            if (now - stacksFirstEvent_ >= 2.0) {
                KillTimer(hwnd_, kStacksTimer);
                stacksFirstEvent_ = -1;
                refreshStacks();
            } else {
                SetTimer(hwnd_, kStacksTimer, 400, nullptr);
            }
            return 0;
        }
        case WM_TIMER:
            if (wp == kRecordTimer) {
                recPill_.update(recorder_.seconds());
                return 0;
            }
            if (wp == kShotResumeTimer) {   // le Dock est de nouveau exclu : la capture du verre ne le verra pas
                KillTimer(hwnd_, kShotResumeTimer);
                if (!menuOpen_ && !switchPanel_) resumeCapture();   // sinon, la fermeture du menu la reprend
                return 0;
            }
            if (wp == kQuickLookTimer) {   // l'aperçu suit la sélection (flèches, clics dans l'Explorateur)
                const HWND owner = quickLook_->owner();
                // Fermé si l'Explorateur (ou le bureau) n'est plus au premier plan : l'aperçu flotte au-dessus de tout.
                if (!quickLook_->isOpen() || !IsWindow(owner) || GetForegroundWindow() != owner) {
                    KillTimer(hwnd_, kQuickLookTimer);
                    quickLook_->close();
                    quickLookWatch_.reset();
                    return 0;
                }
                if (quickLookBusy_) return 0;   // lecture précédente encore en cours (Explorateur lent)
                quickLookBusy_ = true;
                const std::wstring first = quickLookWatch_.first();
                quickLookBusy_ = false;
                if (!quickLook_->isOpen()) return 0;   // fermé (×) pendant la lecture : ne pas le rouvrir
                if (first.empty()) {   // plus rien de sélectionné : l'aperçu se ferme
                    KillTimer(hwnd_, kQuickLookTimer);
                    quickLook_->close();
                    quickLookWatch_.reset();
                } else if (quickLook_->paths().empty() || first != quickLook_->paths().front()) {
                    quickLook_->show(instance_, {first}, 0, owner);
                }
                return 0;
            }
            if (wp == kCornerTimer) {
                KillTimer(hwnd_, kCornerTimer);
                runHotCorner(std::exchange(pendingCorner_, HotCornerAction::Off));
                return 0;
            }
            if (wp == kSwitchTimer) {
                switcherTick();
                return 0;
            }
            if (wp == kStacksTimer) {
                KillTimer(hwnd_, kStacksTimer);
                stacksFirstEvent_ = -1;
                refreshStacks();
                return 0;
            }
            if (wp == kVisibilityTimer) {
                KillTimer(hwnd_, kVisibilityTimer);
                requestFrame();
                return 0;
            }
            if (wp == kFullscreenTimer) {
                checkFullscreen();
                noteForeground();   // place de la fenêtre active (ancrage au clavier compris), pour l'effet génie
                return 0;
            }
            if (wp == kTransitionTimer) {   // génie armé ou en cours, ou masquée : retenue ; sinon animations rendues
                transitions_.release(nowSeconds(), [this](HWND h) { return transitionBusy(heldState(h)); });
                // Plus que des fenêtres masquées (ou aucune) : rien à surveiller avant leur retour (ev.minimized, ev.closed).
                bool waiting = false;
                for (HWND h : transitions_.windows()) waiting = waiting || (IsWindow(h) && !transitionParked(heldState(h)));
                if (!waiting) KillTimer(hwnd_, kTransitionTimer);
                return 0;
            }
            if (wp == kArmTimer) {   // annoncée, mais pas réduite (app qui refuse, ou cache dans la zone de notification)
                KillTimer(hwnd_, kArmTimer);
                genie_.disarm();
                return 0;
            }
            if (wp == kWarmTimer) {
                KillTimer(hwnd_, kWarmTimer);
                if (hoverWarm_ && IsWindow(hoverWarm_) && IsIconic(hoverWarm_)) genie_.warm(instance_, hoverWarm_);
                return 0;
            }
            if (wp == kTrashTimer) {
                KillTimer(hwnd_, kTrashTimer);
                refreshTrash();
                return 0;
            }
            if (wp == kConfigTimer) {
                KillTimer(hwnd_, kConfigTimer);
                double reserveBefore = DockController::reservePx(settings_, metrics_, scale_);
                double heightBefore = DockController::windowHeightPx(settings_, metrics_, scale_);
                loadConfig(false);
                if (reserveBefore != DockController::reservePx(settings_, metrics_, scale_) ||
                    heightBefore != DockController::windowHeightPx(settings_, metrics_, scale_))
                    reposition();
                requestFrame();
                return 0;
            }
            break;
        case WM_APP_APPBAR:
            if (wp == ABN_POSCHANGED) reposition();
            if (wp == ABN_FULLSCREENAPP) checkFullscreen();
            return 0;
        case WM_DISPLAYCHANGE:
        case WM_DPICHANGED:
            onDisplayChanged();
            return 0;
        case WM_APP_BACKDROP:
            onBackdrop();
            return 0;
        case WM_SETTINGCHANGE:
            if (lp && wcscmp(reinterpret_cast<const wchar_t*>(lp), L"ImmersiveColorSet") == 0) {
                bool dark = systemDarkMode();
                if (dark != dark_) {
                    dark_ = dark;
                    icons_.setDark(dark);
                    renderer_.releaseImages();
                    requestFrame();
                }
            }
            return 0;
        case WM_APP_WAKE:
            wakePosted_ = false;
            return 0;
        case WM_APP_PING:
            lastUiBeat_ = GetTickCount64();
            return 0;
        case WM_QUERYENDSESSION:
            return TRUE;
        case WM_ENDSESSION:
            if (wp) {
                // Fermeture de session : on rend la barre Windows tout de suite (Goodbye) et la zone d'écran.
                log::info(L"Fin de session");
                pipe_.stop();
                removeAppBar();
            }
            return 0;
        case WM_HOTKEY:
            onHotKey(int(wp));
            return 0;
        case WM_CLOSE:
            running_ = false;
            PostQuitMessage(0);
            return 0;
        default: break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

int DockApp::runSnapshot(const Options& options) {
    RECT rc;
    GetWindowRect(hwnd_, &rc);
    UINT w = UINT(rc.right - rc.left), h = UINT(rc.bottom - rc.top);
    if (options.hover) {
        double bgBottom = h - metrics_.dockScreenMargin * scale_;
        POINT p{LONG(w / 2.0 + *options.hover * scale_), LONG(bgBottom - 10 * scale_)};
        controller_.setCursor(p);
    }
    refreshStacks();   // icônes « Pile » comme dans le vrai Dock
    for (int i = 0; i < 240; ++i) controller_.tick(1.0 / 120);
    RenderFrame frame = controller_.buildFrame(dark_, icons_);
    if (trace_) logItemPositions(frame);

    std::vector<std::uint8_t> wallpaper;
    if (!options.wallpaper.empty()) {
        UINT ww = 0, wh = 0;
        auto img = readPng(options.wallpaper, ww, wh);
        if (img.empty()) log::warn(L"Fond illisible : %s", options.wallpaper.c_str());
        else wallpaper = resizeBgra(img, ww, wh, w, h);
    }
    auto px = renderer_.renderToBgra(frame, metrics_, settings_.font, w, h, wallpaper);
    bool ok = !px.empty() && writePng(options.snapshot, px.data(), w, h);
    log::info(L"Capture %s : %s", ok ? L"écrite" : L"impossible", options.snapshot.c_str());
    if (!ok || options.reference.empty()) return ok ? 0 : 1;

    // Comparaison : la référence est mise à la largeur du rendu, puis on compare sa bande du bas.
    UINT rw = 0, rh = 0;
    auto ref = readPng(options.reference, rw, rh);
    if (ref.empty()) {
        log::error(L"Référence illisible : %s", options.reference.c_str());
        return 1;
    }
    UINT sh = UINT(std::lround(double(rh) * w / rw));
    ref = resizeBgra(ref, rw, rh, w, sh);
    if (sh < h) {
        log::error(L"Référence trop basse (%u px) pour une fenêtre de %u px", sh, h);
        return 1;
    }
    const std::uint8_t* band = ref.data() + size_t(sh - h) * w * 4;
    DiffStats s = diffImages(px.data(), band, int(w), int(h), 24);
    log::info(L"Comparaison : écart moyen %.2f, max %d, pixels > 24 : %.2f %%", s.meanAbs, s.maxAbs,
              s.fractionAbove * 100);
    if (!options.diff.empty()) {
        auto heat = diffHeatmap(px.data(), band, int(w), int(h));
        writePng(options.diff, heat.data(), w, h);
        std::wstring txt = options.diff + L".txt";
        FILE* f = nullptr;
        if (_wfopen_s(&f, txt.c_str(), L"w") == 0 && f) {
            fprintf(f, "meanAbs %.4f\nmaxAbs %d\nfractionAbove24 %.6f\n", s.meanAbs, s.maxAbs, s.fractionAbove);
            fclose(f);
        }
    }
    return 0;
}

void DockApp::onHotKey(int id) {
    if (id >= kHotSwitch && id <= kHotSwitchHide) {
        switcherKey(id);
        return;
    }
    if (switch_.active()) return;   // Alt maintenu : Alt+Espace n'ouvre pas Spotlight au milieu d'une session
    if (id == kHotMission) {
        openMissionControl();
        return;
    }
    if (id == kHotAppExpose) {   // l'app au premier plan
        openAppExpose(model_.appOfWindow(toId(GetAncestor(GetForegroundWindow(), GA_ROOTOWNER))));
        return;
    }
    if (id == kHotSpotlight) {
        openSpotlight();
        return;
    }
    if (id == kHotOverlay) {
        if (overlay_) {
            overlay_.reset();
            log::info(L"Superposition de calibration masquée");
        } else {
            std::wstring path = dataDir_ + L"\\reference\\overlay.png";
            auto img = std::make_shared<OverlayImage>();
            img->bgra = readPng(path, img->w, img->h);
            if (img->bgra.empty()) {
                log::warn(L"Superposition introuvable : %s", path.c_str());
                return;
            }
            overlay_ = img;
            log::info(L"Superposition de calibration affichée (%ux%u)", img->w, img->h);
        }
    } else if (id == kHotOpacityUp || id == kHotOpacityDown) {
        overlayOpacity_ = std::clamp(overlayOpacity_ + (id == kHotOpacityUp ? 0.1f : -0.1f), 0.1f, 1.0f);
    }
    requestFrame();
}

int DockApp::run(HINSTANCE instance, const Options& options) {
    self_ = this;
    instance_ = instance;
    trace_ = options.trace;
    snapshot_ = !options.snapshot.empty();
    dataDir_ = appDataDir();
    log::init(dataDir_ + L"\\logs");
    log::info(L"MacDock démarre");
    std::wstring iconDir = dataDir_ + L"\\icons";
    CreateDirectoryW(iconDir.c_str(), nullptr);
    icons_.setCustomDir(iconDir);
    dark_ = options.dark.value_or(systemDarkMode());
    icons_.setDark(dark_);

    controller_.init(settings_, metrics_, &model_);
    loadConfig(true);

    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);
    hwnd_ = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE |
                                WS_EX_LAYERED | WS_EX_TRANSPARENT,
                            kClassName, L"MacDock", WS_POPUP, 0, 0, 100, 100, nullptr, nullptr, instance, nullptr);
    if (!hwnd_) { log::error(L"CreateWindowEx a échoué (%lu)", GetLastError()); return 2; }
    SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA);
    taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");
    ChangeWindowMessageFilterEx(hwnd_, taskbarCreated_, MSGFLT_ALLOW, nullptr);

    if (!(snapshot_ ? renderer_.init(hwnd_) : initRenderer())) { log::error(L"Initialisation graphique impossible"); return 2; }
    renderer_.setGpuTiming(trace_);
    if (!snapshot_) {
        if (!settings_.autohide) registerAppBar();
        dragSprite_.create(instance);
        poofSprite_.create(instance);
        apps_.refreshAsync();   // prêt pour le premier clic sur le bouton Apps
    }
    reposition();
    updateGlass();

    WindowTracker::Events ev;
    ev.opened = [this](HWND h, const AppIdentity& id) {
        if (trace_) log::info(L"[trace] ouverte %p %s (%s)", h, id.displayName.c_str(), id.appId.c_str());
        model_.windowOpened(toId(h), id);
        requestFrame();
    };
    ev.closed = [this](HWND h) {
        if (trace_) log::info(L"[trace] fermée %p", h);
        model_.windowClosed(toId(h));
        lastSeen_.erase(toId(h));
        if (transitions_.held(h)) SetTimer(hwnd_, kTransitionTimer, 400, nullptr);   // oubliée au prochain tour
        requestFrame();
    };
    ev.minimized = [this](HWND h, bool m) {
        if (trace_) {
            LARGE_INTEGER q, f;
            QueryPerformanceCounter(&q);
            QueryPerformanceFrequency(&f);
            log::info(L"[trace] %s %p (qpc %.1f ms)", m ? L"réduite" : L"restaurée", h, double(q.QuadPart) * 1000.0 / double(f.QuadPart));
        }
        model_.windowMinimized(toId(h), m);
        if (genieOnMinimize(genieRun(), toId(h), m, false) == GenieReact::Cancel) genie_.cancel();   // restaurée ailleurs
        if (!m && transitions_.held(h)) SetTimer(hwnd_, kTransitionTimer, 400, nullptr);   // animations bientôt rendues
        requestFrame();
    };
    ev.minimizeStarted = [this](HWND h) {   // réduction vue à l'instant : vers sa case du Dock
        ANIMATIONINFO ai{sizeof ai};
        const bool windowsAnimates = SystemParametersInfoW(SPI_GETANIMATION, sizeof ai, &ai, 0) && ai.iMinAnimate;
        if (genieOnMinimize(genieRun(), toId(h), true, true) == GenieReact::Start &&
            genieTakesMinimize(transitions_.held(h), windowsAnimates))
            startGenie(h, false);
        else if (trace_ && !transitions_.held(h))
            log::info(L"[trace] réduction non annoncée %p : animée par Windows", static_cast<void*>(h));
        if (genie_.armed()) genie_.disarm();   // annoncée mais pas animée (pas de case) : la couverture s'en va
        KillTimer(hwnd_, kArmTimer);
    };
    ev.titleChanged = [this](HWND h, const std::wstring& t) { model_.windowTitle(toId(h), t); };
    ev.moved = [this](HWND h) {   // place exacte au moment d'une réduction (déplacée, ancrée, agrandie…)
        RECT r{};
        if (!IsIconic(h) && IsWindowVisible(h) && visibleBounds(h, r)) lastSeen_[toId(h)] = r;
    };
    ev.activated = [this](HWND h) {
        mru_.touch(model_.appOfWindow(toId(h)));
        controller_.setAttention(model_.appOfWindow(toId(h)), false);
        checkFullscreen();
        noteForeground();
        requestFrame();
    };
    ev.flashed = [this](HWND h) {
        if (trace_) log::info(L"[trace] attention %p", h);
        controller_.setAttention(model_.appOfWindow(toId(h)), true);
        requestFrame();
    };
    tracker_.setTrace(trace_);
    thumbnails_.setTrace(trace_);
    if (!snapshot_)   // un Dock précédent (planté, tué) a pu laisser des fenêtres sans animations
        if (const int n = releaseOrphanTransitions()) log::info(L"Animations de Windows rendues à %d fenêtres", n);
    tracker_.start(hwnd_, ev);

    if (snapshot_) {
        int code = runSnapshot(options);
        tracker_.stop();
        DestroyWindow(hwnd_);
        return code;
    }
    RegisterHotKey(hwnd_, kHotOverlay, MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 'O');
    RegisterHotKey(hwnd_, kHotOpacityUp, MOD_CONTROL | MOD_ALT | MOD_SHIFT, VK_UP);
    RegisterHotKey(hwnd_, kHotOpacityDown, MOD_CONTROL | MOD_ALT | MOD_SHIFT, VK_DOWN);
    spotlightMsg_ = RegisterWindowMessageW(L"MacDockSpotlight");
    ChangeWindowMessageFilterEx(hwnd_, spotlightMsg_, MSGFLT_ALLOW, nullptr);
    registerSpotlightHotkey();
    missionMsg_ = RegisterWindowMessageW(L"MacDockMissionControl");
    genieArmMsg_ = RegisterWindowMessageW(L"MacDockGenieArm");
    ChangeWindowMessageFilterEx(hwnd_, genieArmMsg_, MSGFLT_ALLOW, nullptr);
    willMinimizeMsg_ = RegisterWindowMessageW(L"MacDockWillMinimize");
    ChangeWindowMessageFilterEx(hwnd_, willMinimizeMsg_, MSGFLT_ALLOW, nullptr);
    shotRevealMsg_ = RegisterWindowMessageW(L"MacDockScreenshotReveal");
    ChangeWindowMessageFilterEx(hwnd_, missionMsg_, MSGFLT_ALLOW, nullptr);
    registerMissionHotkey();
    registerAppExposeHotkey();
    switcher_.onClick = [this](std::size_t i) {   // clic sur une icône : cette app, tout de suite
        switch_.select(i);
        endSwitch(true);
    };
    registerSwitcherHotkey();
    lastUiBeat_ = GetTickCount64();
    pipe_.setLivenessCheck([this] {
        PostMessageW(hwnd_, WM_APP_PING, 0, 0);
        return GetTickCount64() - lastUiBeat_.load() < 3000;
    });
    pipe_.start(L"\\\\.\\pipe\\MacDock", [this](const ipc::Message& m) {
        if (auto f = ipc::parseFlash(m)) PostMessageW(hwnd_, WM_APP_IPC_FLASH, 0, LPARAM(f->hwnd));
    });

    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    startMouseThread();
    startConfigWatcher();
    watchTrash();
    watchStacks();
    registerDropTarget();
    // Plein écran : premier plan (tracker), avis de la barre d'application, et vérification chaque seconde
    // pour les bascules sans changement de premier plan (F11, vidéo) — sans hook EVENT_OBJECT_LOCATIONCHANGE.
    if (!snapshot_) SetTimer(hwnd_, kFullscreenTimer, 1000, nullptr);

    double last = nowSeconds();
    while (running_) {
        MSG msg;
        if (trace_) {
            static int loops = 0;
            static std::map<UINT, int> kinds;
            static double since = nowSeconds();
            ++loops;
            MSG peek;
            if (PeekMessageW(&peek, nullptr, 0, 0, PM_NOREMOVE)) ++kinds[peek.message];
            if (nowSeconds() - since >= 5) {
                std::wstring s;
                for (auto& [k, n] : kinds) s += std::to_wstring(k) + L"=" + std::to_wstring(n) + L" ";
                log::info(L"[perf] %d tours de boucle en 5 s ; messages : %s", loops, s.c_str());
                loops = 0;
                kinds.clear();
                since = nowSeconds();
            }
        }
        const bool timing = diagnosticCapture() && genie_.running() && !genie_.onGpu();
        const double loopStart = timing ? nowSeconds() : 0;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { running_ = false; break; }
            const double m0 = timing ? nowSeconds() : 0;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (timing && nowSeconds() - m0 > 0.003)
                log::info(L"[diag] boucle : message 0x%04X traité en %.1f ms", msg.message, (nowSeconds() - m0) * 1000);
        }
        if (!running_) break;

        double now = nowSeconds();
        double dt = std::min(now - last, 0.1);
        last = now;
        bool animating = controller_.tick(dt);
        if (stepVisibility(now)) animating = true;
        // Fumée et génie ont leurs propres fenêtres : ils cadencent la boucle sans redessiner le Dock (verre compris)
        // à chaque image ; leur début et leur fin demandent eux-mêmes une image (requestFrame).
        bool overlays = stepPoof(now);
        if (stepGenie(now)) overlays = true;
        if (genie_.armed()) {   // réduction annoncée : la capture arrive et le rendu GPU chauffe pendant l'appui
            genie_.pumpArmed();
            overlays = true;
        }
        syncPointer();
        bool dirty = controller_.consumeDirty();
        // Départ du génie (au plus 150 ms) : la passation au GPU d'abord, l'image du Dock (nouvelle case…) au tour
        // d'après ; elle prendrait le tour où la deuxième image GPU doit partir. Fin d'une restauration (80 ms) : DWM
        // compose la fenêtre rendue, chaque appel de miniature y attend une composition (~6 ms mesurées).
        const bool holdDock = genie_.waiting() || genieSettleUntil_ >= 0;
        if ((animating || dirty) && holdDock) {
            wakeAnimation_ = true;
            animating = dirty = false;
            overlays = true;
        }
        if (animating || dirty || wakeAnimation_ && !holdDock) {
            if (trace_) {
                static int frames = 0, byAnim = 0, byDirty = 0, byWake = 0;
                static double since = now;
                ++frames;
                byAnim += animating;
                byDirty += dirty;
                byWake += wakeAnimation_;
                if (now - since >= 5) {
                    log::info(L"[perf] %d images en 5 s (animation %d, modèle %d, réveil %d) ; arrière-plans reçus %d ; "
                              L"verre GPU %.3f ms",
                              frames, byAnim, byDirty, byWake, capturesTaken_, renderer_.takeGlassGpuMs());
                    capturesTaken_ = 0;
                    frames = byAnim = byDirty = byWake = 0;
                    since = now;
                }
            }
            wakeAnimation_ = false;
            const double r0 = timing ? nowSeconds() : 0;
            renderNow();
            if (timing && nowSeconds() - r0 > 0.003)
                log::info(L"[diag] boucle : image du Dock en %.1f ms (animation %d, modèle %d)", (nowSeconds() - r0) * 1000,
                          int(animating), int(dirty));
        }
        if (timing && nowSeconds() - loopStart > 0.008)
            log::info(L"[diag] boucle : tour en %.1f ms", (nowSeconds() - loopStart) * 1000);
        if (animating || overlays) {
            DCompositionWaitForCompositorClock(0, nullptr, 50);
        } else {
            if (thumbnails_.pending()) thumbnails_.flush();   // au repos : les retraits lents de miniatures DWM
            WaitMessage();
            last = nowSeconds() - 1.0 / 120;   // reprise sans saut d'animation
        }
    }

    log::info(L"MacDock s'arrête");
    genie_.cancel();
    transitions_.releaseAll();   // fenêtres réduites par le génie : animations de Windows rendues
    minAnimate_.restore();
    if (trashNotify_) SHChangeNotifyDeregister(trashNotify_);
    for (ULONG id : stackNotify_) SHChangeNotifyDeregister(id);
    thumbnails_.clear();
    if (dropTarget_) {
        RevokeDragDrop(hwnd_);
        dropTarget_->Release();
        dropTarget_ = nullptr;
    }
    recorder_.stop();   // un enregistrement en cours est finalisé
    viewfinder_.cancel();
    shotToolbar_.close();
    shotThumb_.close();
    for (auto& job : shotJobs_) job.wait();   // captures en cours d'écriture : jamais de fichier coupé
    capture_.stop();
    SetEvent(stopEvent_);
    if (configThread_.joinable()) configThread_.join();
    if (mouseThreadId_) PostThreadMessageW(mouseThreadId_, WM_QUIT, 0, 0);
    if (mouseThread_.joinable()) mouseThread_.join();
    // Après le crochet clavier (il lit l'aperçu) : un aperçu figé (prevhost) n'empêche pas le Dock de s'arrêter.
    if (!quickLook_->shutdown(3000)) {
        log::warn(L"Coup d'œil : aperçu figé, abandonné à l'arrêt");
        (void)quickLook_.release();
    }
    CloseHandle(stopEvent_);
    pipe_.stop();
    tracker_.stop();
    removeAppBar();
    DestroyWindow(hwnd_);
    return exitCode_;
}

} // namespace md
