// Mises à jour automatiques (plan 53), réseau et empreintes : requêtes HTTPS (WinHTTP, proxy du système) et SHA-256
// (CNG). Jamais de HTTP en clair : une redirection vers HTTP est refusée.
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace md {

// GET en HTTPS : le corps si le serveur répond 200, au plus `maxBytes` ; sinon nullopt et `error` dit pourquoi
// (« hors ligne », « HTTP 403 »…). `accept` : en-tête Accept (vide : aucun).
std::optional<std::string> httpsGet(const std::wstring& url, std::size_t maxBytes, const std::wstring& accept = L"",
                                    std::wstring* error = nullptr);
// Téléchargement vers `path` : écrit d'abord dans « path.part », renommé une fois complet (une coupure ne laisse
// jamais un fichier tronqué sous le vrai nom). Au plus `maxBytes`. `cancel` : événement (HANDLE) qui, levé, arrête
// le téléchargement au morceau suivant (arrêt de MacDock).
bool httpsDownload(const std::wstring& url, const std::wstring& path, std::uint64_t maxBytes, std::wstring* error = nullptr,
                   void* cancel = nullptr);

std::string sha256Hex(std::string_view data);       // minuscules
std::string sha256File(const std::wstring& path);   // "" si illisible

} // namespace md
