#include "settings_window.h"

#include <dwmapi.h>
#include <wincodec.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>

#include "../anim/motion.h"
#include "../core/diag.h"
#include "../core/log.h"
#include "../settings/pane_icons.h"

namespace md {

using Microsoft::WRL::ComPtr;
namespace mt = ui::metrics;

namespace {
constexpr UINT_PTR kAnimTimer = 1, kReloadTimer = 2, kCommitTimer = 3;
constexpr ULONGLONG kCommitEveryMs = 120;            // curseur tiré : une écriture au plus toutes les 120 ms
constexpr float kLightsX = 20, kLightsY = 20;        // premier centre des pastilles
constexpr D2D1_RECT_F kSearch{10, 46, mt::sidebarWidth - 10, 46 + mt::searchHeight};
const std::vector<int> kSidebarGroups{1, 4, 4, 2};   // Général | Dock… Bureau | Clavier… Police | Mods, À propos

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
    return r.detail.empty() ? mt::rowHeight : mt::rowHeightDetail;
}

bool inside(const D2D1_RECT_F& r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }
}  // namespace

UINT SettingsWindow::paneMessage() {
    static const UINT msg = RegisterWindowMessageW(L"MacDockSettingsPane");
    return msg;
}

SettingsWindow::~SettingsWindow() {
    if (change_ != INVALID_HANDLE_VALUE) FindCloseChangeNotification(change_);
    if (icon_) DestroyIcon(icon_);
    if (smallIcon_) DestroyIcon(smallIcon_);
}

bool SettingsWindow::create(HINSTANCE instance, const std::wstring& dataDir, PaneId pane) {
    instance_ = instance;
    dir_ = dataDir;
    pane_ = pane;
    dark_ = appsDark();
    model_ = loadModel(dir_);
    buildEnv();
    sidebarTops_ = ui::sidebarRowTops(kSidebarGroups);
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf()));
    font_ = ui::interfaceFont(dwrite_.Get());
    icon_ = makeSettingsIcon(GetSystemMetrics(SM_CXICON) * 2);
    smallIcon_ = makeSettingsIcon(GetSystemMetrics(SM_CXSMICON) * 2);

    WNDCLASSEXW wc{sizeof wc};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = icon_;
    wc.hIconSm = smallIcon_;
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);

    // Sur l'écran du curseur, au tiers haut, à son échelle.
    POINT pt{};
    GetCursorPos(&pt);
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(mon, &mi);
    UINT dpi = 96, dpiY = 96;
    if (HMODULE shcore = LoadLibraryW(L"shcore.dll")) {
        using GetDpi = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
        if (auto f = reinterpret_cast<GetDpi>(GetProcAddress(shcore, "GetDpiForMonitor"))) f(mon, 0, &dpi, &dpiY);
    }
    scale_ = float(dpi) / 96.f;
    const int w = int(std::lround(mt::windowWidth * scale_)), h = int(std::lround(mt::defaultHeight * scale_));
    const RECT& wa = mi.rcWork;
    const int x = wa.left + (wa.right - wa.left - w) / 2, y = wa.top + std::max(0L, (wa.bottom - wa.top - h) / 3);
    hwnd_ = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, kClass, L"Réglages MacDock",
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
            if (msg.message == WM_QUIT) return int(msg.wParam);
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
        ui::Painter p(dc_.Get(), dwrite_.Get(), pal, font_);
        computeGeometry(p, w);
        drawSidebar(p, h);
        drawContent(p, w, h);
        if (menu_) {
            const RowSpec& s = spec(menu_->row);
            ui::drawMenu(p, menu_->rect, s.choices, int(std::lround(valueOf(menu_->row))), menu_->hover);
        }
    }
    const HRESULT hr = dc_->EndDraw();
    swap_->Present(1, 0);
    if (hr == D2DERR_RECREATE_TARGET) {
        releaseTarget();
        createTarget();
    }
}

// ---- Barre latérale ----

void SettingsWindow::drawSidebar(ui::Painter& p, float h) {
    const ui::Palette& pal = p.pal();
    p.rt()->FillRectangle(D2D1::RectF(0, 0, mt::sidebarWidth, h), p.brush(pal.sidebarTint));
    p.rt()->DrawLine(D2D1::Point2F(mt::sidebarWidth - 0.5f, 0), D2D1::Point2F(mt::sidebarWidth - 0.5f, h), p.brush(pal.separator), 1);
    ui::drawWindowLights(p, D2D1::Point2F(kLightsX, kLightsY), active_, lightsHover_, lightsPressed_, 2);
    ui::drawSearchField(p, kSearch, L"", false);
    const auto& panes = paneList();
    for (std::size_t i = 0; i < panes.size() && i < sidebarTops_.size(); ++i) {
        const float top = sidebarTops_[i];
        const D2D1_RECT_F row{mt::sidebarInset, top, mt::sidebarWidth - mt::sidebarInset, top + mt::sidebarRow};
        const bool selected = panes[i].id == pane_;
        if (selected) p.fillRound(row, 6, active_ ? pal.accent : pal.sidebarSelection);
        else if (int(i) == sidebarHover_) p.fillRound(row, 6, ui::Rgba{pal.sidebarSelection.r, pal.sidebarSelection.g,
                                                                         pal.sidebarSelection.b, pal.sidebarSelection.a * 0.5f});
        drawPaneTile(p, D2D1::RectF(row.left + 8, top + 4, row.left + 8 + mt::tile, top + 4 + mt::tile), panes[i].tile, panes[i].icon);
        p.text(panes[i].title, D2D1::RectF(row.left + 36, top, row.right - 6, top + mt::sidebarRow), mt::fontBody,
               selected && active_ ? pal.onAccent : pal.text);
    }
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
            case RowKind::Info: break;
        }
    }
}

void SettingsWindow::drawRow(ui::Painter& p, int i) {
    const ui::Palette& pal = p.pal();
    const Geom& g = geoms_[std::size_t(i)];
    const RowSpec& s = spec(i);
    const bool on = enabled(i);
    p.opacity = on ? 1.f : 0.4f;
    const float labelRight = (s.kind == RowKind::Info ? g.row.right : g.control.left) - mt::rowPadding;
    const float left = g.row.left + mt::rowPadding;
    if (s.kind == RowKind::Slider) {
        p.text(s.label, D2D1::RectF(left, g.cy - 10, g.trackLeft - 16, g.cy + 10), mt::fontBody, pal.text);
    } else if (!s.detail.empty()) {
        p.text(s.label, D2D1::RectF(left, g.row.top + 6, labelRight, g.row.top + 25), mt::fontBody, pal.text);
        p.text(s.detail, D2D1::RectF(left, g.row.top + 24, labelRight, g.row.top + 41), mt::fontDetail, pal.secondaryText);
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
        case RowKind::Info: break;
    }
    p.opacity = 1;
    if (focus_ == i && s.kind != RowKind::Info)
        ui::drawFocusRing(p, g.control, s.kind == RowKind::Switch ? mt::switchHeight / 2 : s.kind == RowKind::Slider ? mt::knob / 2 : 6);
}

void SettingsWindow::drawContent(ui::Painter& p, float w, float h) {
    const ui::Palette& pal = p.pal();
    p.rt()->FillRectangle(D2D1::RectF(mt::sidebarWidth, 0, w, h), p.brush(pal.window));
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
    struct Ctx {
        PaneEnv* env;
        int n;
    } ctx{&env_, 0};
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR mon, HDC, LPRECT, LPARAM lp) -> BOOL {
            auto* c = reinterpret_cast<Ctx*>(lp);
            MONITORINFOEXW mi{};
            mi.cbSize = sizeof mi;
            if (!GetMonitorInfoW(mon, &mi)) return TRUE;
            ++c->n;
            DEVMODEW dm{};
            dm.dmSize = sizeof dm;
            std::wstring name = L"Écran " + std::to_wstring(c->n);
            if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm))
                name += L" — " + std::to_wstring(dm.dmPelsWidth) + L" × " + std::to_wstring(dm.dmPelsHeight);
            if (mi.dwFlags & MONITORINFOF_PRIMARY) name += L" (principal)";
            c->env->screens.push_back(name);
            c->env->screenIds.push_back(mi.szDevice);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&ctx));
}

void SettingsWindow::selectPane(PaneId pane) {
    pane_ = pane;
    scroll_ = 0;
    focus_ = -1;
    menu_.reset();
    rebuildRows();
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

void SettingsWindow::reloadModel() {
    model_ = loadModel(dir_);
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

bool SettingsWindow::focusable(int index) const { return spec(index).kind != RowKind::Info && enabled(index); }

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
        if (!commit(dir_, [&](SettingsModel& m) { s.set(m, value); }, &model_))
            log::warn(L"Réglages : écriture impossible dans %s", dir_.c_str());
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
    if (!pending_ || pressedRow_ < 0) {
        pending_.reset();
        return;
    }
    const double v = *pending_;
    const RowSpec& s = spec(pressedRow_);
    pending_.reset();
    commit(dir_, [&](SettingsModel& m) { s.set(m, v); }, &model_);
    lastCommit_ = GetTickCount64();
}

void SettingsWindow::openMenu(int index) {
    const RowSpec& s = spec(index);
    if (s.choices.empty()) return;
    const ui::Palette pal = ui::palette(dark_);
    ui::Painter p(dc_.Get(), dwrite_.Get(), pal, font_);
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
    for (std::size_t i = 0; i < geoms_.size(); ++i) {
        const Geom& g = geoms_[i];
        if (spec(int(i)).kind == RowKind::Info) continue;
        D2D1_RECT_F hit = g.control;
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
    if (menu_) {
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
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (const int s = sidebarAt(x, y); s >= 0) {
        selectPane(paneList()[std::size_t(s)].id);
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
        case RowKind::Choice: openMenu(i); break;
        default: break;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::onMouseMove(float x, float y) {
    if (!tracking_) {
        TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, hwnd_, 0};
        tracking_ = TrackMouseEvent(&t) != FALSE;
    }
    if (draggingSlider_ && pressedRow_ >= 0) {
        const RowSpec& s = spec(pressedRow_);
        const Geom& g = geoms_[std::size_t(pressedRow_)];
        const double v = ui::sliderValueAt(x, s.min, s.max, s.step, g.trackLeft, g.trackRight);
        if (v != valueOf(pressedRow_)) setValue(pressedRow_, v, false);
        return;
    }
    bool dirty = false;
    if (menu_) {
        const int hover = inside(menu_->rect, x, y)
                              ? ui::menuItemAt(y, menu_->rect.top + mt::menuPadding, mt::menuItem, int(spec(menu_->row).choices.size()))
                              : -1;
        if (hover != menu_->hover) {
            menu_->hover = hover;
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
    ReleaseCapture();
    if (pressedRow_ == -2 && menu_) {   // menu : l'élément sous le doigt
        const int item = inside(menu_->rect, x, y)
                             ? ui::menuItemAt(y, menu_->rect.top + mt::menuPadding, mt::menuItem, int(spec(menu_->row).choices.size()))
                             : -1;
        pressedRow_ = -1;
        if (item >= 0) chooseMenu(item);
        return;
    }
    if (lightsPressed_ >= 0) {
        const int l = lightsPressed_;
        lightsPressed_ = -1;
        if (lightAt(x, y) == l) {
            if (l == 0) PostMessageW(hwnd_, WM_CLOSE, 0, 0);
            else ShowWindow(hwnd_, SW_MINIMIZE);
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    const int i = pressedRow_;
    pressedRow_ = -1;
    if (draggingSlider_) {
        draggingSlider_ = false;
        pressedRow_ = i;
        commitPending();
        pressedRow_ = -1;
        // Dernière valeur écrite telle quelle (le curseur peut s'être arrêté entre deux écritures).
        if (i >= 0) {
            const double v = valueOf(i);
            commit(dir_, [&](SettingsModel& m) { spec(i).set(m, v); }, &model_);
        }
    } else if (i >= 0 && spec(i).kind == RowKind::Switch && controlAt(x, y) == i) {
        setValue(i, valueOf(i) >= 0.5 ? 0 : 1);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

bool SettingsWindow::onKey(WPARAM key) {
    const bool shift = GetKeyState(VK_SHIFT) < 0, ctrl = GetKeyState(VK_CONTROL) < 0;
    if (ctrl && key == 'W') {
        PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        return true;
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
        ensureVisible(focus_);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (key == VK_ESCAPE) {
        focus_ = -1;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (focus_ < 0) {   // sans contrôle choisi, les flèches parcourent les sections
        const auto& panes = paneList();
        int at = 0;
        for (std::size_t i = 0; i < panes.size(); ++i)
            if (panes[i].id == pane_) at = int(i);
        if (key == VK_UP && at > 0) selectPane(panes[std::size_t(at - 1)].id);
        else if (key == VK_DOWN && at + 1 < int(panes.size())) selectPane(panes[std::size_t(at + 1)].id);
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
    if (msg == paneMessage()) {   // seconde ouverture : la section demandée, au premier plan
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
            if (y < mt::titleBar && lightAt(x, y) < 0 && !inside(kSearch, x, y)) return HTCAPTION;
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
            if (!active_) menu_.reset();
            InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        case WM_SETTINGCHANGE:
            if (lp && !wcscmp(reinterpret_cast<const wchar_t*>(lp), L"ImmersiveColorSet")) {
                updateDark();
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            break;
        case WM_DISPLAYCHANGE:
            buildEnv();
            rebuildRows();
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
            onMouseMove(float(p.x), float(p.y));
            return 0;
        }
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
            if (onKey(wp)) return 0;
            break;
        case WM_TIMER:
            if (wp == kAnimTimer) {
                const ULONGLONG now = GetTickCount64();
                const double dt = std::min(0.05, double(now - lastFrame_) / 1000.0);
                lastFrame_ = now;
                bool moving = false;
                for (auto& s : springs_) moving = s.step(dt) || moving;
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
                if (!draggingSlider_) reloadModel();
            } else if (wp == kCommitTimer) {
                commitPending();
            }
            return 0;
        case WM_CLOSE: DestroyWindow(hwnd_); return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
        default: break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

// ---- Icône ----

HICON makeSettingsIcon(int size) {
    ComPtr<IWICImagingFactory> wic;
    ComPtr<ID2D1Factory> d2d;
    ComPtr<IDWriteFactory> dwrite;
    ComPtr<IWICBitmap> bmp;
    ComPtr<ID2D1RenderTarget> rt;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) ||
        FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf())) ||
        FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dwrite.GetAddressOf()))) ||
        FAILED(wic->CreateBitmap(UINT(size), UINT(size), GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp)) ||
        FAILED(d2d->CreateWicBitmapRenderTarget(bmp.Get(), D2D1::RenderTargetProperties(), &rt)))
        return nullptr;
    const ui::Palette pal = ui::palette(false);
    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(0, 0, 0, 0));
    {
        ui::Painter p(rt.Get(), dwrite.Get(), pal, L"");
        const float m = float(size) * 0.06f;
        drawPaneTile(p, D2D1::RectF(m, m, float(size) - m, float(size) - m), 0x8E8E93, PaneIcon::Gear);
    }
    if (FAILED(rt->EndDraw())) return nullptr;
    std::vector<BYTE> px(std::size_t(size) * size * 4);
    WICRect all{0, 0, size, size};
    bmp->CopyPixels(&all, UINT(size) * 4, UINT(px.size()), px.data());
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
