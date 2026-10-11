// Fenêtres de macOS 26 (plan 72), logique pure : une fenêtre qu'on agrandit se remplit entre la barre des menus et le
// Dock (comme une fenêtre glissée contre le haut de l'écran sur macOS), la pastille verte passe en vrai plein écran.
#pragma once
#include <windows.h>

#include <map>

namespace md {

// Fenêtre que MacDock remplit ou met en plein écran : de premier niveau, à barre de titre, bord redimensionnable et
// bouton agrandir, ni outil ni enfant, d'un autre processus.
bool fillCandidate(LONG style, LONG exStyle, bool ownProcess);

enum class MaximizeReply { Ignore, Fill, Previous };
// Fenêtre passée à l'état agrandi : rien (réglage coupé, fenêtre exclue ou app qui insiste), Remplir, ou retour à la
// taille d'avant quand elle était déjà remplie par MacDock (bascule du double-clic de macOS).
MaximizeReply replyToMaximize(bool enabled, bool candidate, bool givenUp, bool wasFilled);

// App qui se réagrandit d'elle-même : agrandie de nouveau moins de kRefillMs après notre remplissage (trop tôt pour un
// double-clic de la personne : ce n'est pas la bascule vers la taille d'avant), deux fois de suite → laissée agrandie
// (jusqu'à ce que la fenêtre soit oubliée).
class FillGuard {
public:
    static constexpr unsigned long long kRefillMs = 700;
    bool givenUp(HWND h) const;
    void filled(HWND h, unsigned long long now);      // nous venons de la remplir
    bool maximized(HWND h, unsigned long long now);   // elle vient de passer à l'état agrandi ; true : aussitôt après nous
    void forget(HWND h);

private:
    struct State {
        unsigned long long filledAt = 0;
        bool filledOnce = false;
        int quick = 0;   // réagrandissements rapides de suite
    };
    std::map<HWND, State> states_;
};

enum class FullscreenStep { Enter, Exit };
// Pastille verte : sortie si MacDock l'a mise en plein écran et qu'elle couvre toujours son écran (à 1 px près),
// entrée sinon (déplacée depuis : l'ancien état est oublié).
FullscreenStep fullscreenStep(bool saved, const RECT& frame, const RECT& monitor);

// État agrandi des fenêtres suivies : seul le passage de « non agrandie » à « agrandie » compte. Une fenêtre inconnue
// (agrandie avant MacDock, ou à l'ouverture de son app) est seulement notée.
class ZoomTracker {
public:
    bool changed(HWND h, bool zoomed);   // true : elle vient de passer à l'état agrandi
    void seed(HWND h, bool zoomed);
    void forget(HWND h);
    void clear() { zoomed_.clear(); }

private:
    std::map<HWND, bool> zoomed_;
};

} // namespace md
