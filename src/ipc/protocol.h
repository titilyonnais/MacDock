// Protocole du named pipe \\.\pipe\MacDock (Dock <-> mod Windhawk).
// En-tête de 12 octets, petit-boutiste : magic u32 "MDCK", version u16, type u16, longueur u32.
// Le mod windhawk/macdock-hide-taskbar.wh.cpp duplique ce format : changer kVersion à chaque évolution.
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace md::ipc {

constexpr std::uint32_t kMagic = 0x4B43444D;   // "MDCK"
constexpr std::uint16_t kVersion = 1;
constexpr std::uint32_t kMaxPayload = 64 * 1024;
constexpr std::size_t kHeaderSize = 12;

enum class MsgType : std::uint16_t { Heartbeat = 1, Overlay = 2, Progress = 3, Flash = 4, Goodbye = 5 };

struct Message {
    MsgType type{};
    std::vector<std::uint8_t> payload;
};

std::vector<std::uint8_t> encode(const Message& m);

class Decoder {
public:
    void feed(const std::uint8_t* data, std::size_t n);
    std::optional<Message> next();
    bool failed() const { return failed_; }   // état définitif

private:
    std::vector<std::uint8_t> buffer_;
    bool failed_ = false;
};

struct OverlayEvent { std::uint64_t hwnd = 0; bool hasOverlay = false; };
struct ProgressEvent { std::uint64_t hwnd = 0; std::uint32_t state = 0; std::uint64_t completed = 0, total = 0; };
struct FlashEvent { std::uint64_t hwnd = 0; };

Message makeOverlay(const OverlayEvent& e);
std::optional<OverlayEvent> parseOverlay(const Message& m);
Message makeProgress(const ProgressEvent& e);
std::optional<ProgressEvent> parseProgress(const Message& m);
Message makeFlash(const FlashEvent& e);
std::optional<FlashEvent> parseFlash(const Message& m);

} // namespace md::ipc
