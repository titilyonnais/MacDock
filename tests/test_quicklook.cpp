// Coup d'œil (Quick Look) : déclenchement par Espace, lecture des fichiers texte, taille de la fenêtre, libellés.
#include <string>
#include <vector>

#include "minitest.h"
#include "../src/quicklook/quicklook_logic.h"

TEST_CASE(quicklook_space_in_file_views_only) {
    using md::QuickLookKey;
    md::QuickLookContext c;
    c.foregroundClass = L"CabinetWClass";   // Explorateur
    c.focusClass = L"DirectUIHWND";        // liste des fichiers
    c.focusInShellView = true;
    CHECK(md::quickLookKey(VK_SPACE, true, false, false, c) == QuickLookKey::Open);
    CHECK(md::quickLookKey(VK_SPACE, false, false, false, c) == QuickLookKey::Swallow);   // relâchement avalé aussi
    CHECK(md::quickLookKey(VK_SPACE, true, true, false, c) == QuickLookKey::Pass);        // avec un modificateur
    CHECK(md::quickLookKey(VK_SPACE, true, false, true, c) == QuickLookKey::Pass);        // frappe simulée
    CHECK(md::quickLookKey('A', true, false, false, c) == QuickLookKey::Pass);
    auto rename = c;   // renommer un fichier : la zone de saisie a le focus
    rename.focusClass = L"Edit";
    rename.focusInShellView = false;
    CHECK(md::quickLookKey(VK_SPACE, true, false, false, rename) == QuickLookKey::Pass);
    auto search = c;   // barre de recherche ou d'adresse
    search.focusInShellView = false;
    CHECK(md::quickLookKey(VK_SPACE, true, false, false, search) == QuickLookKey::Pass);
    auto desktop = c;   // bureau : liste des icônes
    desktop.foregroundClass = L"WorkerW";
    desktop.focusClass = L"SysListView32";
    CHECK(md::quickLookKey(VK_SPACE, true, false, false, desktop) == QuickLookKey::Open);
    auto other = c;
    other.foregroundClass = L"Notepad";
    CHECK(md::quickLookKey(VK_SPACE, true, false, false, other) == QuickLookKey::Pass);
}

TEST_CASE(quicklook_text_files_and_decoding) {
    CHECK(md::quickLookIsText(L"C:\a\notes.TXT"));
    CHECK(md::quickLookIsText(L"main.cpp"));
    CHECK(md::quickLookIsText(L"config.json"));
    CHECK(!md::quickLookIsText(L"photo.jpg"));
    CHECK(!md::quickLookIsText(L"sans-extension"));
    const std::string utf8 = "\xEF\xBB\xBF" "Caf\xC3\xA9";                 // UTF-8 avec BOM
    CHECK(md::quickLookDecode(std::vector<std::uint8_t>(utf8.begin(), utf8.end())) == L"Café");
    const std::vector<std::uint8_t> utf16{0xFF, 0xFE, 'O', 0, 'K', 0};       // UTF-16 LE avec BOM
    CHECK(md::quickLookDecode(utf16) == L"OK");
    const std::string plain = "ligne 1\r\nligne 2";
    CHECK(md::quickLookDecode(std::vector<std::uint8_t>(plain.begin(), plain.end())) == L"ligne 1\r\nligne 2");
}

TEST_CASE(quicklook_window_fits_content_and_screen) {
    // Une photo 4000 × 3000 sur un écran 1920 × 1080 (points) : au plus 70 % de l'écran, proportions gardées, plus
    // la barre de titre ; une petite image n'est pas agrandie au-delà de 2×.
    const SIZE photo = md::quickLookWindowSize(SIZE{4000, 3000}, SIZE{1920, 1080}, 36);
    CHECK(photo.cy <= 1080 * 0.7 + 36 + 1);
    CHECK(std::abs(double(photo.cx) / (photo.cy - 36) - 4.0 / 3.0) < 0.02);
    const SIZE icon = md::quickLookWindowSize(SIZE{64, 64}, SIZE{1920, 1080}, 36);
    CHECK(icon.cx >= 360);   // largeur minimale (titre et bouton)
    CHECK(icon.cy - 36 <= 64 * 2 + 1);
}

TEST_CASE(quicklook_labels) {
    CHECK(md::quickLookSize(0) == L"Zéro octet");
    CHECK(md::quickLookSize(512) == L"512 octets");
    CHECK(md::quickLookSize(1536) == L"2 Ko");
    CHECK(md::quickLookSize(1258291) == L"1,3 Mo");
    CHECK(md::quickLookSize(5368709120ULL) == L"5,4 Go");
}
