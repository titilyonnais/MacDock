// Fil de travail des sources d'état lentes (réseau, radios, lecture en cours, luminosité) : relevé périodique posté
// à la barre, et actions (allumer le Wi-Fi, morceau suivant…) exécutées hors du fil de la barre.
#pragma once
#include <windows.h>

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

#include "status_network.h"
#include "status_power.h"
#include "status_winrt.h"

namespace md {

struct StatusSnapshot {
    NetworkInfo network;
    RadioInfo radios;
    MediaInfo media;
    std::optional<double> brightness;
    BatteryInfo battery;
};

class StatusHub {
public:
    StatusHub() = default;
    StatusHub(const StatusHub&) = delete;
    StatusHub& operator=(const StatusHub&) = delete;
    ~StatusHub() { stop(); }

    // Relevé toutes les intervalMs et après chaque action : msg posté à notify, lParam = StatusSnapshot* (à libérer).
    bool start(HWND notify, UINT msg, DWORD intervalMs = 2000);
    void stop();
    // Action sur le fil (COM MTA) ; key != 0 : remplace l'action de même clé pas encore commencée (curseur glissé).
    void post(std::function<void()> job, int key = 0);
    void refreshNow();   // relevé dès que possible

private:
    struct Job {
        std::function<void()> run;
        int key = 0;
    };
    void loop();
    HWND notify_ = nullptr;
    UINT msg_ = 0;
    DWORD interval_ = 2000;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Job> jobs_;
    bool stopping_ = false, refresh_ = false;
};

} // namespace md
