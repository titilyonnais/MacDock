#include "quicklook_logic.h"

#include <algorithm>
#include <cmath>
#include <cwctype>

namespace md {

namespace {
bool sameClass(const std::wstring& a, const wchar_t* b) { return _wcsicmp(a.c_str(), b) == 0; }
} // namespace

QuickLookKey quickLookKey(unsigned vk, bool down, bool modifiers, bool injected, const QuickLookContext& c) {
    if (vk != VK_SPACE || modifiers || injected) return QuickLookKey::Pass;
    const bool explorer = sameClass(c.foregroundClass, L"CabinetWClass") || sameClass(c.foregroundClass, L"ExploreWClass");
    const bool desktop = sameClass(c.foregroundClass, L"Progman") || sameClass(c.foregroundClass, L"WorkerW");
    if (!explorer && !desktop) return QuickLookKey::Pass;
    // Liste des fichiers seulement : jamais une zone de saisie (renommer, rechercher, adresse).
    const bool list = sameClass(c.focusClass, L"DirectUIHWND") || sameClass(c.focusClass, L"SysListView32");
    if (!list || !c.focusInShellView) return QuickLookKey::Pass;
    return down ? QuickLookKey::Open : QuickLookKey::Swallow;
}

bool quickLookIsText(const std::wstring& path) {
    const auto dot = path.find_last_of(L'.');
    const auto slash = path.find_last_of(L"\\/");
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)) return false;
    std::wstring ext = path.substr(dot + 1);
    for (auto& ch : ext) ch = wchar_t(std::towlower(ch));
    static const wchar_t* kText[] = {L"txt", L"md", L"markdown", L"log", L"json", L"xml", L"csv", L"tsv", L"ini", L"cfg",
                                     L"conf", L"yaml", L"yml", L"toml", L"c", L"cc", L"cpp", L"cxx", L"h", L"hpp", L"cs",
                                     L"java", L"kt", L"py", L"js", L"mjs", L"ts", L"tsx", L"jsx", L"css", L"scss",
                                     L"php", L"rb", L"go", L"rs", L"swift", L"sh", L"bat", L"cmd", L"ps1", L"sql",
                                     L"lua", L"gitignore", L"env", L"reg", L"srt", L"nfo"};
    for (const wchar_t* e : kText)
        if (ext == e) return true;
    return false;
}

std::wstring quickLookDecode(const std::vector<std::uint8_t>& b) {
    if (b.size() >= 2 && b[0] == 0xFF && b[1] == 0xFE) {   // UTF-16 LE
        std::wstring out;
        for (std::size_t i = 2; i + 1 < b.size(); i += 2) out += wchar_t(b[i] | (b[i + 1] << 8));
        return out;
    }
    if (b.size() >= 2 && b[0] == 0xFE && b[1] == 0xFF) {   // UTF-16 BE
        std::wstring out;
        for (std::size_t i = 2; i + 1 < b.size(); i += 2) out += wchar_t((b[i] << 8) | b[i + 1]);
        return out;
    }
    std::size_t start = b.size() >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF ? 3 : 0;
    if (start >= b.size()) return {};
    const char* p = reinterpret_cast<const char*>(b.data() + start);
    const int n = int(b.size() - start);
    // UTF-8 strict d'abord ; un fichier ANSI (accents en Windows-1252) est relu dans la page de code du système.
    int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, p, n, nullptr, 0);
    UINT cp = CP_UTF8;
    if (len <= 0) {
        cp = CP_ACP;
        len = MultiByteToWideChar(cp, 0, p, n, nullptr, 0);
    }
    std::wstring out(std::size_t(std::max(len, 0)), L'\0');
    if (len > 0) MultiByteToWideChar(cp, cp == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0, p, n, out.data(), len);
    return out;
}

void quickLookTrimUtf8(std::vector<std::uint8_t>& b) {
    // Remonte jusqu'au début de la dernière séquence (au plus 3 octets de suite) ; la garde si elle est complète.
    std::size_t i = b.size(), cont = 0;
    while (i > 0 && cont < 3 && (b[i - 1] & 0xC0) == 0x80) {
        --i;
        ++cont;
    }
    if (i == 0) return;
    const std::uint8_t lead = b[i - 1];
    const std::size_t need = lead >= 0xF0 ? 3 : lead >= 0xE0 ? 2 : lead >= 0xC0 ? 1 : 0;
    if (lead >= 0xC0 && cont < need) b.resize(i - 1);   // séquence incomplète : retirée
}

SIZE quickLookWindowSize(SIZE content, SIZE screen, int titleBar) {
    const double maxW = screen.cx * 0.7, maxH = screen.cy * 0.7 - titleBar;
    double w = std::max<LONG>(content.cx, 1), h = std::max<LONG>(content.cy, 1);
    const double fit = std::min({maxW / w, maxH / h, 2.0});   // jamais plus de 2× une petite image
    w *= fit;
    h *= fit;
    const double minW = 360;
    return SIZE{LONG(std::lround(std::max(w, minW))), LONG(std::lround(h)) + titleBar};
}

std::wstring quickLookSize(std::uint64_t bytes) {
    if (bytes == 0) return L"Zéro octet";
    if (bytes == 1) return L"1 octet";
    if (bytes < 1000) return std::to_wstring(bytes) + L" octets";
    static const wchar_t* kUnits[] = {L"Ko", L"Mo", L"Go", L"To"};
    double v = double(bytes) / 1000;
    int u = 0;
    // Unité choisie après l'arrondi affiché : jamais « 1000 Ko » ni « 1000,0 Mo ».
    while (u < 3 && (u == 0 ? std::llround(v) : std::llround(v * 10) / 10.0) >= 1000) {
        v /= 1000;
        ++u;
    }
    if (u == 0) return std::to_wstring(std::llround(v)) + L" Ko";   // Ko arrondis à l'unité, comme le Finder
    const long long tenths = std::llround(v * 10);
    if (tenths % 10 == 0) return std::to_wstring(tenths / 10) + L" " + kUnits[u];   // « 2 Mo », pas « 2,0 Mo »
    return std::to_wstring(tenths / 10) + L"," + std::to_wstring(tenths % 10) + L" " + kUnits[u];
}

namespace {
// Extension en minuscules, sans le point ; vide si le dernier point est dans un dossier.
std::wstring extensionOf(const std::wstring& path) {
    const auto dot = path.find_last_of(L'.');
    const auto slash = path.find_last_of(L"\\/");
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)) return {};
    std::wstring ext = path.substr(dot + 1);
    for (auto& ch : ext) ch = wchar_t(std::towlower(ch));
    return ext;
}

bool among(const std::wstring& ext, std::initializer_list<const wchar_t*> list) {
    for (const wchar_t* e : list)
        if (ext == e) return true;
    return false;
}

bool isImage(const std::wstring& ext) {
    return among(ext, {L"jpg", L"jpeg", L"jfif", L"png", L"gif", L"bmp", L"webp", L"heic", L"heif", L"avif", L"tif", L"tiff",
                       L"ico"});
}
} // namespace

QuickLookMedia quickLookMedia(const std::wstring& path) {
    const std::wstring ext = extensionOf(path);
    if (among(ext, {L"mp4", L"m4v", L"mov", L"wmv", L"avi", L"mkv", L"webm", L"3gp"})) return QuickLookMedia::Video;
    if (among(ext, {L"mp3", L"m4a", L"aac", L"wav", L"flac", L"wma", L"ogg", L"opus"})) return QuickLookMedia::Audio;
    return QuickLookMedia::None;
}

bool quickLookUsesShellPreview(const std::wstring& path) {
    const std::wstring ext = extensionOf(path);
    return !ext.empty() && !isImage(ext) && !quickLookIsText(path) && quickLookMedia(path) == QuickLookMedia::None;
}

SIZE quickLookDocumentSize(const std::wstring& path, SIZE screen, int titleBar) {
    const std::wstring ext = extensionOf(path);
    SIZE c{700, 560};
    if (among(ext, {L"pdf", L"doc", L"docx", L"docm", L"odt", L"rtf", L"xps", L"oxps"})) c = SIZE{620, 820};
    else if (among(ext, {L"xls", L"xlsx", L"xlsm", L"ods", L"ppt", L"pptx", L"odp", L"html", L"htm", L"mht", L"svg"}))
        c = SIZE{900, 600};
    c.cx = std::min<LONG>(c.cx, LONG(screen.cx * 0.9));
    c.cy = std::min<LONG>(c.cy, LONG(screen.cy * 0.9) - titleBar);
    return SIZE{c.cx, c.cy + titleBar};
}

RECT quickLookZoom(const RECT& from, const RECT& to, double t) {
    const double u = std::clamp(t, 0.0, 1.0), e = 1 - std::pow(1 - u, 3);   // ralenti à l'arrivée
    auto mix = [&](LONG a, LONG b) { return LONG(std::lround(a + (b - a) * e)); };
    return RECT{mix(from.left, to.left), mix(from.top, to.top), mix(from.right, to.right), mix(from.bottom, to.bottom)};
}

} // namespace md
