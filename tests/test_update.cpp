// Mises à jour automatiques (plan 53), logique pure : réponses de l'API de GitHub (forme réelle de
// releases/latest et de releases), choix de la version, empreintes, état gardé, décision au démarrage.
#include <string>

#include "minitest.h"
#include "../src/update/update_logic.h"

namespace {

// Réponse de releases (un tableau), réduite aux champs utiles, comme les renvoie l'API (champs en plus ignorés).
const char* kReleases = R"([
  {"url": "https://api.github.com/repos/titilyonnais/MacDock/releases/3", "tag_name": "v0.53.1-rc.1",
   "name": "MacDock 0.53.1-rc.1", "draft": false, "prerelease": true, "body": "Préversion d'essai\r\n",
   "assets": [
     {"name": "MacDock-Setup-0.53.1-rc.1.exe", "size": 3500000, "content_type": "application/x-msdownload",
      "browser_download_url": "https://github.com/titilyonnais/MacDock/releases/download/v0.53.1-rc.1/MacDock-Setup-0.53.1-rc.1.exe"},
     {"name": "SHA256SUMS.txt", "size": 99,
      "browser_download_url": "https://github.com/titilyonnais/MacDock/releases/download/v0.53.1-rc.1/SHA256SUMS.txt"},
     {"name": "SHA256SUMS.txt.sig", "size": 88,
      "browser_download_url": "https://github.com/titilyonnais/MacDock/releases/download/v0.53.1-rc.1/SHA256SUMS.txt.sig"}]},
  {"tag_name": "v0.53.0", "draft": false, "prerelease": false, "body": "Notes \"citées\" et \u00e9chappées",
   "assets": [
     {"name": "MacDock-Setup-0.53.0.exe", "size": 3450000,
      "browser_download_url": "https://github.com/titilyonnais/MacDock/releases/download/v0.53.0/MacDock-Setup-0.53.0.exe"},
     {"name": "SHA256SUMS.txt", "size": 92,
      "browser_download_url": "https://github.com/titilyonnais/MacDock/releases/download/v0.53.0/SHA256SUMS.txt"},
     {"name": "SHA256SUMS.txt.sig", "size": 88,
      "browser_download_url": "https://github.com/titilyonnais/MacDock/releases/download/v0.53.0/SHA256SUMS.txt.sig"}]},
  {"tag_name": "v0.54.0", "draft": true, "prerelease": false, "assets": []},
  {"tag_name": "nightly", "draft": false, "prerelease": false, "assets": []},
  {"tag_name": "v0.52.0", "draft": false, "prerelease": false,
   "assets": [
     {"name": "MacDock-Setup-0.52.0.exe", "size": 3437108,
      "browser_download_url": "https://github.com/titilyonnais/MacDock/releases/download/v0.52.0/MacDock-Setup-0.52.0.exe"},
     {"name": "SHA256SUMS.txt", "size": 92,
      "browser_download_url": "https://github.com/titilyonnais/MacDock/releases/download/v0.52.0/SHA256SUMS.txt"}]}
])";

md::Version ver(const wchar_t* s) { return *md::parseVersion(s); }

} // namespace

TEST_CASE(update_parse_releases) {
    const auto r = md::parseReleases(kReleases);
    REQUIRE(r.size() == 4);   // « nightly » écartée : pas une version
    CHECK(r[0].tag == L"v0.53.1-rc.1");
    CHECK(r[0].prerelease);
    CHECK(r[0].version == ver(L"0.53.1-rc.1"));
    REQUIRE(r[1].assets.size() == 3);
    CHECK(r[1].assets[0].name == L"MacDock-Setup-0.53.0.exe");
    CHECK(r[1].assets[0].size == 3450000u);
    CHECK(r[1].assets[0].url == L"https://github.com/titilyonnais/MacDock/releases/download/v0.53.0/MacDock-Setup-0.53.0.exe");
    CHECK(r[2].draft);
    // releases/latest renvoie un seul objet.
    const auto latest = md::parseReleases(R"({"tag_name": "v0.52.0", "draft": false, "prerelease": false, "assets": []})");
    REQUIRE(latest.size() == 1);
    CHECK(latest[0].version == ver(L"0.52.0"));
    CHECK(md::parseReleases("pas du JSON").empty());
    CHECK(md::parseReleases(R"({"message": "API rate limit exceeded"})").empty());   // limite de l'API atteinte
}

TEST_CASE(update_pick_newest_published) {
    const auto r = md::parseReleases(kReleases);
    const auto offer = md::pickUpdate(r, ver(L"0.52.0"), false);
    REQUIRE(offer.has_value());
    CHECK(offer->version == ver(L"0.53.0"));           // ni la préversion, ni le brouillon
    CHECK(offer->installer.name == L"MacDock-Setup-0.53.0.exe");
    CHECK(offer->sums.name == L"SHA256SUMS.txt");
    CHECK(offer->signature.name == L"SHA256SUMS.txt.sig");
    const auto pre = md::pickUpdate(r, ver(L"0.52.0"), true);
    REQUIRE(pre.has_value());
    CHECK(pre->version == ver(L"0.53.1-rc.1"));        // essais : préversions comprises
    CHECK(!md::pickUpdate(r, ver(L"0.53.0"), false).has_value());   // déjà à jour
    CHECK(!md::pickUpdate(r, ver(L"0.60.0"), true).has_value());    // compilée à la main, plus récente
    // Sans installateur, sans empreintes ou sans signature : rien à proposer. 0.52.0, publiée avant les signatures,
    // n'est pas une mise à jour signée.
    const auto bare = md::parseReleases(R"([{"tag_name": "v0.99.0", "draft": false, "prerelease": false,
        "assets": [{"name": "MacDock-Setup-0.99.0.exe", "size": 1, "browser_download_url": "https://github.com/x"}]}])");
    CHECK(!md::pickUpdate(bare, ver(L"0.52.0"), false).has_value());
    const auto unsigned_ = md::parseReleases(R"([{"tag_name": "v0.99.0", "draft": false, "prerelease": false, "assets": [
        {"name": "MacDock-Setup-0.99.0.exe", "size": 1, "browser_download_url": "https://github.com/a.exe"},
        {"name": "SHA256SUMS.txt", "size": 1, "browser_download_url": "https://github.com/s.txt"}]}])");
    CHECK(!md::pickUpdate(unsigned_, ver(L"0.52.0"), false).has_value());
    // Une adresse qui n'est pas en HTTPS n'est jamais suivie.
    const auto http = md::parseReleases(R"([{"tag_name": "v0.99.0", "draft": false, "prerelease": false, "assets": [
        {"name": "MacDock-Setup-0.99.0.exe", "size": 1, "browser_download_url": "http://github.com/a.exe"},
        {"name": "SHA256SUMS.txt", "size": 1, "browser_download_url": "https://github.com/s.txt"},
        {"name": "SHA256SUMS.txt.sig", "size": 1, "browser_download_url": "https://github.com/s.sig"}]}])");
    CHECK(!md::pickUpdate(http, ver(L"0.52.0"), false).has_value());
}

TEST_CASE(update_expected_sha256) {
    const std::string line = "d3e3c2a044962a450db1a6b4c66999507d319d2ac887baf314beadba00300257  MacDock-Setup-0.52.0.exe";
    const std::optional<std::string> hash("d3e3c2a044962a450db1a6b4c66999507d319d2ac887baf314beadba00300257");
    CHECK(md::expectedSha256(line + "\r\n", L"MacDock-Setup-0.52.0.exe") == hash);   // tel qu'écrit par make-installer
    CHECK(md::expectedSha256(line + "\n", L"MacDock-Setup-0.52.0.exe") == hash);
    CHECK(md::expectedSha256(line, L"MacDock-Setup-0.52.0.exe") == hash);
    CHECK(md::expectedSha256("D3E3C2A044962A450DB1A6B4C66999507D319D2AC887BAF314BEADBA00300258 *Autre.exe\n", L"Autre.exe") ==
          std::optional<std::string>("d3e3c2a044962a450db1a6b4c66999507d319d2ac887baf314beadba00300258"));   // « * », minuscules
    CHECK(!md::expectedSha256(line + "\r\n", L"MacDock-Setup-0.53.0.exe").has_value());
    CHECK(!md::expectedSha256("abc  MacDock-Setup-0.52.0.exe", L"MacDock-Setup-0.52.0.exe").has_value());   // trop court
    CHECK(!md::expectedSha256("", L"x.exe").has_value());
    // Relecture du plan 53, important 2 : un SHA256SUMS.txt signé ne vaut que pour son seul installateur. Une ligne de
    // plus (glissée avant la signature) ne doit jamais faire accepter un autre installateur, ni celui-ci.
    const std::string extra = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa  MacDock-Setup-9.0.0.exe\r\n";
    CHECK(!md::expectedSha256(line + "\r\n" + extra, L"MacDock-Setup-9.0.0.exe").has_value());
    CHECK(!md::expectedSha256(line + "\r\n" + extra, L"MacDock-Setup-0.52.0.exe").has_value());
    CHECK(!md::expectedSha256(extra + line + "\r\n", L"MacDock-Setup-0.52.0.exe").has_value());
    CHECK(!md::expectedSha256(line + "\r\n" + line + "\r\n", L"MacDock-Setup-0.52.0.exe").has_value());   // en double
    CHECK(!md::expectedSha256(line + "\r\nnote\r\n", L"MacDock-Setup-0.52.0.exe").has_value());           // texte en plus
}

TEST_CASE(update_prerelease_needs_prerelease_mode) {
    // Relecture du plan 53 : le drapeau « préversion » vient de l'API, pas de la signature. Une préversion signée pour
    // les essais (0.53.1-rc.1), republiée comme version normale, n'est jamais proposée hors mode préversion : seul le
    // numéro, inclus dans le nom de l'installateur signé, décide.
    const auto r = md::parseReleases(R"([{"tag_name": "v0.53.1-rc.1", "draft": false, "prerelease": false, "assets": [
        {"name": "MacDock-Setup-0.53.1-rc.1.exe", "size": 1, "browser_download_url": "https://github.com/a.exe"},
        {"name": "SHA256SUMS.txt", "size": 1, "browser_download_url": "https://github.com/s.txt"},
        {"name": "SHA256SUMS.txt.sig", "size": 1, "browser_download_url": "https://github.com/s.sig"}]}])");
    CHECK(!md::pickUpdate(r, ver(L"0.53.0"), false).has_value());
    CHECK(md::pickUpdate(r, ver(L"0.53.0"), true).has_value());   // essais (MACDOCK_UPDATE_PRERELEASE=1)
}

TEST_CASE(update_check_due) {
    const std::int64_t h = 3600;
    CHECK(md::checkDue(0, 1000));                        // jamais cherché
    CHECK(!md::checkDue(100 * h, 100 * h + 11 * h));
    CHECK(md::checkDue(100 * h, 100 * h + 12 * h));
    CHECK(md::checkDue(100 * h, 50 * h));                // horloge reculée
}

TEST_CASE(update_state_round_trip) {
    md::UpdateState s;
    s.automatic = false;
    s.lastCheck = 1791500000;
    s.readyVersion = L"0.53.0";
    s.readyPath = L"C:\\Users\\alice\\AppData\\Local\\MacDock\\updates\\MacDock-Setup-0.53.0.exe";
    s.readySha256 = "d3e3c2a044962a450db1a6b4c66999507d319d2ac887baf314beadba00300257";
    s.attemptedVersion = L"0.53.0";
    s.lastError = L"GitHub injoignable";
    const md::UpdateState back = md::parseUpdateState(md::updateStateJson(s));
    CHECK(!back.automatic);
    CHECK(back.lastCheck == s.lastCheck);
    CHECK(back.readyVersion == s.readyVersion);
    CHECK(back.readyPath == s.readyPath);
    CHECK(back.readySha256 == s.readySha256);
    CHECK(back.attemptedVersion == s.attemptedVersion);
    CHECK(back.lastError == s.lastError);
    const md::UpdateState empty = md::parseUpdateState("n'importe quoi");   // fichier abîmé : valeurs par défaut
    CHECK(empty.automatic);
    CHECK(empty.readyVersion.empty());
}

TEST_CASE(update_startup_action) {
    md::UpdateState s;
    s.readyVersion = L"0.53.0";
    s.readyPath = L"x.exe";
    s.readySha256 = "aa";
    CHECK(md::startupAction(s, ver(L"0.52.0"), true) == md::StartupAction::Install);
    CHECK(md::startupAction(s, ver(L"0.52.0"), false) == md::StartupAction::DropStale);   // installateur effacé
    CHECK(md::startupAction(s, ver(L"0.53.0"), true) == md::StartupAction::DropStale);    // déjà installée
    s.attemptedVersion = L"0.53.0";   // lancée au démarrage précédent, et pourtant toujours 0.52.0 : abandon
    CHECK(md::startupAction(s, ver(L"0.52.0"), true) == md::StartupAction::DropAttempt);
    CHECK(md::startupAction(md::UpdateState{}, ver(L"0.52.0"), false) == md::StartupAction::None);
}

TEST_CASE(update_list_endpoint_and_signed_fallback) {
    // Essai de bout en bout du plan 53 : une version tout juste publiée n'est pas encore signée (quelques minutes) ; la
    // liste des versions (et non releases/latest) permet de proposer la précédente, signée.
    CHECK(md::releasesUrl(L"titilyonnais/MacDock") == L"https://api.github.com/repos/titilyonnais/MacDock/releases?per_page=20");
    const auto r = md::parseReleases(R"([
      {"tag_name": "v0.54.0", "draft": false, "prerelease": false, "assets": [
        {"name": "MacDock-Setup-0.54.0.exe", "size": 1, "browser_download_url": "https://github.com/a.exe"},
        {"name": "SHA256SUMS.txt", "size": 1, "browser_download_url": "https://github.com/s.txt"}]},
      {"tag_name": "v0.53.0", "draft": false, "prerelease": false, "assets": [
        {"name": "MacDock-Setup-0.53.0.exe", "size": 1, "browser_download_url": "https://github.com/b.exe"},
        {"name": "SHA256SUMS.txt", "size": 1, "browser_download_url": "https://github.com/t.txt"},
        {"name": "SHA256SUMS.txt.sig", "size": 1, "browser_download_url": "https://github.com/t.sig"}]}])");
    const auto offer = md::pickUpdate(r, *md::parseVersion(L"0.52.0"), false);
    REQUIRE(offer.has_value());
    CHECK(offer->version == *md::parseVersion(L"0.53.0"));
}

TEST_CASE(update_only_from_installed_copy) {   // relecture du plan 53 (copie d'essai ou compilée à la main)
    // Seule la copie posée par l'installateur se met à jour : une copie compilée à la main ou la variante d'essai
    // installerait la vraie ailleurs et y renverrait le démarrage avec Windows.
    const std::wstring installed = L"C:\\Users\\alice\\AppData\\Local\\Programs\\MacDock\\";   // InstallLocation d'Inno
    CHECK(md::installedCopy(L"C:\\Users\\alice\\AppData\\Local\\Programs\\MacDock", installed));
    CHECK(md::installedCopy(L"c:\\users\\ALICE\\appdata\\local\\programs\\macdock\\", installed));
    CHECK(!md::installedCopy(L"C:\\src\\macos-dock\\build\\Release", installed));
    CHECK(!md::installedCopy(L"C:\\Users\\alice\\AppData\\Local\\Programs\\MacDock-essai", installed));
    CHECK(!md::installedCopy(L"C:\\Users\\alice\\AppData\\Local\\Programs", installed));
    CHECK(!md::installedCopy(L"C:\\Users\\alice\\AppData\\Local\\Programs\\MacDock", L""));   // jamais installé
    CHECK(!md::installedCopy(L"", L""));
}
