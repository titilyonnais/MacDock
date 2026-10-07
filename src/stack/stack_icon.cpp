#include "stack_icon.h"

#include <algorithm>

namespace md {

std::vector<StackLayer> stackIconLayers(std::size_t count) {
    // Du dessous vers le dessus : les éléments plus anciens dépassent, inclinés de part et d'autre.
    switch (std::min<std::size_t>(count, 3)) {
        case 0: return {};
        // Les icônes de fichiers ont leur propre marge : les couches occupent toute la forme d'icône.
        case 1: return {{0, 0, 0, 1.0}};
        case 2: return {{0, 0.012, -7, 0.94}, {0, 0, 0, 1.0}};
        default: return {{0, 0.016, -8, 0.92}, {0, 0.008, 6, 0.95}, {0, 0, 0, 1.0}};
    }
}

std::vector<FileRef> stackPreview(const std::vector<StackItem>& sorted) {
    std::vector<FileRef> out;
    for (std::size_t i = 0; i < sorted.size() && i < 3; ++i) out.push_back({sorted[i].path, sorted[i].modified});
    return out;
}

} // namespace md
