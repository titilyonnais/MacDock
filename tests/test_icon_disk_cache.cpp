// Plan 68 : cache des icônes du Dock sur disque (dossiers temporaires seulement).
#include <windows.h>

#include <memory>
#include <string>
#include <vector>

#include "minitest.h"
#include "test_helpers.h"
#include "../src/icons/icon_disk_cache.h"

namespace {

md::IconProvider::Image picture(int size, std::uint8_t seed) {
    md::IconProvider::Image im;
    im.size = size;
    im.bgra.resize(std::size_t(size) * size * 4);
    for (std::size_t i = 0; i < im.bgra.size(); ++i) im.bgra[i] = std::uint8_t(seed + i * 7);
    return im;
}

int filesIn(const std::wstring& dir, const wchar_t* pattern) {
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((dir + L"\\" + pattern).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int n = 0;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) ++n;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return n;
}

// Date d'écriture d'un fichier, `minutes` minutes dans le passé.
void setAge(const std::wstring& path, int minutes) {
    HANDLE h = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER t{};
    t.LowPart = now.dwLowDateTime;
    t.HighPart = now.dwHighDateTime;
    t.QuadPart -= ULONGLONG(minutes) * 60 * 10000000ULL;
    FILETIME ft{t.LowPart, t.HighPart};
    SetFileTime(h, nullptr, nullptr, &ft);
    CloseHandle(h);
}

} // namespace

TEST_CASE(icon_disk_key_follows_style_and_size) {
    const md::IconStyle a;
    md::IconStyle dark = a, shadow = a, custom = a, loose = a;
    dark.dark = true;
    shadow.shadowOpacity = 0.25;
    custom.customDir = L"C:\\icônes";
    loose.strict = false;
    const std::wstring k = md::iconDiskKey(L"app:x|160", a);
    CHECK(!k.empty());
    CHECK(k == md::iconDiskKey(L"app:x|160", a));
    CHECK(k != md::iconDiskKey(L"app:x|160", dark));
    CHECK(k != md::iconDiskKey(L"app:x|160", shadow));
    CHECK(k != md::iconDiskKey(L"app:x|160", custom));
    CHECK(k != md::iconDiskKey(L"app:x|160", loose));
    CHECK(k != md::iconDiskKey(L"app:x|96", a));   // autre taille, autre entrée
    CHECK(md::iconStyleFingerprint(a) != md::iconStyleFingerprint(dark));
}

TEST_CASE(icon_disk_file_roundtrip_and_damage) {
    const auto im = picture(16, 3);
    const std::wstring key = L"app:x|16|style:abc";
    const auto bytes = md::encodeIconFile(key, im);
    const auto back = md::decodeIconFile(bytes, key);
    REQUIRE(back != nullptr);
    CHECK(back->size == 16 && back->bgra == im.bgra);
    CHECK(md::sameIcon(*back, im));
    CHECK(!md::sameIcon(*back, picture(16, 4)));
    CHECK(md::decodeIconFile(bytes, L"app:y|16|style:abc") == nullptr);   // autre clé : collision de nom
    auto truncated = bytes;
    truncated.resize(bytes.size() - 1);
    CHECK(md::decodeIconFile(truncated, key) == nullptr);
    auto longer = bytes;
    longer.push_back(0);
    CHECK(md::decodeIconFile(longer, key) == nullptr);
    auto magic = bytes;
    magic[0] ^= 0xFF;
    CHECK(md::decodeIconFile(magic, key) == nullptr);
    auto version = bytes;
    version[4] = 99;
    CHECK(md::decodeIconFile(version, key) == nullptr);   // autre version du format
    auto huge = bytes;
    huge[8] = huge[9] = huge[10] = 0xFF;
    huge[11] = 0x7F;   // taille absurde
    CHECK(md::decodeIconFile(huge, key) == nullptr);
    CHECK(md::decodeIconFile({}, key) == nullptr);
    CHECK(md::decodeIconFile(std::vector<std::uint8_t>(5, 0), key) == nullptr);
}

TEST_CASE(icon_disk_write_is_atomic_and_read_back) {
    const std::wstring dir = md::testTempDir() + L"\\icon-cache";   // créé à la première écriture
    md::IconDiskCache cache(dir);
    const std::wstring key = md::iconDiskKey(L"app:x|16", md::IconStyle{});
    CHECK(cache.read(key) == nullptr);   // absente
    const auto im = picture(16, 5);
    REQUIRE(cache.write(key, im));
    CHECK(filesIn(dir, L"*.tmp") == 0);   // fichier temporaire renommé
    CHECK(filesIn(dir, L"*") == 1);
    const auto back = cache.read(key);
    REQUIRE(back != nullptr);
    CHECK(back->bgra == im.bgra);
    REQUIRE(cache.write(key, picture(16, 9)));   // remplacée en place
    const auto again = cache.read(key);
    REQUIRE(again != nullptr);
    CHECK(again->bgra == picture(16, 9).bgra);
    CHECK(filesIn(dir, L"*") == 1);
}

TEST_CASE(icon_disk_damaged_file_ignored_then_replaced) {
    const std::wstring dir = md::testTempDir();
    md::IconDiskCache cache(dir);
    const std::wstring key = md::iconDiskKey(L"app:abime|16", md::IconStyle{});
    md::testWriteFile(cache.pathFor(key), "pas une icône");
    CHECK(cache.read(key) == nullptr);   // ignoré, sans plantage
    REQUIRE(cache.write(key, picture(16, 1)));
    CHECK(cache.read(key) != nullptr);
}

TEST_CASE(icon_disk_trim_drops_oldest_and_leftovers) {
    const std::wstring dir = md::testTempDir();
    md::IconDiskCache cache(dir, 3, 1 << 20);
    std::vector<std::wstring> keys;
    for (int i = 0; i < 5; ++i) {
        keys.push_back(md::iconDiskKey(L"app:" + std::to_wstring(i) + L"|16", md::IconStyle{}));
        REQUIRE(cache.write(keys.back(), picture(16, std::uint8_t(i))));
        setAge(cache.pathFor(keys.back()), 50 - i * 10);   // la première est la plus ancienne
    }
    md::testWriteFile(cache.pathFor(keys[0]) + L".tmp", "écriture interrompue");
    md::testWriteFile(dir + L"\\autre.txt", "pas au cache");
    cache.touch(keys[1]);   // encore utilisée : devient la plus récente
    cache.trim();
    CHECK(cache.read(keys[0]) == nullptr);   // la plus ancienne
    CHECK(cache.read(keys[1]) != nullptr);   // touchée
    CHECK(cache.read(keys[2]) == nullptr);   // la plus ancienne qui restait
    CHECK(cache.read(keys[3]) != nullptr);
    CHECK(cache.read(keys[4]) != nullptr);
    CHECK(filesIn(dir, L"*.tmp") == 0);
    CHECK(md::testFileExists(dir + L"\\autre.txt"));   // un fichier qui n'est pas au cache n'est jamais effacé
}

TEST_CASE(icon_disk_trim_respects_size_bound) {
    const std::wstring dir = md::testTempDir();
    md::IconDiskCache cache(dir, 300, 10 * 1024);   // une entrée de 32 px pèse un peu plus de 4 Ko : deux tiennent
    std::vector<std::wstring> keys;
    for (int i = 0; i < 4; ++i) {
        keys.push_back(md::iconDiskKey(L"app:" + std::to_wstring(i) + L"|32", md::IconStyle{}));
        REQUIRE(cache.write(keys.back(), picture(32, std::uint8_t(i))));
        setAge(cache.pathFor(keys.back()), 40 - i * 10);
    }
    cache.trim();
    CHECK(cache.read(keys[0]) == nullptr);
    CHECK(cache.read(keys[1]) == nullptr);
    CHECK(cache.read(keys[2]) != nullptr);
    CHECK(cache.read(keys[3]) != nullptr);
}
