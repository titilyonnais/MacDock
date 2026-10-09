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
    bool typeAhead = false;         // un nom est en train d'être tapé dans la liste : l'espace en fait partie
};

enum class QuickLookKey { Pass, Open, Swallow };

// Espace seul, frappé (pas simulé), dans la liste des fichiers de l'Explorateur ou du bureau : Open à l'appui,
// Swallow au relâchement ; tout le reste passe (zones de saisie, autres apps, modificateurs).
QuickLookKey quickLookKey(unsigned vk, bool down, bool modifiers, bool injected, const QuickLookContext& c);
// Touche qui fait partie d'un nom tapé dans la liste des fichiers (sélection par la saisie) : lettre, chiffre ou signe,
// sans Ctrl, Alt ni ⊞ (`commandModifiers`).
bool typeAheadKey(unsigned vk, bool commandModifiers);
// Saisie en cours : dernière touche de nom il y a moins d'une seconde (`lastTyped` 0 : aucune), comme la sélection par
// la saisie du Finder, où l'espace continue alors le nom au lieu d'ouvrir le Coup d'œil.
bool typeAheadActive(unsigned long long lastTyped, unsigned long long now);

bool quickLookIsText(const std::wstring& path);                     // d'après l'extension
std::wstring quickLookDecode(const std::vector<std::uint8_t>& bytes);   // UTF-8 (BOM ou non), UTF-16 LE/BE, sinon ANSI
// Lecture coupée à une limite : retire une séquence UTF-8 incomplète à la fin (sinon tout serait relu en ANSI).
void quickLookTrimUtf8(std::vector<std::uint8_t>& bytes);

// Taille de la fenêtre (points) pour un contenu donné : proportions gardées, au plus 70 % de l'écran, petite image
// agrandie au plus 2×, largeur minimale ; titleBar ajoutée en hauteur.
SIZE quickLookWindowSize(SIZE content, SIZE screen, int titleBar);

std::wstring quickLookSize(std::uint64_t bytes);   // « 1,3 Mo » comme le Finder (puissances de 1000)

// Vidéo ou son (lu par Media Foundation), d'après l'extension.
enum class QuickLookMedia { None, Video, Audio };
QuickLookMedia quickLookMedia(const std::wstring& path);
// Document confié au gestionnaire d'aperçu du Shell (PDF, Office, HTML, polices…) ; pas les images (miniature), le
// texte (vue maison) ni les médias.
bool quickLookUsesShellPreview(const std::wstring& path);
// Fenêtre d'un document (points, barre d'outils comprise) : page en portrait (PDF, Word), en paysage (tableur,
// présentation, page web), sinon moyenne ; au plus 90 % de l'écran.
SIZE quickLookDocumentSize(const std::wstring& path, SIZE screen, int titleBar);
// Ouverture en zoom depuis l'icône : rectangle à t (0 → from, 1 → to), ralenti à l'arrivée.
RECT quickLookZoom(const RECT& from, const RECT& to, double t);

} // namespace md
