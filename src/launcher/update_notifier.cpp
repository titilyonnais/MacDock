#include "update_notifier.h"

#include <shellapi.h>

#include <chrono>
#include <cstdlib>
#include <string>

#include "../core/log.h"
#include "../update/updater.h"

namespace md {

namespace {

constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT_PTR kCheckTimer = 1;
constexpr UINT kIconId = 1;
constexpr UINT kHourMs = 60 * 60 * 1000;
constexpr wchar_t kClass[] = L"MacDockUpdateNotify";
UpdateNotifier* g_self = nullptr;

UINT firstDelayMs() {
    wchar_t* v = nullptr;
    std::size_t n = 0;
    UINT ms = 60 * 1000;
    if (_wdupenv_s(&v, &n, L"MACDOCK_UPDATE_DELAY") == 0 && v) ms = UINT(std::wcstoul(v, nullptr, 10)) * 1000;
    std::free(v);
    return ms < 1000 ? 1000 : ms;
}

std::int64_t nowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace

void UpdateNotifier::start(HINSTANCE instance) {
    if (thread_.joinable()) return;
    instance_ = instance;
    stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_self = this;
    thread_ = std::thread([this] { run(); });
}

bool UpdateNotifier::stop(unsigned waitMs) {
    if (!thread_.joinable()) return true;
    SetEvent(stop_);
    if (WaitForSingleObject(thread_.native_handle(), waitMs) != WAIT_OBJECT_0) {
        thread_.detach();   // encore dans le réseau : il s'arrêtera avec le processus
        return false;
    }
    thread_.join();
    CloseHandle(stop_);
    stop_ = nullptr;
    g_self = nullptr;
    return true;
}

void UpdateNotifier::run() {
    WNDCLASSW wc{};
    wc.lpfnWndProc = proc;
    wc.hInstance = instance_;
    wc.lpszClassName = kClass;
    RegisterClassW(&wc);
    hwnd_ = CreateWindowExW(0, kClass, L"MacDock, mises à jour", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance_, nullptr);
    if (!hwnd_) return;
    SetTimer(hwnd_, kCheckTimer, firstDelayMs(), nullptr);
    // Messages (minuterie, clic sur la notification) jusqu'à l'arrêt ; une recherche bloque la boucle le temps qu'elle
    // dure, et s'interrompt elle-même quand stop_ est levé.
    while (MsgWaitForMultipleObjectsEx(1, &stop_, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE) != WAIT_OBJECT_0) {
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        if (WaitForSingleObject(stop_, 0) == WAIT_OBJECT_0) break;
    }
    removeIcon();
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
}

LRESULT CALLBACK UpdateNotifier::proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    UpdateNotifier* self = g_self;
    if (!self) return DefWindowProcW(h, msg, wp, lp);
    switch (msg) {
        case WM_TIMER:
            if (wp == kCheckTimer) self->onTimer();
            return 0;
        case kTrayMessage:
            // Clic sur la notification ou sur l'icône : installation tout de suite.
            if (LOWORD(lp) == NIN_BALLOONUSERCLICK || LOWORD(lp) == WM_LBUTTONUP) self->installNow();
            return 0;
        default: break;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void UpdateNotifier::onTimer() {
    SetTimer(hwnd_, kCheckTimer, kHourMs, nullptr);   // réévalué toutes les heures, recherche toutes les 12 heures
    const update::Paths paths = update::defaultPaths();
    const UpdateState s = update::load(paths);
    if (!s.automatic) return;
    if (!checkDue(s.lastCheck, nowSeconds()) && s.readyVersion.empty()) return;
    update::Options options = update::optionsFromEnvironment();
    options.cancel = stop_;   // arrêt du lanceur (installateur, « Quitter MacDock ») : la recherche s'interrompt
    std::wstring ready;
    if (update::check(paths, options, &ready) == update::CheckResult::Ready && ready != announced_) showReady(ready);
}

void UpdateNotifier::showReady(const std::wstring& version) {
    NOTIFYICONDATAW nid{sizeof nid};
    nid.hWnd = hwnd_;
    nid.uID = kIconId;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_INFO;
    nid.uCallbackMessage = kTrayMessage;
    nid.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(1));
    wcsncpy_s(nid.szTip, L"Mise à jour de MacDock prête : clique pour l'installer", _TRUNCATE);
    wcsncpy_s(nid.szInfoTitle, (L"MacDock " + version + L" est prêt").c_str(), _TRUNCATE);
    wcsncpy_s(nid.szInfo, L"Clique ici pour redémarrer MacDock et l'installer. Sinon, il s'installera au prochain démarrage.",
              _TRUNCATE);
    nid.dwInfoFlags = NIIF_USER | NIIF_LARGE_ICON;
    nid.hBalloonIcon = nid.hIcon;
    if (Shell_NotifyIconW(iconShown_ ? NIM_MODIFY : NIM_ADD, &nid)) {
        iconShown_ = true;
        announced_ = version;
        log::info(L"Mise à jour : MacDock %s annoncée", version.c_str());
    }
}

void UpdateNotifier::removeIcon() {
    if (!iconShown_) return;
    NOTIFYICONDATAW nid{sizeof nid};
    nid.hWnd = hwnd_;
    nid.uID = kIconId;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    iconShown_ = false;
}

void UpdateNotifier::installNow() {
    removeIcon();
    if (!update::launchInstaller(update::defaultPaths(), false))   // l'installateur quitte MacDock et le relance
        log::warn(L"Mise à jour : installation impossible depuis la notification");
}

} // namespace md
