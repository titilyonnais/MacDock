// Suivi des fenêtres de premier niveau (hooks WinEvent + ShellHook).
#pragma once
#include <windows.h>

#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#include "../model/app_model.h"

namespace md {

class WindowTracker {
public:
    struct Events {
        std::function<void(HWND, const AppIdentity&)> opened;
        std::function<void(HWND)> closed;
        std::function<void(HWND)> activated;    // fenêtre suivie (éligible au Dock) passée au premier plan
        std::function<void(HWND)> foreground;   // tout changement de premier plan, fenêtre éligible ou non (bureau, dialogue)
        std::function<void(HWND)> flashed;
        std::function<void(HWND, bool)> minimized;
        std::function<void(HWND)> minimizeStarted;   // EVENT_SYSTEM_MINIMIZESTART seulement (pas une fenêtre découverte réduite)
        std::function<void(HWND, const std::wstring&)> titleChanged;
        std::function<void(HWND)> moved;   // fenêtre suivie de l'app au premier plan déplacée ou redimensionnée
    };

    ~WindowTracker() { stop(); }
    bool start(HWND messageWindow, Events events);
    void stop();
    bool handleMessage(UINT msg, WPARAM wp, LPARAM lp);   // à appeler depuis le WndProc
    void rescan();
    HWND foreground() const { return foreground_; }
    void setTrace(bool trace) { trace_ = trace; }

private:
    static void CALLBACK winEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD,
                                      DWORD);
    void onEvent(DWORD event, HWND hwnd);
    void setMoveHook(DWORD pid);   // fil des crochets
    // Crochets WinEvent sur leur propre fil (plan 62) : posés pour tout le système, ils reçoivent les événements
    // d'objets de toutes les apps ; seuls ceux d'une fenêtre sont postés au fil du Dock (winEventMsg_).
    std::thread eventThread_;
    std::atomic<DWORD> eventThreadId_{0};
    std::atomic<bool> hooksOk_{false};
    UINT winEventMsg_ = 0;
    static constexpr UINT kWatchMoves = WM_APP + 1;   // message de fil : nouveau processus au premier plan
    void evaluate(HWND hwnd);
    void forget(HWND hwnd);

    HWND msgWindow_ = nullptr;
    UINT shellMsg_ = 0;
    HWINEVENTHOOK hooks_[6] = {};   // fil des crochets ; [5] : déplacements, pour le seul processus au premier plan
    DWORD movePid_ = 0;
    void watchMoves(HWND foreground);
    Events events_;
    struct Known { std::wstring title; bool minimized = false; };
    std::map<HWND, Known> known_;
    std::map<HWND, ULONGLONG> pending_;   // fenêtres à réévaluer (cadres d'apps du Store en chargement)
    HWND foreground_ = nullptr;
    bool trace_ = false;
    std::map<DWORD, int> eventCounts_;
    std::map<DWORD, int> rawCounts_, rawByProcess_;   // mode diagnostic : avant filtre, par type et par processus
    std::mutex rawMutex_;                             // rawCounts_ et rawByProcess_ (fil des crochets, fil du Dock)
    ULONGLONG countSince_ = 0;
    static WindowTracker* instance_;
};

} // namespace md
