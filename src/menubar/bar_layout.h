// Mise en page de la barre de menus (logique pure, en points depuis le bord gauche de la barre).
#pragma once
#include <cstddef>
#include <vector>

namespace md {

struct BarLayoutInput {
    double barWidth = 0, leftMargin = 10, rightMargin = 10;
    double minGap = 20;                // écart minimal entre le dernier menu affiché et la partie droite
    std::vector<double> leftWidths;    // logo, nom de l'app, menus (largeur de case, marges comprises)
    std::vector<double> rightWidths;   // de gauche à droite (l'horloge en dernier)
    std::size_t keepLeft = 2;          // toujours visibles (logo, nom de l'app)
    double lastGap = 0;                // écart de plus avant la dernière case de droite (l'horloge)
};

struct BarLayout {
    std::vector<double> leftX, rightX;   // bord gauche de chaque case
    std::size_t leftVisible = 0;         // les leftVisible premières cases gauches sont affichées
};

struct BarHit {
    enum class Kind { None, Left, Right } kind = Kind::None;
    std::size_t index = 0;
};

// Les menus qui toucheraient la partie droite sont masqués (comme sur macOS), sauf les keepLeft premiers.
BarLayout layoutBar(const BarLayoutInput& in);
BarHit hitTestBar(const BarLayout& l, const BarLayoutInput& in, double x);
// Largeur de la partie droite, écart avant l'horloge compris.
double rightPartWidth(const BarLayoutInput& in);
// Case de texte, en points : le texte mesuré (pixels) ramené à l'échelle de l'écran, sans arrondi, et ses deux marges.
double textBoxWidth(double textPx, double scale, double padding);

} // namespace md
