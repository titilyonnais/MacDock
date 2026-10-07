// Exécution des actions des menus de la barre : raccourcis envoyés à l'app, commandes de fenêtre, ouverture
// d'URI, navigation de l'Explorateur, actions système (par une interface remplaçable dans les tests).
#pragma once
#include <windows.h>

#include <functional>
#include <string>
#include <vector>

#include "app_menus.h"
#include "foreground_rules.h"

namespace md {

struct SystemActions {
    std::function<void()> sleep, lock, signOut, restart, shutdown;
    std::function<bool(const std::wstring& question)> confirm;   // Oui → true
};
SystemActions realSystemActions();   // system_actions.cpp (exécutable seulement : jamais dans les tests)

// Cible d'une commande : la fenêtre de l'app affichée, retenue quand elle prend le premier plan. Une interface
// ignorée (la barre, ses menus, le Dock, le menu Démarrer) ne la remplace pas.
struct BarTarget {
    HWND window = nullptr;
    std::wstring appId;
};
BarTarget keepTarget(const BarTarget& current, HWND foreground, ForegroundKind kind, const std::wstring& appId);

struct ActionContext {
    BarTarget target;
    std::vector<HWND> appWindows;        // fenêtres de l'app affichée
    std::vector<HWND>* hidden = nullptr; // fenêtres masquées par « Masquer… », rétablies par « Tout afficher »
    std::wstring exePath;                // À propos
};

// false si l'action n'a rien fait (fenêtre disparue, raccourci invalide, refus d'une confirmation).
bool runAction(const MenuAction& a, ActionContext& c, const SystemActions& sys);

} // namespace md
