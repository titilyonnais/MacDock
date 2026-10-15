// Fenêtres de macOS 26 (plan 72), logique pure : une fenêtre qu'on agrandit se remplit entre la barre des menus et le
// Dock, comme une fenêtre glissée contre le haut de l'écran sur macOS.
#pragma once
#include <windows.h>

#include <map>

namespace md {

// Fenêtre que MacDock remplit ou met en plein écran : de premier niveau, à barre de titre, bord redimensionnable et
// bouton agrandir, ni outil ni enfant, d'un autre processus.
bool fillCandidate(LONG style, LONG exStyle, bool ownProcess);

enum class MaximizeReply { Ignore, Fill, Previous };
// Remplir depuis l'agrandissement : toute la zone entre la barre des menus et le Dock, sans marges (comme une fenêtre
// glissée contre le haut sur macOS ; remarque du 12 octobre).
constexpr int kMaximizeFillMarginPt = 0;
// Fenêtre passée à l'état agrandi : rien (réglage coupé, fenêtre exclue ou app qui insiste), Remplir, ou retour à la
// taille d'avant quand elle était déjà remplie par MacDock (bascule du double-clic de macOS).
MaximizeReply replyToMaximize(bool enabled, bool candidate, bool givenUp, bool wasFilled);

// App qui se réagrandit d'elle-même : agrandie de nouveau moins de kRefillMs après notre remplissage (trop tôt pour un
// double-clic de la personne : ce n'est pas la bascule vers la taille d'avant), ou moins de kAppRefillMs après sans la
// moindre entrée de la personne depuis (`lastInput`, même base de temps que `now` ; relecture I3) ; deux fois de suite →
// laissée agrandie (jusqu'à ce que la fenêtre soit oubliée).
class FillGuard {
public:
    static constexpr unsigned long long kRefillMs = 700, kAppRefillMs = 2000;
    bool givenUp(HWND h) const;
    void filled(HWND h, unsigned long long now);      // nous venons de la remplir
    // Elle vient de passer à l'état agrandi ; true : par l'app, aussitôt après nous.
    bool maximized(HWND h, unsigned long long now, unsigned long long lastInput = ~0ull);
    void forget(HWND h);
    void purge();   // fenêtres fermées

private:
    struct State {
        unsigned long long filledAt = 0;
        bool filledOnce = false;
        int quick = 0;   // réagrandissements rapides de suite
    };
    std::map<HWND, State> states_;
};

// État agrandi des fenêtres suivies : seul le passage de « non agrandie » à « agrandie » compte. Une fenêtre inconnue
// (agrandie avant MacDock, ou à l'ouverture de son app) est seulement notée.
class ZoomTracker {
public:
    // true : elle vient de passer à l'état agrandi. Réduite (`iconic`, IsZoomed faux) : rien ne change, une fenêtre
    // agrandie qu'on réduit puis qu'on rouvre reste agrandie (relecture I1).
    bool changed(HWND h, bool zoomed, bool iconic = false);
    void seed(HWND h, bool zoomed);
    void forget(HWND h);
    void clear() { zoomed_.clear(); }

private:
    std::map<HWND, bool> zoomed_;
};

} // namespace md
