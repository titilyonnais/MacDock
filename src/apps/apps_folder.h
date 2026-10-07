// Lecture du dossier Shell « Apps » (shell:AppsFolder) : les apps du menu Démarrer, Win32 et Store.
#pragma once
#include <windows.h>

#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include "app_catalog.h"

namespace md {

// Brut (ni filtré ni trié) ; COM initialisé par l'appelant ; vide si le dossier est illisible.
std::vector<AppEntry> readAppsFolder();

// Catalogue lu dans un fil à part (jusqu'à une demi-seconde) et gardé pour la prochaine ouverture.
class AppCatalog {
public:
    ~AppCatalog();
    void refreshAsync();   // sans effet si une lecture est déjà en cours
    // Dernière liste lue (catalogFrom), en attendant au plus waitMs la première lecture.
    std::vector<AppEntry> get(DWORD waitMs);

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<AppEntry> apps_;
    bool loaded_ = false, running_ = false;
    std::thread thread_;
};

} // namespace md
