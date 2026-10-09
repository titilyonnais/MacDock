#include "update_net.h"

#include <windows.h>
#include <bcrypt.h>
#include <winhttp.h>

#include <cstdio>
#include <vector>

#include "../core/version.h"

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")

namespace md {

namespace {

struct Handle {
    HINTERNET h = nullptr;
    explicit Handle(HINTERNET x = nullptr) : h(x) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() {
        if (h) WinHttpCloseHandle(h);
    }
    explicit operator bool() const { return h != nullptr; }
};

std::wstring lastError(const wchar_t* what) {
    const DWORD e = GetLastError();
    if (e == ERROR_WINHTTP_NAME_NOT_RESOLVED || e == ERROR_WINHTTP_CANNOT_CONNECT || e == ERROR_WINHTTP_TIMEOUT)
        return L"hors ligne ou GitHub injoignable";
    wchar_t buf[96];
    swprintf_s(buf, L"%s (%lu)", what, e);
    return buf;
}

// Ouvre la requête et attend la réponse ; `status` : code HTTP. Redirections suivies (HTTPS seulement, réglage par
// défaut de WinHTTP), proxy du système.
bool open(const std::wstring& url, const std::wstring& accept, Handle& session, Handle& connect, Handle& request,
          DWORD& status, std::wstring* error) {
    URL_COMPONENTS parts{sizeof parts};
    wchar_t host[256] = {}, path[2048] = {};
    parts.lpszHostName = host;
    parts.dwHostNameLength = DWORD(std::size(host));
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = DWORD(std::size(path));
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) {
        if (error) *error = L"adresse refusée (HTTPS seulement)";
        return false;
    }
    const std::wstring agent = std::wstring(L"MacDock/") + kMacDockVersion;
    session.h = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        if (error) *error = lastError(L"WinHttpOpen");
        return false;
    }
    WinHttpSetTimeouts(session.h, 10000, 10000, 15000, 30000);
    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(session.h, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof redirect);
    connect.h = WinHttpConnect(session.h, host, parts.nPort, 0);
    if (!connect) {
        if (error) *error = lastError(L"WinHttpConnect");
        return false;
    }
    request.h = WinHttpOpenRequest(connect.h, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                   WINHTTP_FLAG_SECURE);
    if (!request) {
        if (error) *error = lastError(L"WinHttpOpenRequest");
        return false;
    }
    std::wstring headers = L"X-GitHub-Api-Version: 2022-11-28\r\n";
    if (!accept.empty()) headers += L"Accept: " + accept + L"\r\n";
    if (!WinHttpSendRequest(request.h, headers.c_str(), DWORD(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.h, nullptr)) {
        if (error) *error = lastError(L"requête");
        return false;
    }
    DWORD size = sizeof status;
    if (!WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                             &status, &size, WINHTTP_NO_HEADER_INDEX)) {
        if (error) *error = lastError(L"réponse");
        return false;
    }
    if (status != 200) {
        if (error) *error = L"HTTP " + std::to_wstring(status);
        return false;
    }
    return true;
}

// Lit le corps par morceaux ; `sink` reçoit chaque morceau (false : arrêt). Au plus `maxBytes`.
template <class Sink>
bool readBody(HINTERNET request, std::uint64_t maxBytes, Sink&& sink, std::wstring* error) {
    std::vector<char> buf(64 * 1024);
    std::uint64_t total = 0;
    for (;;) {
        DWORD got = 0;
        if (!WinHttpReadData(request, buf.data(), DWORD(buf.size()), &got)) {
            if (error) *error = lastError(L"lecture");
            return false;
        }
        if (got == 0) return true;
        total += got;
        if (total > maxBytes) {
            if (error) *error = L"réponse trop grande";
            return false;
        }
        if (!sink(buf.data(), got)) {
            if (error) *error = L"écriture impossible";
            return false;
        }
    }
}

struct Sha256 {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    bool ok = false;
    Sha256() {
        ok = BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) &&
             BCRYPT_SUCCESS(BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0));
    }
    ~Sha256() {
        if (hash) BCryptDestroyHash(hash);
        if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    }
    void add(const void* p, std::size_t n) {
        const auto* b = static_cast<const unsigned char*>(p);
        while (ok && n > 0) {   // BCryptHashData prend une taille sur 32 bits
            const ULONG chunk = ULONG(n > 0x40000000 ? 0x40000000 : n);
            ok = BCRYPT_SUCCESS(BCryptHashData(hash, const_cast<PUCHAR>(b), chunk, 0));
            b += chunk;
            n -= chunk;
        }
    }
    std::string hex() {
        unsigned char digest[32] = {};
        if (!ok || !BCRYPT_SUCCESS(BCryptFinishHash(hash, digest, sizeof digest, 0))) return {};
        static const char* digits = "0123456789abcdef";
        std::string out;
        for (unsigned char c : digest) {
            out += digits[c >> 4];
            out += digits[c & 15];
        }
        return out;
    }
};

} // namespace

std::optional<std::string> httpsGet(const std::wstring& url, std::size_t maxBytes, const std::wstring& accept, std::wstring* error) {
    Handle session, connect, request;
    DWORD status = 0;
    if (!open(url, accept, session, connect, request, status, error)) return std::nullopt;
    std::string body;
    if (!readBody(request.h, maxBytes, [&](const char* p, DWORD n) { body.append(p, n); return true; }, error))
        return std::nullopt;
    return body;
}

bool httpsDownload(const std::wstring& url, const std::wstring& path, std::uint64_t maxBytes, std::wstring* error) {
    Handle session, connect, request;
    DWORD status = 0;
    if (!open(url, L"application/octet-stream", session, connect, request, status, error)) return false;
    const std::wstring part = path + L".part";
    HANDLE f = CreateFileW(part.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        if (error) *error = lastError(L"fichier");
        return false;
    }
    const bool ok = readBody(request.h, maxBytes, [&](const char* p, DWORD n) {
        DWORD written = 0;
        return WriteFile(f, p, n, &written, nullptr) && written == n;
    }, error);
    const bool flushed = FlushFileBuffers(f) != FALSE;
    CloseHandle(f);
    if (!ok || !flushed || !MoveFileExW(part.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        if (ok && error) *error = lastError(L"renommage");
        DeleteFileW(part.c_str());
        return false;
    }
    return true;
}

std::string sha256Hex(std::string_view data) {
    Sha256 h;
    h.add(data.data(), data.size());
    return h.hex();
}

std::string sha256File(const std::wstring& path) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f == INVALID_HANDLE_VALUE) return {};
    Sha256 h;
    std::vector<char> buf(256 * 1024);
    bool ok = true;
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(f, buf.data(), DWORD(buf.size()), &got, nullptr)) {
            ok = false;
            break;
        }
        if (got == 0) break;
        h.add(buf.data(), got);
    }
    CloseHandle(f);
    return ok ? h.hex() : std::string();
}

} // namespace md
