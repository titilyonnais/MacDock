#include "dock_window.h"

#include <dcomp.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <shlobj.h>

#include <algorithm>
#include <cmath>
#include <map>

#include "../anim/genie.h"
#include "../apps/apps_window.h"
#include "../calib/image_diff.h"
#include "../calib/png_io.h"
#include "../config/config_store.h"
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
constexpr int kHotOverlay = 1, kHotOpacityUp = 2, kHotOpacityDown = 3, kHotSpotlight = 4;

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
    }
    if (shouldImportDefaultPins(s, settings_)) {
        settings_.pinned = defaultPins();
        settings_.pinnedInitialized = true;
        saveJsonFileAtomic(dataDir_ + L"\\settings.json", settingsToJson(settings_));
        log::info(L"Premier lancement : %zu épingles par défaut", settings_.pinned.size());
    }
    auto m = loadJsonFile(dataDir_ + L"\\dock-metrics.json");
    if (m.fromFile && !m.wasInvalid && jsonVersion(m.value) < kMetricsVersion) {
        m.value = migrateMetricsJson(m.value);
        saveJsonFileAtomic(dataDir_ + L"\\dock-metrics.json", m.value);
        log::info(L"dock-metrics.json migré de la v1 à la v%d", kMetricsVersion);
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
    if (hwnd_ && !snapshot_ && settings_.position != placedPosition_) reposition();   // bord changé à chaud
    if (!snapshot_) minAnimate_.apply(settings_.minimizeEffect);   // l'animation de Windows ne double pas la nôtre
    updateGlass();   // réglage glass modifié à chaud
    if (spotlightMsg_) registerSpotlightHotkey();   // après le démarrage seulement (fenêtre prête)
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

bool DockApp::detectFullscreen() const {
    HWND fg = GetForegroundWindow();
    if (!fg || !IsWindowVisible(fg) || IsIconic(fg)) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (pid == GetCurrentProcessId()) return false;   // menus et sprites du Dock
    wchar_t cls[64] = {};
    GetClassNameW(fg, cls, 64);
    for (const wchar_t* shell : {L"Progman", L"WorkerW", L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd"})
        if (wcscmp(cls, shell) == 0) return false;
    if (MonitorFromWindow(fg, MONITOR_DEFAULTTONULL) != MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY)) return false;
    RECT rc;
    bool caption = (GetWindowLongPtrW(fg, GWL_STYLE) & WS_CAPTION) == WS_CAPTION;
    return GetWindowRect(fg, &rc) && isFullscreenWindow(rc, monitor_, IsZoomed(fg) != FALSE, caption);
}

void DockApp::checkFullscreen() {
    bool fs = detectFullscreen();
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
    in.menuOpen = menuOpen_;
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
    // Écran enregistré s'il est branché (il revient dès qu'on le rebranche), sinon l'écran courant, sinon le principal.
    std::wstring wanted = settings_.screen;
    if (wanted.empty() || std::none_of(monitors_.begin(), monitors_.end(),
                                       [&](const MonitorInfo& m) { return toLower(m.name) == toLower(wanted); }))
        wanted = screenName_;
    std::size_t i = initialMonitor(monitors_, wanted);
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
    LONG_PTR ex = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    ex = transparent ? (ex | WS_EX_TRANSPARENT) : (ex & ~WS_EX_TRANSPARENT);
    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, ex);
}

void DockApp::onMouse(POINT screen) {
    POINT client{screen.x - origin_.x, screen.y - origin_.y};
    bool inside = controller_.isInsideInteractiveZone(client);
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
    if (atEdge != cursorAtEdge_ || inside != cursorInDock_) {
        cursorAtEdge_ = atEdge;
        cursorInDock_ = inside;
        if (settings_.autohide) requestFrame();   // réveille la boucle : le masquage réévalue ses entrées
    }
    // Pas de réveil ici : setCursor ne marque le Dock à redessiner que si son état change.
    controller_.setCursor(inside ? std::optional<POINT>(client) : std::nullopt);
    setTransparent(!inside);
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
    else if (!chosen.empty()) launch(chosen);
    requestFrame();
}

// Écran Apps sur l'écran du Dock ; menu Démarrer si la vue ne peut pas s'ouvrir ou si le catalogue est vide.
void DockApp::openApps() {
    if (menuOpen_) return;   // second clic d'un double-clic, ou une autre fenêtre modale déjà ouverte
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
        if (e && !launch(launchTarget(*e))) log::warn(L"Apps : lancement impossible de %s", e->name.c_str());
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
                if (!launch(it.target)) log::warn(L"Spotlight : lancement impossible de %s", it.title.c_str());
                break;
            case SpotKind::File:
                if (choice->reveal) revealInExplorer(it.target);
                else launch(it.target);
                break;
        }
    }
    apps_.refreshAsync();
    requestFrame();
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

// Agit sur une copie de l'élément : les fenêtres sont relues dans le modèle par appId (stable), jamais
// par index (le Dock a pu changer entre-temps, par exemple pendant un menu).
void DockApp::activateItem(const DockItem& item) {
    switch (item.kind) {
        case ItemKind::App:
            if (auto windows = model_.windowsOf(item.appId); !windows.empty()) {
                activateApp(toHwnds(windows));
            } else {
                std::wstring target = item.launch;
                if (target.empty())
                    if (auto id = model_.identityOf(item.appId)) target = id->launch.empty() ? id->exePath : id->launch;
                if (launch(target)) controller_.startLaunchBounce(item.appId);
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
    return GenieRun{genie_.active(), genie_.active() ? toId(genie_.source()) : 0, genie_.restoring()};
}

void DockApp::noteForeground() {
    HWND fg = GetForegroundWindow();
    RECT r{};
    if (fg && !IsIconic(fg) && GetWindowRect(fg, &r)) lastSeen_[toId(fg)] = r;
}

bool DockApp::startGenie(HWND window, bool restore) {
    if (snapshot_ || settings_.minimizeEffect == MinimizeEffect::Windows || !hwnd_) return false;
    if (genieMustRestoreFirst(genieRun())) {   // une restauration interrompue aboutit quand même
        const HWND previous = genie_.source();
        genie_.finish();
        if (IsWindow(previous)) restoreWindow(previous);
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
    if (!genie_.start(instance_, window, from, *cell, settings_.position, settings_.minimizeEffect, restore, nowSeconds(), slow))
        return false;
    if (trace_) log::info(L"[trace] génie %s %p", restore ? L"restauration" : L"réduction", static_cast<void*>(window));
    requestFrame();   // la miniature de la case s'efface le temps de l'animation
    return true;
}

bool DockApp::stepGenie(double now) {
    if (!genie_.active()) return false;
    if (genie_.step(now)) return true;
    const HWND window = genie_.source();
    const bool restoring = genie_.restoring();
    genie_.finish();   // avant restoreWindow : une animation lancée pendant celle-ci n'est pas coupée
    if (restoring && IsWindow(window)) restoreWindow(window);   // la fenêtre prend la place de son image
    requestFrame();
    return false;
}

void DockApp::saveSettings() {
    saveJsonFileAtomic(dataDir_ + L"\\settings.json", settingsToJson(settings_));
}

void DockApp::showContextMenu(std::optional<std::size_t> index) {
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
        case kCmdShowAll:
            activateItem(item);
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
            minimizeAll(toHwnds(model_.windowsOf(item.appId)));
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
            std::wstring path = L"\"" + dataDir_ + L"\\settings.json\"";
            ShellExecuteW(nullptr, L"open", L"notepad.exe", path.c_str(), nullptr, SW_SHOWNORMAL);
            break;
        }
        case kCmdTrashOpen: openRecycleBin(); break;
        case kCmdTrashEmpty: emptyRecycleBin(hwnd_); break;
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
        case kCmdThemeRestore: {   // à la demande seulement ; plusieurs secondes en Debug : hors du fil de l'interface
            const bool apply = cmd == kCmdThemeApply;
            if (!themeJob_.start([apply] { return apply ? applyMacTheme() : restoreWindowsTheme(); }, hwnd_, WM_APP_THEME))
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
    RenderFrame frame = controller_.buildFrame(dark_, icons_);
    if (trace_ && model_.revision() != loggedRevision_ && !controller_.dragging()) {
        loggedRevision_ = model_.revision();   // positions à jour pour les essais automatisés
        logItemPositions(frame);
    }
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
    if (renderer_.render(frame, metrics_, settings_.font)) {
        renderFailures_ = 0;
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
    if (code == HC_ACTION && wp == WM_MOUSEMOVE && self_) {
        auto* info = reinterpret_cast<MSLLHOOKSTRUCT*>(lp);
        self_->mouseX_ = info->pt.x;
        self_->mouseY_ = info->pt.y;
        // Un seul message en attente à la fois : les mouvements sont fusionnés.
        if (!self_->mousePending_.exchange(true) && !PostMessageW(self_->hwnd_, WM_APP_MOUSE, 0, 0))
            self_->mousePending_ = false;   // file pleine : on réessaiera au prochain mouvement
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

void DockApp::startMouseThread() {
    // Le hook souris bas niveau vit sur son propre thread : la boucle d'animation ne ralentit jamais la souris.
    mouseThread_ = std::thread([this] {
        mouseThreadId_ = GetCurrentThreadId();
        HHOOK hook = SetWindowsHookExW(WH_MOUSE_LL, mouseHookProc, instance_, 0);
        if (!hook) log::error(L"SetWindowsHookEx(WH_MOUSE_LL) a échoué (%lu)", GetLastError());
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {}
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
    if (msg == spotlightMsg_ && spotlightMsg_) {
        openSpotlight();
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
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_LBUTTONDOWN:
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
        requestFrame();
    };
    ev.minimized = [this](HWND h, bool m) {
        if (trace_) log::info(L"[trace] %s %p", m ? L"réduite" : L"restaurée", h);
        model_.windowMinimized(toId(h), m);
        if (genieOnMinimize(genieRun(), toId(h), m, false) == GenieReact::Cancel) genie_.cancel();   // restaurée ailleurs
        requestFrame();
    };
    ev.minimizeStarted = [this](HWND h) {   // réduction vue à l'instant : vers sa case du Dock
        if (genieOnMinimize(genieRun(), toId(h), true, true) == GenieReact::Start) startGenie(h, false);
    };
    ev.titleChanged = [this](HWND h, const std::wstring& t) { model_.windowTitle(toId(h), t); };
    ev.activated = [this](HWND h) {
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
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { running_ = false; break; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!running_) break;

        double now = nowSeconds();
        double dt = std::min(now - last, 0.1);
        last = now;
        bool animating = controller_.tick(dt);
        if (stepVisibility(now)) animating = true;
        if (stepPoof(now)) animating = true;
        if (stepGenie(now)) animating = true;
        bool dirty = controller_.consumeDirty();
        if (animating || dirty || wakeAnimation_) {
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
            renderNow();
        }
        if (animating) {
            DCompositionWaitForCompositorClock(0, nullptr, 50);
        } else {
            WaitMessage();
            last = nowSeconds() - 1.0 / 120;   // reprise sans saut d'animation
        }
    }

    log::info(L"MacDock s'arrête");
    genie_.cancel();
    minAnimate_.restore();   // l'animation de Windows revient
    if (trashNotify_) SHChangeNotifyDeregister(trashNotify_);
    for (ULONG id : stackNotify_) SHChangeNotifyDeregister(id);
    thumbnails_.clear();
    if (dropTarget_) {
        RevokeDragDrop(hwnd_);
        dropTarget_->Release();
        dropTarget_ = nullptr;
    }
    capture_.stop();
    SetEvent(stopEvent_);
    if (configThread_.joinable()) configThread_.join();
    if (mouseThreadId_) PostThreadMessageW(mouseThreadId_, WM_QUIT, 0, 0);
    if (mouseThread_.joinable()) mouseThread_.join();
    CloseHandle(stopEvent_);
    pipe_.stop();
    tracker_.stop();
    removeAppBar();
    DestroyWindow(hwnd_);
    return exitCode_;
}

} // namespace md
