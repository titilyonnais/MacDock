#include "update_logic.h"

#include <algorithm>

#include "../core/json.h"
#include "../core/strings.h"

namespace md {

namespace {

std::wstring field(const json::Value& o, const char* key) {
    const json::Value* v = o.find(key);
    return v ? fromUtf8(v->asString("")) : std::wstring();
}

bool flag(const json::Value& o, const char* key) {
    const json::Value* v = o.find(key);
    return v && v->asBool(false);
}

std::optional<Release> parseRelease(const json::Value& o) {
    if (!o.isObject()) return std::nullopt;
    Release r;
    r.tag = field(o, "tag_name");
    const auto v = parseVersion(r.tag);
    if (!v) return std::nullopt;   // « nightly »… : pas une version
    r.version = *v;
    r.prerelease = flag(o, "prerelease");
    r.draft = flag(o, "draft");
    if (const json::Value* assets = o.find("assets"); assets && assets->isArray())
        for (const json::Value& a : assets->asArray()) {
            if (!a.isObject()) continue;
            ReleaseAsset asset;
            asset.name = field(a, "name");
            asset.url = field(a, "browser_download_url");
            if (const json::Value* size = a.find("size"); size && size->asNumber(-1) >= 0)
                asset.size = std::uint64_t(size->asNumber(0));
            r.assets.push_back(std::move(asset));
        }
    return r;
}

const ReleaseAsset* assetNamed(const Release& r, std::wstring_view name) {
    for (const ReleaseAsset& a : r.assets)
        if (a.name == name) return &a;
    return nullptr;
}

bool https(const std::wstring& url) { return url.rfind(L"https://", 0) == 0; }

bool isHex(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

} // namespace

std::vector<Release> parseReleases(std::string_view text) {
    std::vector<Release> out;
    const auto doc = json::parse(text);
    if (!doc) return out;
    if (doc->isArray()) {
        for (const json::Value& o : doc->asArray())
            if (auto r = parseRelease(o)) out.push_back(std::move(*r));
    } else if (auto r = parseRelease(*doc)) {
        out.push_back(std::move(*r));
    }
    return out;
}

std::wstring installerName(const Version& v) { return L"MacDock-Setup-" + versionText(v) + L".exe"; }

std::optional<UpdateOffer> pickUpdate(const std::vector<Release>& releases, const Version& current, bool prerelease) {
    std::optional<UpdateOffer> best;
    for (const Release& r : releases) {
        if (r.draft || (r.prerelease && !prerelease)) continue;
        if (compareVersions(r.version, current) <= 0) continue;
        if (best && compareVersions(r.version, best->version) <= 0) continue;
        const ReleaseAsset* installer = assetNamed(r, installerName(r.version));
        const ReleaseAsset* sums = assetNamed(r, L"SHA256SUMS.txt");
        const ReleaseAsset* signature = assetNamed(r, L"SHA256SUMS.txt.sig");
        if (!installer || !sums || !signature || !https(installer->url) || !https(sums->url) || !https(signature->url))
            continue;
        best = UpdateOffer{r.version, *installer, *sums, *signature};
    }
    return best;
}

std::optional<std::string> expectedSha256(std::string_view sums, std::wstring_view fileName) {
    const std::string wanted = toUtf8(fileName);
    std::size_t start = 0;
    while (start < sums.size()) {
        std::size_t end = sums.find('\n', start);
        if (end == std::string_view::npos) end = sums.size();
        std::string_view line = sums.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.size() < 66 || !std::all_of(line.begin(), line.begin() + 64, isHex)) continue;
        if (line[64] != ' ') continue;
        std::string_view name = line.substr(65);
        if (!name.empty() && (name.front() == ' ' || name.front() == '*')) name.remove_prefix(1);
        if (name != wanted) continue;
        std::string hash(line.substr(0, 64));
        std::transform(hash.begin(), hash.end(), hash.begin(), [](char c) { return char(c >= 'A' && c <= 'F' ? c + 32 : c); });
        return hash;
    }
    return std::nullopt;
}

UpdateState parseUpdateState(std::string_view text) {
    UpdateState s;
    const auto doc = json::parse(text);
    if (!doc || !doc->isObject()) return s;
    if (const json::Value* v = doc->find("automatic")) s.automatic = v->asBool(true);
    if (const json::Value* v = doc->find("lastCheck")) s.lastCheck = std::int64_t(v->asNumber(0));
    s.readyVersion = field(*doc, "readyVersion");
    s.readyPath = field(*doc, "readyPath");
    if (const json::Value* v = doc->find("readySha256")) s.readySha256 = v->asString("");
    s.attemptedVersion = field(*doc, "attemptedVersion");
    s.lastError = field(*doc, "lastError");
    return s;
}

std::string updateStateJson(const UpdateState& s) {
    json::Value o;
    o.set("automatic", s.automatic);
    o.set("lastCheck", double(s.lastCheck));
    o.set("readyVersion", toUtf8(s.readyVersion));
    o.set("readyPath", toUtf8(s.readyPath));
    o.set("readySha256", s.readySha256);
    o.set("attemptedVersion", toUtf8(s.attemptedVersion));
    o.set("lastError", toUtf8(s.lastError));
    return json::serialize(o);
}

bool checkDue(std::int64_t lastCheck, std::int64_t now, std::int64_t interval) {
    return lastCheck <= 0 || now < lastCheck || now - lastCheck >= interval;
}

StartupAction startupAction(const UpdateState& s, const Version& current, bool installerPresent) {
    if (s.readyVersion.empty()) return StartupAction::None;
    const auto ready = parseVersion(s.readyVersion);
    if (!ready || compareVersions(*ready, current) <= 0 || !installerPresent || s.readySha256.empty())
        return StartupAction::DropStale;
    if (s.attemptedVersion == s.readyVersion) return StartupAction::DropAttempt;
    return StartupAction::Install;
}

} // namespace md
