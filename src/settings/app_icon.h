// Icônes des exécutables de MacDock (plan 49), dessinées par le code (aucune ressource Apple) : tuile arrondie de
// Tahoe en léger dégradé et pictogramme blanc, comme les tuiles de l'app Réglages ; rangées dans un fichier ICO.
#pragma once
#include <cstdint>
#include <utility>
#include <vector>

#include "../core/bgra_image.h"

namespace md {

enum class AppIconKind { Settings, Dock, MenuBar };

// Icône carrée de `size` pixels, alpha non prémultiplié ; COM initialisé par l'appelant (WIC). Plein cadre, au rayon
// de la grille d'Apple : le Dock (« Icônes uniformes ») la passe sous son propre masque au lieu de la mettre en
// « prison ». Échec (COM, Direct2D) : image vide (0 × 0).
BgraImage renderAppIcon(AppIconKind kind, int size);

// Fichier ICO : une image PNG par taille (côté en pixels ; 256 s'écrit 0 dans l'en-tête), dans l'ordre donné.
std::vector<std::uint8_t> icoFile(const std::vector<std::pair<int, std::vector<std::uint8_t>>>& pngs);

} // namespace md
