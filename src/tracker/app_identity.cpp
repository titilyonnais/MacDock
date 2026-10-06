#include "app_identity.h"

// Les en-têtes Shell redéfinissent PID_FIRST_USABLE (avertissement du SDK, sans conséquence).
#pragma warning(push)
#pragma warning(disable : 4005)
#include <appmodel.h>
#include <dwmapi.h>
#include <propkey.h>
#include <propsys.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>
#pragma warning(pop)

#include <vector>

using Microsoft::WRL::ComPtr;

namespace md {
namespace {

std::wstring className(HWND hwnd) {
    wchar_t cls[256] = {};
    GetClassNameW(hwnd, cls, 256);
    return cls;
}

HWND findCoreWindow(HWND frame) {
    HWND found = nullptr;
    DWORD framePid = 0;
    GetWindowThreadProcessId(frame, &framePid);
    struct Ctx { HWND* found; DWORD framePid; } ctx{&found, framePid};
    EnumChildWindows(
        frame,
        [](HWND child, LPARAM lp) -> BOOL {
            auto* c = reinterpret_cast<Ctx*>(lp);
            DWORD pid = 0;
            GetWindowThreadProcessId(child, &pid);
            if (pid != c->framePid && className(child) == L"Windows.UI.Core.CoreWindow") {
                *c->found = child;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&ctx));
    return found;
}

std::wstring processPath(DWORD pid) {
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return {};
    wchar_t path[MAX_PATH * 2];
    DWORD size = DWORD(std::size(path));
    std::wstring out;
    if (QueryFullProcessImageNameW(p, 0, path, &size)) out.assign(path, size);
    CloseHandle(p);
    return out;
}

std::wstring processAumid(DWORD pid) {
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return {};
    wchar_t buf[APPLICATION_USER_MODEL_ID_MAX_LENGTH];
    UINT32 len = APPLICATION_USER_MODEL_ID_MAX_LENGTH;
    std::wstring out;
    if (GetApplicationUserModelId(p, &len, buf) == ERROR_SUCCESS) out = buf;
    CloseHandle(p);
    return out;
}

std::wstring windowAumid(HWND hwnd) {
    ComPtr<IPropertyStore> store;
    if (FAILED(SHGetPropertyStoreForWindow(hwnd, IID_PPV_ARGS(&store)))) return {};
    PROPVARIANT pv;
    PropVariantInit(&pv);
    std::wstring out;
    if (SUCCEEDED(store->GetValue(PKEY_AppUserModel_ID, &pv)) && pv.vt == VT_LPWSTR && pv.pwszVal) out = pv.pwszVal;
    PropVariantClear(&pv);
    return out;
}

std::wstring shellDisplayName(const std::wstring& parsingName) {
    ComPtr<IShellItem> item;
    if (FAILED(SHCreateItemFromParsingName(parsingName.c_str(), nullptr, IID_PPV_ARGS(&item)))) return {};
    PWSTR name = nullptr;
    std::wstring out;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &name)) && name) out = name;
    CoTaskMemFree(name);
    return out;
}

bool isCloakedForGood(HWND hwnd) {
    DWORD cloaked = 0;
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof cloaked))) return false;
    // DWM_CLOAKED_SHELL seul = fenêtre sur un autre bureau virtuel : elle compte comme ouverte.
    return (cloaked & (DWM_CLOAKED_APP | DWM_CLOAKED_INHERITED)) != 0;
}

} // namespace

bool isDockEligibleWindow(HWND hwnd) {
    if (!IsWindow(hwnd) || !IsWindowVisible(hwnd)) return false;
    if (GetAncestor(hwnd, GA_ROOT) != hwnd) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId()) return false;

    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    bool appWindow = (ex & WS_EX_APPWINDOW) != 0;
    if (!appWindow) {
        if (ex & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)) return false;
        if (GetWindow(hwnd, GW_OWNER)) return false;
        if (GetWindowTextLengthW(hwnd) == 0) return false;
    }

    std::wstring cls = className(hwnd);
    static const wchar_t* kExcluded[] = {L"Progman", L"WorkerW", L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd",
                                         L"Windows.UI.Core.CoreWindow", L"XamlExplorerHostIslandWindow",
                                         L"ApplicationManager_ImmersiveShellWindow", L"MacDockWindow"};
    for (auto* e : kExcluded)
        if (cls == e) return false;

    if (isCloakedForGood(hwnd)) return false;
    // Cadre d'app du Store sans contenu (app suspendue ou pas encore chargée).
    if (cls == L"ApplicationFrameWindow" && !findCoreWindow(hwnd)) return false;
    return true;
}

std::wstring exeDisplayName(const std::wstring& exePath) {
    DWORD handle = 0;
    DWORD size = GetFileVersionInfoSizeW(exePath.c_str(), &handle);
    if (size) {
        std::vector<BYTE> data(size);
        if (GetFileVersionInfoW(exePath.c_str(), 0, size, data.data())) {
            struct Translation { WORD lang, codepage; }* tr = nullptr;
            UINT len = 0;
            if (VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&tr), &len) &&
                len >= sizeof(Translation)) {
                wchar_t key[64];
                swprintf_s(key, L"\\StringFileInfo\\%04x%04x\\FileDescription", tr->lang, tr->codepage);
                wchar_t* desc = nullptr;
                if (VerQueryValueW(data.data(), key, reinterpret_cast<void**>(&desc), &len) && len > 1 && desc[0])
                    return desc;
            }
        }
    }
    size_t slash = exePath.find_last_of(L"\\/");
    std::wstring file = slash == std::wstring::npos ? exePath : exePath.substr(slash + 1);
    size_t dot = file.find_last_of(L'.');
    return dot == std::wstring::npos ? file : file.substr(0, dot);
}

std::optional<AppIdentity> identifyWindow(HWND hwnd) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    AppIdentity id;
    if (className(hwnd) == L"ApplicationFrameWindow") {
        HWND core = findCoreWindow(hwnd);
        if (!core) return std::nullopt;
        GetWindowThreadProcessId(core, &pid);
        id.aumid = processAumid(pid);
    } else {
        id.aumid = windowAumid(hwnd);
        if (id.aumid.empty()) id.aumid = processAumid(pid);
    }
    id.exePath = processPath(pid);
    if (id.exePath.empty() && id.aumid.empty()) return std::nullopt;
    id.appId = makeAppId(id.aumid, id.exePath);

    if (!id.aumid.empty()) {
        id.launch = L"shell:AppsFolder\\" + id.aumid;
        id.displayName = shellDisplayName(id.launch);
    }
    if (id.launch.empty() || id.displayName.empty()) {
        // AUMID explicite d'une app de bureau (Chrome, Edge…) : on relance l'exe.
        if (id.displayName.empty()) id.displayName = exeDisplayName(id.exePath);
        if (id.aumid.empty() || id.launch.empty()) id.launch = id.exePath;
    }
    return id;
}

std::optional<AppIdentity> identifyLaunchTarget(const std::wstring& path) {
    AppIdentity id;
    id.launch = path;
    std::wstring lower = path;
    for (auto& c : lower) c = wchar_t(towlower(c));
    if (lower.ends_with(L".lnk")) {
        ComPtr<IShellLinkW> link;
        if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))))
            return std::nullopt;
        ComPtr<IPersistFile> file;
        if (FAILED(link.As(&file)) || FAILED(file->Load(path.c_str(), STGM_READ))) return std::nullopt;
        wchar_t target[MAX_PATH] = {};
        link->GetPath(target, MAX_PATH, nullptr, SLGP_RAWPATH);
        wchar_t expanded[MAX_PATH] = {};
        ExpandEnvironmentStringsW(target, expanded, MAX_PATH);
        id.exePath = expanded;
        ComPtr<IPropertyStore> store;
        if (SUCCEEDED(link.As(&store))) {
            PROPVARIANT pv;
            PropVariantInit(&pv);
            if (SUCCEEDED(store->GetValue(PKEY_AppUserModel_ID, &pv)) && pv.vt == VT_LPWSTR && pv.pwszVal)
                id.aumid = pv.pwszVal;
            PropVariantClear(&pv);
        }
        id.displayName = shellDisplayName(path);
        // Le nom d'un raccourci contient souvent l'extension masquée : on la retire.
        if (id.displayName.ends_with(L".lnk")) id.displayName.resize(id.displayName.size() - 4);
    } else if (lower.starts_with(L"shell:appsfolder\\")) {
        id.aumid = path.substr(17);
        id.displayName = shellDisplayName(path);
    } else {
        id.exePath = path;
        id.displayName = exeDisplayName(path);
    }
    if (id.exePath.empty() && id.aumid.empty()) return std::nullopt;
    id.appId = makeAppId(id.aumid, id.exePath);
    if (id.displayName.empty()) id.displayName = exeDisplayName(id.exePath);
    return id;
}

} // namespace md
