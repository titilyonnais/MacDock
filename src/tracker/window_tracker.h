// Suivi des fenêtres de premier niveau (hooks WinEvent + ShellHook).
#pragma once
#include <windows.h>

#include <functional>
#include <map>
#include <string>

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
    void evaluate(HWND hwnd);
    void forget(HWND hwnd);

    HWND msgWindow_ = nullptr;
    UINT shellMsg_ = 0;
    HWINEVENTHOOK hooks_[5] = {};
    Events events_;
    struct Known { std::wstring title; bool minimized = false; };
    std::map<HWND, Known> known_;
    std::map<HWND, ULONGLONG> pending_;   // fenêtres à réévaluer (cadres d'apps du Store en chargement)
    HWND foreground_ = nullptr;
    bool trace_ = false;
    std::map<DWORD, int> eventCounts_;
    ULONGLONG countSince_ = 0;
    static WindowTracker* instance_;
};

} // namespace md
