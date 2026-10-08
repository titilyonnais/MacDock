#include "quicklook_shell.h"

#include <ole2.h>   // exdisp.h en a besoin (WIN32_LEAN_AND_MEAN)
#include <exdisp.h>
#include <shlguid.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

namespace md {

namespace {

using Microsoft::WRL::ComPtr;

std::vector<std::wstring> selectionOf(IShellBrowser* browser) {
    std::vector<std::wstring> out;
    ComPtr<IShellView> view;
    ComPtr<IFolderView2> folder;
    ComPtr<IShellItemArray> items;
    if (!browser || FAILED(browser->QueryActiveShellView(&view)) || FAILED(view.As(&folder)) ||
        FAILED(folder->GetSelection(FALSE, &items)) || !items)
        return out;
    DWORD n = 0;
    items->GetCount(&n);
    for (DWORD i = 0; i < n; ++i) {
        ComPtr<IShellItem> item;
        PWSTR path = nullptr;
        if (SUCCEEDED(items->GetItemAt(i, &item)) && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
            out.emplace_back(path);
            CoTaskMemFree(path);
        }
    }
    return out;
}

ComPtr<IShellBrowser> browserOf(IDispatch* disp) {
    ComPtr<IServiceProvider> sp;
    ComPtr<IShellBrowser> browser;
    if (disp && SUCCEEDED(disp->QueryInterface(IID_PPV_ARGS(&sp))))
        sp->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&browser));
    return browser;
}

// Onglet visible : dans une fenêtre à onglets, chaque onglet a sa vue, une seule est visible.
bool viewVisible(IShellBrowser* browser) {
    ComPtr<IShellView> view;
    HWND h = nullptr;
    return browser && SUCCEEDED(browser->QueryActiveShellView(&view)) && SUCCEEDED(view->GetWindow(&h)) && h &&
           IsWindowVisible(h);
}

} // namespace

std::vector<std::wstring> shellSelection(HWND foreground) {
    ComPtr<IShellWindows> windows;
    if (!foreground || FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&windows))))
        return {};
    wchar_t cls[64] = {};
    GetClassNameW(foreground, cls, 64);
    if (_wcsicmp(cls, L"Progman") == 0 || _wcsicmp(cls, L"WorkerW") == 0) {   // bureau
        VARIANT loc{}, empty{};
        loc.vt = VT_I4;
        loc.lVal = CSIDL_DESKTOP;
        long hwnd = 0;
        ComPtr<IDispatch> disp;
        if (SUCCEEDED(windows->FindWindowSW(&loc, &empty, SWC_DESKTOP, &hwnd, SWFO_NEEDDISPATCH, &disp)) && disp)
            return selectionOf(browserOf(disp.Get()).Get());
        return {};
    }
    long count = 0;
    windows->get_Count(&count);
    for (long i = 0; i < count; ++i) {
        VARIANT index{};
        index.vt = VT_I4;
        index.lVal = i;
        ComPtr<IDispatch> disp;
        ComPtr<IWebBrowserApp> app;
        SHANDLE_PTR h = 0;
        if (FAILED(windows->Item(index, &disp)) || !disp || FAILED(disp.As(&app)) || FAILED(app->get_HWND(&h)) ||
            reinterpret_cast<HWND>(h) != foreground)
            continue;
        ComPtr<IShellBrowser> browser = browserOf(disp.Get());
        if (viewVisible(browser.Get())) return selectionOf(browser.Get());
    }
    return {};
}

} // namespace md
