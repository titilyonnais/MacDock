// Sélecteur d'apps (logique pure) : historique d'activation, pas, rangement du panneau, raccourci.
#pragma once
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "../spotlight/spot_results.h"

namespace md {

// Apps de la plus récemment activée à la plus ancienne (64 au plus).
class AppMru {
public:
    void touch(const std::wstring& appId);
    // Les apps de running déjà activées, de la plus récente à la plus ancienne, puis les autres dans l'ordre reçu.
    std::vector<std::wstring> order(const std::vector<std::wstring>& running) const;

private:
    std::vector<std::wstring> recent_;
};

std::size_t switcherStart(std::size_t count);   // la deuxième app (la précédente), ou la seule
std::size_t switcherStep(std::size_t selected, std::size_t count, int delta);   // en boucle

struct SwitcherGeometry {   // points, depuis le coin du panneau
    double icon = 64, cell = 88, pad = 12, labelH = 28, width = 0, height = 0;
};
// Rangée de count cases ; icônes réduites pour que le panneau tienne dans 90 % de maxWidth.
SwitcherGeometry switcherLayout(std::size_t count, double maxWidth);
int switcherHit(const SwitcherGeometry& g, std::size_t count, double x, double y);   // -1 : aucune case

// Un appui sur Alt : sélection dans la rangée, panneau après kPanelDelay, fin au relâchement.
class SwitchSession {
public:
    static constexpr double kPanelDelay = 0.15;   // secondes : un Alt+Tab rapide ne montre rien
    enum class Tick { Wait, ShowPanel, Finish };
    // false (rien ne démarre) sans app. frontFirst : la première app est celle au premier plan (sinon, bureau au
    // premier plan par exemple, la sélection part de la première et non de la suivante).
    bool begin(std::size_t count, bool back, double now, bool frontFirst = true);
    bool active() const { return active_; }
    std::size_t selected() const { return selected_; }
    std::size_t count() const { return count_; }
    void step(int delta);
    void select(std::size_t index);   // hors rangée : ignoré
    bool removeSelected();            // app fermée (Q) ; false : plus aucune app, session finie
    void hideSelected();              // app masquée (H) : relâcher Alt sur elle ne l'active pas
    bool activates() const { return active_ && selected_ < hidden_.size() && !hidden_[selected_]; }
    Tick tick(bool altDown, double now);
    void end() { active_ = false; }

private:
    bool active_ = false, panel_ = false;
    std::size_t count_ = 0, selected_ = 0;
    std::vector<bool> hidden_;   // par case
    double start_ = 0;
};

// Fenêtres à activer (indices) : app masquée → toutes ; sinon les non réduites ; sinon la première, restaurée.
struct SwitchActivation {
    std::vector<std::size_t> windows;
    bool restoreFirst = false;
};
SwitchActivation switcherActivation(bool hidden, const std::vector<bool>& iconic);

// Crochet clavier du sélecteur (Windows garde Alt+Tab pour lui : RegisterHotKey échoue) : que faire d'une frappe ?
// Pass : laissée à Windows ; Swallow : avalée sans effet ; les autres : avalée et envoyée à la session.
enum class SwitchKey { Pass, Swallow, Next, Prev, Cancel, Left, Right, Quit, Hide };
SwitchKey switcherKeyAction(unsigned vk, bool down, bool alt, bool shift, bool session, bool repeat, bool injected);

// « alt+tab » (casse ignorée) ; nullopt pour « off » ou une valeur inconnue.
std::optional<HotkeySpec> parseSwitcherHotkey(const std::wstring& text);

} // namespace md
