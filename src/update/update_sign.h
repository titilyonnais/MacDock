// Signature des versions (plan 53) : ECDSA P-256 sur l'empreinte SHA-256 de SHA256SUMS.txt, au format IEEE P1363
// (r‖s, 64 octets), écrite en base64 dans SHA256SUMS.txt.sig par tools/sign-release.ps1 avec la clé « MacDock
// Release » du PC de l'auteur. Vérifiée avec la clé publique inscrite dans MacDock (BCRYPT_ECCPUBLIC_BLOB) : une version
// mal signée n'est jamais installée.
#pragma once
#include <optional>
#include <string_view>
#include <vector>

namespace md {

bool verifySignature(std::string_view data, const std::vector<unsigned char>& signature,
                     const std::vector<unsigned char>& publicBlob);
// Contenu d'un fichier .sig (base64, espaces et fins de ligne ignorés) vérifié sur `sums`.
bool verifyReleaseSignature(std::string_view sums, std::string_view sigBase64, const std::vector<unsigned char>& publicBlob);
// Base64 standard (« + », « / », « = » final) ; espaces et fins de ligne ignorés ; nullopt si invalide ou vide.
std::optional<std::vector<unsigned char>> decodeBase64(std::string_view text);

} // namespace md
