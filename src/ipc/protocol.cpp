#include "protocol.h"

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

} // namespace md::ipc
