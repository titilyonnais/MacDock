#include "update_sign.h"

#include <windows.h>
#include <bcrypt.h>

#pragma comment(lib, "bcrypt.lib")

namespace md {

namespace {

int base64Value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

} // namespace

std::optional<std::vector<unsigned char>> decodeBase64(std::string_view text) {
    std::vector<int> values;
    std::size_t padding = 0;
    for (char c : text) {
        if (c == ' ' || c == '\r' || c == '\n' || c == '\t') continue;
        if (c == '=') {
            ++padding;
            continue;
        }
        if (padding > 0) return std::nullopt;   // du texte après « = »
        const int v = base64Value(c);
        if (v < 0) return std::nullopt;
        values.push_back(v);
    }
    if (values.empty() || padding > 2 || (values.size() + padding) % 4 != 0) return std::nullopt;
    std::vector<unsigned char> out;
    for (std::size_t i = 0; i + 1 < values.size(); i += 4) {
        const unsigned v = unsigned(values[i]) << 18 | unsigned(values[i + 1]) << 12 |
                           (i + 2 < values.size() ? unsigned(values[i + 2]) << 6 : 0) | (i + 3 < values.size() ? unsigned(values[i + 3]) : 0);
        out.push_back(static_cast<unsigned char>(v >> 16));
        if (i + 2 < values.size()) out.push_back(static_cast<unsigned char>(v >> 8 & 0xFF));
        if (i + 3 < values.size()) out.push_back(static_cast<unsigned char>(v & 0xFF));
    }
    return out;
}

bool verifySignature(std::string_view data, const std::vector<unsigned char>& signature, const std::vector<unsigned char>& publicBlob) {
    if (signature.size() != 64 || publicBlob.size() != sizeof(BCRYPT_ECCKEY_BLOB) + 64) return false;
    const auto* header = reinterpret_cast<const BCRYPT_ECCKEY_BLOB*>(publicBlob.data());
    if (header->dwMagic != BCRYPT_ECDSA_PUBLIC_P256_MAGIC || header->cbKey != 32) return false;
    BCRYPT_KEY_HANDLE key = nullptr;
    if (!BCRYPT_SUCCESS(BCryptImportKeyPair(BCRYPT_ECDSA_P256_ALG_HANDLE, nullptr, BCRYPT_ECCPUBLIC_BLOB, &key,
                                            const_cast<PUCHAR>(publicBlob.data()), ULONG(publicBlob.size()), 0)))
        return false;
    unsigned char hash[32] = {};
    const bool ok = BCRYPT_SUCCESS(BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, PUCHAR(data.data()), ULONG(data.size()), hash,
                                              sizeof hash)) &&
                    BCRYPT_SUCCESS(BCryptVerifySignature(key, nullptr, hash, sizeof hash, const_cast<PUCHAR>(signature.data()),
                                                         ULONG(signature.size()), 0));
    BCryptDestroyKey(key);
    return ok;
}

bool verifyReleaseSignature(std::string_view sums, std::string_view sigBase64, const std::vector<unsigned char>& publicBlob) {
    const auto sig = decodeBase64(sigBase64);
    return sig && verifySignature(sums, *sig, publicBlob);
}

} // namespace md
