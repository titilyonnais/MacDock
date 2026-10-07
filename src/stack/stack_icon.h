// Icône d'une pile affichée « comme Pile » : ses derniers éléments empilés, ceux du dessous légèrement
// inclinés, comme sur macOS (géométrie pure).
#pragma once
#include <string>
#include <vector>

#include "../icons/icon_provider.h"
#include "stack_model.h"

namespace md {

struct StackLayer {
    double dx = 0, dy = 0;   // décalage du centre, en fraction de la case
    double angle = 0;        // inclinaison en degrés (sens horaire)
    double scale = 1;        // côté de l'image, en fraction de la forme d'icône
};

// Couches du dessous vers le dessus (3 au plus) ; la dernière est l'élément le plus en vue, droit.
std::vector<StackLayer> stackIconLayers(std::size_t count);
// 3 premiers éléments selon le tri (le premier est au-dessus de la pile), avec leur date de modification.
std::vector<FileRef> stackPreview(const std::vector<StackItem>& sorted);

} // namespace md
