// Serveur du named pipe : un client (le mod Windhawk) à la fois, battement de cœur toutes les 1 s.
#pragma once
#include <windows.h>

#include <atomic>
#include <functional>
#include <string>
#include <thread>

#include "protocol.h"

namespace md::ipc {

class PipeServer {
public:
    using Handler = std::function<void(const Message&)>;   // appelé sur le thread du pipe

    ~PipeServer() { stop(); }
    bool start(const std::wstring& name, Handler handler);
    void stop();   // envoie Goodbye au client puis ferme
    bool clientConnected() const { return connected_; }

private:
    void run();
    bool writeMessage(HANDLE pipe, const Message& m);

    std::wstring name_;
    Handler handler_;
    std::thread thread_;
    HANDLE stopEvent_ = nullptr;
    std::atomic<bool> connected_{false};
};

} // namespace md::ipc
