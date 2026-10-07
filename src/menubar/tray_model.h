// Icônes des autres apps (zone de notification relayée par le mod Windhawk) : liste et messages de clic (logique pure).
#pragma once
#include <windows.h>

#include <cstdint>
#include <functional>
#include <vector>

#include "../ipc/protocol.h"

namespace md {

struct TrayIcon {
    ipc::TrayIconEvent e;
    std::uint64_t order = 0;   // ordre d'ajout : la plus récente est à gauche
};

class TrayModel {
public:
    void update(const ipc::TrayIconEvent& e);   // ajoute ou remplace (clé : guid, sinon hwnd et uid)
    void remove(const ipc::TrayIconEvent& e);
    void clear();
    bool prune(const std::function<bool(std::uint64_t hwnd)>& alive);   // true si une icône est partie
    std::vector<const TrayIcon*> visible() const;   // ni masquées ni sans image, la plus récente d'abord
    const TrayIcon* find(const ipc::TrayIconEvent& key) const;

private:
    std::vector<TrayIcon> icons_;
    std::uint64_t next_ = 0;
};

struct TrayPost {
    WPARAM wp;
    LPARAM lp;
    UINT msg;
};

// Messages à poster à e.hwnd pour un clic (bouton 0 gauche, 1 droit) au point écran pt, au format de e.version.
std::vector<TrayPost> trayClick(const ipc::TrayIconEvent& e, int button, POINT pt);

} // namespace md
