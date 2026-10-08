// Feux tricolores à l'écran : un petit calque par fenêtre à barre de titre, posé à la place de ses boutons réduire /
// agrandir / fermer, juste au-dessus d'elle dans l'ordre d'affichage, qui la suit quand elle bouge et lui envoie ses
// commandes système. Celles de la fenêtre active sont en couleur, celles des autres grises (en couleur au survol),
// comme sur macOS. Ces boutons (de Windows, ou dessinés par l'app : Chromium, Electron…) sont repérés par DWM ou en
// sondant la fenêtre (WM_NCHITTEST) ; le calque les recouvre, rien du contenu de l'app n'est caché.
#pragma once
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <thread>
#include <vector>

#include "traffic_lights.h"

namespace md {

UINT effectiveDpi(HWND h);            // DPI réel de l'écran de la fenêtre
LightsWindowInfo readInfo(HWND h);    // style, cadre, zone client, processus… d'une fenêtre de premier niveau

// Les pastilles vivent sur leur propre fil : la sonde des boutons (jusqu'à 60 ms d'attente d'une app occupée) ne fige
// jamais la barre de menus. Les méthodes publiques postent à ce fil.
class TrafficWindow {
public:
    ~TrafficWindow() { destroy(); }
    bool create(HINSTANCE instance);   // lance le fil
    // Fenêtre active (nullptr : aucune), en couleur ; les autres fenêtres visibles à barre de titre ont leurs pastilles
    // grises. Même fenêtre et même mode : couleurs sous les pastilles remesurées (thème changé…). Off : aucune.
    void attach(HWND active, LightsMode mode);
    void destroy();
    HWND target() const { return publicTarget_.load(); }
    // Capture d'écran du Dock (⊞⇧3, ⊞⇧4) : calques visibles aux captures le temps de la copie. Synchrone (le fil
    // répond en moins de 200 ms, sinon on n'attend plus).
    void setCaptureVisible(bool on);

private:
    enum class Spot { None, Over };   // pas de pastilles (aucun bouton trouvé), ou sur les boutons de la fenêtre
    struct Placement {
        HWND target = nullptr;
        SIZE size{};
        bool zoomed = false, valid = false;
        UINT dpi = 0;
        Spot spot = Spot::None;
        RECT buttons{};          // boutons de la fenêtre, relatifs au coin haut droit du cadre
    };
    // Le calque d'une fenêtre et tout ce qui la concerne.
    struct Layer {
        HWND hwnd = nullptr, target = nullptr;
        DWORD pid = 0;
        LightsLayout layout{};
        LightsState state{};
        Placement placement{};
        Spot spot = Spot::None;
        double scale = 1;
        bool shown = false, tracking = false, painted = false;
        int pressed = -1;
        bool dragging = false;          // déplacement de la cible depuis le fond (notre propre boucle)
        POINT dragStart{};
        RECT dragFrom{};
        SIZE paintedSize{};
        ULONGLONG bounceStart = 0;
        int probeRetries = 0;           // sondes interrompues (app occupée) reprises au plus 3 fois
        bool refused = false;           // visible mais sans barre de titre (plein écran) : sa barre peut revenir
        ULONGLONG revealAt = 0;         // pastilles retenues jusqu'à cet instant (sortie du plein écran, zoom)
        bool removing = false;          // détruit par nous : pas de recréation
    };

    void run();
    void doAttach(HWND active, LightsMode mode);
    void sync();                        // toutes les fenêtres de premier niveau visibles
    Layer* consider(HWND target);       // calque créé si la fenêtre veut des pastilles
    Layer* layerOf(HWND target);
    void remove(HWND target);
    void removeAll();
    bool createLayer(Layer& l);
    void hookProcess(DWORD pid);
    void unhookProcess(DWORD pid);
    void hookGlobal();
    void unhookGlobal();
    void restack();                     // chaque calque juste au-dessus de sa fenêtre
    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    static void CALLBACK onEvent(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD);
    // deferred : reporté (servi plus tard par la boucle) ; l'état de la fenêtre est alors relu plutôt que supposé.
    void event(DWORD event, HWND hwnd, bool deferred = false);
    void purgeHidden();               // calques de fenêtres fermées ou invisibles retirés (plafond atteint)
    LRESULT handle(Layer& l, UINT msg, WPARAM wp, LPARAM lp);
    void place(Layer& l, bool resample, bool probe = false);   // relit la cible, décide, dessine, replace le calque
    Placement measure(const Layer& l, const LightsWindowInfo& info, UINT dpi, bool& complete);
    void hide(Layer& l);
    void sample(Layer& l, const RECT& frame, UINT dpi);
    void paint(Layer& l);
    void raise(Layer& l);              // juste au-dessus de sa cible
    static void moveLayer(HWND layer, const RECT& screenRect);

    std::map<HWND, std::unique_ptr<Layer>> layers_;   // par fenêtre cible
    std::map<HWND, Layer*> byLayer_;                  // par calque
    std::map<DWORD, std::pair<HWINEVENTHOOK, int>> processHooks_;   // déplacements, par processus suivi
    std::vector<HWINEVENTHOOK> globalHooks_;
    HWND active_ = nullptr;
    LightsMode mode_ = LightsMode::Off;
    // Pendant une lecture ou une sonde, les attentes de réponse d'une app laissent passer les WinEvent : ils sont
    // reportés après (message au fil) plutôt que traités au milieu.
    int busy_ = 0;
    bool restackPending_ = false;
    bool captureVisible_ = false;    // capture d'écran en cours : calques visibles aux captures
    HANDLE captureDone_ = nullptr;   // signalé par le fil une fois l'affichage des calques changé
    bool quitting_ = false;
    HINSTANCE instance_ = nullptr;
    std::thread thread_;
    std::atomic<DWORD> threadId_{0};
    std::atomic<HWND> publicTarget_{nullptr};
    static TrafficWindow* self_;
};

} // namespace md
