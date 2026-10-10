// Séance de menus de la barre (logique pure, plan 71) : avant chaque titre parcouru, ce que deviennent les menus.
#pragma once

namespace md {

enum class MenuSessionStep { Continue, Relayout, End };

// appChanged : l'app active n'est plus celle dont les menus ont été ouverts (comme macOS, ses menus se referment) ;
// layoutPending : une mise en page a été demandée pendant le menu (horloge, icônes, réglages), refaite avant le titre
// suivant (sinon les menus se dessinaient dans des cases périmées : ta photo 1 du 10 octobre).
inline MenuSessionStep menuSessionStep(bool appChanged, bool layoutPending) {
    if (appChanged) return MenuSessionStep::End;
    return layoutPending ? MenuSessionStep::Relayout : MenuSessionStep::Continue;
}

} // namespace md
