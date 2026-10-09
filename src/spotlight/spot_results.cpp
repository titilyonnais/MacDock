#include "spot_results.h"

#include <algorithm>

#include "../core/strings.h"
#include "../settings/pane_catalog.h"
#include "spot_calc.h"
#include "spot_convert.h"

namespace md {

namespace {

constexpr std::size_t kMaxApps = 6, kMaxSettings = 4, kMaxFiles = 8;

std::wstring trimmed(const std::wstring& s) {
    const auto b = s.find_first_not_of(L" \t");
    if (b == std::wstring::npos) return {};
    return s.substr(b, s.find_last_not_of(L" \t") - b + 1);
}

SpotItem appItem(const AppEntry& e) { return {SpotKind::App, e.name, L"Application", launchTarget(e)}; }

SpotItem settingItem(const SettingsPage& p) {
    if (p.macDock) return {SpotKind::Pane, p.title, L"Réglages MacDock", p.target};
    return {SpotKind::Setting, p.title, L"Paramètres Windows", p.target};
}

// Nombre copié : sans séparateur de milliers, lisible par un tableur.
std::wstring plainNumber(const std::wstring& shown) {
    std::wstring plain;
    for (wchar_t c : shown)
        if (c != L'\u202F') plain.push_back(c);
    return plain;
}

std::wstring percentEncode(const std::wstring& text) {
    static const char hex[] = "0123456789ABCDEF";
    std::wstring out;
    for (unsigned char c : toUtf8(text)) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            out.push_back(wchar_t(c));
        } else {
            out.push_back(L'%');
            out.push_back(wchar_t(hex[c >> 4]));
            out.push_back(wchar_t(hex[c & 15]));
        }
    }
    return out;
}

} // namespace

std::vector<SpotSection> spotlightResults(const std::wstring& query, const std::vector<AppEntry>& apps,
                                          const std::vector<SpotItem>& files, const std::vector<SettingsPage>& pages) {
    const std::wstring q = trimmed(query);
    std::vector<SpotSection> out;
    if (q.empty()) return out;
    SpotSection best{L"Meilleur résultat", {}}, appSection{L"Applications", {}}, settings{L"Réglages", {}},
        docs{L"Documents", {}};
    const std::vector<std::size_t> found = searchApps(apps, q);
    const std::vector<std::size_t> pageHits = searchSettings(pages, q);
    constexpr std::size_t kNone = std::size_t(-1);
    std::size_t next = 0, bestPage = kNone;
    if (const auto v = evaluateExpression(q)) {
        const std::wstring result = formatNumber(*v);
        best.items.push_back({SpotKind::Calc, result, q + L" =", plainNumber(result)});
    } else if (const auto c = convertUnits(q)) {   // 6 chiffres, l'unité après une espace insécable
        const std::wstring result = formatNumber(c->value, 6);
        best.items.push_back({SpotKind::Calc, result + L'\u00A0' + c->to,
                              formatNumber(c->amount) + L'\u00A0' + c->from + L" =", plainNumber(result)});
    } else {
        for (std::size_t i : pageHits)
            if (exactSettingTitle(pages[i], q)) {
                bestPage = i;
                break;
            }
        if (bestPage == kNone && found.empty() && !pageHits.empty()) bestPage = pageHits[0];
        if (bestPage != kNone) {
            best.items.push_back(settingItem(pages[bestPage]));
        } else if (!found.empty()) {
            best.items.push_back(appItem(apps[found[0]]));
            next = 1;
        }
    }
    for (std::size_t i = next; i < found.size() && appSection.items.size() < kMaxApps; ++i)
        appSection.items.push_back(appItem(apps[found[i]]));
    for (std::size_t i : pageHits)
        if (i != bestPage && settings.items.size() < kMaxSettings) settings.items.push_back(settingItem(pages[i]));
    for (std::size_t i = 0; i < files.size() && docs.items.size() < kMaxFiles; ++i) docs.items.push_back(files[i]);
    for (SpotSection* s : {&best, &appSection, &settings, &docs})
        if (!s->items.empty()) out.push_back(std::move(*s));
    return out;
}

std::size_t spotCount(const std::vector<SpotSection>& sections) {
    std::size_t n = 0;
    for (const auto& s : sections) n += s.items.size();
    return n;
}

const SpotItem* spotAt(const std::vector<SpotSection>& sections, std::size_t index) {
    for (const auto& s : sections) {
        if (index < s.items.size()) return &s.items[index];
        index -= s.items.size();
    }
    return nullptr;
}

std::vector<SpotSection> spotTrim(std::vector<SpotSection> sections, std::size_t maxRows) {
    std::vector<SpotSection> out;
    for (auto& s : sections) {
        if (!maxRows) break;
        if (s.items.size() > maxRows) s.items.resize(maxRows);
        maxRows -= s.items.size();
        if (!s.items.empty()) out.push_back(std::move(s));
    }
    return out;
}

DockClick dockClickGate(bool spotlightOpen, bool modalOpen) {
    if (spotlightOpen) return DockClick::CloseSpotlight;
    return modalOpen ? DockClick::Ignore : DockClick::Proceed;
}

SpotAction spotAction(const SpotItem& item, bool reveal) {
    switch (item.kind) {
        case SpotKind::Calc: return {SpotActionKind::Copy, item.target};
        case SpotKind::App: return {SpotActionKind::Launch, item.target};
        case SpotKind::File: return {reveal ? SpotActionKind::Reveal : SpotActionKind::Launch, item.target};
        case SpotKind::Setting:   // une page des Paramètres, jamais un autre programme
            if (item.target.starts_with(L"ms-settings:")) return {SpotActionKind::Launch, item.target};
            break;
        case SpotKind::Pane:
            if (paneFromKey(toUtf8(item.target))) return {SpotActionKind::OpenPane, item.target};
            break;
    }
    return {};
}

bool wantsFileSearch(const std::wstring& query) {
    const std::wstring q = trimmed(query);
    return !q.empty() && !evaluateExpression(q) && !convertUnits(q);
}

std::wstring shortFolder(const std::wstring& folder) {
    std::vector<std::wstring> parts;
    std::size_t start = 0;
    while (start <= folder.size()) {
        const std::size_t end = std::min(folder.find(L'\\', start), folder.size());
        if (end > start) parts.push_back(folder.substr(start, end - start));
        start = end + 1;
    }
    if (parts.empty()) return {};
    if (parts.size() == 1) return parts[0];
    return parts[parts.size() - 2] + L" › " + parts.back();
}

std::wstring searchMsUrl(const std::wstring& query, const std::wstring& folder) {
    return L"search-ms:query=" + percentEncode(query) + L"&crumb=location:" + percentEncode(folder);
}

std::optional<HotkeySpec> parseSpotlightHotkey(const std::wstring& text) { return parseHotkey(text); }

void spotEraseLast(std::wstring& query) {
    if (query.empty()) return;
    query.pop_back();
    if (!query.empty() && IS_HIGH_SURROGATE(query.back())) query.pop_back();
}

std::wstring spotPasteLine(const std::wstring& clip) {
    std::wstring out = clip.substr(0, clip.find_first_of(L"\r\n"));
    for (wchar_t& c : out)
        if (c == L'\t') c = L' ';
    spotClip(out, 128);
    return out;
}

void spotClip(std::wstring& text, std::size_t max) {
    if (text.size() <= max) return;
    text.resize(max);
    if (!text.empty() && IS_HIGH_SURROGATE(text.back())) text.pop_back();
}

bool spotAcceptChar(const std::wstring& query, wchar_t c) {
    if (c < 32 || c == 127 || query.size() >= 128) return false;
    if (IS_HIGH_SURROGATE(c)) return query.size() < 127;   // place pour la moitié basse
    if (IS_LOW_SURROGATE(c)) return !query.empty() && IS_HIGH_SURROGATE(query.back());
    return true;
}

} // namespace md
