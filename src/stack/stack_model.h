// Contenu d'une pile (dossier épinglé) : énumération, tri, plafond, présentation automatique.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "../config/settings.h"

namespace md {

constexpr std::size_t kFanMaxItems = 15;    // éventail : 15 éléments au plus
constexpr std::size_t kGridMaxItems = 48;   // grille : 48 éléments au plus
constexpr std::size_t kFanAutoMax = 9;      // automatique : éventail jusqu'à 9 éléments, grille au-delà

struct StackItem {
    std::wstring path, name;
    std::uint64_t created = 0, modified = 0;   // FILETIME (100 ns) : date d'ajout = création dans le dossier
    bool isFolder = false;
};

std::vector<StackItem> sortStack(std::vector<StackItem> items, StackSort sort);
std::vector<StackItem> capStack(std::vector<StackItem> items, std::size_t max);
StackView resolveView(StackView v, std::size_t count);
// Éléments visibles du dossier (ni cachés, ni système), non triés ; vide si illisible.
std::vector<StackItem> listFolder(const std::wstring& folder);

} // namespace md
