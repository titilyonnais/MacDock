// Réseau de la barre de menus : Wi-Fi (WlanAPI : interface, réseau connecté, signal, réseaux visibles, connexion
// par profil) et Ethernet (cartes actives). Lent : fil de travail (StatusHub).
#pragma once
#include <string>
#include <vector>

namespace md {

struct WifiNetwork {
    std::wstring ssid;
    int quality = 0;   // 0..100
    bool secured = false, known = false, connected = false;
};

struct NetworkInfo {
    bool wifiInterface = false;   // une carte Wi-Fi existe
    bool wifiOn = false;          // sa radio est allumée
    bool ethernet = false;        // une carte Ethernet est connectée
    std::wstring ssid;            // réseau Wi-Fi connecté
    int quality = 0;
    std::vector<WifiNetwork> networks;   // triés (sortNetworks)
};

int wifiBars(int quality);   // 0..3, comme les arcs de l'icône
// Connecté d'abord, puis les réseaux connus, puis par signal ; un seul par SSID (le meilleur), réseaux masqués exclus.
std::vector<WifiNetwork> sortNetworks(std::vector<WifiNetwork> list);

NetworkInfo readNetwork();
bool connectWifi(const std::wstring& ssid);   // réseau connu (profil enregistré) seulement

} // namespace md
