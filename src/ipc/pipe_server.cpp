#include "pipe_server.h"

#include "../core/log.h"

namespace md::ipc {

namespace {
constexpr DWORD kHeartbeatMs = 1000;

struct Event {
    HANDLE h = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ~Event() { if (h) CloseHandle(h); }
};
} // namespace

bool PipeServer::start(const std::wstring& name, Handler handler) {
    if (thread_.joinable()) return true;
    name_ = name;
    handler_ = std::move(handler);
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stopEvent_) return false;
    thread_ = std::thread([this] { run(); });
    return true;
}

void PipeServer::stop() {
    if (!thread_.joinable()) return;
    SetEvent(stopEvent_);
    thread_.join();
    CloseHandle(stopEvent_);
    stopEvent_ = nullptr;
}

bool PipeServer::writeMessage(HANDLE pipe, const Message& m) {
    auto bytes = encode(m);
    Event ev;
    OVERLAPPED ov{};
    ov.hEvent = ev.h;
    DWORD written = 0;
    if (!WriteFile(pipe, bytes.data(), DWORD(bytes.size()), nullptr, &ov) && GetLastError() != ERROR_IO_PENDING)
        return false;
    if (WaitForSingleObject(ev.h, 2000) != WAIT_OBJECT_0) {
        CancelIoEx(pipe, &ov);
        GetOverlappedResult(pipe, &ov, &written, TRUE);
        return false;
    }
    return GetOverlappedResult(pipe, &ov, &written, FALSE) && written == bytes.size();
}

void PipeServer::run() {
    while (WaitForSingleObject(stopEvent_, 0) != WAIT_OBJECT_0) {
        HANDLE pipe = CreateNamedPipeW(name_.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                       PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                       1, 4096, 4096, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) {
            log::error(L"CreateNamedPipe a échoué (%lu)", GetLastError());
            if (WaitForSingleObject(stopEvent_, 2000) == WAIT_OBJECT_0) return;
            continue;
        }

        // Attente d'un client.
        Event connectEv;
        OVERLAPPED cov{};
        cov.hEvent = connectEv.h;
        bool connected = false;
        if (ConnectNamedPipe(pipe, &cov)) connected = true;
        else if (GetLastError() == ERROR_PIPE_CONNECTED) connected = true;
        else if (GetLastError() == ERROR_IO_PENDING) {
            HANDLE waits[2] = {connectEv.h, stopEvent_};
            DWORD r = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
            if (r == WAIT_OBJECT_0) {
                DWORD dummy = 0;
                connected = GetOverlappedResult(pipe, &cov, &dummy, FALSE) != FALSE;
            } else {
                CancelIoEx(pipe, &cov);
                CloseHandle(pipe);
                return;
            }
        }
        if (!connected) { CloseHandle(pipe); continue; }

        connected_ = true;
        log::info(L"Mod Windhawk connecté au pipe");
        Decoder decoder;
        Event readEv;
        OVERLAPPED rov{};
        std::uint8_t buf[4096];
        bool readPending = false;
        auto uiAlive = [this] { return !alive_ || alive_(); };
        bool alive = !uiAlive() || writeMessage(pipe, {MsgType::Heartbeat, {}});

        while (alive) {
            if (!readPending) {
                ResetEvent(readEv.h);
                rov = {};
                rov.hEvent = readEv.h;
                if (!ReadFile(pipe, buf, sizeof buf, nullptr, &rov) && GetLastError() != ERROR_IO_PENDING) break;
                readPending = true;
            }
            HANDLE waits[2] = {readEv.h, stopEvent_};
            DWORD r = WaitForMultipleObjects(2, waits, FALSE, kHeartbeatMs);
            if (r == WAIT_TIMEOUT) {
                // Interface figée : pas de battement, le mod réaffichera la barre Windows.
                if (uiAlive()) alive = writeMessage(pipe, {MsgType::Heartbeat, {}});
            } else if (r == WAIT_OBJECT_0) {
                DWORD got = 0;
                readPending = false;
                if (!GetOverlappedResult(pipe, &rov, &got, FALSE)) break;
                decoder.feed(buf, got);
                while (auto m = decoder.next())
                    if (handler_) handler_(*m);
                if (decoder.failed()) {
                    log::warn(L"Flux IPC invalide : déconnexion du client");
                    break;
                }
            } else {
                writeMessage(pipe, {MsgType::Goodbye, {}});
                break;
            }
        }

        if (readPending) {
            CancelIoEx(pipe, &rov);
            DWORD dummy = 0;
            GetOverlappedResult(pipe, &rov, &dummy, TRUE);
        }
        connected_ = false;
        // Pas de DisconnectNamedPipe : il jetterait les données non lues (dont Goodbye).
        // Fermer l'instance laisse le client lire la fin du flux, puis recevoir ERROR_BROKEN_PIPE.
        CloseHandle(pipe);
        log::info(L"Client du pipe déconnecté");
    }
}

} // namespace md::ipc
