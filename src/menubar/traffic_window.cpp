#include "traffic_window.h"

#include <dwmapi.h>
#include <shellscalingapi.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <future>
#include <map>
#include <optional>

#include "../core/diag.h"
#include "../core/log.h"

namespace md {

TrafficWindow* TrafficWindow::self_ = nullptr;

namespace {
constexpr wchar_t kLightsClass[] = L"MacMenuBarLights";
constexpr UINT_PTR kSampleTimer = 1;   // couleur remesurée 200 ms après le dernier déplacement
constexpr UINT_PTR kProbeTimer = 2;    // boutons de Windows resondés une fois le redimensionnement calmé
constexpr UINT_PTR kBounceTimer = 3;   // rebond de la pastille relâchée
constexpr double kBounceMs = 240;
// Fenêtre qui retrouve sa barre de titre (sortie du plein écran) : l'app la redessine un peu après avoir pris sa
// nouvelle taille ; les pastilles attendent, pour ne pas flotter sur l'ancienne image.
constexpr UINT_PTR kRevealTimer = 5;
constexpr ULONGLONG kRevealMs = 120;
// Messages postés au fil des pastilles (sans fenêtre : les calques vont et viennent). kMsgEvent : WinEvent reporté
// (wParam l'événement, lParam la fenêtre) ; kMsgRecreate : calque à refaire (wParam sa fenêtre cible).
constexpr UINT kMsgAttach = WM_APP + 1, kMsgQuit = WM_APP + 4, kMsgRecreate = WM_APP + 5, kMsgCapture = WM_APP + 6,
               kMsgEvent = WM_APP + 7, kMsgRestack = WM_APP + 8;
constexpr std::size_t kMaxLayers = 48;   // au-delà, les fenêtres suivantes restent sans pastilles

bool isCloaked(HWND h) {   // sur un autre bureau virtuel, ou cachée par DWM
    DWORD cloaked = 0;
    return SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof cloaked)) && cloaked;
}

BOOL CALLBACK collectTopLevel(HWND h, LPARAM lp) {
    if (IsWindowVisible(h) && !IsIconic(h)) reinterpret_cast<std::vector<HWND>*>(lp)->push_back(h);
    return TRUE;
}

// Fil occupé à lire ou sonder une fenêtre (messages envoyés à d'autres apps) : les WinEvent qui arrivent pendant ces
// attentes sont reportés, aucun calque n'est retiré sous nos pieds.
struct Busy {
    int& depth;
    explicit Busy(int& d) : depth(d) { ++depth; }
    ~Busy() { --depth; }
};


} // namespace

// DPI réel de l'écran de la fenêtre (une app non consciente du DPI répond 96 à GetDpiForWindow).
UINT effectiveDpi(HWND h) {
    UINT x = 96, y = 96;
    if (FAILED(GetDpiForMonitor(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), MDT_EFFECTIVE_DPI, &x, &y))) return 96;
    return x;
}

// Processus d'intégrité plus élevée que la nôtre (ou illisible) : UIPI refuserait nos messages.
static bool higherIntegrity(DWORD pid) {
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

// Pastille jaune enfoncée : le Dock prépare l'effet génie (capture de la fenêtre) pendant l'appui, comme il le fait pour
// le bouton « réduire » de Windows, que les pastilles cachent ; sinon la capture partirait au relâchement, en retard.
// L'animation de Windows est coupée d'abord, de façon synchrone (léger, borné à 50 ms) : un clic rapide ne la devance
// pas ; la préparation du génie, plus lourde, est postée.
static void announceMinimize(HWND target, POINT p) {
    static const UINT arm = RegisterWindowMessageW(L"MacDockGenieArm");
    static const UINT will = RegisterWindowMessageW(L"MacDockWillMinimize");
    if (HWND dock = FindWindowW(L"MacDockWindow", nullptr)) {
        SendMessageTimeoutW(dock, will, reinterpret_cast<WPARAM>(target), 0, SMTO_ABORTIFHUNG, 50, nullptr);
        PostMessageW(dock, arm, reinterpret_cast<WPARAM>(target), MAKELPARAM(WORD(SHORT(p.x)), WORD(SHORT(p.y))));
    }
}

LightsWindowInfo readInfo(HWND h) {
    LightsWindowInfo w;
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
    TITLEBARINFO tb{sizeof tb};
    if (GetTitleBarInfo(h, &tb) && tb.rcTitleBar.bottom > tb.rcTitleBar.top) w.captionBottom = tb.rcTitleBar.bottom;
    MONITORINFO mi{sizeof mi};
    if (GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi)) w.monitor = mi.rcMonitor;
    return w;
}

bool TrafficWindow::create(HINSTANCE instance) {
    if (thread_.joinable()) return true;
    self_ = this;
    instance_ = instance;
    quitting_ = false;
    WNDCLASSEXW wc{sizeof wc};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = proc;
    wc.hInstance = instance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kLightsClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        log::warn(L"Feux tricolores : classe impossible (%lu)", GetLastError());
        return false;
    }
    std::promise<void> started;
    std::future<void> ready = started.get_future();
    thread_ = std::thread([this, &started] {
        MSG m;
        PeekMessageW(&m, nullptr, WM_USER, WM_USER, PM_NOREMOVE);   // file de messages prête avant de répondre
        threadId_ = GetCurrentThreadId();
        started.set_value();
        run();
    });
    ready.get();
    return true;
}

void TrafficWindow::run() {
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (!m.hwnd) {
            switch (m.message) {
                case kMsgAttach:
                    if (!quitting_) doAttach(reinterpret_cast<HWND>(m.wParam), LightsMode(m.lParam));
                    continue;
                case kMsgEvent:
                    if (!quitting_) event(DWORD(m.wParam), reinterpret_cast<HWND>(m.lParam), true);
                    continue;
                case kMsgRestack:
                    restackPending_ = false;
                    restack();
                    continue;
                case kMsgRecreate: {   // calque détruit sans nous : refait
                    const HWND target = reinterpret_cast<HWND>(m.wParam);
                    remove(target);
                    if (Layer* l = consider(target)) l->state.inactive = target != active_;
                    continue;
                }
                case kMsgCapture:
                    captureVisible_ = m.wParam != 0;
                    for (auto& [h, l] : layers_)
                        if (l->hwnd)
                            SetWindowDisplayAffinity(l->hwnd, captureVisible_ || diagnosticCapture() ? WDA_NONE : WDA_EXCLUDEFROMCAPTURE);
                    if (captureDone_) SetEvent(captureDone_);
                    continue;
                case kMsgQuit:
                    quitting_ = true;
                    removeAll();
                    unhookGlobal();
                    PostQuitMessage(0);
                    continue;
                default: break;
            }
        }
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
}

void TrafficWindow::attach(HWND active, LightsMode mode) {
    if (threadId_ && !PostThreadMessageW(threadId_, kMsgAttach, reinterpret_cast<WPARAM>(active), LPARAM(mode)))
        log::warn(L"Feux tricolores : message perdu (%lu)", GetLastError());
}

void TrafficWindow::setCaptureVisible(bool on) {
    if (!threadId_) return;
    if (!captureDone_) captureDone_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!captureDone_) return;
    ResetEvent(captureDone_);
    if (PostThreadMessageW(threadId_, kMsgCapture, on ? 1 : 0, 0)) WaitForSingleObject(captureDone_, 200);
}

void TrafficWindow::destroy() {
    if (thread_.joinable()) {
        PostThreadMessageW(threadId_, kMsgQuit, 0, 0);
        // Borné : un glisser en cours vers une app figée ne doit pas bloquer la fermeture de la barre.
        if (WaitForSingleObject(thread_.native_handle(), 3000) == WAIT_OBJECT_0) thread_.join();
        else thread_.detach();
    }
    threadId_ = 0;
    if (self_ == this) self_ = nullptr;
    if (captureDone_) CloseHandle(captureDone_);
    captureDone_ = nullptr;
}

// ---- Fenêtres suivies ----

void TrafficWindow::doAttach(HWND active, LightsMode mode) {
    const bool modeChanged = mode != mode_;
    mode_ = mode;
    const HWND before = active_;
    active_ = active;
    publicTarget_ = active;
    if (mode == LightsMode::Off) {
        removeAll();
        unhookGlobal();
        return;
    }
    if (globalHooks_.empty()) hookGlobal();
    if (modeChanged) {   // fenêtres réévaluées selon le mode
        removeAll();
        sync();
    }
    if (before != active) {
        if (Layer* p = layerOf(before)) {   // devenue inactive : grises, et sa barre de titre change de teinte
            p->state.inactive = true;
            p->painted = false;
            place(*p, false);
            if (p->shown) SetTimer(p->hwnd, kSampleTimer, 150, nullptr);
        }
    } else if (!modeChanged) {   // même fenêtre : couleurs sous les pastilles remesurées (thème changé…)
        for (auto& [h, l] : layers_) place(*l, true);
    }
    if (Layer* a = consider(active)) {
        a->state.inactive = false;
        a->painted = false;
        place(*a, true);
        if (a->shown) SetTimer(a->hwnd, kSampleTimer, 300, nullptr);   // remesure une fois l'apparition finie
    }
}

void TrafficWindow::sync() {
    for (auto it = layers_.begin(); it != layers_.end();) {   // fenêtres fermées depuis
        const HWND target = it->first;
        ++it;
        if (!IsWindow(target)) remove(target);
    }
    std::vector<HWND> all;
    EnumWindows(collectTopLevel, reinterpret_cast<LPARAM>(&all));
    for (HWND h : all)
        if (!isCloaked(h)) consider(h);
}

TrafficWindow::Layer* TrafficWindow::layerOf(HWND target) {
    if (!target) return nullptr;
    const auto it = layers_.find(target);
    return it == layers_.end() ? nullptr : it->second.get();
}

TrafficWindow::Layer* TrafficWindow::consider(HWND target) {
    if (!target || quitting_ || mode_ == LightsMode::Off || !IsWindow(target)) return nullptr;
    if (Layer* l = layerOf(target)) return l;
    if (GetAncestor(target, GA_ROOT) != target) return nullptr;
    if (layers_.size() >= kMaxLayers) purgeHidden();
    if (layers_.size() >= kMaxLayers && target != active_) return nullptr;   // la fenêtre active passe toujours
    Busy busy(busy_);
    // Tri rapide avant de lire la fenêtre en entier (menus, bulles, fenêtres outils : nombreux et jamais éligibles).
    const LONG_PTR style = GetWindowLongPtrW(target, GWL_STYLE), ex = GetWindowLongPtrW(target, GWL_EXSTYLE);
    if ((style & WS_CAPTION) != WS_CAPTION || (style & WS_CHILD) || (ex & WS_EX_TOOLWINDOW)) return nullptr;
    LightsWindowInfo info = readInfo(target);
    // Encore réduite quand elle devient active (restauration depuis le Dock) : suivie quand même, ses pastilles
    // arrivent quand elle réapparaît (place() la masque tant qu'elle est réduite).
    info.iconic = false;
    if (!wantsLights(info, mode_, effectiveDpi(target))) {
        if (diagnosticCapture() && target == active_)
            log::info(L"[diag] pastilles %p [%ls] : refusée (style %08lx, élevée %d)", target, info.className.c_str(),
                      info.style, int(info.elevated));
        return nullptr;
    }
    auto layer = std::make_unique<Layer>();
    layer->target = target;
    GetWindowThreadProcessId(target, &layer->pid);
    if (!layer->pid) return nullptr;   // disparue entre-temps (0 : un crochet sur tous les processus)
    layer->state.inactive = target != active_;
    if (!createLayer(*layer)) return nullptr;
    Layer* l = layer.get();
    byLayer_[l->hwnd] = l;
    layers_[target] = std::move(layer);
    hookProcess(l->pid);
    place(*l, true);
    SetTimer(l->hwnd, kProbeTimer, 500, nullptr);   // une app qui démarre dessine ses boutons un peu après
    return l;
}

bool TrafficWindow::createLayer(Layer& l) {
    // La mesure de la couleur ne voit pas les calques (sauf en diagnostic : visibles aux enregistreurs d'écran ; la
    // mesure se fait à côté d'eux de toute façon).
    const DWORD affinity = diagnosticCapture() || captureVisible_ ? WDA_NONE : WDA_EXCLUDEFROMCAPTURE;
    l.hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kLightsClass, L"", WS_POPUP, 0, 0, 1, 1,
                             nullptr, nullptr, instance_, nullptr);
    if (!l.hwnd) {
        log::warn(L"Feux tricolores : fenêtre impossible (%lu)", GetLastError());
        return false;
    }
    SetWindowDisplayAffinity(l.hwnd, affinity);
    return true;
}

void TrafficWindow::remove(HWND target) {
    const auto it = layers_.find(target);
    if (it == layers_.end()) return;
    Layer& l = *it->second;
    l.removing = true;
    if (l.hwnd) {
        byLayer_.erase(l.hwnd);
        if (IsWindow(l.hwnd)) DestroyWindow(l.hwnd);
    }
    unhookProcess(l.pid);
    layers_.erase(it);
}

void TrafficWindow::purgeHidden() {
    std::vector<HWND> gone;
    for (const auto& [h, l] : layers_)
        if (h != active_ && (!IsWindow(h) || !IsWindowVisible(h))) gone.push_back(h);
    for (HWND h : gone) remove(h);
}

void TrafficWindow::removeAll() {
    while (!layers_.empty()) remove(layers_.begin()->first);
}

void TrafficWindow::hookProcess(DWORD pid) {
    auto& [hook, count] = processHooks_[pid];
    if (count++ == 0)   // déplacements des fenêtres de ce processus seulement
        hook = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, nullptr, onEvent, pid, 0,
                               WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
}

void TrafficWindow::unhookProcess(DWORD pid) {
    const auto it = processHooks_.find(pid);
    if (it == processHooks_.end()) return;
    if (--it->second.second > 0) return;
    if (it->second.first) UnhookWinEvent(it->second.first);
    processHooks_.erase(it);
}

void TrafficWindow::hookGlobal() {
    // Fenêtres montrées, masquées, réduites, détruites, changées de bureau virtuel (cloak), activées : de tous les
    // processus (les fenêtres qui n'ont pas encore de calque doivent pouvoir en recevoir un).
    const DWORD flags = WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS;
    const std::pair<DWORD, DWORD> ranges[] = {{EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND},
                                              {EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND},
                                              {EVENT_OBJECT_DESTROY, EVENT_OBJECT_REORDER},
                                              {EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED}};
    for (auto [from, to] : ranges)
        if (HWINEVENTHOOK h = SetWinEventHook(from, to, nullptr, onEvent, 0, 0, flags)) globalHooks_.push_back(h);
}

void TrafficWindow::unhookGlobal() {
    for (HWINEVENTHOOK h : globalHooks_) UnhookWinEvent(h);
    globalHooks_.clear();
    for (auto& [pid, hook] : processHooks_)
        if (hook.first) UnhookWinEvent(hook.first);
    processHooks_.clear();
}

void CALLBACK TrafficWindow::onEvent(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD) {
    TrafficWindow* t = self_;
    if (!t || !hwnd) return;
    if (event == EVENT_OBJECT_REORDER) {   // ordre des fenêtres du bureau changé (⎇⎋…) : calques replacés
        if (hwnd == GetDesktopWindow() && !t->restackPending_ && !t->layers_.empty()) {
            t->restackPending_ = true;
            PostThreadMessageW(t->threadId_, kMsgRestack, 0, 0);
        }
        return;   // l'ordre des enfants d'une fenêtre ne nous concerne pas
    }
    if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF) return;
    switch (event) {   // tri : seules les fenêtres suivies, ou de premier niveau qui apparaissent
        case EVENT_OBJECT_SHOW:
        case EVENT_SYSTEM_MINIMIZEEND:
        case EVENT_OBJECT_UNCLOAKED:
        case EVENT_SYSTEM_FOREGROUND:
            if (!t->layers_.count(hwnd) && GetAncestor(hwnd, GA_ROOT) != hwnd) return;
            break;
        default:
            if (!t->layers_.count(hwnd)) return;
            break;
    }
    if (t->busy_) {   // au milieu d'une lecture ou d'une sonde : traité juste après, par la boucle du fil
        PostThreadMessageW(t->threadId_, kMsgEvent, event, reinterpret_cast<LPARAM>(hwnd));
        return;
    }
    t->event(event, hwnd);
}

void TrafficWindow::event(DWORD ev, HWND hwnd, bool deferred) {
    switch (ev) {
        case EVENT_OBJECT_DESTROY: remove(hwnd); break;
        case EVENT_OBJECT_HIDE:
        case EVENT_SYSTEM_MINIMIZESTART:
        case EVENT_OBJECT_CLOAKED:
            // Reporté : la fenêtre a pu réapparaître depuis (son SHOW servi avant ce message) ; son état est relu.
            if (Layer* l = layerOf(hwnd)) deferred ? place(*l, false) : hide(*l);
            break;
        case EVENT_SYSTEM_MINIMIZEEND:
            if (Layer* l = layerOf(hwnd)) {
                // Revenue avec l'animation de Windows (pas par le génie, qui la coupe et la marque) : les pastilles
                // attendent qu'elle soit arrivée.
                ANIMATIONINFO ai{sizeof ai};
                const bool animated = SystemParametersInfoW(SPI_GETANIMATION, sizeof ai, &ai, 0) && ai.iMinAnimate;
                if (const unsigned wait = lightsRestoreWaitMs(animated, GetPropW(hwnd, L"MacDockTransitionsHeld") != nullptr)) {
                    hide(*l);
                    l->revealAt = GetTickCount64() + wait;
                    SetTimer(l->hwnd, kRevealTimer, wait, nullptr);
                }
                place(*l, true);
            } else {
                consider(hwnd);
            }
            break;
        case EVENT_OBJECT_SHOW:
        case EVENT_OBJECT_UNCLOAKED:
            if (Layer* l = layerOf(hwnd)) place(*l, true);
            else consider(hwnd);
            break;
        case EVENT_OBJECT_LOCATIONCHANGE:
            if (Layer* l = layerOf(hwnd)) {
                place(*l, false);
                if (l->shown) SetTimer(l->hwnd, kSampleTimer, 200, nullptr);
            }
            break;
        case EVENT_SYSTEM_FOREGROUND:   // passée devant (avec ses fenêtres possédées) : tous les calques recalés
            restack();
            break;
        default: break;
    }
}

void TrafficWindow::restack() {
    for (auto& [h, l] : layers_)
        if (l->shown && GetWindow(l->target, GW_HWNDPREV) != l->hwnd) raise(*l);
}

// ---- Un calque ----

void TrafficWindow::moveLayer(HWND layer, const RECT& r) {
    if (!layer) return;
    POINT pos{r.left, r.top};
    UpdateLayeredWindow(layer, nullptr, &pos, nullptr, nullptr, nullptr, 0, nullptr, 0);   // même image, déplacée
}

void TrafficWindow::hide(Layer& l) {
    if (l.hwnd && l.shown) ShowWindow(l.hwnd, SW_HIDE);
    l.shown = false;
    // La nouvelle sonde reste programmée : une fenêtre masquée faute de mesure complète (app qui démarre) doit
    // recevoir ses pastilles à la reprise.
    if (l.hwnd) KillTimer(l.hwnd, kSampleTimer);
}

void TrafficWindow::sample(Layer& l, const RECT& frame, UINT dpi) {
    // Ligne à mi-hauteur des boutons, juste à gauche du calque : la barre de titre que les boutons interrompent (Mica
    // en dégradé : la teinte de leur hauteur). Une seule copie de l'écran pour les neuf points.
    const LONG y = (l.layout.window.top + l.layout.window.bottom) / 2;
    const LONG step = std::max(1L, LONG(std::lround(3.0 * dpi / 96)));
    const LONG right = l.layout.window.left - 4, left = std::max(right - 8 * step, frame.left + 9);
    if (right <= frame.left + 8) return;
    const int w = int(right - left + 1);
    std::vector<std::uint32_t> samples;
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader = {sizeof(BITMAPINFOHEADER), w, -1, 1, 32, BI_RGB};
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bmp && bits) {
        HGDIOBJ old = SelectObject(mem, bmp);
        if (BitBlt(mem, 0, 0, w, 1, screen, left, y, SRCCOPY)) {
            GdiFlush();
            const auto* px = static_cast<const std::uint32_t*>(bits);
            for (LONG x = right; x >= left; x -= step) samples.push_back(px[x - left] & 0xFFFFFF);   // 0xRRGGBB
        }
        SelectObject(mem, old);
    }
    if (bmp) DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    if (samples.empty()) return;
    const std::uint32_t color = dominantColor(samples);
    if (color == l.state.patchColor && l.painted) return;
    l.state.patchColor = color;
    const double lum = 0.299 * ((color >> 16) & 0xFF) + 0.587 * ((color >> 8) & 0xFF) + 0.114 * (color & 0xFF);
    l.state.dark = lum < 128;
    l.painted = false;
}

TrafficWindow::Placement TrafficWindow::measure(const Layer& l, const LightsWindowInfo& info, UINT dpi, bool& complete) {
    Placement p;
    p.target = l.target;
    p.size = SIZE{info.frame.right - info.frame.left, info.frame.bottom - info.frame.top};
    p.zoomed = info.zoomed;
    p.dpi = dpi;
    p.valid = true;
    const HWND target = l.target;
    // Sonde en lecture seule : WM_NCHITTEST ne fait que répondre une zone. 20 ms par appel au plus et 60 ms en
    // tout : une app occupée n'arrête pas les pastilles (la mesure est alors reprise plus tard).
    const ULONGLONG deadline = GetTickCount64() + 60;
    bool late = false;
    const HitProbe hit = [&](POINT pt) -> LRESULT {
        if (late || GetTickCount64() > deadline) {
            late = true;
            return HTNOWHERE;
        }
        POINT q = pt;
        PhysicalToLogicalPointForPerMonitorDPI(target, &q);   // une app non consciente du DPI répond en points logiques
        DWORD_PTR r = HTNOWHERE;
        if (!SendMessageTimeoutW(target, WM_NCHITTEST, 0, MAKELPARAM(WORD(SHORT(q.x)), WORD(SHORT(q.y))),
                                 SMTO_ABORTIFHUNG | SMTO_ERRORONEXIT, 20, &r)) {
            late = true;
            return HTNOWHERE;
        }
        return LRESULT(r);
    };
    RECT window{}, bounds{};
    GetWindowRect(target, &window);
    if (FAILED(DwmGetWindowAttribute(target, DWMWA_CAPTION_BUTTON_BOUNDS, &bounds, sizeof bounds))) bounds = {};
    const RECT b = captionButtons(window, info.frame, bounds, dpi, hit);
    const bool found = b.right > b.left && b.bottom > b.top;
    complete = !late;
    p.spot = found ? Spot::Over : Spot::None;   // sans boutons trouvés : pas de pastilles, l'app garde les siens
    if (found) p.buttons = RECT{b.left - info.frame.right, b.top - info.frame.top, b.right - info.frame.right, b.bottom - info.frame.top};
    return p;
}

void TrafficWindow::place(Layer& l, bool resample, bool probe) {
    if (!l.hwnd) return;
    Busy busy(busy_);
    if (!IsWindow(l.target)) {   // fermée : retirée par la boucle du fil (l'appelant tient encore ce calque)
        hide(l);
        PostThreadMessageW(GetCurrentThreadId(), kMsgEvent, EVENT_OBJECT_DESTROY, reinterpret_cast<LPARAM>(l.target));
        return;
    }
    LightsWindowInfo info = readInfo(l.target);
    MONITORINFO mon{sizeof mon};
    if (GetMonitorInfoW(MonitorFromWindow(l.target, MONITOR_DEFAULTTONEAREST), &mon))
        info.frame = visibleFrame(info.frame, mon.rcWork, info.zoomed);   // agrandie : pas sous la barre de menus
    const UINT dpi = effectiveDpi(l.target);
    if (!IsWindowVisible(l.target) || isCloaked(l.target) || !wantsLights(info, mode_, dpi)) {
        if (IsWindowVisible(l.target) && !info.iconic) l.refused = true;   // plein écran : sans barre de titre
        hide(l);
        return;
    }
    const RECT& f = info.frame;
    const SIZE size{f.right - f.left, f.bottom - f.top};
    // Agrandie ou rendue à sa taille sous nos yeux : DWM anime le cadre, les pastilles reviennent à la fin.
    unsigned zoomWait = 0;
    if (l.shown && l.placement.valid) {
        ANIMATIONINFO ai{sizeof ai};
        const bool animated = SystemParametersInfoW(SPI_GETANIMATION, sizeof ai, &ai, 0) && ai.iMinAnimate;
        zoomWait = lightsZoomWaitMs(l.placement.zoomed, info.zoomed, animated);
    }
    if (probe || !l.placement.valid || l.placement.zoomed != info.zoomed || l.placement.dpi != dpi) {
        bool complete = true;
        const ULONGLONG t0 = GetTickCount64();
        Placement p = measure(l, info, dpi, complete);
        if (diagnosticCapture() && l.target == active_)
            log::info(L"[diag] pastilles %p : place %d, boutons %ld,%ld,%ld,%ld, complet %d, %llu ms", l.target, int(p.spot),
                      p.buttons.left, p.buttons.top, p.buttons.right, p.buttons.bottom, int(complete), GetTickCount64() - t0);
        if (complete) {
            l.probeRetries = 0;
            l.placement = p;
        } else {
            // Sonde interrompue (app occupée) : réponses manquantes, la place déduite serait fausse. On garde la mesure
            // précédente, sinon rien jusqu'à la reprise.
            if (!l.placement.valid) {
                l.placement = p;
                l.placement.spot = Spot::None;
            }
            if (l.probeRetries++ < 3) SetTimer(l.hwnd, kProbeTimer, 500, nullptr);
        }
    } else if (size.cx != l.placement.size.cx || size.cy != l.placement.size.cy) {
        // Redimensionnement : les boutons restent ancrés à droite ; nouvelle sonde une fois le geste calmé.
        SetTimer(l.hwnd, kProbeTimer, 150, nullptr);
    }
    l.spot = l.placement.spot;
    const RECT& pb = l.placement.buttons;
    const bool hasButtons = pb.right > pb.left;
    const RECT buttons{f.right + pb.left, f.top + pb.top, f.right + pb.right, f.top + pb.bottom};
    if (l.spot != Spot::Over || !hasButtons) {
        hide(l);
        return;
    }
    if (l.refused) {   // la barre de titre revient (sortie du plein écran) : un instant de patience
        l.refused = false;
        l.revealAt = GetTickCount64() + kRevealMs;
        SetTimer(l.hwnd, kRevealTimer, UINT(kRevealMs), nullptr);
    }
    if (zoomWait) {
        hide(l);
        l.revealAt = GetTickCount64() + zoomWait;
        SetTimer(l.hwnd, kRevealTimer, zoomWait, nullptr);
    }
    if (GetTickCount64() < l.revealAt) return;   // montrées par kRevealTimer
    const LightsLayout layout = lightsOverButtons(buttons, dpi, info.zoomed);
    const bool resized = layout.window.right - layout.window.left != l.layout.window.right - l.layout.window.left ||
                         layout.window.bottom - layout.window.top != l.layout.window.bottom - l.layout.window.top ||
                         layout.topGap != l.layout.topGap;
    l.layout = layout;
    l.scale = dpi / 96.0;
    // Fermer : menu système, ou bouton fermer trouvé par la sonde (Electron sans menu système).
    const bool closable = !(info.classStyle & CS_NOCLOSE) && ((info.style & WS_SYSMENU) || hasButtons);
    const bool enabled[3] = {closable, (info.style & WS_MINIMIZEBOX) != 0, (info.style & WS_MAXIMIZEBOX) != 0};
    for (int i = 0; i < 3; ++i)
        if (l.state.enabled[i] != enabled[i]) {
            l.state.enabled[i] = enabled[i];
            l.painted = false;
        }
    if (resample || !l.painted) sample(l, info.frame, dpi);
    if (resized) l.painted = false;
    if (!l.painted) paint(l);
    else moveLayer(l.hwnd, l.layout.window);
    raise(l);
}

void TrafficWindow::paint(Layer& l) {
    l.paintedSize = {};
    const int w = int(l.layout.window.right - l.layout.window.left), h = int(l.layout.window.bottom - l.layout.window.top);
    if (!l.hwnd || w <= 0 || h <= 0) return;
    const auto px = renderLights(l.layout, l.state, l.scale);
    BITMAPINFO bi{};
    bi.bmiHeader = {sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB};
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bmp && bits) {
        std::memcpy(bits, px.data(), px.size());
        HGDIOBJ old = SelectObject(mem, bmp);
        POINT pos{l.layout.window.left, l.layout.window.top}, zero{0, 0};
        SIZE size{w, h};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        UpdateLayeredWindow(l.hwnd, screen, &pos, &size, mem, &zero, 0, &blend, ULW_ALPHA);
        SelectObject(mem, old);
        l.paintedSize = size;
    }
    if (bmp) DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    l.painted = l.paintedSize.cx > 0;
}

void TrafficWindow::raise(Layer& l) {
    if (!l.hwnd || !l.target) return;
    // Ordre voulu : le calque, juste au-dessus de sa fenêtre (sous celles qui la recouvrent).
    // Fenêtre « toujours au-dessus » : le calque l'est aussi, sinon il passerait sous elle.
    const bool topmost = (GetWindowLongPtrW(l.target, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
    const bool layerTopmost = (GetWindowLongPtrW(l.hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
    constexpr UINT kKeep = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE;
    if (topmost != layerTopmost) SetWindowPos(l.hwnd, topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, kKeep);
    const HWND above = GetWindow(l.target, GW_HWNDPREV);
    const bool inOrder = above == l.hwnd;
    UINT flags = kKeep | SWP_SHOWWINDOW;
    // La fenêtre est la première de sa bande : le haut des fenêtres ordinaires, ou des fenêtres toujours au-dessus.
    HWND after = topmost ? HWND_TOPMOST : HWND_TOP;
    if (inOrder) {
        flags |= SWP_NOZORDER;   // déjà juste au-dessus
    } else {
        HWND a = above;
        while (a && a == l.hwnd) a = GetWindow(a, GW_HWNDPREV);
        const bool aTopmost = a && (GetWindowLongPtrW(a, GWL_EXSTYLE) & WS_EX_TOPMOST);
        if (a && aTopmost == topmost) after = a;
    }
    SetWindowPos(l.hwnd, after, 0, 0, 0, 0, flags);
    l.shown = true;
}

LRESULT CALLBACK TrafficWindow::proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (TrafficWindow* t = self_) {
        const auto it = t->byLayer_.find(hwnd);
        if (it != t->byLayer_.end()) return t->handle(*it->second, msg, wp, lp);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT TrafficWindow::handle(Layer& l, UINT msg, WPARAM wp, LPARAM lp) {
    const HWND from = l.hwnd;
    const auto screenPoint = [] {   // position écran du message : juste même si le calque a bougé depuis
        const DWORD pos = GetMessagePos();
        return POINT{short(LOWORD(pos)), short(HIWORD(pos))};
    };
    const auto overGroup = [&](POINT p) {
        return p.x >= l.layout.circles[0].left - 2 && p.x <= l.layout.circles[2].right + 2 &&
               p.y >= l.layout.circles[0].top - 2 && p.y <= l.layout.circles[0].bottom + 2;
    };
    switch (msg) {
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_NCDESTROY:   // détruit sans nous (ne devrait pas arriver) : refait par la boucle du fil
            byLayer_.erase(from);
            l.hwnd = nullptr;
            l.shown = l.tracking = false;
            if (!quitting_ && !l.removing)
                PostThreadMessageW(GetCurrentThreadId(), kMsgRecreate, reinterpret_cast<WPARAM>(l.target), 0);
            return DefWindowProcW(from, msg, wp, lp);   // la fenêtre de l'app reste active
        case WM_MOUSEMOVE: {
            if (l.dragging) {
                const POINT p = screenPoint();
                SetWindowPos(l.target, nullptr, l.dragFrom.left + p.x - l.dragStart.x, l.dragFrom.top + p.y - l.dragStart.y, 0,
                             0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
                return 0;
            }
            if (!l.tracking) {
                TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, from, 0};
                l.tracking = TrackMouseEvent(&t) != FALSE;
            }
            const POINT p = screenPoint();
            const bool hover = overGroup(p);
            // Pastille appuyée : enfoncée seulement tant que le doigt reste dessus, comme sur macOS.
            const int pressed = l.pressed >= 0 && hitLight(l.layout, p) == l.pressed ? l.pressed : -1;
            if (hover != l.state.hover || pressed != l.state.pressed) {
                l.state.hover = hover;   // symboles ×, −, + sur les trois (en couleur sur une fenêtre inactive)
                l.state.pressed = pressed;
                paint(l);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            l.tracking = false;
            if (l.state.hover) {
                l.state.hover = false;
                paint(l);
            }
            return 0;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK: {
            const POINT p = screenPoint();
            const int hit = hitLight(l.layout, p);
            l.pressed = -1;
            switch (lightsMouse(msg == WM_LBUTTONDBLCLK, hit, l.state.enabled)) {
                case LightsMouse::Press:   // sur une fenêtre inactive aussi, sans l'activer (macOS)
                    l.pressed = hit;
                    l.state.pressed = hit;
                    l.state.bouncing = -1;
                    SetCapture(from);
                    paint(l);
                    if (hit == 1) announceMinimize(l.target, p);   // le génie se prépare avant le relâchement
                    break;
                case LightsMouse::Drag:   // le fond appartient à la barre de titre : on déplace la fenêtre nous-mêmes
                    if (l.target != GetAncestor(GetForegroundWindow(), GA_ROOT)) SetForegroundWindow(l.target);
                    if (!IsZoomed(l.target) && GetWindowRect(l.target, &l.dragFrom)) {
                        l.dragging = true;
                        l.dragStart = p;
                        SetCapture(from);
                    }
                    break;
                case LightsMouse::Zoom:
                    if (UINT cmd = captionDoubleClick((GetWindowLongPtrW(l.target, GWL_STYLE) & WS_MAXIMIZEBOX) != 0,
                                                      IsZoomed(l.target) != FALSE))
                        PostMessageW(l.target, WM_SYSCOMMAND, cmd, 0);
                    break;
                case LightsMouse::None: break;
            }
            return 0;
        }
        case WM_CAPTURECHANGED:
            l.pressed = -1;
            l.dragging = false;
            if (l.state.pressed >= 0) {
                l.state.pressed = -1;
                paint(l);
            }
            return 0;
        case WM_LBUTTONUP: {
            if (l.dragging) {
                l.dragging = false;
                ReleaseCapture();
                return 0;
            }
            if (l.pressed < 0) return 0;
            const int pressed = l.pressed;
            l.pressed = -1;
            ReleaseCapture();
            // Petit rebond élastique de la pastille relâchée (Golden Gate), puis la commande.
            l.state.pressed = -1;
            l.state.bouncing = pressed;
            l.state.bounce = 1 - 0.06;
            l.bounceStart = GetTickCount64();
            SetTimer(from, kBounceTimer, 15, nullptr);
            paint(l);
            if (hitLight(l.layout, screenPoint()) == pressed && IsWindow(l.target))
                PostMessageW(l.target, WM_SYSCOMMAND, lightCommand(pressed, IsZoomed(l.target) != FALSE), 0);
            return 0;
        }
        case WM_TIMER:
            if (wp == kRevealTimer) {
                KillTimer(from, kRevealTimer);
                l.revealAt = 0;
                place(l, true, true);   // nouvelle sonde : les boutons ont pu changer de taille
            } else if (wp == kSampleTimer) {
                KillTimer(from, kSampleTimer);
                place(l, true);
            } else if (wp == kProbeTimer) {
                KillTimer(from, kProbeTimer);
                place(l, true, true);
            } else if (wp == kBounceTimer) {
                const double t = double(GetTickCount64() - l.bounceStart) / kBounceMs;
                if (t >= 1 || l.state.bouncing < 0) {
                    KillTimer(from, kBounceTimer);
                    l.state.bouncing = -1;
                    l.state.bounce = 1;
                } else {   // ressort amorti : 0,94 → léger dépassement → 1
                    l.state.bounce = 1 - 0.06 * std::cos(2.4 * 3.14159265358979 * t) * std::exp(-3.5 * t);
                }
                if (l.shown) paint(l);
            }
            return 0;
        default: break;
    }
    return DefWindowProcW(from, msg, wp, lp);
}

} // namespace md
