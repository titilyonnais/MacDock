#include "icon_disk_cache.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cwchar>

namespace md {
namespace {

constexpr std::uint32_t kMagic = 0x4349444D;   // « MDIC »
constexpr std::uint32_t kVersion = 1;
constexpr std::size_t kHeader = 16;
constexpr std::uint32_t kMaxSize = 1024;         // côté d'une image, en pixels
constexpr std::uint32_t kMaxKeyChars = 1 << 16;  // une pile garde ses fichiers dans sa clé
constexpr std::uint64_t kMaxFile = kHeader + std::uint64_t(kMaxKeyChars) * 2 + std::uint64_t(kMaxSize) * kMaxSize * 4;

std::uint64_t fnv(const void* data, std::size_t n, std::uint64_t h = 1469598103934665603ull) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

template <class T>
std::uint64_t fnvValue(const T& v, std::uint64_t h) {
    return fnv(&v, sizeof v, h);
}

std::wstring hex(std::uint64_t v) {
    wchar_t buf[17];
    swprintf_s(buf, L"%016llx", static_cast<unsigned long long>(v));
    return buf;
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(std::uint8_t(v >> (8 * i)));
}

std::uint32_t get32(const std::uint8_t* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}

constexpr wchar_t kExtension[] = L".mdicon";
constexpr wchar_t kTemporary[] = L".mdicon.tmp";

} // namespace

std::uint64_t iconStyleFingerprint(const IconStyle& s) {
    std::uint64_t h = fnvValue(std::uint8_t(s.strict ? 1 : 0), 1469598103934665603ull);
    h = fnvValue(std::uint8_t(s.dark ? 1 : 0), h);
    h = fnvValue(s.shapeRatio, h);
    h = fnvValue(s.cornerRatio, h);
    h = fnvValue(s.jailInset, h);
    h = fnvValue(s.shadowOpacity, h);
    return fnv(s.customDir.data(), s.customDir.size() * sizeof(wchar_t), h);
}

std::wstring iconDiskKey(const std::wstring& cacheKey, const IconStyle& style) {
    return cacheKey + L"|style:" + hex(iconStyleFingerprint(style));
}

std::wstring iconDiskSlot(const IconJob& job) {
    if (job.spec.kind == IconJobKind::Stack) return L"#stack|" + job.spec.key + L"|" + std::to_wstring(job.spec.px);
    return job.cacheKey;
}

std::vector<std::uint8_t> encodeIconFile(const std::wstring& diskKey, const IconProvider::Image& image) {
    std::vector<std::uint8_t> out;
    out.reserve(kHeader + diskKey.size() * 2 + image.bgra.size());
    put32(out, kMagic);
    put32(out, kVersion);
    put32(out, std::uint32_t(image.size));
    put32(out, std::uint32_t(diskKey.size()));
    for (wchar_t c : diskKey) {
        out.push_back(std::uint8_t(c));
        out.push_back(std::uint8_t(c >> 8));
    }
    out.insert(out.end(), image.bgra.begin(), image.bgra.end());
    return out;
}

IconProvider::ImagePtr decodeIconFile(const std::vector<std::uint8_t>& bytes, const std::wstring& diskKey) {
    if (bytes.size() < kHeader) return nullptr;
    const std::uint32_t magic = get32(&bytes[0]), version = get32(&bytes[4]), size = get32(&bytes[8]),
                        chars = get32(&bytes[12]);
    if (magic != kMagic || version != kVersion || size == 0 || size > kMaxSize || chars != diskKey.size()) return nullptr;
    const std::uint64_t pixels = std::uint64_t(size) * size * 4;
    if (bytes.size() != kHeader + std::uint64_t(chars) * 2 + pixels) return nullptr;
    for (std::size_t i = 0; i < chars; ++i)
        if (wchar_t(bytes[kHeader + 2 * i] | (bytes[kHeader + 2 * i + 1] << 8)) != diskKey[i]) return nullptr;
    auto image = std::make_shared<IconProvider::Image>();
    image->size = int(size);
    image->bgra.assign(bytes.begin() + std::ptrdiff_t(kHeader + std::size_t(chars) * 2), bytes.end());
    return image;
}

bool sameIcon(const IconProvider::Image& a, const IconProvider::Image& b) {
    return a.size == b.size && a.bgra == b.bgra;
}

IconDiskCache::IconDiskCache(std::wstring dir, std::size_t maxEntries, std::uint64_t maxBytes)
    : dir_(std::move(dir)), maxEntries_(maxEntries), maxBytes_(maxBytes) {}

std::wstring IconDiskCache::pathFor(const std::wstring& diskKey) const {
    return dir_ + L"\\" + hex(fnv(diskKey.data(), diskKey.size() * sizeof(wchar_t))) + kExtension;
}

IconProvider::ImagePtr IconDiskCache::read(const std::wstring& diskKey) const {
    HANDLE h = CreateFileW(pathFor(diskKey).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) return nullptr;
    std::vector<std::uint8_t> bytes;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(h, &size) && size.QuadPart >= LONGLONG(kHeader) && std::uint64_t(size.QuadPart) <= kMaxFile) {
        bytes.resize(std::size_t(size.QuadPart));
        DWORD got = 0;
        if (!ReadFile(h, bytes.data(), DWORD(bytes.size()), &got, nullptr) || got != bytes.size()) bytes.clear();
    }
    CloseHandle(h);
    return bytes.empty() ? nullptr : decodeIconFile(bytes, diskKey);
}

bool IconDiskCache::write(const std::wstring& diskKey, const IconProvider::Image& image) {
    if (image.size <= 0 || std::uint32_t(image.size) > kMaxSize || diskKey.size() > kMaxKeyChars ||
        image.bgra.size() != std::size_t(image.size) * std::size_t(image.size) * 4)
        return false;
    if (GetFileAttributesW(dir_.c_str()) == INVALID_FILE_ATTRIBUTES)
        SHCreateDirectoryExW(nullptr, dir_.c_str(), nullptr);   // dossiers parents compris
    const std::wstring path = pathFor(diskKey), temporary = dir_ + L"\\" + path.substr(dir_.size() + 1, 16) + kTemporary;
    const std::vector<std::uint8_t> bytes = encodeIconFile(diskKey, image);
    HANDLE h = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0;
    const bool written = WriteFile(h, bytes.data(), DWORD(bytes.size()), &put, nullptr) && put == bytes.size();
    CloseHandle(h);
    if (!written || !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(temporary.c_str());
        return false;
    }
    return true;
}

void IconDiskCache::touch(const std::wstring& diskKey) {
    HANDLE h = CreateFileW(pathFor(diskKey).c_str(), FILE_WRITE_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    SetFileTime(h, nullptr, nullptr, &now);
    CloseHandle(h);
}

void IconDiskCache::remove(const std::wstring& diskKey) { DeleteFileW(pathFor(diskKey).c_str()); }

void IconDiskCache::trim() {
    struct Entry {
        std::wstring path;
        std::uint64_t bytes = 0, written = 0;
    };
    std::vector<Entry> entries;
    WIN32_FIND_DATAW fd{};
    HANDLE find = FindFirstFileW((dir_ + L"\\*").c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        const std::wstring name = fd.cFileName, path = dir_ + L"\\" + name;
        if (name.ends_with(kTemporary)) {   // écriture interrompue (arrêt) : jamais renommée
            DeleteFileW(path.c_str());
            continue;
        }
        if (!name.ends_with(kExtension)) continue;   // pas au cache : laissé tel quel
        entries.push_back({path, (std::uint64_t(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow,
                           (std::uint64_t(fd.ftLastWriteTime.dwHighDateTime) << 32) | fd.ftLastWriteTime.dwLowDateTime});
    } while (FindNextFileW(find, &fd));
    FindClose(find);
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.written > b.written; });
    std::uint64_t total = 0;   // les plus récentes d'abord : on garde tant que la borne le permet
    for (std::size_t i = 0; i < entries.size(); ++i) {
        total += entries[i].bytes;
        if (i >= maxEntries_ || total > maxBytes_) DeleteFileW(entries[i].path.c_str());
    }
}

} // namespace md
