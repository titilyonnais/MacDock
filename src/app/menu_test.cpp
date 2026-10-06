#include "menu_test.h"

#include <d3d11.h>
#include <shellscalingapi.h>
#include <wrl/client.h>

#include "../core/log.h"
#include "../glass/backdrop_capture.h"
#include "../popup/menu_window.h"

namespace md {

int runMenuTest(HINSTANCE instance) {
    HMONITOR mon = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(mon, &mi);
    UINT dpiX = 96, dpiY = 96;
    GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    Microsoft::WRL::ComPtr<ID3D11Device> dev;
    auto adapter = BackdropCapture::adapterFor(mon);
    D3D11CreateDevice(adapter.Get(), adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr,
                      D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &dev, nullptr, nullptr);

    MenuModel options;
    options.items = {{10, L"Garder dans le Dock", true}, {11, L"Ouvrir à la connexion"}, {12, L"Afficher dans l'Explorateur"}};
    MenuModel m;
    MenuItem opt{1, L"Options"};
    opt.submenu = options.items;
    m.items = {opt, {}, {2, L"Afficher toutes les fenêtres"}, {3, L"Masquer"}, {4, L"Quitter"}};

    MenuWindow::Env env;
    env.instance = instance;
    env.device = dev.Get();
    env.scale = float(dpiX) / 96.0f;
    env.font = L"SF Pro Text";
    env.trace = true;
    POINT anchor{(mi.rcMonitor.left + mi.rcMonitor.right) / 2, mi.rcMonitor.bottom - LONG(200 * env.scale)};
    int choice = MenuWindow::track(env, m, anchor);
    log::info(L"menu-test : choix %d", choice);
    return choice ? 0 : 1;
}

} // namespace md
