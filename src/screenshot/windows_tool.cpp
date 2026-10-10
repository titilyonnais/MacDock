#include "windows_tool.h"

#include <windows.h>

#include <cwctype>

namespace md {

bool windowsShotKey(unsigned vk, bool down, bool repeat, bool wasDown, bool win, bool shift) {
    if (vk == VK_SNAPSHOT && !down && !wasDown) return true;   // clavier qui n'envoie que le relâchement
    if (!down || repeat) return false;
    if (vk == VK_SNAPSHOT) return true;   // avec ⊞ ou Alt, la capture est immédiate (voir les limites du plan 63)
    return vk == 'S' && win && shift;
}

bool isCaptureToolProcess(const std::wstring& exeName) {
    std::wstring n;
    for (wchar_t c : exeName) n += wchar_t(std::towlower(c));
    return n == L"snippingtool.exe" || n == L"screenclippinghost.exe" || n == L"screensketch.exe";
}

void ToolReveal::start(std::uint64_t now) {
    active_ = true;
    toolSeen_ = false;
    startedAt_ = keyAt_ = now;
}

bool ToolReveal::keep(std::uint64_t now, bool toolInFront) {
    if (!active_) return false;
    if (now - startedAt_ >= kMaxMs) return active_ = false;
    if (toolInFront) {
        toolSeen_ = true;
        toolSeenAt_ = now;
        return true;
    }
    if (toolSeen_ ? now - toolSeenAt_ >= kAfterToolMs : now - keyAt_ >= kMinMs) active_ = false;
    return active_;
}

} // namespace md
