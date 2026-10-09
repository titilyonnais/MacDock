// Version de MacDock (plan 52) : lecture des numéros (« v0.52.0 », étiquettes de GitHub) et ordre façon semver, qui
// décide des mises à jour (plan 53).
#include "minitest.h"
#include "../src/core/version.h"

namespace {
md::Version v(int major, int minor, int patch, const wchar_t* pre = L"") { return {major, minor, patch, pre}; }
int cmp(const wchar_t* a, const wchar_t* b) { return md::compareVersions(*md::parseVersion(a), *md::parseVersion(b)); }
} // namespace

TEST_CASE(version_parse_accepts_tags_and_short_forms) {
    CHECK(md::parseVersion(L"0.52.0") == v(0, 52, 0));
    CHECK(md::parseVersion(L"v0.52.0") == v(0, 52, 0));    // étiquette de GitHub
    CHECK(md::parseVersion(L"V1.2.3") == v(1, 2, 3));
    CHECK(md::parseVersion(L"0.52") == v(0, 52, 0));       // correctif omis
    CHECK(md::parseVersion(L"0.53.0-rc.1") == v(0, 53, 0, L"rc.1"));
    CHECK(md::parseVersion(L"1.2.3+build.7") == v(1, 2, 3));   // métadonnées ignorées
    CHECK(md::parseVersion(L"1.2.3-beta+exp") == v(1, 2, 3, L"beta"));
}

TEST_CASE(version_parse_rejects_garbage) {
    for (const wchar_t* bad : {L"", L"v", L"1", L"1.", L"1.x", L"1.2.3.4", L"-1.2.3", L"1.2.-3", L"1.2.3-", L" 1.2.3",
                               L"1.2.3 ", L"99999999999.0.0", L"1..3", L"1.2.3-rc..1", L"latest"})
        CHECK(!md::parseVersion(bad).has_value());
}

TEST_CASE(version_order_like_semver) {
    CHECK(cmp(L"0.52.0", L"0.53.0") < 0);
    CHECK(cmp(L"0.52.1", L"0.52.0") > 0);
    CHECK(cmp(L"1.0.0", L"0.99.9") > 0);
    CHECK(cmp(L"0.52.0", L"v0.52.0") == 0);
    CHECK(cmp(L"0.53.0-rc.1", L"0.53.0") < 0);     // une préversion passe avant sa version
    CHECK(cmp(L"0.53.0-rc.2", L"0.53.0-rc.1") > 0);
    CHECK(cmp(L"0.53.0-rc.10", L"0.53.0-rc.2") > 0);   // champs numériques comparés comme des nombres
    CHECK(cmp(L"1.0.0-alpha", L"1.0.0-beta") < 0);
    CHECK(cmp(L"1.0.0-alpha", L"1.0.0-alpha.1") < 0);  // moins de champs : avant
    CHECK(cmp(L"1.0.0-1", L"1.0.0-alpha") < 0);        // numérique avant texte
}

TEST_CASE(version_current_is_readable) {
    const auto current = md::parseVersion(md::kMacDockVersion);
    REQUIRE(current.has_value());
    CHECK(current->major == MACDOCK_VERSION_MAJOR);
    CHECK(current->minor == MACDOCK_VERSION_MINOR);
    CHECK(current->patch == MACDOCK_VERSION_PATCH);
    CHECK(md::versionText(*current) == std::wstring(L"" MACDOCK_VERSION_STRING));   // préversion comprise
    CHECK(md::versionText(*current) == md::kMacDockVersion);
    CHECK(md::versionText(v(0, 53, 0, L"rc.1")) == L"0.53.0-rc.1");
}
