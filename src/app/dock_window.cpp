#include "dock_window.h"

#include <dcomp.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <shlobj.h>

#include <algorithm>
#include <cmath>
#include <map>

#include "../calib/image_diff.h"
#include "../calib/png_io.h"
#include "../config/config_store.h"
#include "../core/log.h"
#include "../popup/menu_window.h"
#include "../shell/default_pins.h"
#include "../shell/shell_actions.h"
#include "../tracker/app_identity.h"
#include "dock_menus.h"

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
constexpr UINT_PTR kConfigTimer = 0x4346;   // "CF"
constexpr UINT_PTR kTrashTimer = 0x5442;    // "TB"
// Raccourcis de calibration (Ctrl+Alt+Maj) : superposition, opacité + et −.
constexpr int kHotOverlay = 1, kHotOpacityUp = 2, kHotOpacityDown = 3;

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
    icons_.setStrictTahoe(settings_.tahoeStrictIcons);
    icons_.setGrid(metrics_.iconShapeRatio, metrics_.iconCornerRatio, metrics_.iconJailInset, metrics_.iconShadowOpacity);
    controller_.setSettings(settings_);
    controller_.setMetrics(metrics_);
    updateGlass();   // réglage glass modifié à chaud
}

void DockApp::savePinned() {
    settings_.pinned = model_.pinnedEntries();
    saveSettings();
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

void DockApp::reposition() {
    HMONITOR mon = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(mon, &mi);
    monitor_ = mi.rcMonitor;
    UINT dpiX = 96, dpiY = 96;
    GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    scale_ = float(dpiX) / 96.0f;

    int reserve = int(DockController::reservePx(settings_, metrics_, scale_));
    int height = int(DockController::windowHeightPx(settings_, metrics_, scale_));
    int width = monitor_.right - monitor_.left;

    int bottom = monitor_.bottom;
    if (appBar_) {
        APPBARDATA abd{};
        abd.cbSize = sizeof abd;
        abd.hWnd = hwnd_;
        abd.uEdge = ABE_BOTTOM;
        abd.rc = {monitor_.left, monitor_.bottom - reserve, monitor_.right, monitor_.bottom};
        SHAppBarMessage(ABM_QUERYPOS, &abd);   // évite les autres barres (barre Windows si le mod est absent)
        abd.rc.top = abd.rc.bottom - reserve;
        SHAppBarMessage(ABM_SETPOS, &abd);
        bottom = abd.rc.bottom;
    }
    origin_ = POINT{monitor_.left, bottom - height};
    SetWindowPos(hwnd_, HWND_TOPMOST, origin_.x, origin_.y, width, height,
                 SWP_NOACTIVATE | (snapshot_ ? 0 : SWP_SHOWWINDOW));
    renderer_.resize(UINT(width), UINT(height));
    controller_.setViewport(UINT(width), UINT(height), scale_);
    if (capture_.status() != BackdropCapture::Status::Off)
        capture_.setRegion({origin_.x, origin_.y, origin_.x + width, origin_.y + height});
    requestFrame();
}

bool DockApp::initRenderer() {
    HMONITOR mon = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
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
    if (!p) return;
    DockItem item = *p;
    switch (item.kind) {
        case ItemKind::App:
            if (item.running) {
                activateApp(toHwnds(item.windows));
            } else {
                std::wstring target = item.launch;
                if (target.empty())
                    if (auto id = model_.identityOf(item.appId)) target = id->launch.empty() ? id->exePath : id->launch;
                if (launch(target)) controller_.startLaunchBounce(item.appId);
            }
            break;
        case ItemKind::AppsButton: openStartMenu(); break;
        case ItemKind::Stack: openFolder(item.launch); break;
        case ItemKind::Trash: openRecycleBin(); break;
        case ItemKind::MinimizedWindow:
            restoreWindow(reinterpret_cast<HWND>(static_cast<std::uintptr_t>(item.window)));
            break;
        default: break;
    }
    requestFrame();
}

void DockApp::saveSettings() {
    saveJsonFileAtomic(dataDir_ + L"\\settings.json", settingsToJson(settings_));
}

void DockApp::showContextMenu(std::optional<std::size_t> index) {
    const DockItem* p = index ? controller_.itemAt(*index) : nullptr;
    MenuContext ctx;
    ctx.item = p ? *p : DockItem{ItemKind::Separator};
    ctx.settings = settings_;
    const DockItem& item = ctx.item;
    if (item.kind == ItemKind::App) {
        if (auto id = model_.identityOf(item.appId)) ctx.exePath = id->exePath;
        ctx.openAtLogin = isOpenAtLogin(ctx.exePath);
        for (WindowId w : model_.windowsOf(item.appId)) ctx.windows.emplace_back(w, model_.titleOf(w));
    } else if (item.kind == ItemKind::Trash) {
        ctx.trashFull = recycleBinHasItems();
    }

    // Ancrage : centré au-dessus de l'icône (ou du curseur, hors icône), juste au-dessus du Dock.
    RenderFrame frame = controller_.buildFrame(dark_, icons_);
    POINT cursor;
    GetCursorPos(&cursor);
    float top = frame.bgTop;
    LONG x = cursor.x;
    if (index && *index < frame.icons.size()) {
        const RenderIcon& icon = frame.icons[*index];
        x = origin_.x + LONG(std::lround(icon.cx));
        if (!icon.separator) top = std::min(top, icon.cy - icon.size / 2);
    }
    POINT anchor{x, origin_.y + LONG(std::lround(top - 6 * scale_))};

    MenuWindow::Env env;
    env.instance = instance_;
    env.device = renderer_.device();
    env.dark = dark_;
    env.glass = settings_.glass && !captureFailed_ && !renderer_.isWarp();
    env.scale = scale_;
    env.font = renderer_.fontName(settings_.font);
    env.metrics = metrics_;
    env.trace = trace_;
    controller_.setCursor(std::nullopt);   // l'agrandissement retombe pendant le menu, comme sur macOS
    requestFrame();
    // Une seule duplication de l'écran par processus : celle du Dock cède la place à celle du menu.
    pauseCapture();
    int cmd = MenuWindow::track(env, buildDockMenu(ctx), anchor);
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
            if (index) onClick(*index);
            break;
        case kCmdKeep:
            if (item.pinned) {
                if (model_.unpin(item.key)) savePinned();
            } else {
                std::size_t at = 0;   // après la dernière app épinglée (avant les piles)
                for (auto& e : model_.pinnedEntries()) at += e.kind != PinKind::Stack;
                if (model_.pin(item.appId, at)) savePinned();
            }
            break;
        case kCmdLogin:
            setOpenAtLogin(ctx.exePath, item.name, !ctx.openAtLogin);
            break;
        case kCmdReveal:
            if (item.kind == ItemKind::Stack) openFolder(item.launch);
            else revealInExplorer(ctx.exePath);
            break;
        case kCmdHide:
            model_.setHidden(item.appId, true);
            minimizeAll(toHwnds(item.windows));
            break;
        case kCmdQuit:
            for (HWND h : toHwnds(item.windows)) PostMessageW(h, WM_CLOSE, 0, 0);
            break;
        case kCmdAutohide:
            settings_.autohide = !settings_.autohide;
            saveSettings();
            applySettings();
            break;
        case kCmdMagnify:
            settings_.magnification = !settings_.magnification;
            saveSettings();
            applySettings();
            break;
        case kCmdPosLeft:
        case kCmdPosBottom:
        case kCmdPosRight:
            break;   // Gauche et Droite : plan 4
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
        case kCmdRestore:
            restoreWindow(reinterpret_cast<HWND>(static_cast<std::uintptr_t>(item.window)));
            break;
        case kCmdCloseWindow:
            PostMessageW(reinterpret_cast<HWND>(static_cast<std::uintptr_t>(item.window)), WM_CLOSE, 0, 0);
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

void DockApp::refreshTrash() {
    bool full = recycleBinHasItems();
    if (trace_) log::info(L"[trace] corbeille %s", full ? L"pleine" : L"vide");
    model_.setTrashFull(full);
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
    if (msg == taskbarCreated_ && taskbarCreated_) {
        log::info(L"Explorateur redémarré : réenregistrement");
        removeAppBar();
        registerAppBar();
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
        case WM_APP_TRASH: {
            LONG event = 0;
            PIDLIST_ABSOLUTE* pidls = nullptr;
            if (HANDLE lock = SHChangeNotification_Lock(HANDLE(wp), DWORD(lp), &pidls, &event))
                SHChangeNotification_Unlock(lock);
            if (trace_) log::info(L"[trace] corbeille : avis 0x%lx", unsigned long(event));
            SetTimer(hwnd_, kTrashTimer, 300, nullptr);   // une suppression multiple envoie une rafale d'avis
            return 0;
        }
        case WM_TIMER:
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
            return 0;
        case WM_DISPLAYCHANGE:
        case WM_DPICHANGED:
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
        registerAppBar();
        dragSprite_.create(instance);
        poofSprite_.create(instance);
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
        requestFrame();
    };
    ev.minimized = [this](HWND h, bool m) {
        if (trace_) log::info(L"[trace] %s %p", m ? L"réduite" : L"restaurée", h);
        model_.windowMinimized(toId(h), m);
        requestFrame();
    };
    ev.titleChanged = [this](HWND h, const std::wstring& t) { model_.windowTitle(toId(h), t); };
    ev.activated = [this](HWND h) {
        controller_.setAttention(model_.appOfWindow(toId(h)), false);
        requestFrame();
    };
    ev.flashed = [this](HWND h) {
        if (trace_) log::info(L"[trace] attention %p", h);
        controller_.setAttention(model_.appOfWindow(toId(h)), true);
        requestFrame();
    };
    tracker_.setTrace(trace_);
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
        if (stepPoof(now)) animating = true;
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
    if (trashNotify_) SHChangeNotifyDeregister(trashNotify_);
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
