// Barre de menus : sources d'état (son, alimentation, réseau, radios, lecture en cours, luminosité). Les lectures
// réelles ne changent rien : aucun réglage de l'utilisateur n'est modifié.
#include <windows.h>
#include <objbase.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <wrl/client.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

#include "minitest.h"
#include "../src/menubar/status_audio.h"
#include "../src/menubar/status_hub.h"
#include "../src/menubar/status_network.h"
#include "../src/menubar/status_power.h"
#include "../src/menubar/status_winrt.h"

TEST_CASE(status_power_from_system) {
    SYSTEM_POWER_STATUS desktop{};
    desktop.ACLineStatus = 1;
    desktop.BatteryFlag = 128;   // pas de batterie système
    desktop.BatteryLifePercent = 255;
    auto d = md::batteryFrom(desktop);
    CHECK(!d.present);
    CHECK(d.onAC);

    SYSTEM_POWER_STATUS laptop{};
    laptop.ACLineStatus = 1;
    laptop.BatteryFlag = 8;   // en charge
    laptop.BatteryLifePercent = 87;
    auto l = md::batteryFrom(laptop);
    CHECK(l.present);
    CHECK(l.charging);
    CHECK_EQ(l.percent, 87);

    laptop.ACLineStatus = 0;
    laptop.BatteryFlag = 2;   // faible, sur batterie
    laptop.BatteryLifePercent = 12;
    l = md::batteryFrom(laptop);
    CHECK(!l.charging);
    CHECK(!l.onAC);
    CHECK_EQ(l.percent, 12);

    SYSTEM_POWER_STATUS unknown{};
    unknown.BatteryFlag = 255;
    CHECK(!md::batteryFrom(unknown).present);
}

TEST_CASE(status_wifi_bars_and_sort) {
    CHECK_EQ(md::wifiBars(0), 0);
    CHECK_EQ(md::wifiBars(20), 1);
    CHECK_EQ(md::wifiBars(55), 2);
    CHECK_EQ(md::wifiBars(100), 3);
    std::vector<md::WifiNetwork> list = {
        {L"Voisin", 90, true, false, false},
        {L"Maison", 40, true, true, true},
        {L"Bureau", 70, true, true, false},
        {L"Voisin", 30, true, false, false},   // même SSID, autre point d'accès : un seul
        {L"", 80, false, false, false},        // réseau masqué : ignoré
        {L"Café", 95, false, false, false},
    };
    auto s = md::sortNetworks(list);
    REQUIRE(s.size() == 4);
    CHECK(s[0].ssid == L"Maison");   // connecté d'abord
    CHECK(s[1].ssid == L"Bureau");   // puis les réseaux connus
    CHECK(s[2].ssid == L"Café");     // puis par signal
    CHECK(s[3].ssid == L"Voisin");
    CHECK_EQ(s[3].quality, 90);      // le meilleur point d'accès du SSID
}

TEST_CASE(status_audio_reads_default_output) {
    struct Com {   // libéré même si le test est sauté
        Com() { CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); }
        ~Com() { CoUninitialize(); }
    } com;
    {
        md::AudioStatus a;
        const bool ready = a.init();
        SKIP_ON_CI_IF(!ready, "aucune sortie audio sur la machine de CI");
        REQUIRE(ready);   // ce poste a des sorties audio
        const float v = a.volume();
        CHECK(v >= 0 && v <= 1);
        auto outs = a.outputs();
        REQUIRE(!outs.empty());
        int defaults = 0;
        for (const auto& o : outs) {
            CHECK(!o.name.empty());
            CHECK(!o.id.empty());
            defaults += o.isDefault;
        }
        CHECK_EQ(defaults, 1);
        CHECK(!a.setDefault(L"{identifiant-inexistant}"));   // rien n'est changé
    }
}

TEST_CASE(status_network_reads_without_crash) {
    auto n = md::readNetwork();
    if (!n.wifiInterface) {
        CHECK(n.networks.empty());
        CHECK(n.ssid.empty());
    }
    CHECK(n.quality >= 0 && n.quality <= 100);
}

TEST_CASE(status_winrt_reads_on_worker) {
    md::RadioInfo radios;
    md::MediaInfo media;
    bool ran = false;
    std::thread t([&] {   // comme le fil de StatusHub : jamais d'attente WinRT sur un fil STA
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        radios = md::readRadios();
        media = md::readMedia();
        ran = true;
        CoUninitialize();
    });
    t.join();
    CHECK(ran);
    CHECK(radios.wifiPresent || !radios.wifiOn);
    CHECK(radios.btPresent || !radios.btOn);
    CHECK(media.present || media.title.empty());
}

namespace {
constexpr UINT kSnapshotMsg = WM_APP + 9;
}

TEST_CASE(status_hub_posts_snapshots) {
    HWND sink = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
    REQUIRE(sink != nullptr);
    md::StatusHub hub;
    REQUIRE(hub.start(sink, kSnapshotMsg, 200));
    auto log = std::make_shared<std::vector<int>>();
    auto mutex = std::make_shared<std::mutex>();
    auto add = [log, mutex](int v) {
        std::lock_guard lock(*mutex);
        log->push_back(v);
    };
    hub.post([add] {
        Sleep(300);
        add(1);
    });
    hub.post([add] { add(2); }, 7);
    hub.post([add] { add(3); }, 7);   // remplace le travail 2, pas encore commencé
    int snapshots = 0;
    for (const ULONGLONG until = GetTickCount64() + 5000; GetTickCount64() < until && snapshots < 2;) {
        MSG m;
        while (PeekMessageW(&m, sink, kSnapshotMsg, kSnapshotMsg, PM_REMOVE)) {
            std::unique_ptr<md::StatusSnapshot> s(reinterpret_cast<md::StatusSnapshot*>(m.lParam));
            CHECK(s != nullptr);
            ++snapshots;
        }
        Sleep(20);
    }
    hub.stop();
    for (MSG m; PeekMessageW(&m, sink, kSnapshotMsg, kSnapshotMsg, PM_REMOVE);)
        delete reinterpret_cast<md::StatusSnapshot*>(m.lParam);
    DestroyWindow(sink);
    CHECK(snapshots >= 2);
    std::lock_guard lock(*mutex);
    CHECK(*log == (std::vector<int>{1, 3}));
}

TEST_CASE(status_audio_notifier_posts) {   // notification Core Audio : message posté, sans toucher au volume
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    {
        HWND w = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
        REQUIRE(w != nullptr);
        constexpr UINT kMsg = WM_APP + 77;
        auto notifier = md::makeAudioNotifier(w, kMsg);
        REQUIRE(notifier != nullptr);
        Microsoft::WRL::ComPtr<IAudioEndpointVolumeCallback> volume;
        Microsoft::WRL::ComPtr<IMMNotificationClient> devices;
        REQUIRE(SUCCEEDED(notifier.As(&volume)));
        REQUIRE(SUCCEEDED(notifier.As(&devices)));
        MSG m;
        CHECK(!PeekMessageW(&m, w, kMsg, kMsg, PM_REMOVE));
        AUDIO_VOLUME_NOTIFICATION_DATA data{};
        volume->OnNotify(&data);
        CHECK(PeekMessageW(&m, w, kMsg, kMsg, PM_REMOVE));
        devices->OnDefaultDeviceChanged(eCapture, eConsole, L"micro");   // micro : rien
        CHECK(!PeekMessageW(&m, w, kMsg, kMsg, PM_REMOVE));
        devices->OnDefaultDeviceChanged(eRender, eConsole, L"sortie");
        CHECK(PeekMessageW(&m, w, kMsg, kMsg, PM_REMOVE));
        md::AudioStatus audio;
        audio.init();
        if (audio.volume() >= 0) CHECK(audio.watch(w, kMsg));   // ce poste a une sortie
        audio.unwatch();
        DestroyWindow(w);
    }
    CoUninitialize();
}

TEST_CASE(status_hub_paces_slow_reads) {   // luminosité (WMI, DDC/CI) : rarement, sauf à la demande
    CHECK(md::slowReadDue(5000, 0, false));          // premier relevé
    CHECK(!md::slowReadDue(7000, 5000, false));      // relevé périodique suivant : non
    CHECK(md::slowReadDue(7000, 5000, true));        // menu ouvert ou action : oui
    CHECK(md::slowReadDue(5000 + md::kSlowReadMs, 5000, false));
}
