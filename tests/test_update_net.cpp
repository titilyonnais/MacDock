// Mises à jour (plan 53) : empreintes SHA-256 (vecteurs du NIST), signature ECDSA P-256 des versions (clé jetable :
// signée, refusée dès qu'un octet change), refus de tout ce qui n'est pas HTTPS. Aucun accès au réseau : la suite
// tourne souvent, et l'API de GitHub limite les requêtes (les vraies recherches de MacDock en pâtiraient).
#include <windows.h>
#include <bcrypt.h>

#include <cstdio>
#include <string>
#include <vector>

#include "minitest.h"
#include "../src/update/update_net.h"
#include "../src/update/update_sign.h"

#pragma comment(lib, "bcrypt.lib")

namespace {

// Clé ECDSA P-256 jetable : sa clé publique (BCRYPT_ECCPUBLIC_BLOB) et la signature P1363 (r‖s) de `data`.
struct TestKey {
    BCRYPT_KEY_HANDLE key = nullptr;
    TestKey() {
        BCryptGenerateKeyPair(BCRYPT_ECDSA_P256_ALG_HANDLE, &key, 256, 0);
        BCryptFinalizeKeyPair(key, 0);
    }
    ~TestKey() {
        if (key) BCryptDestroyKey(key);
    }
    std::vector<unsigned char> publicBlob() const {
        ULONG size = 0;
        BCryptExportKey(key, nullptr, BCRYPT_ECCPUBLIC_BLOB, nullptr, 0, &size, 0);
        std::vector<unsigned char> blob(size);
        BCryptExportKey(key, nullptr, BCRYPT_ECCPUBLIC_BLOB, blob.data(), size, &size, 0);
        return blob;
    }
    std::vector<unsigned char> sign(const std::string& data) const {
        unsigned char hash[32] = {};
        BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, PUCHAR(data.data()), ULONG(data.size()), hash, sizeof hash);
        ULONG size = 0;
        BCryptSignHash(key, nullptr, hash, sizeof hash, nullptr, 0, &size, 0);
        std::vector<unsigned char> sig(size);
        BCryptSignHash(key, nullptr, hash, sizeof hash, sig.data(), size, &size, 0);
        sig.resize(size);
        return sig;
    }
};

std::string base64(const std::vector<unsigned char>& bytes) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const unsigned v = bytes[i] << 16 | (i + 1 < bytes.size() ? bytes[i + 1] << 8 : 0) | (i + 2 < bytes.size() ? bytes[i + 2] : 0);
        out += t[v >> 18 & 63];
        out += t[v >> 12 & 63];
        out += i + 1 < bytes.size() ? t[v >> 6 & 63] : '=';
        out += i + 2 < bytes.size() ? t[v & 63] : '=';
    }
    return out;
}

const std::string kSums = "05d931b8efa699bb3956e19162f0a3fe5b3beb230a6738b662dc0989872d0c93  MacDock-Setup-0.52.0.exe\r\n";

} // namespace

TEST_CASE(update_sha256_nist_vectors) {
    CHECK(md::sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(md::sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(md::sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    const std::wstring path = std::wstring(tmp) + L"macdock-sha-test-" + std::to_wstring(GetCurrentProcessId()) + L".bin";
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") == 0 && f) {
        fwrite("abc", 1, 3, f);
        fclose(f);
    }
    CHECK(md::sha256File(path) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    DeleteFileW(path.c_str());
    CHECK(md::sha256File(path).empty());   // disparu : illisible
}

TEST_CASE(update_signature_verified) {
    TestKey key;
    const auto pub = key.publicBlob();
    const auto sig = key.sign(kSums);
    REQUIRE(sig.size() == 64);   // P1363 : r‖s
    CHECK(md::verifySignature(kSums, sig, pub));
    CHECK(md::verifyReleaseSignature(kSums, base64(sig) + "\r\n", pub));   // fichier .sig : base64, fin de ligne
    std::string changed = kSums;
    changed[0] = changed[0] == '0' ? '1' : '0';   // une autre empreinte : refusée
    CHECK(!md::verifySignature(changed, sig, pub));
    auto badSig = sig;
    badSig[10] ^= 1;
    CHECK(!md::verifySignature(kSums, badSig, pub));
    TestKey other;   // signée par une autre clé que celle de MacDock : refusée
    CHECK(!md::verifySignature(kSums, other.sign(kSums), pub));
    CHECK(!md::verifySignature(kSums, {}, pub));
    CHECK(!md::verifySignature(kSums, sig, {1, 2, 3}));   // clé publique illisible
    CHECK(!md::verifyReleaseSignature(kSums, "pas du base64 !", pub));
    CHECK(!md::verifyReleaseSignature(kSums, "", pub));
}

TEST_CASE(update_base64_decode) {
    CHECK(md::decodeBase64("YWJj") == std::optional<std::vector<unsigned char>>(std::vector<unsigned char>{'a', 'b', 'c'}));
    CHECK(md::decodeBase64("YW Jj\r\n") == std::optional<std::vector<unsigned char>>(std::vector<unsigned char>{'a', 'b', 'c'}));
    CHECK(md::decodeBase64("YQ==") == std::optional<std::vector<unsigned char>>(std::vector<unsigned char>{'a'}));
    CHECK(!md::decodeBase64("Y!Jj").has_value());
    CHECK(!md::decodeBase64("").has_value());
}

TEST_CASE(update_https_only) {
    std::wstring error;
    CHECK(!md::httpsGet(L"http://api.github.com/repos/x", 1000, L"", &error).has_value());   // refusé avant tout réseau
    CHECK(!error.empty());
    CHECK(!md::httpsGet(L"pas une adresse", 1000).has_value());
    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    const std::wstring path = std::wstring(tmp) + L"macdock-dl-test.exe";
    CHECK(!md::httpsDownload(L"ftp://example.com/a.exe", path, 1000));
    CHECK(GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES);   // rien d'écrit
}
