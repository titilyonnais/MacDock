#include "bar_actions.h"

#include <ole2.h>
#include <exdisp.h>
#include <propvarutil.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wrl/client.h>

#include <algorithm>

#include "../core/log.h"
#include "../shell/shell_actions.h"
#include "../tracker/app_identity.h"
#include "shortcut.h"

namespace md {
namespace {

template <class T> using Com = Microsoft::WRL::ComPtr<T>;

bool liveWindow(HWND h) { return h && IsWindow(h); }

// Fenêtre de l'Explorateur : navigation sur place (comme le menu Aller du Finder) ; false si ce n'en est pas une.
bool navigateExplorer(HWND window, const std::wstring& path) {
    Com<IShellWindows> windows;
    if (!window || FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&windows))))
        return false;
    long count = 0;
    windows->get_Count(&count);
    for (long i = 0; i < count; ++i) {
        VARIANT index{};
        index.vt = VT_I4;
        index.lVal = i;
        Com<IDispatch> disp;
        if (FAILED(windows->Item(index, &disp)) || !disp) continue;
        Com<IWebBrowser2> app;
        if (FAILED(disp.As(&app))) continue;
        SHANDLE_PTR h = 0;
        if (FAILED(app->get_HWND(&h)) || reinterpret_cast<HWND>(h) != window) continue;
        PIDLIST_ABSOLUTE pidl = nullptr;
        if (FAILED(SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, nullptr))) return false;
        VARIANT target{};
        HRESULT hr = InitVariantFromBuffer(pidl, ILGetSize(pidl), &target);
        CoTaskMemFree(pidl);
        if (FAILED(hr)) return false;
        VARIANT empty{};
        VariantInit(&empty);
        hr = app->Navigate2(&target, &empty, &empty, &empty, &empty);
        VariantClear(&target);
        return SUCCEEDED(hr);
    }
    return false;
}

bool shellOpen(const std::wstring& file, const std::wstring& params = {}) {
    auto r = reinterpret_cast<INT_PTR>(
        ShellExecuteW(nullptr, L"open", file.c_str(), params.empty() ? nullptr : params.c_str(), nullptr, SW_SHOWNORMAL));
    if (r <= 32) log::warn(L"Barre : ouverture impossible de %s (%lld)", file.c_str(), static_cast<long long>(r));
    return r > 32;
}

struct OthersCollect {
    const std::vector<HWND>* mine;
    std::vector<HWND> others;
};

BOOL CALLBACK collectOthers(HWND h, LPARAM lp) {
    auto* c = reinterpret_cast<OthersCollect*>(lp);
    if (!IsIconic(h) && isDockEligibleWindow(h) && std::find(c->mine->begin(), c->mine->end(), h) == c->mine->end())
        c->others.push_back(h);
    return TRUE;
}

bool confirmed(const SystemActions& sys, const wchar_t* question) {
    return sys.confirm && sys.confirm(question);
}

void call(const std::function<void()>& f) {
    if (f) f();
}

} // namespace

BarTarget keepTarget(const BarTarget& current, HWND foreground, ForegroundKind kind, const std::wstring& appId) {
    if (kind == ForegroundKind::Ignore || !foreground) return current;
    return {foreground, appId};
}

bool runAction(const MenuAction& a, ActionContext& c, const SystemActions& sys) {
    HWND target = c.target.window;
    switch (a.kind) {
        case ActionKind::None: return false;
        case ActionKind::Shortcut: {
            auto s = parseShortcut(a.arg);
            if (!s || !liveWindow(target)) return false;
            forceForeground(target);
            Sleep(40);   // le temps que l'app reçoive l'activation avant les touches
            auto inputs = shortcutInputs(*s);
            return SendInput(UINT(inputs.size()), inputs.data(), sizeof(INPUT)) == inputs.size();
        }
        case ActionKind::CloseWindow:
            if (!liveWindow(target)) return false;
            return PostMessageW(target, WM_CLOSE, 0, 0) != FALSE;
        case ActionKind::Minimize:
            if (!liveWindow(target)) return false;
            ShowWindow(target, SW_MINIMIZE);
            return true;
        case ActionKind::Zoom:
            if (!liveWindow(target)) return false;
            ShowWindow(target, IsZoomed(target) ? SW_RESTORE : SW_MAXIMIZE);
            forceForeground(target);
            return true;
        case ActionKind::BringAllToFront:
            if (c.appWindows.empty()) return false;
            activateApp(c.appWindows);
            return true;
        case ActionKind::ActivateWindow: {
            HWND h = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(a.window));
            if (!liveWindow(h)) return false;
            if (IsIconic(h)) ShowWindow(h, SW_RESTORE);
            return forceForeground(h);
        }
        case ActionKind::HideApp:
            if (c.appWindows.empty()) return false;
            minimizeAll(c.appWindows);
            if (c.hidden) c.hidden->insert(c.hidden->end(), c.appWindows.begin(), c.appWindows.end());
            return true;
        case ActionKind::HideOthers: {
            OthersCollect col{&c.appWindows, {}};
            EnumWindows(collectOthers, reinterpret_cast<LPARAM>(&col));
            for (HWND h : col.others) ShowWindow(h, SW_SHOWMINNOACTIVE);
            if (c.hidden) c.hidden->insert(c.hidden->end(), col.others.begin(), col.others.end());
            return !col.others.empty();
        }
        case ActionKind::ShowAll: {
            if (!c.hidden || c.hidden->empty()) return false;
            for (HWND h : *c.hidden)
                if (liveWindow(h) && IsIconic(h)) ShowWindow(h, SW_SHOWNOACTIVATE);
            c.hidden->clear();
            return true;
        }
        case ActionKind::QuitApp:
            if (c.appWindows.empty()) return false;
            for (HWND h : c.appWindows) PostMessageW(h, WM_CLOSE, 0, 0);
            return true;
        case ActionKind::AboutApp:
            if (c.exePath.empty()) return false;
            return SHObjectProperties(nullptr, SHOP_FILEPATH, c.exePath.c_str(), nullptr) != FALSE;
        case ActionKind::EmptyTrash:
            emptyRecycleBin(nullptr);   // avec la confirmation de l'Explorateur
            return true;
        case ActionKind::OpenUri: return shellOpen(a.arg);
        case ActionKind::GoTo: {
            wchar_t cls[32] = {};
            if (liveWindow(target)) GetClassNameW(target, cls, 32);
            if (wcscmp(cls, L"CabinetWClass") == 0 && navigateExplorer(target, a.arg)) return true;
            return shellOpen(L"explorer.exe", a.arg);
        }
        case ActionKind::Sleep: call(sys.sleep); return true;
        case ActionKind::Lock: call(sys.lock); return true;
        case ActionKind::SignOut:
            if (!confirmed(sys, L"Voulez-vous vraiment fermer la session ? Les apps ouvertes seront fermées.")) return false;
            call(sys.signOut);
            return true;
        case ActionKind::Restart:
            if (!confirmed(sys, L"Voulez-vous vraiment redémarrer l'ordinateur ?")) return false;
            call(sys.restart);
            return true;
        case ActionKind::Shutdown:
            if (!confirmed(sys, L"Voulez-vous vraiment éteindre l'ordinateur ?")) return false;
            call(sys.shutdown);
            return true;
    }
    return false;
}

} // namespace md
