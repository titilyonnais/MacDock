#include "settings_window.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <wtsapi32.h>
#include <shellscalingapi.h>
#include <shobjidl.h>
#include <wincodec.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <thread>

#include "../anim/motion.h"
#include "../config/config_store.h"
#include "../core/diag.h"
#include "../core/log.h"
#include "../core/version.h"
#include "../interact/hotkey.h"
#include "../settings/backup.h"
#include "../settings/instance.h"
#include "../settings/mods.h"
#include "../settings/app_icon.h"
#include "../settings/pane_icons.h"
#include "../settings/screens.h"

namespace md {

using Microsoft::WRL::ComPtr;
namespace mt = ui::metrics;

namespace {
constexpr UINT_PTR kAnimTimer = 1, kReloadTimer = 2, kCommitTimer = 3, kEnvTimer = 4;
// kMsgRecordKey : touche lue par le crochet de l'enregistreur (wParam vk, lParam MOD_…) ; kMsgActionDone : fil d'une
// action fini (wParam : relire l'environnement, lParam : réussie).
constexpr UINT kMsgRecordKey = WM_APP + 10, kMsgActionDone = WM_APP + 11, kMsgUnhook = WM_APP + 12;
constexpr UINT_PTR kUnhookTimer = 5;   // touches gardées jamais relâchées (bureau sécurisé…) : le crochet part quand même
constexpr ULONGLONG kCommitEveryMs = 120;            // curseur tiré : une écriture au plus toutes les 120 ms
constexpr float kLightsX = mt::lightsX, kLightsY = mt::lightsY;   // premier centre des pastilles (dans le panneau)
constexpr D2D1_RECT_F kSearch{mt::sidebarInset, 46, mt::sidebarWidth - mt::sidebarInset, 46 + mt::searchHeight};

bool appsDark() {
    // MACDOCK_SETTINGS_THEME=light|dark (essais) : sinon le mode des applications de Windows.
    wchar_t forced[16] = {};
    if (GetEnvironmentVariableW(L"MACDOCK_SETTINGS_THEME", forced, 16)) return !wcscmp(forced, L"dark");
    DWORD v = 1, size = sizeof v;
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme",
                 RRF_RT_REG_DWORD, nullptr, &v, &size);
    return v == 0;
}

float rowHeight(const RowSpec& r) {
    if (r.kind == RowKind::Slider) return mt::rowHeightSlider;
    if (r.kind == RowKind::Shortcut) return mt::rowHeightDetail;   // place pour un conflit ou la consigne d'écoute
    return r.detail.empty() ? mt::rowHeight : mt::rowHeightDetail;
}

bool inside(const D2D1_RECT_F& r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }

// Modificateurs tenus pendant l'écoute d'un raccourci : le crochet garde les touches pour lui, l'état asynchrone du
// clavier ne les voit donc pas ; il les suit lui-même (gauche et droite).
struct HeldMods {
    bool ctrl[2]{}, alt[2]{}, shift[2]{}, win[2]{};
    UINT mods() const {
        return (ctrl[0] || ctrl[1] ? MOD_CONTROL : 0u) | (alt[0] || alt[1] ? MOD_ALT : 0u) | (shift[0] || shift[1] ? MOD_SHIFT : 0u) |
               (win[0] || win[1] ? MOD_WIN : 0u);
    }
    bool track(DWORD vk, bool down) {   // vrai pour une touche de modification
        switch (vk) {
            case VK_LCONTROL: ctrl[0] = down; return true;
            case VK_RCONTROL: ctrl[1] = down; return true;
            case VK_LMENU: alt[0] = down; return true;
            case VK_RMENU: alt[1] = down; return true;
            case VK_LSHIFT: shift[0] = down; return true;
            case VK_RSHIFT: shift[1] = down; return true;
            case VK_LWIN: win[0] = down; return true;
            case VK_RWIN: win[1] = down; return true;
            default: return false;
        }
    }
};
HeldMods g_held;
KeyGate g_gate;   // quelles touches le crochet garde (relâches comprises), voir hotkey.h

bool modifierKey(DWORD vk) {
    switch (vk) {
        case VK_LCONTROL: case VK_RCONTROL: case VK_LMENU: case VK_RMENU:
        case VK_LSHIFT: case VK_RSHIFT: case VK_LWIN: case VK_RWIN: return true;
        default: return false;
    }
}

// MacDock (lanceur, Dock, barre) a-t-il encore une fenêtre ou une instance unique ?
bool macdockAlive() {
    for (const std::wstring& w : macdockWindows())
        if (FindWindowW(w.c_str(), nullptr)) return true;
    for (const std::wstring& m : macdockMutexes())
        if (HANDLE h = OpenMutexW(SYNCHRONIZE, FALSE, m.c_str())) {
            CloseHandle(h);
            return true;
        }
    return false;
}

// Explorateur de cette session seulement, arrêté sans droits élevés ; Windows le relance de lui-même, sinon nous.
void restartExplorer(const std::wstring& windowsDir) {
    DWORD session = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe{sizeof pe};
        for (BOOL more = Process32FirstW(snap, &pe); more; more = Process32NextW(snap, &pe)) {
            DWORD s = 0;
            if (_wcsicmp(pe.szExeFile, L"explorer.exe") || !ProcessIdToSessionId(pe.th32ProcessID, &s) || s != session) continue;
            if (HANDLE p = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID)) {
                TerminateProcess(p, 1);
                CloseHandle(p);
            }
        }
        CloseHandle(snap);
    }
    for (int i = 0; i < 20; ++i) {   // Winlogon relance le bureau en une à deux secondes
        Sleep(250);
        if (FindWindowW(L"Shell_TrayWnd", nullptr)) return;
    }
    SHELLEXECUTEINFOW sei{sizeof sei};
    const std::wstring exe = windowsDir + L"\\explorer.exe";
    sei.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    sei.lpFile = exe.c_str();
    sei.nShow = SW_SHOWDEFAULT;
    ShellExecuteExW(&sei);
}

D2D1_RECT_F searchClear() { return D2D1::RectF(kSearch.right - 24, kSearch.top, kSearch.right, kSearch.bottom); }
}  // namespace

SettingsWindow* SettingsWindow::recordTarget_ = nullptr;

UINT SettingsWindow::paneMessage() {
    static const UINT msg = RegisterWindowMessageW(L"MacDockSettingsPane");
    return msg;
}

SettingsWindow::~SettingsWindow() {
    if (worker_.joinable()) worker_.join();
    if (change_ != INVALID_HANDLE_VALUE) FindCloseChangeNotification(change_);
    if (icon_) DestroyIcon(icon_);
    if (smallIcon_) DestroyIcon(smallIcon_);
}

bool SettingsWindow::create(HINSTANCE instance, const std::wstring& dataDir, PaneId pane, bool testMode) {
    instance_ = instance;
    dir_ = dataDir;
    pane_ = pane;
    testMode_ = testMode;
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    exeDir_ = exe;
    exeDir_ = exeDir_.substr(0, exeDir_.find_last_of(L"\\/"));
    // Démarrage avec Windows : la clé Run réelle ; en essai (--data), un fichier du dossier d'essai.
    const std::wstring launcher = exeDir_ + L"\\MacDockLauncher.exe";
    io_ = testMode_ ? fileStartupIo(dir_ + L"\\startup-test.json", launcher) : registryIo(launcher);
    dark_ = appsDark();
    model_ = loadModel(dir_, &files_, &io_);
    updateProblem();
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf()));
    font_ = ui::interfaceFont(dwrite_.Get());
    buildEnv();
    refreshSidebar();
    icon_ = makeSettingsIcon(GetSystemMetrics(SM_CXICON) * 2);
    smallIcon_ = makeSettingsIcon(GetSystemMetrics(SM_CXSMICON) * 2);

    WNDCLASSEXW wc{sizeof wc};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = icon_;
    wc.hIconSm = smallIcon_;
    const std::wstring windowClass = settingsInstance(testMode_).windowClass;   // l'essai a la sienne
    wc.lpszClassName = windowClass.c_str();
    RegisterClassExW(&wc);

    // Sur l'écran du curseur, au tiers haut, à son échelle.
    POINT pt{};
    GetCursorPos(&pt);
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(mon, &mi);
    UINT dpi = 96, dpiY = 96;
    GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dpi, &dpiY);
    scale_ = float(dpi) / 96.f;
    const int w = int(std::lround(mt::windowWidth * scale_)), h = int(std::lround(mt::defaultHeight * scale_));
    const RECT& wa = mi.rcWork;
    const int x = wa.left + (wa.right - wa.left - w) / 2, y = wa.top + std::max(0L, (wa.bottom - wa.top - h) / 3);
    hwnd_ = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, windowClass.c_str(), testMode_ ? L"Réglages MacDock (essai)" : L"Réglages MacDock",
                            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX, x, y, w, h, nullptr,
                            nullptr, instance, this);
    if (!hwnd_) return false;
    const MARGINS glass{-1, -1, -1, -1};
    DwmExtendFrameIntoClientArea(hwnd_, &glass);
    const int backdrop = 3;   // DWMSBT_TRANSIENTWINDOW : acrylique derrière la barre latérale translucide
    DwmSetWindowAttribute(hwnd_, 38 /* DWMWA_SYSTEMBACKDROP_TYPE */, &backdrop, sizeof backdrop);
    const DWORD corner = 2;   // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd_, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &corner, sizeof corner);
    updateDark();
    if (!initGraphics()) return false;
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    selectPane(pane);
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    change_ = FindFirstChangeNotificationW(dir_.c_str(), FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME);
    WTSRegisterSessionNotification(hwnd_, NOTIFY_FOR_THIS_SESSION);   // verrouillage : l'écoute d'un raccourci s'arrête
    return true;
}

int SettingsWindow::run() {
    MSG msg{};
    for (;;) {
        const DWORD n = change_ != INVALID_HANDLE_VALUE ? 1 : 0;
        const DWORD r = MsgWaitForMultipleObjectsEx(n, &change_, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (n && r == WAIT_OBJECT_0) {   // un fichier du dossier a changé (le Dock, la barre, ou nous) : relu un peu après
            FindNextChangeNotification(change_);
            if (hwnd_) SetTimer(hwnd_, kReloadTimer, 150, nullptr);
            continue;
        }
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                closing_->store(true);   // une recherche de mise à jour n'est plus attendue
                if (worker_.joinable()) worker_.join();   // « Relancer » ou un installateur va jusqu'au bout
                return int(msg.wParam);
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
}

// ---- Graphismes ----

bool SettingsWindow::initGraphics() {
    const D3D_DRIVER_TYPE drivers[] = {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP};
    for (D3D_DRIVER_TYPE d : drivers)
        if (SUCCEEDED(D3D11CreateDevice(nullptr, d, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &d3d_,
                                        nullptr, nullptr)))
            break;
    if (!d3d_) return false;
    ComPtr<IDXGIDevice> dxgi;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory2> factory;
    if (FAILED(d3d_.As(&dxgi)) || FAILED(dxgi->GetAdapter(&adapter)) || FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) return false;
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    pxW_ = UINT(std::max(1L, rc.right - rc.left));
    pxH_ = UINT(std::max(1L, rc.bottom - rc.top));
    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width = pxW_;
    sd.Height = pxH_;
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    sd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    if (FAILED(factory->CreateSwapChainForComposition(d3d_.Get(), &sd, nullptr, &swap_))) return false;
    D2D1_FACTORY_OPTIONS fo{};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &fo,
                                 reinterpret_cast<void**>(d2d_.GetAddressOf()))) ||
        FAILED(d2d_->CreateDevice(dxgi.Get(), &d2dDevice_)) ||
        FAILED(d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc_)))
        return false;
    if (FAILED(DCompositionCreateDevice(dxgi.Get(), IID_PPV_ARGS(&dcomp_))) ||
        FAILED(dcomp_->CreateTargetForHwnd(hwnd_, TRUE, &dcompTarget_)) || FAILED(dcomp_->CreateVisual(&visual_)))
        return false;
    visual_->SetContent(swap_.Get());
    dcompTarget_->SetRoot(visual_.Get());
    dcomp_->Commit();
    return createTarget();
}

void SettingsWindow::releaseTarget() {
    if (dc_) dc_->SetTarget(nullptr);
    target_.Reset();
}

bool SettingsWindow::createTarget() {
    ComPtr<IDXGISurface> surface;
    if (FAILED(swap_->GetBuffer(0, IID_PPV_ARGS(&surface)))) return false;
    const auto props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                                               D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (FAILED(dc_->CreateBitmapFromDxgiSurface(surface.Get(), &props, &target_))) return false;
    dc_->SetTarget(target_.Get());
    return true;
}

void SettingsWindow::resize() {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const UINT w = UINT(std::max(1L, rc.right - rc.left)), h = UINT(std::max(1L, rc.bottom - rc.top));
    if (!swap_ || (w == pxW_ && h == pxH_)) return;
    pxW_ = w;
    pxH_ = h;
    releaseTarget();
    swap_->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
    createTarget();
    scrollBy(0);   // le contenu peut maintenant tenir
}

float SettingsWindow::widthPt() const { return float(pxW_) / scale_; }
float SettingsWindow::heightPt() const { return float(pxH_) / scale_; }

void SettingsWindow::render() {
    if (!dc_ || !target_) return;
    const ui::Palette pal = ui::palette(dark_);
    const float w = widthPt(), h = heightPt();
    dc_->BeginDraw();
    dc_->SetTransform(D2D1::Matrix3x2F::Scale(scale_, scale_));
    dc_->Clear(D2D1::ColorF(0, 0, 0, 0));
    {
        ui::Painter p(dc_.Get(), dwrite_.Get(), pal, font_, &formats_);
        ui::drawWindowBackground(p, w, h);   // Tahoe : barre latérale de verre flottante, le contenu tout autour
        computeGeometry(p, w);
        drawSidebar(p, h);
        drawContent(p, w, h);
        if (menu_) {
            const RowSpec& s = spec(menu_->row);
            ui::drawMenu(p, menu_->rect, s.choices, int(std::lround(valueOf(menu_->row))), menu_->hover);
        }
        if (sheet_) {
            sheet_->layout = ui::layoutSheet(p, w, mt::titleBar, sheet_->spec);
            ui::drawSheet(p, sheet_->layout, sheet_->spec, w, h, -1, sheet_->pressed, float(sheet_->appear.value()));
        }
    }
    const HRESULT hr = dc_->EndDraw();
    const HRESULT shown = swap_->Present(1, 0);
    if (hr == D2DERR_RECREATE_TARGET || shown == DXGI_ERROR_DEVICE_REMOVED || shown == DXGI_ERROR_DEVICE_RESET) recreateGraphics();
}

void SettingsWindow::recreateGraphics() {
    releaseTarget();
    target_.Reset();
    dc_.Reset();
    d2dDevice_.Reset();
    d2d_.Reset();
    swap_.Reset();
    visual_.Reset();
    dcompTarget_.Reset();
    dcomp_.Reset();
    d3d_.Reset();
    if (!initGraphics()) log::error(L"Réglages : graphismes impossibles à refaire");
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// ---- Barre latérale ----

void SettingsWindow::drawSidebar(ui::Painter& p, float h) {
    const ui::Palette& pal = p.pal();
    const ui::Panel panel = ui::sidebarPanel(h);   // les sections restent dans le panneau (défilement de la fenêtre)
    p.rt()->PushAxisAlignedClip(D2D1::RectF(panel.left, panel.top, panel.right, ui::sidebarVisibleBottom(h)),
                                D2D1_ANTIALIAS_MODE_ALIASED);
    ui::drawWindowLights(p, D2D1::Point2F(kLightsX, kLightsY), active_, lightsHover_, lightsPressedInside_ ? lightsPressed_ : -1, 2);
    ui::drawSearchField(p, kSearch, query_, searchFocused_, query_.empty() ? 0.f : kSearch.right - searchClear().left);
    if (searchFocused_) {   // curseur d'insertion après le texte, jamais sur ⓧ
        const float x = std::min(kSearch.left + 27 + (query_.empty() ? 0.f : p.textWidth(query_, mt::fontBody)) + 1,
                                 query_.empty() ? kSearch.right - 8 : searchClear().left - 2);
        p.rt()->DrawLine(D2D1::Point2F(x, kSearch.top + 7), D2D1::Point2F(x, kSearch.bottom - 7), p.brush(pal.accent), 1.2f);
    }
    if (!query_.empty()) {   // ⓧ : efface la recherche
        const D2D1_RECT_F c = searchClear();
        const D2D1_POINT_2F o{(c.left + c.right) / 2, (c.top + c.bottom) / 2};
        p.fillCircle(o, 7, pal.tertiaryText);
        ID2D1SolidColorBrush* ink = p.brush(pal.window);
        p.rt()->DrawLine(D2D1::Point2F(o.x - 2.6f, o.y - 2.6f), D2D1::Point2F(o.x + 2.6f, o.y + 2.6f), ink, 1.4f);
        p.rt()->DrawLine(D2D1::Point2F(o.x - 2.6f, o.y + 2.6f), D2D1::Point2F(o.x + 2.6f, o.y - 2.6f), ink, 1.4f);
    }
    const auto& panes = paneList();
    if (visible_.empty())
        p.text(L"Aucun résultat", D2D1::RectF(mt::sidebarInset, mt::sidebarTop, mt::sidebarWidth - mt::sidebarInset, mt::sidebarTop + 30),
               mt::fontBody, pal.secondaryText, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_TEXT_ALIGNMENT_CENTER);
    for (std::size_t k = 0; k < visible_.size() && k < sidebarTops_.size(); ++k) {
        const std::size_t i = std::size_t(visible_[k]);
        const float top = sidebarTops_[k];
        const D2D1_RECT_F row{mt::sidebarInset, top, mt::sidebarWidth - mt::sidebarInset, top + mt::sidebarRow};
        const bool selected = panes[i].id == pane_;
        if (selected) p.fillRound(row, 6, active_ ? pal.accent : pal.sidebarSelection);
        else if (int(k) == sidebarHover_) p.fillRound(row, 6, ui::Rgba{pal.sidebarSelection.r, pal.sidebarSelection.g,
                                                                         pal.sidebarSelection.b, pal.sidebarSelection.a * 0.5f});
        drawPaneTile(p, D2D1::RectF(row.left + 8, top + 4, row.left + 8 + mt::tile, top + 4 + mt::tile), panes[i].tile, panes[i].icon);
        p.text(panes[i].title, D2D1::RectF(row.left + 36, top, row.right - 6, top + mt::sidebarRow), mt::fontBody,
               selected && active_ ? pal.onAccent : pal.text);
    }
    p.rt()->PopAxisAlignedClip();
}

// ---- Contenu ----

void SettingsWindow::computeGeometry(ui::Painter& p, float w) {
    geoms_.assign(rows_.size(), Geom{});
    const float x0 = mt::sidebarWidth + mt::contentMargin, x1 = w - mt::contentMargin;
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const RowRef& ref = rows_[i];
        const ui::RowBox& box = layout_.groups[std::size_t(ref.group)].rows[std::size_t(ref.row)];
        const RowSpec& s = spec(int(i));
        Geom& g = geoms_[i];
        const float top = box.top - scroll_;
        g.row = D2D1::RectF(x0, top, x1, top + box.height);
        g.cy = s.kind == RowKind::Slider ? top + 20 : top + box.height / 2;
        const float right = x1 - mt::rowPadding;
        switch (s.kind) {
            case RowKind::Switch:
                g.control = D2D1::RectF(right - mt::switchWidth, g.cy - mt::switchHeight / 2, right, g.cy + mt::switchHeight / 2);
                break;
            case RowKind::Choice: {
                const std::size_t idx = std::size_t(std::clamp(std::lround(valueOf(int(i))), 0L, long(s.choices.size()) - 1));
                const float cw = s.choices.empty() ? 80 : ui::popupWidth(p, s.choices[idx]);
                g.control = D2D1::RectF(right - cw, g.cy - mt::popupHeight / 2, right, g.cy + mt::popupHeight / 2);
                break;
            }
            case RowKind::Segmented: {
                const float cw = ui::segmentedWidth(p, s.choices);
                g.control = D2D1::RectF(right - cw, g.cy - mt::segmentHeight / 2, right, g.cy + mt::segmentHeight / 2);
                break;
            }
            case RowKind::Slider:
                g.trackRight = right - mt::knob / 2;
                g.trackLeft = g.trackRight - mt::sliderWidth;
                g.control = D2D1::RectF(g.trackLeft - mt::knob / 2, g.cy - mt::knob / 2, g.trackRight + mt::knob / 2, g.cy + mt::knob / 2);
                break;
            case RowKind::Value: {
                const std::wstring t = s.text ? s.text(model_) : std::wstring();
                const float tw = std::min(std::ceil(p.textWidth(t, mt::fontBody)) + 2, (right - x0) * 0.62f);
                g.control = D2D1::RectF(right - tw, g.cy - 10, right, g.cy + 10);
                break;
            }
            case RowKind::Buttons: {
                float x = right;   // boutons alignés à droite, dans l'ordre
                g.buttons.assign(s.buttons.size(), D2D1_RECT_F{});
                for (int b = int(s.buttons.size()) - 1; b >= 0; --b) {
                    const float bw = ui::buttonWidth(p, s.buttons[std::size_t(b)].label);
                    g.buttons[std::size_t(b)] = D2D1::RectF(x - bw, g.cy - mt::buttonHeight / 2, x, g.cy + mt::buttonHeight / 2);
                    x -= bw + mt::buttonGap;
                }
                const std::wstring t = s.text ? s.text(model_) : std::wstring();   // état en gris, avant les boutons
                const float tw = t.empty() ? 0.f : std::ceil(p.textWidth(t, mt::fontBody)) + 2 + (s.buttons.empty() ? 0.f : 10.f);
                const float end = s.buttons.empty() ? right : x + mt::buttonGap;
                g.control = D2D1::RectF(end - tw, g.cy - mt::buttonHeight / 2, right, g.cy + mt::buttonHeight / 2);
                break;
            }
            case RowKind::Shortcut:
                g.control = D2D1::RectF(right - mt::shortcutWidth, g.cy - mt::shortcutHeight / 2, right, g.cy + mt::shortcutHeight / 2);
                break;
            case RowKind::Info: break;
        }
    }
}

void SettingsWindow::drawRow(ui::Painter& p, int i) {
    const ui::Palette& pal = p.pal();
    const Geom& g = geoms_[std::size_t(i)];
    const RowSpec& s = spec(i);
    if (std::find(highlight_.begin(), highlight_.end(), i) != highlight_.end())   // trouvée par la recherche
        p.fillRound(D2D1::RectF(g.row.left + 4, g.row.top + 3, g.row.right - 4, g.row.bottom - 3), 8,
                    ui::Rgba{pal.accent.r, pal.accent.g, pal.accent.b, 0.13f});
    const bool on = enabled(i);
    p.opacity = on ? 1.f : 0.4f;
    const float labelRight = (s.kind == RowKind::Info ? g.row.right : g.control.left) - mt::rowPadding;
    const float left = g.row.left + mt::rowPadding;
    // Sous-titre : celui de la ligne ; pour un raccourci, la consigne d'écoute ou un conflit passent devant.
    std::wstring detail = s.detail;
    ui::Rgba detailInk = pal.secondaryText;
    if (s.kind == RowKind::Shortcut) {
        const std::wstring other = shortcutConflict(model_, s.label);
        if (recording_ == i && !recordNote_.empty()) {
            detail = recordNote_;
            detailInk = pal.danger;
        } else if (recording_ == i) {
            detail = L"Échap annule, Retour arrière efface";
        } else if (!other.empty()) {
            detail = L"Déjà utilisé par " + other;
            detailInk = pal.danger;
        }
    }
    if (s.kind == RowKind::Slider) {
        p.text(s.label, D2D1::RectF(left, g.cy - 10, g.trackLeft - 16, g.cy + 10), mt::fontBody, pal.text);
    } else if (!detail.empty()) {
        p.text(s.label, D2D1::RectF(left, g.row.top + 6, labelRight, g.row.top + 25), mt::fontBody, pal.text);
        p.text(detail, D2D1::RectF(left, g.row.top + 24, labelRight, g.row.top + 41), mt::fontDetail, detailInk);
    } else {
        p.text(s.label, D2D1::RectF(left, g.cy - 10, labelRight, g.cy + 10), mt::fontBody, pal.text);
    }
    const bool pressed = pressedRow_ == i;
    const double v = valueOf(i);
    switch (s.kind) {
        case RowKind::Switch: ui::drawSwitch(p, g.control, float(springs_[std::size_t(i)].value()), pressed && hoverRow_ == i); break;
        case RowKind::Slider: {
            const float t = s.max > s.min ? float((v - s.min) / (s.max - s.min)) : 0.f;
            ui::drawSlider(p, g.trackLeft, g.trackRight, g.cy, t, pressed && draggingSlider_);
            p.text(s.minLabel, D2D1::RectF(g.trackLeft - 6, g.cy + 10, g.trackLeft + 100, g.cy + 26), mt::fontDetail, pal.secondaryText);
            p.text(s.maxLabel, D2D1::RectF(g.trackRight - 100, g.cy + 10, g.trackRight + 6, g.cy + 26), mt::fontDetail, pal.secondaryText,
                   DWRITE_FONT_WEIGHT_REGULAR, DWRITE_TEXT_ALIGNMENT_TRAILING);
            break;
        }
        case RowKind::Choice: {
            const std::size_t idx = std::size_t(std::clamp(std::lround(v), 0L, long(s.choices.size()) - 1));
            ui::drawPopup(p, g.control, s.choices.empty() ? std::wstring() : s.choices[idx], pressed || (menu_ && menu_->row == i));
            break;
        }
        case RowKind::Segmented: ui::drawSegmented(p, g.control, s.choices, int(std::lround(v))); break;
        case RowKind::Value: ui::drawValue(p, g.control, s.text ? s.text(model_) : std::wstring()); break;
        case RowKind::Buttons: {
            const std::wstring t = s.text ? s.text(model_) : std::wstring();
            const float end = g.buttons.empty() ? g.control.right : g.buttons.front().left - 10;
            if (!t.empty()) ui::drawValue(p, D2D1::RectF(g.control.left, g.cy - 10, end, g.cy + 10), t);
            for (std::size_t b = 0; b < g.buttons.size(); ++b)
                ui::drawButton(p, g.buttons[b], s.buttons[b].label, false, pressed && pressedButton_ == int(b));
            break;
        }
        case RowKind::Shortcut: {
            std::wstring shown;   // symboles (« ⌃⌥Espace ») ; vide : « Aucun »
            if (s.text)
                if (const auto hk = parseHotkey(s.text(model_), true)) shown = hotkeyLabel(*hk);
            ui::drawShortcutField(p, g.control, shown, recording_ == i, !shortcutConflict(model_, s.label).empty());
            break;
        }
        case RowKind::Info: break;
    }
    p.opacity = 1;
    if (focus_ == i && focusable(i)) {
        if (s.kind == RowKind::Buttons && !g.buttons.empty())
            ui::drawFocusRing(p, g.buttons[std::size_t(std::clamp(focusButton_, 0, int(g.buttons.size()) - 1))], mt::buttonRadius);
        else
            ui::drawFocusRing(p, g.control, s.kind == RowKind::Switch ? mt::switchHeight / 2 : s.kind == RowKind::Slider ? mt::knob / 2 : 6);
    }
}

void SettingsWindow::drawContent(ui::Painter& p, float w, float h) {
    const ui::Palette& pal = p.pal();   // fond déjà peint (drawWindowBackground)
    const PaneInfo& info = paneInfo(pane_);
    if (info.ready) {
        p.rt()->PushAxisAlignedClip(D2D1::RectF(mt::sidebarWidth, mt::titleBar, w, h), D2D1_ANTIALIAS_MODE_ALIASED);
        const float x0 = mt::sidebarWidth + mt::contentMargin, x1 = w - mt::contentMargin;
        std::size_t flat = 0;
        for (std::size_t gi = 0; gi < groups_.size(); ++gi) {
            const GroupSpec& gs = groups_[gi];
            const ui::GroupBox& box = layout_.groups[gi];
            if (!gs.title.empty())
                p.text(gs.title, D2D1::RectF(x0 + 2, box.titleTop - scroll_, x1, box.titleTop - scroll_ + mt::groupTitle - 4), mt::fontGroupTitle,
                       pal.text, DWRITE_FONT_WEIGHT_SEMI_BOLD);
            p.fillRound(D2D1::RectF(x0, box.top - scroll_, x1, box.top + box.height - scroll_), mt::groupRadius, pal.group);
            for (std::size_t r = 0; r < gs.rows.size(); ++r, ++flat) {
                if (r > 0) {
                    const float y = box.rows[r].top - scroll_;
                    p.rt()->DrawLine(D2D1::Point2F(x0 + mt::rowPadding, y), D2D1::Point2F(x1 - mt::rowPadding, y), p.brush(pal.separator), 0.75f);
                }
                drawRow(p, int(flat));
            }
            if (!gs.footer.empty())
                p.paragraph(gs.footer, D2D1::RectF(x0 + 2, box.footerTop - scroll_, x1, box.footerTop - scroll_ + mt::footerHeight),
                            mt::fontDetail, pal.secondaryText);
        }
        p.rt()->PopAxisAlignedClip();
    } else {
        // Section du plan 42 : sa tuile en grand, et un mot.
        const float cx = (mt::sidebarWidth + w) / 2, cy = (mt::titleBar + h) / 2 - 40;
        drawPaneTile(p, D2D1::RectF(cx - 32, cy - 32, cx + 32, cy + 32), info.tile, info.icon);
        p.text(info.title, D2D1::RectF(mt::sidebarWidth, cy + 44, w, cy + 70), 17, pal.text, DWRITE_FONT_WEIGHT_SEMI_BOLD,
               DWRITE_TEXT_ALIGNMENT_CENTER);
        p.text(L"Cette section arrive bientôt.", D2D1::RectF(mt::sidebarWidth, cy + 72, w, cy + 92), mt::fontBody, pal.secondaryText,
               DWRITE_FONT_WEIGHT_REGULAR, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    // Zone de titre : le nom de la section ; un trait dessous quand le contenu défile sous elle.
    p.text(info.title, D2D1::RectF(mt::sidebarWidth + mt::contentMargin, 0, w - mt::contentMargin, mt::titleBar - 6), 15, pal.text,
           DWRITE_FONT_WEIGHT_SEMI_BOLD);
    if (!problem_.empty())
        p.text(problem_, D2D1::RectF(mt::sidebarWidth + 120, 0, w - mt::contentMargin, mt::titleBar - 6), mt::fontDetail,
               ui::rgb(dark_ ? 0xFF4245 : 0xFF383C), DWRITE_FONT_WEIGHT_REGULAR, DWRITE_TEXT_ALIGNMENT_TRAILING);
    if (scroll_ > 0.5f)
        p.rt()->DrawLine(D2D1::Point2F(mt::sidebarWidth, mt::titleBar), D2D1::Point2F(w, mt::titleBar), p.brush(pal.separator), 1);
    // Barre de défilement superposée, fine, qui s'efface.
    const float view = h - mt::titleBar;
    if (scrollbarAlpha_ > 0.01 && layout_.height - mt::titleBar > view + 1) {
        const float content = layout_.height - mt::titleBar, maxScroll = content - view;
        const float trackTop = mt::titleBar + 4, trackH = view - 8;
        const float thumbH = std::max(30.f, trackH * view / content);
        const float y = trackTop + (trackH - thumbH) * (maxScroll > 0 ? scroll_ / maxScroll : 0);
        p.fillRound(D2D1::RectF(w - 9, y, w - 3, y + thumbH), 3,
                    ui::Rgba{pal.text.r, pal.text.g, pal.text.b, float(0.4 * scrollbarAlpha_)});
    }
}

// ---- Modèle et lignes ----

void SettingsWindow::buildEnv() {
    env_ = {};
    // Écrans : leur nom (« DELL U2720Q », « Écran intégré »), sinon « Écran N », puis la définition (plan 46).
    struct Ctx {
        PaneEnv* env;
        std::map<std::wstring, std::wstring> names;
        std::vector<ScreenChoice> screens;
    } ctx{&env_, monitorNames(), {}};
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR mon, HDC, LPRECT, LPARAM lp) -> BOOL {
            auto* c = reinterpret_cast<Ctx*>(lp);
            MONITORINFOEXW mi{};
            mi.cbSize = sizeof mi;
            if (!GetMonitorInfoW(mon, &mi)) return TRUE;
            ScreenChoice s;
            if (const auto it = c->names.find(mi.szDevice); it != c->names.end()) s.name = it->second;
            DEVMODEW dm{};
            dm.dmSize = sizeof dm;
            if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm)) {
                s.width = int(dm.dmPelsWidth);
                s.height = int(dm.dmPelsHeight);
            }
            s.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
            c->screens.push_back(std::move(s));
            c->env->screenIds.push_back(mi.szDevice);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&ctx));
    env_.screens = screenLabels(ctx.screens);
    // Polices proposées : celles d'une courte liste qui sont installées.
    ComPtr<IDWriteFontCollection> fonts;
    if (dwrite_) dwrite_->GetSystemFontCollection(&fonts, FALSE);
    for (const wchar_t* f : {L"SF Pro", L"SF Pro Text", L"SF Pro Display", L"SF Pro Rounded", L"Inter", L"Segoe UI Variable",
                             L"Segoe UI", L"Helvetica Neue", L"Helvetica", L"Avenir Next", L"Arial"}) {
        UINT32 index = 0;
        BOOL exists = FALSE;
        if (fonts && SUCCEEDED(fonts->FindFamilyName(f, &index, &exists)) && exists) env_.fonts.push_back(f);
    }
    env_.windhawk = windhawkInstalled();
    for (const ModInfo& mod : macdockMods())
        env_.mods.push_back({mod, installedMod(mod.id), modSourceVersion(modSourcePath(exeDir_, mod.id))});
    env_.running = FindWindowW(L"MacDockWindow", nullptr) != nullptr;
    env_.version = kMacDockVersion;
    env_.dataDir = dir_;
    // Mise à jour de logiciels : update.json du dossier des réglages (écrit par le lanceur).
    UpdateState update;
    {
        std::ifstream f(dir_ + L"\\update.json", std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        update = parseUpdateState(ss.str());
    }
    std::wstring checkedAt;
    if (update.lastCheck > 0) {   // heure locale, à la française : « le 09/10/2026 à 10:28 »
        ULARGE_INTEGER t{};
        t.QuadPart = ULONGLONG(update.lastCheck) * 10000000ULL + 116444736000000000ULL;
        FILETIME ft{t.LowPart, t.HighPart}, local{};
        SYSTEMTIME st{};
        if (FileTimeToLocalFileTime(&ft, &local) && FileTimeToSystemTime(&local, &st)) {
            wchar_t text[64] = {};
            swprintf_s(text, L"le %02u/%02u/%04u à %02u:%02u", st.wDay, st.wMonth, st.wYear, st.wHour, st.wMinute);
            checkedAt = text;
        }
    }
    const UpdateStatus status = updateStatus(update, checkedAt);
    env_.updateTitle = status.title;
    env_.updateDetail = status.detail;
    env_.updateReady = !update.readyVersion.empty();
}

void SettingsWindow::selectPane(PaneId pane) {
    endPress();
    pane_ = pane;
    scroll_ = 0;
    focus_ = -1;
    hoverRow_ = -1;
    menu_.reset();
    stopRecording();
    pressedButton_ = -1;
    focusButton_ = 0;
    rebuildRows();
    refreshSidebar();   // lignes à souligner dans la nouvelle section
    geoms_.clear();     // refaites à la prochaine image : aucun indice de l'ancienne section
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::rebuildRows() {
    groups_ = paneGroups(pane_, env_);
    rows_.clear();
    std::vector<ui::GroupShape> shapes;
    for (std::size_t g = 0; g < groups_.size(); ++g) {
        ui::GroupShape s;
        s.title = !groups_[g].title.empty();
        s.footer = !groups_[g].footer.empty();
        for (std::size_t r = 0; r < groups_[g].rows.size(); ++r) {
            s.rows.push_back(rowHeight(groups_[g].rows[r]));
            rows_.push_back({int(g), int(r)});
        }
        shapes.push_back(std::move(s));
    }
    layout_ = ui::layoutPane(shapes);
    springs_.assign(rows_.size(), Spring());
    const SpringParams sp = springFromResponse(0.28, 0.86);
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        springs_[i].setParams(sp.stiffness, sp.damping);
        springs_[i].snap(spec(int(i)).kind == RowKind::Switch ? valueOf(int(i)) : 0);
    }
}

void SettingsWindow::updateProblem() {
    if (files_.dockInvalid || files_.barInvalid)
        problem_ = files_.dockInvalid ? L"settings.json est invalide : il ne sera pas modifié"
                                      : L"menubar.json est invalide : il ne sera pas modifié";
    else
        problem_.clear();
}

void SettingsWindow::reportWrite(bool ok) {
    if (ok) {
        updateProblem();
        return;
    }
    model_ = loadModel(dir_, &files_, &io_);   // l'écran montre ce qui est vraiment enregistré
    updateProblem();
    if (problem_.empty()) problem_ = L"Écriture impossible dans le dossier des réglages";
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if (spec(int(i)).kind == RowKind::Switch) springs_[i].setTarget(valueOf(int(i)));
    animate();
    log::warn(L"Réglages : %s", problem_.c_str());
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::endPress() {
    if (hwnd_) KillTimer(hwnd_, kCommitTimer);
    const int row = pressedRow_;
    const bool dragging = draggingSlider_;
    pressedRow_ = -1;
    draggingSlider_ = false;
    lightsPressed_ = -1;
    pending_.reset();
    if (dragging && row >= 0 && row < int(rows_.size())) {   // dernière valeur du curseur, écrite une fois
        const RowSpec& s = spec(row);
        const double v = valueOf(row);
        reportWrite(commit(dir_, [&](SettingsModel& m) { s.set(m, v); }, &model_, &io_));
    }
    pressedButton_ = -1;
    if (hwnd_ && GetCapture() == hwnd_) ReleaseCapture();
    if (reloadPending_) {
        reloadPending_ = false;
        reloadModel();
    }
}

void SettingsWindow::reloadModel() {
    model_ = loadModel(dir_, &files_, &io_);
    updateProblem();
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if (spec(int(i)).kind == RowKind::Switch) springs_[i].setTarget(valueOf(int(i)));
    animate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

const RowSpec& SettingsWindow::spec(int index) const {
    const RowRef& r = rows_[std::size_t(index)];
    return groups_[std::size_t(r.group)].rows[std::size_t(r.row)];
}

double SettingsWindow::valueOf(int index) const {
    const RowSpec& s = spec(index);
    return s.get ? s.get(model_) : 0;
}

bool SettingsWindow::enabled(int index) const {
    const RowSpec& s = spec(index);
    return !s.enabled || s.enabled(model_);
}

bool SettingsWindow::focusable(int index) const {
    const RowSpec& s = spec(index);
    if (s.kind == RowKind::Info || s.kind == RowKind::Value || (s.kind == RowKind::Buttons && s.buttons.empty())) return false;
    return enabled(index);
}

void SettingsWindow::setValue(int index, double value, bool commitNow) {
    const RowSpec& s = spec(index);
    if (!s.set) return;
    s.set(model_, value);   // affichage tout de suite
    if (s.kind == RowKind::Switch) {
        springs_[std::size_t(index)].setTarget(valueOf(index));
        animate();
    }
    if (commitNow) {
        pending_.reset();
        reportWrite(commit(dir_, [&](SettingsModel& m) { s.set(m, value); }, &model_, &io_));
        lastCommit_ = GetTickCount64();
    } else {
        pending_ = value;
        if (GetTickCount64() - lastCommit_ >= kCommitEveryMs) commitPending();
        else SetTimer(hwnd_, kCommitTimer, UINT(kCommitEveryMs), nullptr);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::commitPending() {
    KillTimer(hwnd_, kCommitTimer);
    if (!pending_ || pressedRow_ < 0 || pressedRow_ >= int(rows_.size())) {
        pending_.reset();
        return;
    }
    const double v = *pending_;
    const RowSpec& s = spec(pressedRow_);
    pending_.reset();
    reportWrite(commit(dir_, [&](SettingsModel& m) { s.set(m, v); }, &model_, &io_));
    lastCommit_ = GetTickCount64();
}

void SettingsWindow::openMenu(int index) {
    const RowSpec& s = spec(index);
    if (s.choices.empty()) return;
    const ui::Palette pal = ui::palette(dark_);
    ui::Painter p(dc_.Get(), dwrite_.Get(), pal, font_, &formats_);
    const Geom& g = geoms_[std::size_t(index)];
    const int checked = int(std::clamp(std::lround(valueOf(index)), 0L, long(s.choices.size()) - 1));
    const float mw = std::max(ui::menuWidth(p, s.choices), g.control.right - g.control.left + 26);
    const float mh = 2 * mt::menuPadding + float(s.choices.size()) * mt::menuItem;
    // Comme sur Mac : l'élément coché s'ouvre à la place du bouton.
    float top = g.cy - mt::menuPadding - float(checked) * mt::menuItem - mt::menuItem / 2;
    top = std::clamp(top, mt::titleBar, std::max(mt::titleBar, heightPt() - mh - 6));
    float left = g.control.left - 26;
    left = std::min(left, widthPt() - mw - 6);
    menu_ = OpenMenu{index, D2D1::RectF(left, top, left + mw, top + mh), checked};
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::chooseMenu(int item) {
    if (!menu_) return;
    const int row = menu_->row;
    menu_.reset();
    if (item >= 0) setValue(row, double(item));
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::animate() {
    if (!animating_ && hwnd_) {
        animating_ = true;
        lastFrame_ = GetTickCount64();
        SetTimer(hwnd_, kAnimTimer, 15, nullptr);
    }
}

void SettingsWindow::scrollBy(float points) {
    const float maxScroll = std::max(0.f, layout_.height - heightPt());
    const float before = scroll_;
    scroll_ = std::clamp(scroll_ + points, 0.f, maxScroll);
    if (points != 0 && maxScroll > 0) {
        scrollbarAlpha_ = 1;
        scrollbarShownAt_ = GetTickCount64();
        animate();
    }
    if (scroll_ != before && menu_) menu_.reset();
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::ensureVisible(int index) {
    if (index < 0 || index >= int(rows_.size())) return;
    const RowRef& r = rows_[std::size_t(index)];
    const ui::RowBox& box = layout_.groups[std::size_t(r.group)].rows[std::size_t(r.row)];
    const float top = box.top - scroll_, bottom = top + box.height;
    if (top < mt::titleBar + 8) scrollBy(top - mt::titleBar - 8);
    else if (bottom > heightPt() - 8) scrollBy(bottom - heightPt() + 8);
}

void SettingsWindow::updateDark() {
    dark_ = appsDark();
    const BOOL dark = dark_;
    DwmSetWindowAttribute(hwnd_, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof dark);
}

// ---- Souris et clavier ----

POINT SettingsWindow::toPoints(LPARAM lp) const {
    return POINT{LONG(float(GET_X_LPARAM(lp)) / scale_), LONG(float(GET_Y_LPARAM(lp)) / scale_)};
}

int SettingsWindow::controlAt(float x, float y) const {
    if (!paneInfo(pane_).ready || y < mt::titleBar) return -1;
    for (std::size_t i = 0; i < geoms_.size() && i < rows_.size(); ++i) {
        const Geom& g = geoms_[i];
        const RowKind kind = spec(int(i)).kind;
        if (kind == RowKind::Info || kind == RowKind::Value) continue;
        D2D1_RECT_F hit = g.control;
        if (kind == RowKind::Buttons) {   // les boutons seulement, pas le texte d'état
            if (g.buttons.empty()) continue;
            hit = D2D1::RectF(g.buttons.front().left, g.buttons.front().top, g.buttons.back().right, g.buttons.back().bottom);
        }
        hit.left -= 4;
        hit.right += 4;
        hit.top = std::min(hit.top, g.cy - 14);
        hit.bottom = std::max(hit.bottom, g.cy + 14);
        if (inside(hit, x, y)) return int(i);
    }
    return -1;
}

int SettingsWindow::sidebarAt(float x, float y) const {
    if (x < mt::sidebarInset || x > mt::sidebarWidth - mt::sidebarInset) return -1;
    if (y > ui::sidebarVisibleBottom(heightPt())) return -1;   // sous la limite visible du panneau : rien n'est cliquable
    return ui::sidebarRowAt(y, sidebarTops_);
}

int SettingsWindow::lightAt(float x, float y) const {
    for (int i = 0; i < 3; ++i) {
        const float dx = x - (kLightsX + i * ui::kLightSpacing), dy = y - kLightsY;
        if (dx * dx + dy * dy <= (ui::kLightRadius + 2) * (ui::kLightRadius + 2)) return i;
    }
    return -1;
}

void SettingsWindow::onMouseDown(float x, float y) {
    if (diagnosticCapture())
        log::info(L"[diag] réglages : clic %.0f,%.0f pt (échelle %.2f) → barre %d, contrôle %d", x, y, scale_, sidebarAt(x, y), controlAt(x, y));
    SetCapture(hwnd_);
    if (sheet_) {   // feuille ouverte : seuls ses boutons répondent
        sheet_->pressed = ui::sheetButtonAt(sheet_->layout, x, y);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (recording_ >= 0) stopRecording();   // un clic arrête l'écoute (sur un champ de raccourci, elle reprend plus bas)
    if (menu_) {
        menuPressAt_.reset();
        if (inside(menu_->rect, x, y)) {
            menu_->hover = ui::menuItemAt(y, menu_->rect.top + mt::menuPadding, mt::menuItem, int(spec(menu_->row).choices.size()));
            pressedRow_ = -2;   // relâché sur un élément : choisi
        } else {
            menu_.reset();      // clic ailleurs : fermé, sans autre effet
            pressedRow_ = -3;
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (const int l = lightAt(x, y); l >= 0 && l < 2) {
        lightsPressed_ = l;
        lightsPressedInside_ = true;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    const bool wasSearching = searchFocused_;
    searchFocused_ = inside(kSearch, x, y);
    if (searchFocused_) {
        if (!query_.empty() && inside(searchClear(), x, y)) setQuery(L"");
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (wasSearching) InvalidateRect(hwnd_, nullptr, FALSE);
    if (const int s = sidebarAt(x, y); s >= 0 && s < int(visible_.size())) {
        selectPane(paneList()[std::size_t(visible_[std::size_t(s)])].id);
        return;
    }
    const int i = controlAt(x, y);
    if (i < 0 || !enabled(i)) {
        focus_ = -1;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    focus_ = -1;
    pressedRow_ = i;
    hoverRow_ = i;
    const RowSpec& s = spec(i);
    const Geom& g = geoms_[std::size_t(i)];
    switch (s.kind) {
        case RowKind::Slider:
            draggingSlider_ = true;
            setValue(i, ui::sliderValueAt(x, s.min, s.max, s.step, g.trackLeft, g.trackRight), false);
            break;
        case RowKind::Segmented: {
            const int seg = ui::segmentAt(x, g.control.left, g.control.right, int(s.choices.size()));
            if (seg >= 0) setValue(i, double(seg));
            break;
        }
        case RowKind::Choice:
            openMenu(i);
            menuPressAt_ = D2D1::Point2F(x, y);   // le doigt peut glisser jusqu'à un élément et y relâcher
            menuPressTime_ = GetMessageTime();    // l'instant de l'appui, pas celui où il est traité
            menuDragMax_ = 0;
            break;
        case RowKind::Buttons:
            pressedButton_ = buttonAt(i, x, y);
            if (pressedButton_ < 0) pressedRow_ = -1;
            break;
        case RowKind::Shortcut:
            pressedRow_ = -1;
            startRecording(i);
            break;
        default: break;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::onMouseMove(float x, float y, bool buttonDown) {
    if (!tracking_) {
        TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, hwnd_, 0};
        tracking_ = TrackMouseEvent(&t) != FALSE;
    }
    if (draggingSlider_ && (!buttonDown || pressedRow_ < 0 || pressedRow_ >= int(geoms_.size()))) {
        endPress();   // bouton relâché sans que l'on ait vu WM_LBUTTONUP : le glisser s'arrête là
        return;
    }
    if (draggingSlider_) {
        const RowSpec& s = spec(pressedRow_);
        const Geom& g = geoms_[std::size_t(pressedRow_)];
        const double v = ui::sliderValueAt(x, s.min, s.max, s.step, g.trackLeft, g.trackRight);
        if (v != valueOf(pressedRow_)) setValue(pressedRow_, v, false);
        return;
    }
    bool dirty = false;
    if (menuPressAt_ && buttonDown)   // appui qui a ouvert le menu : le plus grand écart compte (aller puis retour)
        menuDragMax_ = std::max(menuDragMax_, std::hypot(x - menuPressAt_->x, y - menuPressAt_->y));
    if (menu_) {
        const int hover = inside(menu_->rect, x, y)
                              ? ui::menuItemAt(y, menu_->rect.top + mt::menuPadding, mt::menuItem, int(spec(menu_->row).choices.size()))
                              : -1;
        if (hover != menu_->hover) {
            menu_->hover = hover;
            dirty = true;
        }
    }
    if (lightsPressed_ >= 0) {   // pastille enfoncée : sombre seulement tant que le doigt reste dessus
        const bool insideLight = lightAt(x, y) == lightsPressed_;
        if (insideLight != lightsPressedInside_) {
            lightsPressedInside_ = insideLight;
            dirty = true;
        }
    }
    const bool lights = x < kLightsX + 2 * ui::kLightSpacing + 12 && y < kLightsY + 12;
    const int side = sidebarAt(x, y), row = controlAt(x, y);
    if (lights != lightsHover_ || side != sidebarHover_ || row != hoverRow_) dirty = true;
    lightsHover_ = lights;
    sidebarHover_ = side;
    hoverRow_ = row;
    if (dirty) InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::onMouseUp(float x, float y) {
    // L'état est lu avant ReleaseCapture : WM_CAPTURECHANGED (endPress) le remettrait à zéro.
    const int pressed = pressedRow_;
    const int light = lightsPressed_;
    const int button = pressedButton_;
    if (sheet_) {
        const int b = sheet_->pressed;
        sheet_->pressed = -1;
        ReleaseCapture();
        if (b >= 0 && ui::sheetButtonAt(sheet_->layout, x, y) == b) closeSheet(b);
        else InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (draggingSlider_) {   // curseur : la dernière valeur, écrite une fois
        endPress();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    pressedRow_ = -1;
    lightsPressed_ = -1;
    ReleaseCapture();
    if (pressed == -2 && menu_) {   // menu : l'élément sous le doigt
        const int item = inside(menu_->rect, x, y)
                             ? ui::menuItemAt(y, menu_->rect.top + mt::menuPadding, mt::menuItem, int(spec(menu_->row).choices.size()))
                             : -1;
        if (item >= 0) chooseMenu(item);
        return;
    }
    if (menu_ && menuPressAt_ && pressed == menu_->row) {   // l'appui qui l'a ouvert : appuyer, glisser, relâcher
        const int item = inside(menu_->rect, x, y)
                             ? ui::menuItemAt(y, menu_->rect.top + mt::menuPadding, mt::menuItem, int(spec(menu_->row).choices.size()))
                             : -1;
        const float moved = std::max(menuDragMax_, std::hypot(x - menuPressAt_->x, y - menuPressAt_->y));
        const double held = double(DWORD(GetMessageTime()) - DWORD(menuPressTime_)) / 1000.0;
        if (diagnosticCapture())
            log::info(L"[diag] réglages : relâché %.0f,%.0f pt sur le menu (élément %d, %.0f pt, %.2f s)", x, y, item, moved, held);
        menuPressAt_.reset();
        const ui::MenuRelease r = ui::menuRelease(item, moved, held);
        if (r.kind == ui::MenuRelease::Choose) {
            chooseMenu(r.item);
        } else if (r.kind == ui::MenuRelease::Close) {
            menu_.reset();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return;
    }
    if (light >= 0) {
        const int l = light;
        if (lightAt(x, y) == l) {
            if (l == 0) PostMessageW(hwnd_, WM_CLOSE, 0, 0);
            else ShowWindow(hwnd_, SW_MINIMIZE);
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (pressed >= 0 && pressed < int(rows_.size()) && spec(pressed).kind == RowKind::Switch && controlAt(x, y) == pressed)
        setValue(pressed, valueOf(pressed) >= 0.5 ? 0 : 1);
    pressedButton_ = -1;
    if (pressed >= 0 && pressed < int(rows_.size()) && spec(pressed).kind == RowKind::Buttons && button >= 0 &&
        buttonAt(pressed, x, y) == button)
        runAction(spec(pressed).buttons[std::size_t(button)]);   // copié : l'action peut refaire les lignes
    InvalidateRect(hwnd_, nullptr, FALSE);
}

bool SettingsWindow::onKey(WPARAM key, bool repeat) {
    const bool shift = GetKeyState(VK_SHIFT) < 0, ctrl = GetKeyState(VK_CONTROL) < 0;
    // Entrée ou Espace tenue : une seule action (relecture du plan 42 : la répétition validait la feuille ouverte par
    // le premier appui) ; les flèches gardent leur répétition.
    if (repeat && (key == VK_RETURN || key == VK_SPACE)) return true;
    if (recording_ >= 0 && !repeat) {   // sans crochet (essai, ou crochet refusé) : la touche arrive ici, Ctrl+W compris
        UINT mods = 0;
        if (GetKeyState(VK_CONTROL) < 0) mods |= MOD_CONTROL;
        if (GetKeyState(VK_MENU) < 0) mods |= MOD_ALT;
        if (GetKeyState(VK_SHIFT) < 0) mods |= MOD_SHIFT;
        if (GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0) mods |= MOD_WIN;
        onRecordKey(UINT(key), mods);
        return true;
    }
    if (ctrl && key == 'W') {
        PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        return true;
    }
    if (sheet_) {   // feuille modale : Échap, Entrée ; le reste est ignoré
        if (key == VK_ESCAPE && sheet_->cancel >= 0) closeSheet(sheet_->cancel);
        else if (key == VK_RETURN || key == VK_SPACE) closeSheet(sheet_->spec.primary);
        return true;
    }
    if (ctrl && key == 'F') {
        menu_.reset();   // la frappe va à la recherche : plus de menu ouvert qui garderait les flèches
        searchFocused_ = true;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (searchFocused_) {
        switch (key) {
            case VK_ESCAPE:
                if (!query_.empty()) setQuery(L"");
                else searchFocused_ = false;
                InvalidateRect(hwnd_, nullptr, FALSE);
                return true;
            case VK_RETURN:   // la première section trouvée ; la recherche reste affichée
                if (!visible_.empty()) {
                    searchFocused_ = false;
                    selectPane(paneList()[std::size_t(visible_.front())].id);
                }
                return true;
            case VK_TAB: searchFocused_ = false; break;
            case VK_UP:
            case VK_DOWN: break;   // parcourent les sections trouvées, comme sans recherche
            default: return false;   // les caractères arrivent par WM_CHAR
        }
    }
    if (menu_) {
        const int n = int(spec(menu_->row).choices.size());
        switch (key) {
            case VK_ESCAPE: menu_.reset(); break;
            case VK_UP: menu_->hover = std::max(0, menu_->hover - 1); break;
            case VK_DOWN: menu_->hover = std::min(n - 1, menu_->hover + 1); break;
            case VK_RETURN:
            case VK_SPACE: chooseMenu(menu_->hover); return true;
            default: return false;
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (key == VK_TAB) {
        std::vector<bool> f(rows_.size());
        for (std::size_t i = 0; i < rows_.size(); ++i) f[i] = focusable(int(i));
        focus_ = ui::nextFocus(focus_, f, shift);
        focusButton_ = 0;
        ensureVisible(focus_);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (key == VK_ESCAPE) {
        focus_ = -1;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (focus_ < 0 || searchFocused_) {   // sans contrôle choisi, les flèches parcourent les sections visibles
        const auto& panes = paneList();
        int at = -1;
        for (std::size_t k = 0; k < visible_.size(); ++k)
            if (panes[std::size_t(visible_[k])].id == pane_) at = int(k);
        const int n = int(visible_.size());
        if (key == VK_UP && n && at != 0) selectPane(panes[std::size_t(visible_[std::size_t(at < 0 ? 0 : at - 1)])].id);
        else if (key == VK_DOWN && n && at + 1 < n) selectPane(panes[std::size_t(visible_[std::size_t(at + 1)])].id);
        else return false;
        return true;
    }
    const RowSpec& s = spec(focus_);
    const double v = valueOf(focus_);
    switch (s.kind) {
        case RowKind::Switch:
            if (key == VK_SPACE || key == VK_RETURN) setValue(focus_, v >= 0.5 ? 0 : 1);
            else return false;
            return true;
        case RowKind::Choice:
            if (key == VK_SPACE || key == VK_RETURN || key == VK_DOWN || key == VK_UP) openMenu(focus_);
            else return false;
            return true;
        case RowKind::Segmented: {
            const int n = int(s.choices.size()), cur = int(std::lround(v));
            if (key == VK_LEFT && cur > 0) setValue(focus_, cur - 1);
            else if (key == VK_RIGHT && cur + 1 < n) setValue(focus_, cur + 1);
            else return false;
            return true;
        }
        case RowKind::Slider: {
            const double step = std::max(s.step, (s.max - s.min) / (shift ? 10 : 56));
            if (key == VK_LEFT || key == VK_DOWN) setValue(focus_, std::max(s.min, v - step));
            else if (key == VK_RIGHT || key == VK_UP) setValue(focus_, std::min(s.max, v + step));
            else return false;
            return true;
        }
        case RowKind::Buttons: {
            const int n = int(s.buttons.size());
            if (key == VK_LEFT && focusButton_ > 0) --focusButton_;
            else if (key == VK_RIGHT && focusButton_ + 1 < n) ++focusButton_;
            else if ((key == VK_SPACE || key == VK_RETURN) && n) runAction(s.buttons[std::size_t(std::clamp(focusButton_, 0, n - 1))]);
            else return false;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        case RowKind::Shortcut:
            if (key == VK_SPACE || key == VK_RETURN) startRecording(focus_);
            else return false;
            return true;
        case RowKind::Value:
        case RowKind::Info: return false;
    }
    return false;
}

// ---- Messages ----

LRESULT CALLBACK SettingsWindow::proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* self = static_cast<SettingsWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    }
    if (auto* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) return self->handle(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT SettingsWindow::handle(UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == kMsgRecordKey) {
        onRecordKey(UINT(wp), UINT(lp));
        return 0;
    }
    if (msg == kMsgUnhook) {
        if (recording_ < 0) releaseRecordHook(false);
        return 0;
    }
    if (msg == kMsgActionDone) {
        busy_ = false;
        if (!lp) showMessage(L"L'action n'a pas abouti", L"Windows ne l'a pas lancée, ou elle a échoué (le journal en dit plus).");
        if (wp) {   // MacDock lancé, arrêté, mod installé… : état relu, et encore un peu plus tard (démarrage du Dock)
            buildEnv();
            rebuildRows();
            refreshSidebar();
            geoms_.clear();
            SetTimer(hwnd_, kEnvTimer, 1500, nullptr);
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    }
    if (msg == paneMessage()) {   // seconde ouverture : la section demandée (sinon celle affichée), au premier plan
        if (wp < paneList().size()) selectPane(paneList()[wp].id);
        if (IsIconic(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);
        SetForegroundWindow(hwnd_);
        return 1;
    }
    switch (msg) {
        case WM_NCCALCSIZE:
            if (wp) {   // pas de cadre Windows : toute la fenêtre est à nous (DWM garde l'ombre et les coins)
                if (IsZoomed(hwnd_)) {
                    auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
                    const UINT dpi = GetDpiForWindow(hwnd_);
                    const int f = GetSystemMetricsForDpi(SM_CXFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
                    InflateRect(&p->rgrc[0], -f, -f);
                }
                return 0;
            }
            break;
        case WM_NCHITTEST: {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd_, &pt);
            const float x = float(pt.x) / scale_, y = float(pt.y) / scale_;
            const LONG edge = LONG(std::lround(6 * scale_));
            if (!IsZoomed(hwnd_)) {
                if (pt.y < edge) return HTTOP;
                if (pt.y >= LONG(pxH_) - edge) return HTBOTTOM;
            }
            // Le rectangle de survol des pastilles reste à nous : entre deux cercles, une zone de titre ferait quitter la
            // fenêtre au pointeur (WM_MOUSELEAVE) et clignoter leurs symboles.
            const bool overLights = x < kLightsX + 2 * ui::kLightSpacing + 12 && y < kLightsY + 12;
            if (y < mt::titleBar && !overLights && !inside(kSearch, x, y)) return HTCAPTION;
            return HTCLIENT;
        }
        case WM_GETMINMAXINFO: {
            auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
            mm->ptMinTrackSize.x = mm->ptMaxTrackSize.x = LONG(std::lround(mt::windowWidth * scale_));
            mm->ptMinTrackSize.y = LONG(std::lround(mt::minHeight * scale_));
            return 0;
        }
        case WM_DPICHANGED: {
            scale_ = float(HIWORD(wp)) / 96.f;
            const RECT* r = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_SIZE:
            resize();
            render();
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd_, &ps);
            EndPaint(hwnd_, &ps);
            render();
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_ACTIVATE:
            active_ = LOWORD(wp) != WA_INACTIVE;
            if (!active_) {
                endPress();
                menu_.reset();
                stopRecording();   // le clavier n'est plus à nous
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        case WM_SETTINGCHANGE:
            if (lp && !wcscmp(reinterpret_cast<const wchar_t*>(lp), L"ImmersiveColorSet")) {
                updateDark();
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            break;
        case WM_DISPLAYCHANGE:
            endPress();
            menu_.reset();
            buildEnv();
            rebuildRows();
            refreshSidebar();   // les noms d'écrans sont cherchés
            geoms_.clear();
            InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK: {
            const POINT p = toPoints(lp);
            onMouseDown(float(p.x), float(p.y));
            return 0;
        }
        case WM_MOUSEMOVE: {
            const POINT p = toPoints(lp);
            onMouseMove(float(p.x), float(p.y), (wp & MK_LBUTTON) != 0);
            return 0;
        }
        case WM_CAPTURECHANGED:   // Win, Alt+Tab, une invite UAC… : l'appui ou le glisser s'arrête
            if (reinterpret_cast<HWND>(lp) != hwnd_) endPress();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case WM_CANCELMODE:
            endPress();
            menu_.reset();
            break;
        case WM_LBUTTONUP: {
            const POINT p = toPoints(lp);
            onMouseUp(float(p.x), float(p.y));
            return 0;
        }
        case WM_MOUSELEAVE:
            tracking_ = false;
            if (lightsHover_ || sidebarHover_ >= 0 || hoverRow_ >= 0) {
                lightsHover_ = false;
                sidebarHover_ = hoverRow_ = -1;
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return 0;
        case WM_MOUSEWHEEL:
            scrollBy(-float(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA * 48);
            return 0;
        case WM_KEYDOWN:
            if (onKey(wp, (lp & (1 << 30)) != 0)) return 0;   // bit 30 : touche déjà enfoncée (répétition)
            break;
        case WM_SYSKEYDOWN:   // Alt+… : seulement pour l'écoute d'un raccourci (sans crochet, essais)
            if (recording_ >= 0 && onKey(wp, (lp & (1 << 30)) != 0)) return 0;
            break;
        case WM_WTSSESSION_CHANGE:
            if (wp == WTS_SESSION_LOCK) {   // les relâches se font sur le bureau sécurisé, invisibles au crochet
                stopRecording();
                releaseRecordHook(true);
            }
            return 0;
        case WM_CHAR:
            if (searchFocused_ && !sheet_) {
                const wchar_t c = wchar_t(wp);
                if (c == 8) {
                    if (!query_.empty()) setQuery(query_.substr(0, query_.size() - 1));
                } else if (c >= 32 && c != 127) {
                    setQuery(query_ + c);
                }
                return 0;
            }
            break;
        case WM_TIMER:
            if (wp == kAnimTimer) {
                const ULONGLONG now = GetTickCount64();
                const double dt = std::min(0.05, double(now - lastFrame_) / 1000.0);
                lastFrame_ = now;
                bool moving = false;
                for (auto& s : springs_) moving = s.step(dt) || moving;
                if (sheet_) moving = sheet_->appear.step(dt) || moving;
                if (scrollbarAlpha_ > 0 && now - scrollbarShownAt_ > 800) {   // s'efface après 0,8 s
                    scrollbarAlpha_ = std::max(0.0, scrollbarAlpha_ - dt / 0.25);
                    moving = true;
                }
                if (scrollbarAlpha_ > 0) moving = true;
                if (!moving) {
                    KillTimer(hwnd_, kAnimTimer);
                    animating_ = false;
                }
                render();
            } else if (wp == kReloadTimer) {
                KillTimer(hwnd_, kReloadTimer);
                if (draggingSlider_) reloadPending_ = true;   // relus à la fin du glisser
                else reloadModel();
            } else if (wp == kCommitTimer) {
                commitPending();
            } else if (wp == kUnhookTimer) {   // relâches jamais vues : le crochet part quand même
                KillTimer(hwnd_, kUnhookTimer);
                if (recording_ < 0) releaseRecordHook(true);
            } else if (wp == kEnvTimer) {
                KillTimer(hwnd_, kEnvTimer);
                if (pressedRow_ < 0 && !draggingSlider_) {
                    buildEnv();
                    rebuildRows();
                    refreshSidebar();
                    geoms_.clear();
                    InvalidateRect(hwnd_, nullptr, FALSE);
                }
            }
            return 0;
        case WM_CLOSE: DestroyWindow(hwnd_); return 0;
        case WM_DESTROY:
            stopRecording();
            releaseRecordHook(true);
            WTSUnRegisterSessionNotification(hwnd_);
            PostQuitMessage(0);
            return 0;
        default: break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

// ---- Recherche ----

void SettingsWindow::setQuery(std::wstring query) {
    query_ = std::move(query);
    refreshSidebar();
}

void SettingsWindow::refreshSidebar() {
    matches_ = searchPanes(query_, env_);
    const SidebarView view = sidebarView(matches_);
    visible_ = view.panes;
    sidebarTops_ = ui::sidebarRowTops(view.groups);
    highlight_.clear();
    if (!query_.empty())
        for (const PaneMatch& m : matches_)
            if (m.pane == pane_) highlight_ = m.rows;
    sidebarHover_ = -1;
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

// ---- Boutons, actions et feuilles ----

int SettingsWindow::buttonAt(int row, float x, float y) const {
    if (row < 0 || row >= int(geoms_.size())) return -1;
    const auto& buttons = geoms_[std::size_t(row)].buttons;
    for (std::size_t b = 0; b < buttons.size(); ++b)
        if (inside(buttons[b], x, y)) return int(b);
    return -1;
}

void SettingsWindow::runAction(ButtonSpec button) {
    ButtonContext ctx{exeDir_, dir_, button.arg};
    wchar_t sys[MAX_PATH] = {}, win[MAX_PATH] = {};   // PowerShell et l'Explorateur par leur chemin complet
    if (GetSystemDirectoryW(sys, MAX_PATH)) ctx.systemDir = sys;
    if (GetWindowsDirectoryW(win, MAX_PATH)) ctx.windowsDir = win;
    std::wstring modTitle = button.arg;
    for (const ModInfo& m : macdockMods())
        if (m.id == button.arg) modTitle = m.title;
    switch (button.action) {
        case PaneAction::Export: exportTo(); return;
        case PaneAction::Import: importFrom(); return;
        case PaneAction::Reset:
            openSheet({L"Rétablir les réglages par défaut ?",
                       L"Les préférences du Dock et de la barre des menus reviennent à leur valeur d'origine. Les apps épinglées restent "
                       L"dans le Dock.",
                       {L"Annuler", L"Rétablir"}, 1},
                      0, [this](int choice) {
                          if (choice != 1) return;
                          if (resetSettings(dir_)) {
                              reloadModel();
                              return;
                          }
                          reloadModel();
                          showMessage(L"Réglages non rétablis", files_.dockInvalid
                                                                    ? L"settings.json est invalide : ses apps épinglées ne se lisent "
                                                                      L"pas, il n'a pas été touché."
                                                                    : L"Écriture impossible dans le dossier des réglages.");
                      });
            return;
        case PaneAction::InstallMod:
            openSheet({L"Installer « " + modTitle + L" » ?",
                       L"Windows va demander l'autorisation administrateur. Pour prendre la police, l'Explorateur redémarre : ses "
                       L"fenêtres se ferment et le bureau se redessine.",
                       {L"Installer et redémarrer l'Explorateur", L"Installer sans redémarrer", L"Annuler"}, 0},
                      2, [this, ctx](int choice) mutable {
                          if (choice != 0 && choice != 1) return;
                          ctx.restartExplorer = choice == 0;
                          runCommands(actionCommands(PaneAction::InstallMod, ctx), true);
                      });
            return;
        case PaneAction::UninstallMod:
            openSheet({L"Retirer « " + modTitle + L" » ?",
                       L"Windows va demander l'autorisation administrateur. Les apps reprennent leur police à leur prochain lancement.",
                       {L"Annuler", L"Retirer"}, 1},
                      0, [this, ctx](int choice) {
                          if (choice == 1) runCommands(actionCommands(PaneAction::UninstallMod, ctx), true);
                      });
            return;
        default: break;
    }
    const bool refresh = button.action == PaneAction::Launch || button.action == PaneAction::Quit ||
                         button.action == PaneAction::Restart || button.action == PaneAction::CheckUpdate;
    runCommands(actionCommands(button.action, ctx), refresh);
}

void SettingsWindow::runCommands(std::vector<ActionCommand> commands, bool refreshEnv) {
    if (commands.empty()) return;
    if (busy_) {   // un installateur ou un redémarrage tourne déjà
        showMessage(L"Un instant", L"Une action de MacDock est encore en cours.");
        return;
    }
    if (testMode_) {   // essai : rien ne touche au vrai système
        for (const auto& c : commands)
            log::info(L"[essai] action non lancée : %s %s (%s)%s", c.file.c_str(), c.params.c_str(), c.verb.c_str(),
                      c.restartExplorer ? L" — redémarrage de l'Explorateur" : L"");
        PostMessageW(hwnd_, kMsgActionDone, refreshEnv ? 1 : 0, 1);
        return;
    }
    busy_ = true;
    if (worker_.joinable()) worker_.join();   // le précédent a fini (busy_ était faux)
    const HWND hwnd = hwnd_;
    wchar_t win[MAX_PATH] = {};
    GetWindowsDirectoryW(win, MAX_PATH);
    worker_ = std::thread([commands = std::move(commands), refreshEnv, hwnd, windowsDir = std::wstring(win), closing = closing_] {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        LPARAM result = 1;   // 1 : réussie ; 0 : échec à signaler ; 2 : annulée par l'utilisateur (rien à dire)
        for (const auto& c : commands) {
            if (c.restartExplorer) {
                restartExplorer(windowsDir);
                continue;
            }
            SHELLEXECUTEINFOW sei{sizeof sei};
            sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
            sei.hwnd = hwnd;
            sei.lpVerb = c.verb.c_str();
            sei.lpFile = c.file.c_str();
            sei.lpParameters = c.params.empty() ? nullptr : c.params.c_str();
            sei.nShow = c.verb == L"runas" ? SW_SHOWNORMAL : SW_SHOWDEFAULT;   // l'installateur montre sa console
            if (!ShellExecuteExW(&sei)) {   // autorisation refusée, fichier introuvable : la suite n'a plus de sens
                const DWORD error = GetLastError();
                log::warn(L"Réglages : %s %s refusé (%lu)", c.file.c_str(), c.params.c_str(), error);
                result = error == ERROR_CANCELLED ? 2 : 0;
                break;
            }
            if (sei.hProcess) {
                if (c.wait) {
                    DWORD code = 0;
                    if (waitForExit(sei.hProcess, 180000, c.abandonOnClose ? closing.get() : nullptr) &&
                        GetExitCodeProcess(sei.hProcess, &code) && code != 0 &&
                        !c.ignoreExitCode) {   // l'installateur a échoué (compilation, Windhawk absent…)
                        log::warn(L"Réglages : %s a fini avec le code %lu", c.file.c_str(), code);
                        result = 0;
                    }
                }
                CloseHandle(sei.hProcess);
            }
            if (result == 0) break;
            if (c.waitStopped)   // --quit rend la main tout de suite : on attend la vraie fin (10 s au plus)
                for (int i = 0; i < 100 && macdockAlive(); ++i) Sleep(100);
        }
        CoUninitialize();
        PostMessageW(hwnd, kMsgActionDone, refreshEnv ? 1 : 0, result);
    });
}

std::optional<std::wstring> SettingsWindow::fileDialog(bool save) {
    if (testMode_) {   // essai : MACDOCK_SETTINGS_FILE remplace le dialogue
        wchar_t f[MAX_PATH] = {};
        if (GetEnvironmentVariableW(L"MACDOCK_SETTINGS_FILE", f, MAX_PATH)) return std::wstring(f);
    }
    ComPtr<IFileDialog> dialog;
    if (FAILED(CoCreateInstance(save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog))))
        return std::nullopt;
    const COMDLG_FILTERSPEC types[] = {{L"Sauvegarde de MacDock (*.json)", L"*.json"}};
    dialog->SetFileTypes(1, types);
    dialog->SetDefaultExtension(L"json");
    if (save) dialog->SetFileName(L"Réglages MacDock.json");
    dialog->SetTitle(save ? L"Exporter les réglages" : L"Importer des réglages");
    if (FAILED(dialog->Show(hwnd_))) return std::nullopt;   // annulé
    ComPtr<IShellItem> item;
    PWSTR path = nullptr;
    if (FAILED(dialog->GetResult(&item)) || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) return std::nullopt;
    std::wstring out = path;
    CoTaskMemFree(path);
    return out;
}

void SettingsWindow::exportTo() {
    const auto backup = exportSettings(dir_);
    if (!backup) {
        showMessage(L"Exportation impossible", L"Un fichier de réglages est invalide ou illisible : la sauvegarde serait vide. "
                                               L"Corrige-le ou rétablis les réglages par défaut.");
        return;
    }
    const auto path = fileDialog(true);
    if (!path) return;
    if (!saveJsonFileAtomic(*path, *backup))
        showMessage(L"Exportation impossible", L"Le fichier n'a pas pu être écrit à cet endroit.");
    else
        log::info(L"Réglages exportés : %s", path->c_str());
}

void SettingsWindow::importFrom() {
    const auto path = fileDialog(false);
    if (!path) return;
    const auto file = readSettingsBackup(*path);   // rien n'est écrit à côté du fichier choisi
    if (!file) {
        showMessage(L"Importation impossible", L"Ce fichier ne peut pas être lu.");
        return;
    }
    if (!isSettingsBackup(*file)) {
        showMessage(L"Importation impossible", L"Ce fichier n'est pas une sauvegarde des réglages de MacDock.");
        return;
    }
    // Confirmation : tout est remplacé, épingles comprises ; une épingle réseau serait contactée par le Dock.
    std::wstring message = L"Les réglages du Dock et de la barre des menus, apps épinglées comprises, seront remplacés par ceux de "
                           L"la sauvegarde.";
    if (const int net = networkPins(*file))
        message += L" Attention : " + std::to_wstring(net) + (net > 1 ? L" épingles pointent" : L" épingle pointe") +
                   L" vers un ordinateur du réseau, que le Dock contactera.";
    openSheet({L"Importer ces réglages ?", message, {L"Annuler", L"Importer"}, 1}, 0,
              [this, backup = *file, from = *path](int choice) {
                  if (choice != 1) return;
                  switch (importSettings(dir_, backup)) {
                      case ImportResult::Ok:
                          log::info(L"Réglages importés : %s", from.c_str());
                          reloadModel();
                          break;
                      case ImportResult::NotABackup:
                          showMessage(L"Importation impossible", L"Ce fichier n'est pas une sauvegarde des réglages de MacDock.");
                          break;
                      case ImportResult::Invalid:
                          showMessage(L"Importation impossible", L"La sauvegarde est abîmée : rien n'a été changé.");
                          break;
                      case ImportResult::WriteFailed:
                          showMessage(L"Importation impossible", L"Écriture impossible dans le dossier des réglages.");
                          break;
                  }
              });
}

void SettingsWindow::openSheet(ui::SheetSpec spec, int cancel, std::function<void(int)> done) {
    endPress();
    menu_.reset();
    stopRecording();
    searchFocused_ = false;
    OpenSheet sh;
    sh.spec = std::move(spec);
    sh.done = std::move(done);
    sh.cancel = cancel;
    const SpringParams sp = springFromResponse(0.32, 0.9);
    sh.appear.setParams(sp.stiffness, sp.damping);
    sh.appear.snap(0);
    sh.appear.setTarget(1);
    sheet_ = std::move(sh);
    animate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::closeSheet(int choice) {
    if (!sheet_) return;
    auto done = std::move(sheet_->done);
    sheet_.reset();
    InvalidateRect(hwnd_, nullptr, FALSE);
    if (done) done(choice);
}

void SettingsWindow::showMessage(std::wstring title, std::wstring message) {
    openSheet({std::move(title), std::move(message), {L"OK"}, 0}, 0, nullptr);
}

// ---- Enregistreur de raccourci ----

void SettingsWindow::startRecording(int row) {
    if (row < 0 || row >= int(rows_.size()) || spec(row).kind != RowKind::Shortcut) return;
    stopRecording();
    recording_ = row;
    recordNote_.clear();
    focus_ = row;
    // Modificateurs déjà tenus (Ctrl+clic, Maj+Entrée) : comptés, mais leurs relâches restent au système (g_gate).
    g_held = {};
    for (DWORD vk : {VK_LCONTROL, VK_RCONTROL, VK_LMENU, VK_RMENU, VK_LSHIFT, VK_RSHIFT, VK_LWIN, VK_RWIN})
        g_held.track(vk, GetAsyncKeyState(int(vk)) < 0);
    g_gate.listen(true);
    recordTarget_ = this;
    KillTimer(hwnd_, kUnhookTimer);
    // Crochet clavier le temps de l'écoute : les combinaisons avec ⊞ (que Windows prendrait) arrivent ici.
    if (!recordHook_) recordHook_ = SetWindowsHookExW(WH_KEYBOARD_LL, recordHook, instance_, 0);
    if (!recordHook_) log::warn(L"Réglages : crochet clavier impossible (%lu), touches lues par la fenêtre", GetLastError());
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::stopRecording() {
    g_gate.listen(false);
    if (recording_ >= 0 && hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    recording_ = -1;
    recordNote_.clear();
    releaseRecordHook(false);
}

void SettingsWindow::releaseRecordHook(bool force) {
    if (!recordHook_) return;
    if (!force && g_gate.pending()) {   // des touches gardées attendent leur relâche : le crochet les avalera encore
        if (hwnd_) SetTimer(hwnd_, kUnhookTimer, 2000, nullptr);
        return;
    }
    UnhookWindowsHookEx(recordHook_);
    recordHook_ = nullptr;
    g_gate.reset();
    if (hwnd_) KillTimer(hwnd_, kUnhookTimer);
    if (recordTarget_ == this) recordTarget_ = nullptr;
}

LRESULT CALLBACK SettingsWindow::recordHook(int code, WPARAM wp, LPARAM lp) {
    SettingsWindow* self = recordTarget_;
    if (code == HC_ACTION && self) {
        const auto* k = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lp);
        const bool down = wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN;
        g_held.track(k->vkCode, down);   // toutes, gardées ou non : l'état des modificateurs reste juste
        if (g_gate.swallow(k->vkCode, down)) {
            if (self->recording_ >= 0 && down && !modifierKey(k->vkCode))
                PostMessageW(self->hwnd_, kMsgRecordKey, k->vkCode, g_held.mods());
            if (self->recording_ < 0 && !g_gate.pending()) PostMessageW(self->hwnd_, kMsgUnhook, 0, 0);   // dernière relâche
            return 1;   // gardée : rien d'autre ne la voit
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

void SettingsWindow::onRecordKey(UINT vk, UINT mods) {
    const int row = recording_;
    if (row < 0) return;
    const HotkeyRecord r = recordHotkey(vk, mods);
    switch (r.kind) {
        case RecordKind::Wait: return;
        case RecordKind::Cancel: stopRecording(); return;
        case RecordKind::Reserved:
            recordNote_ = L"Windows ou MacDock garde ce raccourci : choisis-en un autre";
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        case RecordKind::Common:
            recordNote_ = L"Les apps s'en servent (Ctrl+C, Maj+→…) : ajoute ⌥ ou ⊞";
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        case RecordKind::Clear:
            stopRecording();
            commitShortcut(row, L"off");
            return;
        case RecordKind::Accept: {
            const std::wstring text = hotkeyText(r.spec);
            const RowSpec& s = spec(row);
            // Une autre app (ou MacDock pour une autre fonction) l'a déjà : RegisterHotKey échouerait sans rien dire.
            if (!(s.text && hotkeyConflict(s.text(model_), text))) {
                constexpr int kProbe = 0x4D44;
                if (!RegisterHotKey(nullptr, kProbe, r.spec.mods | MOD_NOREPEAT, r.spec.vk)) {
                    recordNote_ = L"Déjà pris par Windows ou une autre app : choisis-en un autre";
                    InvalidateRect(hwnd_, nullptr, FALSE);
                    return;
                }
                UnregisterHotKey(nullptr, kProbe);
            }
            stopRecording();
            commitShortcut(row, text);
            return;
        }
    }
}

void SettingsWindow::commitShortcut(int row, const std::wstring& text) {
    if (row < 0 || row >= int(rows_.size())) return;
    const RowSpec& s = spec(row);
    if (!s.setText) return;
    s.setText(model_, text);   // affiché tout de suite
    reportWrite(commit(dir_, [&](SettingsModel& m) { s.setText(m, text); }, &model_, &io_));
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// ---- Icône ----

HICON makeSettingsIcon(int size) {
    // La même icône que celle de l'exécutable (res/settings.ico), en alpha droit comme l'attend CreateIconIndirect.
    const BgraImage im = renderAppIcon(AppIconKind::Settings, size);
    if (im.w != size) return nullptr;
    const std::vector<BYTE>& px = im.px;
    BITMAPV5HEADER bi{};
    bi.bV5Size = sizeof bi;
    bi.bV5Width = size;
    bi.bV5Height = -size;
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;
    void* bits = nullptr;
    HDC dc = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    if (!color || !bits) return nullptr;
    memcpy(bits, px.data(), px.size());
    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO ii{TRUE, 0, 0, mask, color};
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}

}  // namespace md
