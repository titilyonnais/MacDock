// Spotlight, façon macOS Tahoe : champ de recherche en verre au tiers haut de l'écran, résultats dessous dans le
// même panneau. API modale, comme AppsWindow : Échap, un clic ailleurs (perte d'activation) ou closeOpen() la ferment.
#pragma once
#include <windows.h>

#include <optional>
#include <string>
#include <vector>

#include "../apps/apps_window.h"
#include "../core/bgra_image.h"
#include "../popup/menu_window.h"
#include "spot_results.h"

namespace md {

class SpotlightWindow {
public:
    struct Request {
        std::vector<AppEntry> apps;   // catalogue (catalogFrom)
        HMONITOR monitor = nullptr;   // écran du curseur
        std::wstring profile;         // dossier où chercher les documents
        AppsIconStyle icons;          // allure des icônes d'apps (celle du Dock)
    };
    struct Choice {
        SpotItem item;
        bool reveal = false;   // Ctrl+Entrée ou Ctrl+clic sur un document : le montrer dans l'Explorateur
    };
    // nullopt : fermé sans choix, ou ouverture impossible.
    static std::optional<Choice> track(const MenuWindow::Env& env, const Request& request);
    static bool isOpen();
    static void closeOpen();   // ferme le panneau ouvert (second appui sur le raccourci)
};

// Même dessin hors écran (Direct2D sur une bitmap, aucune fenêtre) : fond d'écran Tahoe, panneau dépoli, cases de
// couleur à la place des icônes.
BgraImage spotlightSnapshot(const std::wstring& query, const std::vector<AppEntry>& apps,
                            const std::vector<SpotItem>& files, bool dark, int width, int height);

} // namespace md
