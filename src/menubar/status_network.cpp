#include "status_network.h"

#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <wlanapi.h>

#include <algorithm>
#include <memory>

namespace md {
namespace {

std::wstring ssidText(const DOT11_SSID& s) {
    std::string raw(reinterpret_cast<const char*>(s.ucSSID), std::min<ULONG>(s.uSSIDLength, DOT11_SSID_MAX_LENGTH));
    if (raw.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, raw.data(), int(raw.size()), nullptr, 0);
    UINT cp = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    if (n <= 0) {   // SSID non UTF-8 : page de code du système
        cp = CP_ACP;
        flags = 0;
        n = MultiByteToWideChar(cp, flags, raw.data(), int(raw.size()), nullptr, 0);
    }
    std::wstring out(size_t(std::max(n, 0)), L'\0');
    if (n > 0) MultiByteToWideChar(cp, flags, raw.data(), int(raw.size()), out.data(), n);
    return out;
}

struct WlanHandle {
    HANDLE h = nullptr;
    WlanHandle() {
        DWORD version = 0;
        if (WlanOpenHandle(2, nullptr, &version, &h) != ERROR_SUCCESS) h = nullptr;
    }
    ~WlanHandle() {
        if (h) WlanCloseHandle(h, nullptr);
    }
};

template <class T> struct WlanPtr {
    T* p = nullptr;
    ~WlanPtr() {
        if (p) WlanFreeMemory(p);
    }
};

bool ethernetUp() {
    ULONG size = 16 * 1024;
    for (int attempt = 0; attempt < 3; ++attempt) {
        std::unique_ptr<std::uint8_t[]> buf(new std::uint8_t[size]);
        auto* list = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.get());
        const ULONG r = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                             nullptr, list, &size);
        if (r == ERROR_BUFFER_OVERFLOW) continue;
        if (r != NO_ERROR) return false;
        for (auto* a = list; a; a = a->Next)
            if (a->IfType == IF_TYPE_ETHERNET_CSMACD && a->OperStatus == IfOperStatusUp && a->FirstGatewayAddress) return true;
        return false;
    }
    return false;
}

} // namespace

int wifiBars(int quality) {
    if (quality <= 0) return 0;
    if (quality < 34) return 1;
    if (quality < 67) return 2;
    return 3;
}

std::vector<WifiNetwork> sortNetworks(std::vector<WifiNetwork> list) {
    std::erase_if(list, [](const WifiNetwork& n) { return n.ssid.empty(); });
    std::sort(list.begin(), list.end(), [](const WifiNetwork& a, const WifiNetwork& b) {
        if (a.connected != b.connected) return a.connected;
        if (a.known != b.known) return a.known;
        return a.quality > b.quality;
    });
    std::vector<WifiNetwork> out;
    for (auto& n : list)
        if (std::none_of(out.begin(), out.end(), [&](const WifiNetwork& o) { return o.ssid == n.ssid; }))
            out.push_back(std::move(n));
    return out;
}

NetworkInfo readNetwork() {
    NetworkInfo info;
    info.ethernet = ethernetUp();
    WlanHandle wlan;
    if (!wlan.h) return info;
    WlanPtr<WLAN_INTERFACE_INFO_LIST> ifaces;
    if (WlanEnumInterfaces(wlan.h, nullptr, &ifaces.p) != ERROR_SUCCESS || !ifaces.p || ifaces.p->dwNumberOfItems == 0)
        return info;
    const GUID id = ifaces.p->InterfaceInfo[0].InterfaceGuid;
    info.wifiInterface = true;

    DWORD size = 0;
    WlanPtr<WLAN_RADIO_STATE> radio;
    if (WlanQueryInterface(wlan.h, &id, wlan_intf_opcode_radio_state, nullptr, &size, reinterpret_cast<void**>(&radio.p),
                           nullptr) == ERROR_SUCCESS &&
        radio.p && radio.p->dwNumberOfPhys > 0)
        info.wifiOn = radio.p->PhyRadioState[0].dot11SoftwareRadioState == dot11_radio_state_on &&
                      radio.p->PhyRadioState[0].dot11HardwareRadioState == dot11_radio_state_on;

    WlanPtr<WLAN_CONNECTION_ATTRIBUTES> conn;
    if (WlanQueryInterface(wlan.h, &id, wlan_intf_opcode_current_connection, nullptr, &size, reinterpret_cast<void**>(&conn.p),
                           nullptr) == ERROR_SUCCESS &&
        conn.p && conn.p->isState == wlan_interface_state_connected) {
        info.ssid = ssidText(conn.p->wlanAssociationAttributes.dot11Ssid);
        info.quality = int(std::min<ULONG>(conn.p->wlanAssociationAttributes.wlanSignalQuality, 100));
    }

    WlanPtr<WLAN_AVAILABLE_NETWORK_LIST> nets;
    if (WlanGetAvailableNetworkList(wlan.h, &id, 0, nullptr, &nets.p) == ERROR_SUCCESS && nets.p) {
        std::vector<WifiNetwork> list;
        for (DWORD i = 0; i < nets.p->dwNumberOfItems; ++i) {
            const auto& n = nets.p->Network[i];
            WifiNetwork w;
            w.ssid = ssidText(n.dot11Ssid);
            w.quality = int(std::min<ULONG>(n.wlanSignalQuality, 100));
            w.secured = n.bSecurityEnabled != FALSE;
            w.known = (n.dwFlags & WLAN_AVAILABLE_NETWORK_HAS_PROFILE) != 0;
            w.connected = (n.dwFlags & WLAN_AVAILABLE_NETWORK_CONNECTED) != 0;
            list.push_back(std::move(w));
        }
        info.networks = sortNetworks(std::move(list));
    }
    return info;
}

bool connectWifi(const std::wstring& ssid) {
    WlanHandle wlan;
    if (!wlan.h || ssid.empty()) return false;
    WlanPtr<WLAN_INTERFACE_INFO_LIST> ifaces;
    if (WlanEnumInterfaces(wlan.h, nullptr, &ifaces.p) != ERROR_SUCCESS || !ifaces.p || ifaces.p->dwNumberOfItems == 0)
        return false;
    const GUID id = ifaces.p->InterfaceInfo[0].InterfaceGuid;
    WlanPtr<WLAN_AVAILABLE_NETWORK_LIST> nets;
    if (WlanGetAvailableNetworkList(wlan.h, &id, 0, nullptr, &nets.p) != ERROR_SUCCESS || !nets.p) return false;
    for (DWORD i = 0; i < nets.p->dwNumberOfItems; ++i) {
        const auto& n = nets.p->Network[i];
        if (ssidText(n.dot11Ssid) != ssid || !(n.dwFlags & WLAN_AVAILABLE_NETWORK_HAS_PROFILE)) continue;
        WLAN_CONNECTION_PARAMETERS p{};
        p.wlanConnectionMode = wlan_connection_mode_profile;
        p.strProfile = n.strProfileName;
        p.dot11BssType = n.dot11BssType;
        return WlanConnect(wlan.h, &id, &p, nullptr) == ERROR_SUCCESS;
    }
    return false;
}

} // namespace md
