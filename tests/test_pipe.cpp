#include <windows.h>

#include <atomic>

#include "minitest.h"
#include "../src/ipc/pipe_server.h"

TEST_CASE(pipe_heartbeat_and_client_message) {
    std::wstring name = L"\\\\.\\pipe\\MacDockTest-" + std::to_wstring(GetCurrentProcessId());
    std::atomic<int> flashes{0};
    md::ipc::PipeServer server;
    REQUIRE(server.start(name, [&](const md::ipc::Message& m) {
        if (md::ipc::parseFlash(m)) ++flashes;
    }));

    HANDLE client = INVALID_HANDLE_VALUE;
    for (int i = 0; i < 50 && client == INVALID_HANDLE_VALUE; ++i) {
        client = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (client == INVALID_HANDLE_VALUE) Sleep(20);
    }
    REQUIRE(client != INVALID_HANDLE_VALUE);

    // Battement de cœur reçu immédiatement à la connexion.
    std::uint8_t buf[64];
    DWORD got = 0;
    REQUIRE(ReadFile(client, buf, 12, &got, nullptr));
    md::ipc::Decoder d;
    d.feed(buf, got);
    auto hb = d.next();
    REQUIRE(hb.has_value());
    CHECK(hb->type == md::ipc::MsgType::Heartbeat);

    auto bytes = md::ipc::encode(md::ipc::makeFlash({42}));
    DWORD written = 0;
    CHECK(WriteFile(client, bytes.data(), DWORD(bytes.size()), &written, nullptr));
    for (int i = 0; i < 100 && flashes == 0; ++i) Sleep(10);
    CHECK_EQ(flashes.load(), 1);
    CHECK(server.clientConnected());

    // À l'arrêt, le serveur envoie Goodbye.
    server.stop();
    bool goodbye = false;
    while (ReadFile(client, buf, sizeof buf, &got, nullptr) && got > 0) {
        d.feed(buf, got);
        while (auto m = d.next()) goodbye |= m->type == md::ipc::MsgType::Goodbye;
        if (goodbye) break;
    }
    CHECK(goodbye);
    CloseHandle(client);
}
