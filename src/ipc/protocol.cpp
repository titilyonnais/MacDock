#include "protocol.h"

#include <algorithm>
#include <cstring>

namespace md::ipc {
namespace {

template <class T>
void put(std::vector<std::uint8_t>& out, T v) {
    std::uint8_t bytes[sizeof(T)];
    std::memcpy(bytes, &v, sizeof(T));   // x64 : petit-boutiste natif
    out.insert(out.end(), bytes, bytes + sizeof(T));
}

template <class T>
T get(const std::uint8_t* p) {
    T v;
    std::memcpy(&v, p, sizeof(T));
    return v;
}

} // namespace

std::vector<std::uint8_t> encode(const Message& m) {
    std::vector<std::uint8_t> out;
    out.reserve(kHeaderSize + m.payload.size());
    put(out, kMagic);
    put(out, kVersion);
    put(out, std::uint16_t(m.type));
    put(out, std::uint32_t(m.payload.size()));
    out.insert(out.end(), m.payload.begin(), m.payload.end());
    return out;
}

void Decoder::feed(const std::uint8_t* data, std::size_t n) {
    if (failed_ || n == 0) return;
    buffer_.insert(buffer_.end(), data, data + n);
}

std::optional<Message> Decoder::next() {
    if (failed_ || buffer_.size() < kHeaderSize) return std::nullopt;
    const std::uint8_t* p = buffer_.data();
    if (get<std::uint32_t>(p) != kMagic || get<std::uint16_t>(p + 4) != kVersion) {
        failed_ = true;
        buffer_.clear();
        return std::nullopt;
    }
    std::uint32_t len = get<std::uint32_t>(p + 8);
    if (len > kMaxPayload) {
        failed_ = true;
        buffer_.clear();
        return std::nullopt;
    }
    if (buffer_.size() < kHeaderSize + len) return std::nullopt;
    Message m;
    m.type = MsgType(get<std::uint16_t>(p + 6));
    m.payload.assign(p + kHeaderSize, p + kHeaderSize + len);
    buffer_.erase(buffer_.begin(), buffer_.begin() + std::ptrdiff_t(kHeaderSize + len));
    return m;
}

Message makeOverlay(const OverlayEvent& e) {
    Message m{MsgType::Overlay, {}};
    put(m.payload, e.hwnd);
    put(m.payload, std::uint8_t(e.hasOverlay ? 1 : 0));
    return m;
}

std::optional<OverlayEvent> parseOverlay(const Message& m) {
    if (m.type != MsgType::Overlay || m.payload.size() != 9) return std::nullopt;
    return OverlayEvent{get<std::uint64_t>(m.payload.data()), m.payload[8] != 0};
}

Message makeProgress(const ProgressEvent& e) {
    Message m{MsgType::Progress, {}};
    put(m.payload, e.hwnd);
    put(m.payload, e.state);
    put(m.payload, e.completed);
    put(m.payload, e.total);
    return m;
}

std::optional<ProgressEvent> parseProgress(const Message& m) {
    if (m.type != MsgType::Progress || m.payload.size() != 28) return std::nullopt;
    const std::uint8_t* p = m.payload.data();
    return ProgressEvent{get<std::uint64_t>(p), get<std::uint32_t>(p + 8), get<std::uint64_t>(p + 12),
                         get<std::uint64_t>(p + 20)};
}

Message makeFlash(const FlashEvent& e) {
    Message m{MsgType::Flash, {}};
    put(m.payload, e.hwnd);
    return m;
}

std::optional<FlashEvent> parseFlash(const Message& m) {
    if (m.type != MsgType::Flash || m.payload.size() != 8) return std::nullopt;
    return FlashEvent{get<std::uint64_t>(m.payload.data())};
}

// TrayUpdate : hwnd u64, uid, callback, version, flags u32, hidden u8, guid 16 octets, longueur du texte u16, texte
// UTF-16, largeur et hauteur u16, pixels BGRA.
Message makeTrayUpdate(const TrayIconEvent& e) {
    Message m{MsgType::TrayUpdate, {}};
    put(m.payload, e.hwnd);
    put(m.payload, e.uid);
    put(m.payload, e.callback);
    put(m.payload, e.version);
    put(m.payload, e.flags);
    put(m.payload, std::uint8_t(e.hidden ? 1 : 0));
    m.payload.insert(m.payload.end(), e.guid, e.guid + 16);
    const std::size_t n = std::min(e.tip.size(), kTrayTipMax);
    put(m.payload, std::uint16_t(n));
    for (std::size_t i = 0; i < n; ++i) put(m.payload, std::uint16_t(e.tip[i]));
    put(m.payload, e.w);
    put(m.payload, e.h);
    m.payload.insert(m.payload.end(), e.bgra.begin(), e.bgra.end());
    return m;
}

std::optional<TrayIconEvent> parseTrayUpdate(const Message& m) {
    constexpr std::size_t kFixed = 8 + 4 * 4 + 1 + 16 + 2;
    if (m.type != MsgType::TrayUpdate || m.payload.size() < kFixed) return std::nullopt;
    const std::uint8_t* p = m.payload.data();
    TrayIconEvent e;
    e.hwnd = get<std::uint64_t>(p);
    e.uid = get<std::uint32_t>(p + 8);
    e.callback = get<std::uint32_t>(p + 12);
    e.version = get<std::uint32_t>(p + 16);
    e.flags = get<std::uint32_t>(p + 20);
    e.hidden = p[24] != 0;
    std::memcpy(e.guid, p + 25, 16);
    const std::size_t n = get<std::uint16_t>(p + 41);
    if (n > kTrayTipMax || m.payload.size() < kFixed + 2 * n + 4) return std::nullopt;
    for (std::size_t i = 0; i < n; ++i) e.tip.push_back(wchar_t(get<std::uint16_t>(p + kFixed + 2 * i)));
    const std::uint8_t* q = p + kFixed + 2 * n;
    e.w = get<std::uint16_t>(q);
    e.h = get<std::uint16_t>(q + 2);
    if (e.w > kTrayImageMax || e.h > kTrayImageMax) return std::nullopt;
    const std::size_t pixels = std::size_t(e.w) * e.h * 4;
    if (m.payload.size() != kFixed + 2 * n + 4 + pixels) return std::nullopt;
    e.bgra.assign(q + 4, q + 4 + pixels);
    return e;
}

Message makeTrayRemove(std::uint64_t hwnd, std::uint32_t uid, const std::uint8_t guid[16]) {
    Message m{MsgType::TrayRemove, {}};
    put(m.payload, hwnd);
    put(m.payload, uid);
    m.payload.insert(m.payload.end(), guid, guid + 16);
    return m;
}

std::optional<TrayIconEvent> parseTrayRemove(const Message& m) {
    if (m.type != MsgType::TrayRemove || m.payload.size() != 28) return std::nullopt;
    TrayIconEvent e;
    e.hwnd = get<std::uint64_t>(m.payload.data());
    e.uid = get<std::uint32_t>(m.payload.data() + 8);
    std::memcpy(e.guid, m.payload.data() + 12, 16);
    return e;
}

} // namespace md::ipc
