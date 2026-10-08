// Coup d'œil (Quick Look de macOS) : logique pure — déclenchement par Espace dans les vues de fichiers, lecture des
// fichiers texte, taille de la fenêtre, libellés.
#pragma once
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace md {

struct QuickLookContext {
    std::wstring foregroundClass;   // classe de la fenêtre au premier plan
    std::wstring focusClass;        // classe du contrôle qui a le focus clavier
    bool focusInShellView = false;  // le focus est dans la vue des fichiers (SHELLDLL_DefView)
};

enum class QuickLookKey { Pass, Open, Swallow };

// Espace seul, frappé (pas simulé), dans la liste des fichiers de l'Explorateur ou du bureau : Open à l'appui,
// Swallow au relâchement ; tout le reste passe (zones de saisie, autres apps, modificateurs).
QuickLookKey quickLookKey(unsigned vk, bool down, bool modifiers, bool injected, const QuickLookContext& c);

bool quickLookIsText(const std::wstring& path);                     // d'après l'extension
std::wstring quickLookDecode(const std::vector<std::uint8_t>& bytes);   // UTF-8 (BOM ou non), UTF-16 LE/BE, sinon ANSI

// Taille de la fenêtre (points) pour un contenu donné : proportions gardées, au plus 70 % de l'écran, petite image
// agrandie au plus 2×, largeur minimale ; titleBar ajoutée en hauteur.
SIZE quickLookWindowSize(SIZE content, SIZE screen, int titleBar);

std::wstring quickLookSize(std::uint64_t bytes);   // « 1,3 Mo » comme le Finder (puissances de 1000)

} // namespace md
