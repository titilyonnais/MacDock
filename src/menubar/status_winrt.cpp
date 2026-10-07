#include "status_winrt.h"

#include <windows.h>
#include <winrt/Windows.Devices.Radios.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>

#include "../core/log.h"

namespace md {
namespace {

using namespace winrt::Windows::Devices::Radios;
using namespace winrt::Windows::Media::Control;

GlobalSystemMediaTransportControlsSession currentSession() {
    auto manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
    return manager ? manager.GetCurrentSession() : nullptr;
}

} // namespace

RadioInfo readRadios() {
    RadioInfo info;
    try {
        for (const auto& r : Radio::GetRadiosAsync().get()) {
            const bool on = r.State() == RadioState::On;
            if (r.Kind() == RadioKind::WiFi) {
                info.wifiPresent = true;
                info.wifiOn = info.wifiOn || on;
            } else if (r.Kind() == RadioKind::Bluetooth) {
                info.btPresent = true;
                info.btOn = info.btOn || on;
            }
        }
    } catch (const winrt::hresult_error& e) {
        log::warn(L"Barre : radios illisibles (0x%08X)", static_cast<unsigned>(e.code()));
    }
    return info;
}

bool setRadio(bool bluetooth, bool on) {
    try {
        if (Radio::RequestAccessAsync().get() != RadioAccessStatus::Allowed) return false;
        bool done = false;
        for (const auto& r : Radio::GetRadiosAsync().get()) {
            if (r.Kind() != (bluetooth ? RadioKind::Bluetooth : RadioKind::WiFi)) continue;
            done = r.SetStateAsync(on ? RadioState::On : RadioState::Off).get() == RadioAccessStatus::Allowed || done;
        }
        return done;
    } catch (const winrt::hresult_error& e) {
        log::warn(L"Barre : radio non modifiée (0x%08X)", static_cast<unsigned>(e.code()));
        return false;
    }
}

MediaInfo readMedia() {
    MediaInfo info;
    try {
        auto session = currentSession();
        if (!session) return info;
        auto props = session.TryGetMediaPropertiesAsync().get();
        info.present = true;
        if (props) {
            info.title = props.Title().c_str();
            info.artist = props.Artist().c_str();
        }
        auto playback = session.GetPlaybackInfo();
        info.playing = playback &&
                       playback.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
    } catch (const winrt::hresult_error&) {
        return {};   // session disparue entre deux appels : rien à montrer
    }
    return info;
}

bool mediaCommand(int button) {
    try {
        auto session = currentSession();
        if (!session) return false;
        switch (button) {
            case 0: return session.TrySkipPreviousAsync().get();
            case 1: return session.TryTogglePlayPauseAsync().get();
            case 2: return session.TrySkipNextAsync().get();
            default: return false;
        }
    } catch (const winrt::hresult_error&) {
        return false;
    }
}

} // namespace md
