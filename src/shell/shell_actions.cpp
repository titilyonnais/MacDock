#include "shell_actions.h"

#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <algorithm>

#include "../core/log.h"

namespace md {

using Microsoft::WRL::ComPtr;

bool launch(const std::wstring& target) {
    SHELLEXECUTEINFOW sei{sizeof sei};
    sei.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
    sei.lpVerb = L"open";
    sei.lpFile = target.c_str();
    sei.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&sei)) return true;
    log::warn(L"Lancement impossible de %s (%lu)", target.c_str(), GetLastError());
    return false;
}

bool forceForeground(HWND hwnd) {
    if (SetForegroundWindow(hwnd)) return true;
    // Le Dock (WS_EX_NOACTIVATE) vient de recevoir le clic, mais Windows peut encore refuser :
    // une frappe Alt synthétique débloque le verrou de premier plan.
    INPUT in[2] = {};
    in[0].type = in[1].type = INPUT_KEYBOARD;
    in[0].ki.wVk = in[1].ki.wVk = VK_MENU;
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
    return SetForegroundWindow(hwnd) != FALSE;
}

void activateApp(const std::vector<HWND>& windows) {
    if (windows.empty()) return;
    // Ordre Z actuel (du haut vers le bas) pour préserver l'empilement relatif.
    std::vector<HWND> ordered;
    for (HWND h = GetTopWindow(nullptr); h; h = GetWindow(h, GW_HWNDNEXT))
        if (std::find(windows.begin(), windows.end(), h) != windows.end()) ordered.push_back(h);
    for (HWND h : windows)
        if (std::find(ordered.begin(), ordered.end(), h) == ordered.end()) ordered.push_back(h);

    for (HWND h : ordered)
        if (IsIconic(h)) ShowWindowAsync(h, SW_RESTORE);
    // Du bas vers le haut : chaque fenêtre passe au-dessus de la précédente.
    for (auto it = ordered.rbegin(); it != ordered.rend(); ++it)
        SetWindowPos(*it, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
    forceForeground(ordered.front());
}

void restoreWindow(HWND hwnd) {
    if (IsIconic(hwnd)) ShowWindowAsync(hwnd, SW_RESTORE);
    forceForeground(hwnd);
}

void minimizeAll(const std::vector<HWND>& windows) {
    for (HWND h : windows) ShowWindowAsync(h, SW_MINIMIZE);
}

void openRecycleBin() { launch(L"shell:RecycleBinFolder"); }

void openFolder(const std::wstring& path) { launch(path); }

void openStartMenu() {
    INPUT in[2] = {};
    in[0].type = in[1].type = INPUT_KEYBOARD;
    in[0].ki.wVk = in[1].ki.wVk = VK_LWIN;
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
}

void revealInExplorer(const std::wstring& path) {
    if (path.empty()) return;
    std::wstring args = L"/select,\"" + path + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

namespace {
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

std::wstring lower(std::wstring s) {
    for (auto& c : s) c = wchar_t(towlower(c));
    return s;
}

// Nom de la valeur Run qui lance exePath (quelle qu'en soit l'origine), vide sinon.
std::wstring runValueFor(const std::wstring& exePath) {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &key) != ERROR_SUCCESS) return {};
    std::wstring found;
    for (DWORD i = 0;; ++i) {
        wchar_t name[512];
        wchar_t data[2048];
        DWORD nameLen = 512, dataLen = sizeof data, type = 0;
        if (RegEnumValueW(key, i, name, &nameLen, nullptr, &type, reinterpret_cast<BYTE*>(data), &dataLen) != ERROR_SUCCESS)
            break;
        if (type != REG_SZ && type != REG_EXPAND_SZ) continue;
        data[std::min<DWORD>(dataLen / sizeof(wchar_t), 2047)] = 0;
        if (runCommandLaunches(data, exePath)) {
            found = name;
            break;
        }
    }
    RegCloseKey(key);
    return found;
}
} // namespace

bool runCommandLaunches(const std::wstring& command, const std::wstring& exePath) {
    if (command.empty() || exePath.empty()) return false;
    std::wstring cmd = lower(command), want = lower(exePath);
    return cmd == want || cmd.starts_with(L"\"" + want + L"\"") || cmd.starts_with(want + L" ");
}

bool isOpenAtLogin(const std::wstring& exePath) { return !exePath.empty() && !runValueFor(exePath).empty(); }

bool setOpenAtLogin(const std::wstring& exePath, const std::wstring& name, bool on) {
    if (exePath.empty()) return false;
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return false;
    LSTATUS st = ERROR_SUCCESS;
    if (on) {
        std::wstring value = L"MacDock: " + (name.empty() ? exePath : name);
        std::wstring cmd = L"\"" + exePath + L"\"";
        st = RegSetValueExW(key, value.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()),
                            DWORD((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        // Retire toutes les entrées qui lancent cet exécutable, y compris celles créées par l'app elle-même.
        for (std::wstring v = runValueFor(exePath); !v.empty(); v = runValueFor(exePath))
            if ((st = RegDeleteValueW(key, v.c_str())) != ERROR_SUCCESS) break;
    }
    RegCloseKey(key);
    if (st != ERROR_SUCCESS) log::warn(L"Ouverture à la connexion de %s : erreur %ld", exePath.c_str(), long(st));
    return st == ERROR_SUCCESS;
}

std::wstring quoteArguments(const std::vector<std::wstring>& paths) {
    std::wstring out;
    for (auto& p : paths) {
        if (!out.empty()) out += L' ';
        out += L"\"" + p + L"\"";
    }
    return out;
}

namespace {
ComPtr<IShellItemArray> itemArray(const std::vector<std::wstring>& paths) {
    std::vector<PIDLIST_ABSOLUTE> pidls;
    for (auto p : paths) {
        std::replace(p.begin(), p.end(), L'/', L'\\');   // ILCreateFromPath refuse les barres obliques
        if (PIDLIST_ABSOLUTE id = ILCreateFromPathW(p.c_str())) pidls.push_back(id);
        else log::warn(L"Fichier introuvable : %s", p.c_str());
    }
    ComPtr<IShellItemArray> arr;
    if (!pidls.empty())
        SHCreateShellItemArrayFromIDLists(UINT(pidls.size()), const_cast<LPCITEMIDLIST*>(
                                              reinterpret_cast<const LPCITEMIDLIST*>(pidls.data())), &arr);
    for (auto id : pidls) ILFree(id);
    return arr;
}
} // namespace

bool openWith(const std::wstring& exePath, const std::wstring& aumid, const std::vector<std::wstring>& paths) {
    if (paths.empty()) return false;
    if (!aumid.empty()) {
        ComPtr<IApplicationActivationManager> mgr;
        auto arr = itemArray(paths);
        DWORD pid = 0;
        HRESULT hr = arr ? CoCreateInstance(CLSID_ApplicationActivationManager, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&mgr))
                         : E_FAIL;
        if (SUCCEEDED(hr)) hr = mgr->ActivateForFile(aumid.c_str(), arr.Get(), L"open", &pid);
        if (SUCCEEDED(hr)) return true;
        log::warn(L"Ouverture avec %s impossible (0x%08lx)", aumid.c_str(), static_cast<unsigned long>(hr));
        if (exePath.empty()) return false;
    }
    std::wstring args = quoteArguments(paths);
    SHELLEXECUTEINFOW sei{sizeof sei};
    sei.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
    sei.lpVerb = L"open";
    sei.lpFile = exePath.c_str();
    sei.lpParameters = args.c_str();
    sei.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&sei)) return true;
    log::warn(L"Ouverture avec %s impossible (%lu)", exePath.c_str(), GetLastError());
    return false;
}

namespace {
// IFileOperation avec l'interface de progression de l'Explorateur ; apply ajoute les opérations.
template <class F> bool fileOperation(HWND owner, DWORD flags, F apply) {
    ComPtr<IFileOperation> op;
    if (FAILED(CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&op)))) return false;
    op->SetOwnerWindow(owner);
    op->SetOperationFlags(flags);
    if (FAILED(apply(op.Get()))) return false;
    HRESULT hr = op->PerformOperations();
    if (FAILED(hr)) log::warn(L"Opération sur les fichiers impossible (0x%08lx)", static_cast<unsigned long>(hr));
    return SUCCEEDED(hr);
}
} // namespace

bool recycle(const std::vector<std::wstring>& paths, HWND owner) {
    auto arr = itemArray(paths);
    if (!arr) return false;
    return fileOperation(owner, FOF_ALLOWUNDO | FOFX_RECYCLEONDELETE | FOFX_ADDUNDORECORD,
                         [&](IFileOperation* op) { return op->DeleteItems(arr.Get()); });
}

bool moveInto(const std::vector<std::wstring>& paths, const std::wstring& folder, HWND owner) {
    auto arr = itemArray(paths);
    ComPtr<IShellItem> dest;
    if (!arr || FAILED(SHCreateItemFromParsingName(folder.c_str(), nullptr, IID_PPV_ARGS(&dest)))) return false;
    return fileOperation(owner, FOF_ALLOWUNDO | FOFX_ADDUNDORECORD,
                         [&](IFileOperation* op) { return op->MoveItems(arr.Get(), dest.Get()); });
}

bool recycleBinHasItems() {
    SHQUERYRBINFO info{sizeof info};
    if (FAILED(SHQueryRecycleBinW(nullptr, &info))) return false;
    return info.i64NumItems > 0;
}

void emptyRecycleBin(HWND owner) {
    HRESULT hr = SHEmptyRecycleBinW(owner, nullptr, 0);
    if (FAILED(hr) && hr != HRESULT_FROM_WIN32(ERROR_CANCELLED) && hr != E_UNEXPECTED)
        log::warn(L"Vidage de la Corbeille impossible (0x%08lx)", static_cast<unsigned long>(hr));
}

std::wstring downloadsFolder() {
    PWSTR path = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &path))) out = path;
    CoTaskMemFree(path);
    return out;
}

} // namespace md
