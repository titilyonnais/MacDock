// Protocole du named pipe \\.\pipe\MacDock (Dock <-> mod Windhawk).
// En-tête de 12 octets, petit-boutiste : magic u32 "MDCK", version u16, type u16, longueur u32.
// Le mod windhawk/macdock-hide-taskbar.wh.cpp duplique ce format : changer kVersion à chaque évolution.
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace md::ipc {

constexpr std::uint32_t kMagic = 0x4B43444D;   // "MDCK"
constexpr std::uint16_t kVersion = 1;
constexpr std::uint32_t kMaxPayload = 64 * 1024;
constexpr std::size_t kHeaderSize = 12;

// TrayUpdate et TrayRemove : pipe \\.\pipe\MacMenuBar (mod -> barre de menus). Types inconnus : ignorés.
enum class MsgType : std::uint16_t { Heartbeat = 1, Overlay = 2, Progress = 3, Flash = 4, Goodbye = 5, TrayUpdate = 6, TrayRemove = 7 };

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
// Icône de la zone de notification d'une app, état complet (le mod fusionne les NIM_MODIFY partiels).
struct TrayIconEvent {
    std::uint64_t hwnd = 0;   // fenêtre de l'app
    std::uint32_t uid = 0, callback = 0, version = 0, flags = 0;   // flags : NIF_*
    bool hidden = false;
    std::wstring tip;              // 127 caractères au plus
    std::uint8_t guid[16] = {};    // NIF_GUID, sinon zéros
    std::uint16_t w = 0, h = 0;    // image BGRA prémultipliée, 64 x 64 au plus (0 : pas d'icône)
    std::vector<std::uint8_t> bgra;
};
constexpr std::size_t kTrayTipMax = 127;
constexpr std::uint16_t kTrayImageMax = 64;

Message makeTrayUpdate(const TrayIconEvent& e);
std::optional<TrayIconEvent> parseTrayUpdate(const Message& m);   // tailles vérifiées
Message makeTrayRemove(std::uint64_t hwnd, std::uint32_t uid, const std::uint8_t guid[16]);
std::optional<TrayIconEvent> parseTrayRemove(const Message& m);   // hwnd, uid et guid seulement
Message makeFlash(const FlashEvent& e);
std::optional<FlashEvent> parseFlash(const Message& m);

} // namespace md::ipc
