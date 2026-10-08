// Touche ⌘ (option « altAsCommand ») : Alt de gauche joue le rôle de la touche ⌘ de macOS, sous le pouce.
// ⌘C, V, X, Z, A, S, W, T, N, F, P, O, R, Y, « , » → Ctrl+… ; ⌘Q → Alt+F4 ; ⌘← → Début ; ⌘→ → Fin ; ⌘↑ → Ctrl+Début ;
// ⌘↓ → Ctrl+Fin (Maj gardée). Toute autre touche reçoit un vrai Alt (Alt+Tab, Alt+F4, Alt+Entrée). Alt Gr jamais touché.
// Logique pure : état du crochet clavier (un seul fil).
#pragma once
#include <optional>

namespace md {

struct Chord {
    unsigned vk = 0;
    bool ctrl = false, shift = false, alt = false;
};

struct CommandAction {
    enum class Kind {
        Pass,          // la frappe passe telle quelle
        Swallow,       // avalée
        Send,          // avalée ; chord est envoyé à la place (appui et relâchement)
        AltThenPass,   // Alt est d'abord rendu à Windows (appui simulé), puis la frappe passe
    } kind = Kind::Pass;
    Chord chord;
};

// Raccourci de macOS pour vk avec ⌘ ; nullopt : pas un raccourci ⌘ (la touche reçoit un vrai Alt).
// explorer : l'Explorateur est au premier plan (raccourcis du Finder : ⌘↑ parent, ⌘↓ ouvrir, ⌘⌫ Corbeille).
std::optional<Chord> commandChord(unsigned vk, bool shift, bool explorer = false);

class CommandKeys {
public:
    // Une frappe physique (les nôtres, simulées, ne passent pas par ici). shift : Maj enfoncée.
    CommandAction onKey(unsigned vk, bool down, bool shift, bool explorer = false);

private:
    bool held_ = false;      // Alt de gauche enfoncé, gardé de côté
    bool realAlt_ = false;   // Alt rendu à Windows pour cet appui
};

} // namespace md
