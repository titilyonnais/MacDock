// Fil de mise à jour du lanceur (plan 53) : première recherche une minute après le démarrage (MACDOCK_UPDATE_DELAY en
// secondes pour les essais), puis dès que 12 heures sont passées. Une version prête est annoncée par une notification
// de Windows (icône du lanceur, relayée dans la barre des menus) ; un clic dessus, ou sur l'icône, l'installe tout de
// suite : l'installateur quitte MacDock, le remplace et le relance. Sans clic, elle s'installe au prochain démarrage.
#pragma once
#include <windows.h>

#include <atomic>
#include <string>
#include <thread>

namespace md {

class UpdateNotifier {
public:
    ~UpdateNotifier() { stop(); }
    void start(HINSTANCE instance);   // rien si « Rechercher automatiquement » est coupé (update.json)
    void stop();

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
    std::atomic<DWORD> threadId_{0};
    bool iconShown_ = false;
    std::wstring announced_;   // version déjà annoncée (une seule notification par version)
};

} // namespace md
