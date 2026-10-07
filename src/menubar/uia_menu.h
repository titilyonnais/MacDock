// Vrais menus lus par UI Automation : apps sans barre de menus Win32 mais avec une barre de menus accessible
// (Bloc-notes de Windows 11, Qt…). Les requêtes passent par un fil de travail : une app lente ou figée ne bloque
// jamais la barre.
#pragma once
#include <windows.h>

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "win32_menu.h"

struct IUIAutomation;
struct IUIAutomationElement;

namespace md {

// false pour les fenêtres jamais interrogées : Chromium, Electron et Firefox (une requête y active tout l'arbre
// d'accessibilité et ralentit l'app), Explorateur et bureau (menus propres), classe inconnue.
bool shouldProbeUia(std::wstring_view className);

// Lecteur UI Automation ; un seul fil : celui qui l'a initialisé (COM MTA).
class UiaMenus {
public:
    UiaMenus() = default;
    UiaMenus(const UiaMenus&) = delete;
    UiaMenus& operator=(const UiaMenus&) = delete;
    ~UiaMenus();

    bool init();   // délais : connexion 1 s, transaction 1,5 s
    // Titres de la barre de menus de la fenêtre (hors barre système), sans leurs entrées ; vide s'il n'y en a pas.
    std::vector<RawMenuItem> titles(HWND window);
    // Entrées du titre à cette position : déplie le menu de l'app, lit ses entrées, le replie.
    std::optional<std::vector<RawMenuItem>> items(HWND window, int title);
    // Déplie le chemin (titre, entrée, sous-entrée…) et invoque la dernière entrée ; name la vérifie.
    bool invoke(HWND window, const std::vector<int>& path, const std::wstring& name);

private:
    IUIAutomationElement* menuBar(HWND window);   // référence ajoutée, ou nullptr
    IUIAutomation* uia_ = nullptr;
};

// Fil de travail des requêtes UI Automation, dans l'ordre d'arrivée.
class UiaWorker {
public:
    using Job = std::function<void(UiaMenus&)>;
    UiaWorker() = default;
    UiaWorker(const UiaWorker&) = delete;
    UiaWorker& operator=(const UiaWorker&) = delete;
    ~UiaWorker() { stop(); }

    bool start();
    void stop();   // attend la fin du travail en cours (au plus les délais d'UI Automation)
    void post(Job job);
    // Attend la fin du travail au plus timeoutMs ; false si dépassé (il s'exécute quand même : ce qu'il capture
    // doit lui survivre).
    bool call(Job job, DWORD timeoutMs);

private:
    void loop();
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Job> jobs_;
    bool stopping_ = false, ok_ = false;
};

} // namespace md
