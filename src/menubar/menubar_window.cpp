#include "menubar_window.h"

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
#include "../core/log.h"
#include "../core/strings.h"
#include "../tracker/app_identity.h"
#include "bar_color.h"
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

constexpr wchar_t kClassName[] = L"MacMenuBarWindow";
constexpr UINT WM_APP_APPBAR = WM_APP + 1;
constexpr UINT WM_APP_SAMPLE = WM_APP + 2;
constexpr UINT WM_APP_UIA_TITLES = WM_APP + 3;   // lParam : UiaTitles* (à libérer)
constexpr UINT WM_APP_STATUS = WM_APP + 4;       // lParam : StatusSnapshot* (à libérer)
constexpr UINT WM_APP_VOLUME = WM_APP + 5;       // Core Audio : wParam 1 = sortie par défaut changée
constexpr int kBrightnessJob = 1;                // curseur de luminosité glissé : seule la dernière valeur part
constexpr DWORD kUiaItemsWaitMs = 2500;           // lecture d'un menu à son ouverture

struct UiaTitles {
    unsigned generation = 0;
    HWND window = nullptr;
    std::vector<RawMenuItem> titles;
};
constexpr UINT_PTR kClockTimer = 0x434C;        // "CL"
constexpr UINT_PTR kSampleTimeout = 0x5354;     // "ST" : pas d'image utilisable de la capture
constexpr UINT_PTR kResampleTimer = 0x5253;     // "RS" : diaporama de fonds d'écran
constexpr UINT_PTR kResampleSoon = 0x5253 + 1;  // après un changement de fond (transition de Windows)
constexpr UINT_PTR kConfigTimer = 0x4346;       // "CF"
constexpr UINT_PTR kFullscreenTimer = 0x4653;   // "FS"
constexpr UINT_PTR kRecentTimer = 0x5243;       // "RC" : écriture différée des apps récentes
constexpr UINT_PTR kVisibilityTimer = 0x5649;   // "VI"
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
    if (!f.fromFile && !f.wasInvalid && !f.unreadable && hwnd_) saveJsonFileAtomic(path, menuBarSettingsToJson(settings_));
    fileTime(path, settingsTime_);
    auto m = loadJsonFile(dataDir_ + L"\\dock-metrics.json");
    if (m.fromFile && !m.wasInvalid) glassMetrics_ = metricsFromJson(m.value);
}

void MenuBarApp::checkSettingsFile() {
    FILETIME t{};
    if (!fileTime(dataDir_ + L"\\menubar.json", t) || CompareFileTime(&t, &settingsTime_) == 0) return;
    log::info(L"menubar.json modifié : rechargement");
    loadSettings(false);
    applySettings();
}

void MenuBarApp::applySettings() {
    visibility_.setTimings({0, 0.5, 0.25, 0.25});
    syncAppBar();
    reposition();
}

void MenuBarApp::loadLogo() {
    UINT w = 0, h = 0;
    auto px = readPng(dataDir_ + L"\\menubar-logo.png", w, h);
    LogoImage logo;
    if (!px.empty()) {
        logo.w = w;
        logo.h = h;
        logo.bgra = std::move(px);
        log::info(L"Barre : logo personnalisé %ux%u", w, h);
    }
    renderer_.setLogo(std::move(logo));
}

// ---- Placement ----

void MenuBarApp::registerAppBar() {
    APPBARDATA abd{};
    abd.cbSize = sizeof abd;
    abd.hWnd = hwnd_;
    abd.uCallbackMessage = WM_APP_APPBAR;
    appBar_ = SHAppBarMessage(ABM_NEW, &abd) != FALSE;
}

void MenuBarApp::removeAppBar() {
    if (!appBar_) return;
    APPBARDATA abd{};
    abd.cbSize = sizeof abd;
    abd.hWnd = hwnd_;
    SHAppBarMessage(ABM_REMOVE, &abd);
    appBar_ = false;
}

void MenuBarApp::syncAppBar() {
    if (!hwnd_) return;
    const bool want = !settings_.autohide;
    if (want == appBar_) return;
    if (want) registerAppBar();
    else removeAppBar();
}

void MenuBarApp::reposition() {
    HMONITOR mon = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(mon, &mi);
    monitor_ = mi.rcMonitor;
    UINT dpiX = 96, dpiY = 96;
    GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    const float scale = float(dpiX) / 96.0f;
    heightPx_ = int(std::lround(settings_.metrics.height * scale));
    if (scale != scale_ || font_.empty() || hwnd_ == nullptr) {
        scale_ = scale;
        font_ = renderer_.setFont(settings_.font, float(settings_.metrics.fontSize) * scale_);
    } else {
        font_ = renderer_.setFont(settings_.font, float(settings_.metrics.fontSize) * scale_);   // réglage changé
    }
    if (hwnd_ && appBar_) {
        APPBARDATA abd{};
        abd.cbSize = sizeof abd;
        abd.hWnd = hwnd_;
        abd.uEdge = ABE_TOP;
        abd.rc = {monitor_.left, monitor_.top, monitor_.right, monitor_.top + heightPx_};
        SHAppBarMessage(ABM_QUERYPOS, &abd);
        abd.rc.bottom = abd.rc.top + heightPx_;
        SHAppBarMessage(ABM_SETPOS, &abd);
    }
    const int width = monitor_.right - monitor_.left;
    if (hwnd_) {
        SetWindowPos(hwnd_, HWND_TOPMOST, monitor_.left, monitor_.top - yOffsetPx_, width, heightPx_,
                     SWP_NOACTIVATE | (visible_ ? SWP_SHOWWINDOW : 0));
        renderer_.resize(UINT(width), UINT(heightPx_));
    }
    if (trace_) log::info(L"[trace] barre : %d x %d px (échelle %.2f), zone réservée %s, police %s", width, heightPx_,
                          scale_, appBar_ ? L"oui" : L"non", font_.c_str());
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

void MenuBarApp::onForeground(HWND h) {
    if (!h) return;
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
        if (hwnd_) SetTimer(hwnd_, kRecentTimer, 5000, nullptr);
    }
    active_ = a;
    checkFullscreen();
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
    HWND bar = hwnd_;
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
    if (hwnd_) KillTimer(hwnd_, kRecentTimer);
    if (!recentDirty_ || !hwnd_) return;
    recentDirty_ = false;
    if (!saveJsonFileAtomic(dataDir_ + L"\\menubar-recent.json", recentToJson(recent_)))
        log::warn(L"Barre : apps récentes non enregistrées");
}

std::vector<RecentEntry> MenuBarApp::withIcons(std::vector<RecentEntry> list, bool documents) const {
    const int px = int(std::lround(kMenuIconSize * scale_));
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

BarContext MenuBarApp::context(bool recentDocs) const {
    BarContext c;
    c.appName = active_.name.empty() ? L"Explorateur" : active_.name;
    c.userName = userDisplayName();
    c.explorer = active_.explorer || active_.name.empty();
    c.desktop = active_.desktop || active_.name.empty();
    if (!c.desktop)
        for (WindowId id : model_.windowsOf(active_.appId)) c.windows.push_back({id, model_.titleOf(id)});
    c.activeWindow = toId(target_.window);
    c.source = active_.source;
    c.real = active_.real;
    c.menuOwner = toId(active_.menuOwner);
    c.recentApps = withIcons(recent_.apps, false);
    PWSTR folder = nullptr;
    if (recentDocs && SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Recent, KF_FLAG_DONT_VERIFY, nullptr, &folder)))
        c.recentDocs = withIcons(recentDocuments(folder, kRecentMax, recent_.clearedAt), true);
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
    const MenuBarMetrics& m = settings_.metrics;
    const double pad = m.titlePadding;
    layoutIn_ = {};
    layoutIn_.barWidth = double(monitor_.right - monitor_.left) / scale_;
    layoutIn_.leftMargin = m.leftMargin;
    layoutIn_.rightMargin = m.rightMargin;
    for (const auto& menu : menus_.menus) {
        if (menu.logo) layoutIn_.leftWidths.push_back(m.logoSize + 2 * pad);
        else layoutIn_.leftWidths.push_back(std::ceil(renderer_.measure(menu.title, menu.bold) / scale_) + 2 * pad);
    }
    updateClock();
    status_ = statusItems(statusState());
    for (const auto& item : status_)
        layoutIn_.rightWidths.push_back(item.kind == StatusKind::Clock
                                            ? std::ceil(renderer_.measure(clock_, false) / scale_) + 2 * pad
                                            : m.statusWidth);
    layout_ = layoutBar(layoutIn_);
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

BarFrame MenuBarApp::frame() const {
    BarFrame f;
    f.scale = scale_;
    f.darkText = darkText_;
    f.metrics = settings_.metrics;
    for (std::size_t i = 0; i < layout_.leftVisible && i < menus_.menus.size(); ++i) {
        BarDrawItem it;
        it.text = menus_.menus[i].title;
        it.bold = menus_.menus[i].bold;
        it.logo = menus_.menus[i].logo;
        it.x = float(layout_.leftX[i]) * scale_;
        it.width = float(layoutIn_.leftWidths[i]) * scale_;
        it.highlighted = int(i) == highlight_;
        f.items.push_back(std::move(it));
    }
    for (std::size_t j = 0; j < layout_.rightX.size() && j < status_.size(); ++j) {
        BarDrawItem it;
        if (status_[j].kind == StatusKind::Clock) it.text = clock_;
        it.glyph = status_[j].glyph;
        it.level = status_[j].level;
        it.alt = status_[j].alt;
        it.x = float(layout_.rightX[j]) * scale_;
        it.width = float(layoutIn_.rightWidths[j]) * scale_;
        it.highlighted = highlight_ == int(layout_.leftVisible + j);
        f.items.push_back(std::move(it));
    }
    return f;
}

void MenuBarApp::render() {
    if (!hwnd_) return;
    if (renderer_.render(frame())) {
        renderFailures_ = 0;
        return;
    }
    log::warn(L"Barre : rendu impossible (0x%08lX)", static_cast<unsigned long>(renderer_.lastError()));
    if (isDeviceLost(renderer_.lastError()) || ++renderFailures_ >= 3) recoverDevice();
}

void MenuBarApp::recoverDevice() {
    if (menuOpen_) return;   // le menu ouvert utilise encore le device : on réessaiera au prochain rendu
    log::warn(L"Barre : device graphique perdu, recréation");
    renderer_.reset();
    renderFailures_ = 0;
    if (!renderer_.init(hwnd_)) {
        log::error(L"Barre : recréation du device impossible, arrêt (le lanceur relancera la barre)");
        exitCode_ = 3;
        PostQuitMessage(3);
        return;
    }
    font_.clear();
    reposition();   // surface à la bonne taille, police, mise en page, puis rendu
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
    SetTimer(hwnd_, kClockTimer, ms + 20, nullptr);
}

// ---- Couleur du texte ----

void MenuBarApp::startSample() {
    if (menuOpen_ || sampler_.running() || !visible_) return;
    // La barre est exclue de la capture le temps de l'échantillon : on mesure le fond, pas son texte.
    SetWindowDisplayAffinity(hwnd_, WDA_EXCLUDEFROMCAPTURE);
    RECT strip{monitor_.left, monitor_.top, monitor_.right, monitor_.top + heightPx_};
    HMONITOR mon = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    if (!sampler_.start(hwnd_, WM_APP_SAMPLE, mon, strip)) {
        finishSample(std::nullopt);
        return;
    }
    SetTimer(hwnd_, kSampleTimeout, 2000, nullptr);
}

void MenuBarApp::onSample() {
    if (!sampler_.running()) return;
    if (sampler_.failed()) {
        finishSample(std::nullopt);
        return;
    }
    if (auto lum = sampler_.take(renderer_.device())) finishSample(lum);
}

void MenuBarApp::finishSample(std::optional<double> luminance) {
    sampler_.stop();
    KillTimer(hwnd_, kSampleTimeout);
    SetWindowDisplayAffinity(hwnd_, WDA_NONE);   // la barre réapparaît dans les captures d'écran
    lastSample_ = nowSeconds();
    const bool before = darkText_;
    if (luminance) darkText_ = chooseDarkText(*luminance, darkText_);
    else darkText_ = !systemDarkMode();   // repli : le thème de Windows
    if (trace_) {
        if (luminance) log::info(L"[trace] barre : luminance du fond %.3f → texte %s", *luminance, darkText_ ? L"foncé" : L"clair");
        else log::info(L"[trace] barre : fond non mesuré → texte %s (thème)", darkText_ ? L"foncé" : L"clair");
    }
    if (before != darkText_) render();
}

// ---- Plein écran et masquage ----

bool MenuBarApp::detectFullscreen() const {
    HWND fg = GetForegroundWindow();
    if (!fg || !IsWindowVisible(fg) || IsIconic(fg)) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (pid == GetCurrentProcessId()) return false;
    wchar_t cls[64] = {};
    GetClassNameW(fg, cls, 64);
    for (const wchar_t* shell : {L"Progman", L"WorkerW", L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd"})
        if (wcscmp(cls, shell) == 0) return false;
    if (MonitorFromWindow(fg, MONITOR_DEFAULTTONULL) != MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY))
        return false;
    RECT rc;
    const bool caption = (GetWindowLongPtrW(fg, GWL_STYLE) & WS_CAPTION) == WS_CAPTION;
    return GetWindowRect(fg, &rc) && isFullscreenWindow(rc, monitor_, IsZoomed(fg) != FALSE, caption);
}

void MenuBarApp::checkFullscreen() {
    const bool fs = detectFullscreen();
    if (fs == fullscreen_) return;
    fullscreen_ = fs;
    if (trace_) log::info(L"[trace] barre : plein écran %s", fs ? L"oui" : L"non");
    stepVisibility();
}

void MenuBarApp::stepVisibility() {
    if (!hwnd_) return;
    POINT pt{};
    GetCursorPos(&pt);
    const bool onMonitor = pt.x >= monitor_.left && pt.x < monitor_.right;
    VisibilityInputs in;
    in.autohide = settings_.autohide;
    in.fullscreen = fullscreen_;
    in.cursorAtEdge = onMonitor && pt.y <= monitor_.top + 1;
    in.cursorInDock = onMonitor && pt.y >= monitor_.top && pt.y < monitor_.top + heightPx_ - yOffsetPx_;
    in.menuOpen = menuOpen_;
    const bool animating = visibility_.update(in, nowSeconds());
    const int offset = int(std::lround((1.0 - visibility_.shown()) * heightPx_));
    const bool show = !visibility_.hidden();
    if (offset != yOffsetPx_ || show != visible_) {
        yOffsetPx_ = offset;
        visible_ = show;
        SetWindowPos(hwnd_, HWND_TOPMOST, monitor_.left, monitor_.top - yOffsetPx_, 0, 0,
                     SWP_NOACTIVATE | SWP_NOSIZE | (show ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    }
    // Tant que le curseur peut faire apparaître la barre ou qu'elle s'anime, on la suit de près.
    const bool watch = animating || settings_.autohide || fullscreen_;
    if (watch && !visibilityTimer_) SetTimer(hwnd_, kVisibilityTimer, 16, nullptr);
    if (!watch && visibilityTimer_) KillTimer(hwnd_, kVisibilityTimer);
    visibilityTimer_ = watch;
}

// ---- Menus et actions ----

MenuWindow::Env MenuBarApp::menuEnv() const {
    MenuWindow::Env env;
    env.instance = instance_;
    env.device = renderer_.device();
    env.dark = systemDarkMode();
    env.glass = true;
    env.scale = scale_;
    env.font = font_;
    env.metrics = glassMetrics_;
    env.trace = trace_;
    return env;
}

void MenuBarApp::onPress(POINT client) {
    const BarHit hit = hitTestBar(layout_, layoutIn_, double(client.x) / scale_);
    if (hit.kind == BarHit::Kind::Left) {
        openMenu(hit.index);
    } else if (hit.kind == BarHit::Kind::Right && hit.index < status_.size()) {
        const StatusKind k = status_[hit.index].kind;
        if (opensMenu(k)) {
            openMenu(layout_.leftVisible + hit.index);
        } else {   // recherche de Windows (Win+S) ; horloge : centre de notifications (Win+N)
            auto inputs = shortcutInputs(*parseShortcut(k == StatusKind::Search ? L"Win+S" : L"Win+N"));
            SendInput(UINT(inputs.size()), inputs.data(), sizeof(INPUT));
        }
    }
}

void MenuBarApp::openSettingsFile() {
    ShellExecuteW(nullptr, L"open", L"notepad.exe", (L"\"" + dataDir_ + L"\\menubar.json\"").c_str(), nullptr, SW_SHOWNORMAL);
}

void MenuBarApp::onRightClick(POINT client) {
    if (hitTestBar(layout_, layoutIn_, double(client.x) / scale_).kind != BarHit::Kind::None) return;
    MenuModel m;
    m.items.push_back({kCmdBarSettings, L"Réglages de la barre des menus…"});
    MenuItem autohide{kCmdBarAutohide, L"Masquer automatiquement la barre des menus"};
    autohide.checked = settings_.autohide;
    m.items.push_back(autohide);
    m.items.push_back({});
    m.items.push_back({kCmdBarQuit, L"Quitter la barre des menus"});
    POINT anchor{client.x + monitor_.left, monitor_.top + heightPx_ + LONG(std::lround(scale_))};
    if (sampler_.running()) finishSample(std::nullopt);
    menuOpen_ = true;
    ReleaseCapture();
    const int r = MenuWindow::track(menuEnv(), m, anchor, MenuWindow::Side::Below);
    menuOpen_ = false;
    if (layoutPending_) {
        relayout();
        render();
    }
    if (r <= 0) restoreTargetFocus();
    switch (r) {
        case kCmdBarSettings: openSettingsFile(); break;
        case kCmdBarAutohide:
            settings_.autohide = !settings_.autohide;
            saveJsonFileAtomic(dataDir_ + L"\\menubar.json", menuBarSettingsToJson(settings_));
            fileTime(dataDir_ + L"\\menubar.json", settingsTime_);
            applySettings();
            stepVisibility();
            break;
        case kCmdBarQuit: PostMessageW(hwnd_, WM_CLOSE, 0, 0); break;
        default: break;
    }
}

void MenuBarApp::openMenu(std::size_t index) {
    if (sampler_.running()) finishSample(std::nullopt);   // une seule duplication de l'écran par processus
    int current = int(index), previous = -1;
    const BarTarget target = target_;   // la cible au moment de l'ouverture, quoi qu'il arrive pendant le menu
    for (;;) {
        menuOpen_ = false;
        const int left = int(layout_.leftVisible), n = left + int(status_.size());
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
            render();
            StatusCommand chosen;
            const int r = trackStatus(std::size_t(current - left), barLink(current), chosen);
            if (auto next = menuSwitchTarget(r)) {
                previous = current;
                current = *next;
                continue;
            }
            menuOpen_ = false;
            highlight_ = -1;
            if (layoutPending_) relayout();
            render();
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
        menus_ = buildBarMenus(context(true));   // liste des fenêtres, vrais menus et documents récents à jour
        menuOpen_ = true;
        if (current < 0 || std::size_t(current) >= layout_.leftVisible || std::size_t(current) >= menus_.menus.size()) break;
        // Copies : MenuWindow lit le modèle pendant toute sa boucle modale.
        const MenuModel model = menus_.menus[std::size_t(current)].model;
        const std::map<int, MenuAction> actions = menus_.actions;
        highlight_ = current;
        render();
        const MenuWindow::BarLink link = barLink(current);
        POINT anchor{link.titles[std::size_t(current)].left, link.titles[std::size_t(current)].bottom + LONG(std::lround(scale_))};
        ReleaseCapture();   // sinon l'appui sur le titre garde la souris : glisser-relâcher dans le menu ne marcherait pas
        if (trace_) log::info(L"[trace] barre : menu « %s » ouvert", menus_.menus[std::size_t(current)].title.c_str());
        const int r = MenuWindow::track(menuEnv(), model, anchor, MenuWindow::Side::Below, &link);
        if (auto next = menuSwitchTarget(r)) {
            previous = current;
            current = *next;
            continue;
        }
        menuOpen_ = false;
        highlight_ = -1;
        if (layoutPending_) relayout();
        render();
        target_ = target;
        if (r > 0) {
            if (auto it = actions.find(r); it != actions.end()) execute(it->second);
        } else {
            restoreTargetFocus();
        }
        return;
    }
    highlight_ = -1;
    menuOpen_ = false;
    if (layoutPending_) relayout();
    render();
}

MenuWindow::BarLink MenuBarApp::barLink(int current) const {
    RECT win{};
    GetWindowRect(hwnd_, &win);
    MenuWindow::BarLink link;
    const auto box = [&](double x, double w) {
        const LONG l = win.left + LONG(std::lround(x * scale_));
        return RECT{l, win.top, l + LONG(std::lround(w * scale_)), win.bottom};
    };
    for (std::size_t i = 0; i < layout_.leftVisible; ++i) link.titles.push_back(box(layout_.leftX[i], layoutIn_.leftWidths[i]));
    for (std::size_t j = 0; j < status_.size() && j < layout_.rightX.size(); ++j)   // sans menu : rectangle vide
        link.titles.push_back(opensMenu(status_[j].kind) ? box(layout_.rightX[j], layoutIn_.rightWidths[j]) : RECT{});
    link.current = current;
    return link;
}

int MenuBarApp::trackStatus(std::size_t j, const MenuWindow::BarLink& link, StatusCommand& chosen) {
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
        else if (actionOf(id) == StatusAction::Brightness) hub_.post([v] { setBrightness(v); }, kBrightnessJob);
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
    POINT anchor{box.left, box.bottom + LONG(std::lround(scale_))};
    ReleaseCapture();
    if (trace_) log::info(L"[trace] barre : menu d'état %d ouvert", int(kind));
    const int r = MenuWindow::track(menuEnv(), menu.model, anchor, MenuWindow::Side::Below, &link, &live);
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

LRESULT CALLBACK MenuBarApp::wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (self_ && self_->hwnd_ == hwnd) return self_->handle(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT MenuBarApp::handle(UINT msg, WPARAM wp, LPARAM lp) {
    if (tracker_.handleMessage(msg, wp, lp)) return 0;
    switch (msg) {
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;   // le clavier reste à l'app
        case WM_LBUTTONDOWN: onPress(POINT{short(LOWORD(lp)), short(HIWORD(lp))}); return 0;
        case WM_RBUTTONUP: onRightClick(POINT{short(LOWORD(lp)), short(HIWORD(lp))}); return 0;
        case WM_APP_APPBAR:
            if (wp == ABN_POSCHANGED) reposition();
            else if (wp == ABN_FULLSCREENAPP) checkFullscreen();
            return 0;
        case WM_APP_SAMPLE: onSample(); return 0;
        case WM_APP_UIA_TITLES: onUiaTitles(lp); return 0;
        case WM_APP_STATUS: onStatus(lp); return 0;
        case WM_APP_VOLUME:
            if (wp == 1) audio_.watch(hwnd_, WM_APP_VOLUME);   // nouvelle sortie par défaut : on la suit
            updateStatusItems();
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
                case kSampleTimeout: finishSample(sampler_.sawBlack() ? std::optional<double>(0.0) : std::nullopt); break;
                case kResampleTimer: startSample(); break;
                case kResampleSoon: KillTimer(hwnd_, kResampleSoon); startSample(); break;
                case kConfigTimer: checkSettingsFile(); break;
                case kRecentTimer: saveRecent(); break;
                case kFullscreenTimer: checkFullscreen(); break;
                case kVisibilityTimer: stepVisibility(); break;
                default: break;
            }
            return 0;
        case WM_SETTINGCHANGE:
            if (wp == SPI_SETDESKWALLPAPER ||
                (lp && CompareStringOrdinal(reinterpret_cast<const wchar_t*>(lp), -1, L"ImmersiveColorSet", -1, TRUE) == CSTR_EQUAL))
                SetTimer(hwnd_, kResampleSoon, 800, nullptr);   // après la transition du fond
            return 0;
        case WM_DISPLAYCHANGE:
        case WM_DPICHANGED:
            reposition();
            SetTimer(hwnd_, kResampleSoon, 800, nullptr);
            return 0;
        case WM_TIMECHANGE:
            updateClock();
            relayout();
            render();
            scheduleClock();
            return 0;
        case WM_ENDSESSION:
            if (wp) removeAppBar();
            return 0;
        case WM_CLOSE:
            removeAppBar();
            PostQuitMessage(0);
            return 0;
        default: break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

int MenuBarApp::runSnapshot(const Options& options) {
    if (!renderer_.initOffscreen()) return 2;
    HMONITOR mon = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(mon, &mi);
    monitor_ = mi.rcMonitor;
    UINT dpiX = 96, dpiY = 96;
    GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    scale_ = float(dpiX) / 96.0f;
    heightPx_ = int(std::lround(settings_.metrics.height * scale_));
    font_ = renderer_.setFont(settings_.font, float(settings_.metrics.fontSize) * scale_);
    const UINT W = UINT(monitor_.right - monitor_.left), H = UINT(heightPx_);
    const bool dark = options.dark.value_or(systemDarkMode());

    // Fond : le fond d'écran donné (mis à la taille de l'écran, bande du haut), sinon un dégradé selon le thème.
    std::vector<std::uint8_t> bg(std::size_t(W) * H * 4);
    UINT ww = 0, wh = 0;
    auto wall = options.wallpaper.empty() ? std::vector<std::uint8_t>() : readPng(options.wallpaper, ww, wh);
    if (!wall.empty()) {
        const UINT mh = UINT(monitor_.bottom - monitor_.top);
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
    darkText_ = chooseDarkText(lum, false);
    active_.name = options.app.empty() ? L"Notes" : options.app;
    active_.explorer = active_.name == L"Explorateur";
    audio_.init();
    snap_.network = readNetwork();   // les radios et la lecture en cours (WinRT, fil MTA) ne servent pas ici
    snap_.battery = readBattery();
    relayout();
    highlight_ = options.open;
    std::vector<std::uint8_t> out;
    if (!renderer_.renderToImage(frame(), bg, W, H, out) || !writePng(options.snapshot, out.data(), W, H)) {
        log::error(L"Barre : --snapshot impossible");
        return 1;
    }
    log::info(L"Barre --snapshot : %ux%u, luminance %.3f, texte %s, police %s, %zu titres visibles sur %zu", W, H, lum,
              darkText_ ? L"foncé" : L"clair", font_.c_str(), layout_.leftVisible, menus_.menus.size());
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
    darkText_ = !systemDarkMode();
    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);
    hwnd_ = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, kClassName,
                            L"MacMenuBar", WS_POPUP, 0, 0, 100, 24, nullptr, nullptr, instance, nullptr);
    if (!hwnd_) {
        log::error(L"Barre : CreateWindowEx a échoué (%lu)", GetLastError());
        return 2;
    }
    if (!renderer_.init(hwnd_)) {
        log::error(L"Barre : initialisation graphique impossible");
        return 2;
    }
    loadSettings(false);   // écrit menubar.json s'il manque (hwnd_ existe maintenant)
    visibility_.setTimings({0, 0.5, 0.25, 0.25});
    if (!settings_.autohide) registerAppBar();
    reposition();

    WindowTracker::Events ev;
    ev.opened = [this](HWND h, const AppIdentity& id) { model_.windowOpened(toId(h), id); };
    ev.closed = [this](HWND h) { model_.windowClosed(toId(h)); };
    ev.minimized = [this](HWND h, bool m) { model_.windowMinimized(toId(h), m); };
    ev.titleChanged = [this](HWND h, const std::wstring& t) { model_.windowTitle(toId(h), t); };
    ev.foreground = [this](HWND h) { onForeground(h); };   // bureau et dialogues compris
    ev.flashed = [](HWND) {};
    tracker_.start(hwnd_, ev);
    uia_.start();   // sinon : menus Win32 et génériques seulement
    audio_.init();   // sinon : pas d'icône du son
    audio_.watch(hwnd_, WM_APP_VOLUME);
    if (!hub_.start(hwnd_, WM_APP_STATUS)) log::warn(L"Barre : relevés d'état indisponibles");
    loadRecent();
    onForeground(GetForegroundWindow());
    if (active_.name.empty()) {   // rien d'identifiable au premier plan : le bureau
        active_.name = L"Explorateur";
        active_.explorer = active_.desktop = true;
        relayout();
    }

    render();
    scheduleClock();
    startSample();
    SetTimer(hwnd_, kResampleTimer, 60000, nullptr);
    SetTimer(hwnd_, kConfigTimer, 2000, nullptr);
    SetTimer(hwnd_, kFullscreenTimer, 1000, nullptr);
    checkFullscreen();
    stepVisibility();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    log::info(L"MacMenuBar s'arrête");
    sampler_.stop();
    tracker_.stop();
    uia_.stop();
    hub_.stop();
    audio_.unwatch();
    saveRecent();
    removeAppBar();
    DestroyWindow(hwnd_);
    for (MSG m; PeekMessageW(&m, nullptr, WM_APP_UIA_TITLES, WM_APP_UIA_TITLES, PM_REMOVE);)   // résultats en attente
        delete reinterpret_cast<UiaTitles*>(m.lParam);
    for (MSG m; PeekMessageW(&m, nullptr, WM_APP_STATUS, WM_APP_STATUS, PM_REMOVE);)
        delete reinterpret_cast<StatusSnapshot*>(m.lParam);
    return exitCode_;
}

} // namespace md
