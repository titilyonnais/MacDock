#include "status_hub.h"

#include <objbase.h>

#include <chrono>

#include "status_brightness.h"

namespace md {

bool StatusHub::start(HWND notify, UINT msg, DWORD intervalMs) {
    if (thread_.joinable()) return true;
    notify_ = notify;
    msg_ = msg;
    interval_ = intervalMs;
    stopping_ = false;
    thread_ = std::thread([this] { loop(); });
    return true;
}

void StatusHub::stop() {
    if (!thread_.joinable()) return;
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        jobs_.clear();
    }
    wake_.notify_all();
    thread_.join();
}

void StatusHub::post(std::function<void()> job, int key) {
    {
        std::lock_guard lock(mutex_);
        if (stopping_) return;
        if (key != 0)
            for (auto& j : jobs_)
                if (j.key == key) {
                    j.run = std::move(job);
                    wake_.notify_one();
                    return;
                }
        jobs_.push_back({std::move(job), key});
    }
    wake_.notify_one();
}

void StatusHub::refreshNow() {
    {
        std::lock_guard lock(mutex_);
        refresh_ = true;
    }
    wake_.notify_one();
}

void StatusHub::loop() {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);   // WinRT et WMI : appartement MTA
    for (;;) {
        {
            std::unique_lock lock(mutex_);
            wake_.wait_for(lock, std::chrono::milliseconds(interval_), [this] { return stopping_ || refresh_ || !jobs_.empty(); });
            if (stopping_) break;
            refresh_ = false;
        }
        for (;;) {   // actions en attente, puis un relevé qui en montre l'effet
            Job job;
            {
                std::lock_guard lock(mutex_);
                if (stopping_ || jobs_.empty()) break;
                job = std::move(jobs_.front());
                jobs_.pop_front();
            }
            if (job.run) job.run();
        }
        {
            std::lock_guard lock(mutex_);
            if (stopping_) break;
        }
        auto* s = new StatusSnapshot;
        s->network = readNetwork();
        s->radios = readRadios();
        s->media = readMedia();
        s->brightness = readBrightness();
        s->battery = readBattery();
        if (!notify_ || !PostMessageW(notify_, msg_, 0, reinterpret_cast<LPARAM>(s))) delete s;
    }
    if (SUCCEEDED(com)) CoUninitialize();
}

} // namespace md
