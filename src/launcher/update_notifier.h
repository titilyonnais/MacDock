// Fil de mise à jour du lanceur (plan 53) : première recherche une minute après le démarrage (MACDOCK_UPDATE_DELAY en
// secondes pour les essais), puis dès que 12 heures sont passées. Une version prête est annoncée par une notification
// de Windows (icône du lanceur, relayée dans la barre des menus) ; un clic dessus, ou sur l'icône, l'installe tout de
// suite : l'installateur quitte MacDock, le remplace et le relance. Sans clic, elle s'installe au prochain démarrage.
#pragma once
#include <windows.h>

#include <string>
#include <thread>

namespace md {

class UpdateNotifier {
public:
    ~UpdateNotifier() { stop(); }
    void start(HINSTANCE instance);   // « Rechercher automatiquement » (update.json) relu à chaque passage
    // Arrêt : une recherche en cours s'interrompt au plus tôt (attente du verrou, téléchargement) ; au plus `waitMs`.
    // false : le fil est encore pris dans le réseau ; le processus doit alors se terminer tout de suite (ExitProcess),
    // sans détruire cet objet.
    bool stop(unsigned waitMs = 3000);

private:
    void run();
    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    void onTimer();
    void showReady(const std::wstring& version);
    void removeIcon();
    void installNow();

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    std::thread thread_;
    HANDLE stop_ = nullptr;   // créé avant le fil : un arrêt demandé aussitôt après le départ n'est jamais perdu
    bool iconShown_ = false;
    std::wstring announced_;   // version déjà annoncée (une seule notification par version)
};

} // namespace md
