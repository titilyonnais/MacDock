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

TEST_CASE(quicklook_space_continues_type_ahead) {
    // Plan 47 : « mon rapport » tapé dans la liste des fichiers sélectionne le fichier par son nom ; l'espace tapée
    // moins d'une seconde après une lettre va à l'Explorateur (comme le Finder), à l'appui comme au relâchement.
    using md::QuickLookKey;
    md::QuickLookContext c;
    c.foregroundClass = L"CabinetWClass";
    c.focusClass = L"DirectUIHWND";
    c.focusInShellView = true;
    c.typeAhead = true;
    CHECK(md::quickLookKey(VK_SPACE, true, false, false, c) == QuickLookKey::Pass);
    CHECK(md::quickLookKey(VK_SPACE, false, false, false, c) == QuickLookKey::Pass);
    // Lettres, chiffres et signes comptent ; pas un raccourci (Ctrl, Alt, ⊞), ni les flèches ou Échap.
    CHECK(md::typeAheadKey('M', false));
    CHECK(md::typeAheadKey('7', false));
    CHECK(md::typeAheadKey(VK_OEM_PERIOD, false));
    CHECK(!md::typeAheadKey('C', true));
    CHECK(!md::typeAheadKey(VK_DOWN, false));
    CHECK(!md::typeAheadKey(VK_ESCAPE, false));
    CHECK(!md::typeAheadKey(VK_SPACE, false));   // l'espace elle-même ne relance pas la saisie
    // Une seconde, comme la sélection par la saisie du Finder.
    CHECK(md::typeAheadActive(10'000, 10'999));
    CHECK(!md::typeAheadActive(10'000, 11'000));
    CHECK(!md::typeAheadActive(0, 500));   // rien tapé encore
}

TEST_CASE(quicklook_text_files_and_decoding) {
    CHECK(md::quickLookIsText(L"C:\\a\\notes.TXT"));
    CHECK(!md::quickLookIsText(L"C:\\v1.2\\LISEZMOI"));   // point dans le nom du dossier : pas une extension
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
    CHECK(md::quickLookSize(1) == L"1 octet");
    CHECK(md::quickLookSize(512) == L"512 octets");
    CHECK(md::quickLookSize(999700) == L"1 Mo");     // jamais « 1000 Ko »
    CHECK(md::quickLookSize(2000000) == L"2 Mo");    // pas de « ,0 », comme le Finder
    CHECK(md::quickLookSize(1536) == L"2 Ko");
    CHECK(md::quickLookSize(1258291) == L"1,3 Mo");
    CHECK(md::quickLookSize(5368709120ULL) == L"5,4 Go");
}

TEST_CASE(quicklook_text_cut_inside_utf8_sequence) {
    // Lecture limitée : si la coupe tombe au milieu d'un « é », le reste ne doit pas être relu en ANSI.
    const std::string text = "abc\xC3\xA9";   // « abcé » en UTF-8
    std::vector<std::uint8_t> cut(text.begin(), text.end() - 1);   // « abc » + premier octet de « é »
    md::quickLookTrimUtf8(cut);
    CHECK(md::quickLookDecode(cut) == L"abc");
    std::vector<std::uint8_t> whole(text.begin(), text.end());
    md::quickLookTrimUtf8(whole);   // séquence complète : rien n'est retiré
    CHECK(md::quickLookDecode(whole) == L"abcé");
}

TEST_CASE(quicklook_media_kinds) {
    using md::QuickLookMedia;
    CHECK(md::quickLookMedia(L"C:\\v\\Film.MP4") == QuickLookMedia::Video);
    CHECK(md::quickLookMedia(L"C:\\v\\clip.mov") == QuickLookMedia::Video);
    CHECK(md::quickLookMedia(L"C:\\m\\chanson.mp3") == QuickLookMedia::Audio);
    CHECK(md::quickLookMedia(L"C:\\m\\son.FLAC") == QuickLookMedia::Audio);
    CHECK(md::quickLookMedia(L"C:\\dossier.mp4\\notes") == QuickLookMedia::None);   // le point est dans le dossier
    CHECK(md::quickLookMedia(L"C:\\a\\notes.txt") == QuickLookMedia::None);
}

TEST_CASE(quicklook_shell_previews_for_documents_only) {
    CHECK(md::quickLookUsesShellPreview(L"C:\\d\\rapport.pdf"));
    CHECK(md::quickLookUsesShellPreview(L"C:\\d\\lettre.DOCX"));
    CHECK(md::quickLookUsesShellPreview(L"C:\\d\\police.ttf"));
    CHECK(!md::quickLookUsesShellPreview(L"C:\\d\\photo.jpg"));    // miniature : plus nette et plus rapide
    CHECK(!md::quickLookUsesShellPreview(L"C:\\d\\notes.txt"));    // vue texte maison
    CHECK(!md::quickLookUsesShellPreview(L"C:\\d\\film.mp4"));     // lecteur vidéo
    CHECK(!md::quickLookUsesShellPreview(L"C:\\d\\sansextension"));
    CHECK(md::quickLookUsesShellPreview(L"C:\\d\\page.html"));   // la page rendue, pas son code
    CHECK(!md::quickLookIsText(L"C:\\d\\page.htm"));
}

TEST_CASE(quicklook_document_window_sizes) {
    const SIZE big{2560, 1400};
    SIZE s = md::quickLookDocumentSize(L"C:\\d\\a.pdf", big, 44);
    CHECK(s.cx == 620 && s.cy == 820 + 44);   // page en portrait
    s = md::quickLookDocumentSize(L"C:\\d\\a.xlsx", big, 44);
    CHECK(s.cx == 900 && s.cy == 600 + 44);   // feuille en paysage
    s = md::quickLookDocumentSize(L"C:\\d\\a.pdf", SIZE{960, 540}, 44);   // 1080p à 200 %
    CHECK(s.cx <= 960 * 9 / 10 && s.cy <= 540 * 9 / 10);
}

TEST_CASE(quicklook_zoom_from_the_icon) {
    const RECT from{100, 100, 164, 164}, to{500, 300, 1500, 1000};
    RECT r = md::quickLookZoom(from, to, 0);
    CHECK(r.left == 100 && r.right == 164);
    r = md::quickLookZoom(from, to, 1);
    CHECK(r.left == 500 && r.top == 300 && r.right == 1500 && r.bottom == 1000);
    r = md::quickLookZoom(from, to, 0.5);
    CHECK(r.left > 300 && r.left < 500);   // ralentit à l'arrivée : plus de la moitié du chemin
    CHECK(r.right - r.left > 532);
}
