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

bool toolShowsVeil(unsigned vk, bool win, bool alt) { return vk == 'S' || (vk == VK_SNAPSHOT && !win && !alt); }

BarLayerChange barLayerChange(bool lowered, ShotReveal reveal) {
    const bool want = reveal == ShotReveal::UnderVeil;
    return want == lowered ? BarLayerChange{false, !lowered} : BarLayerChange{true, !want};
}

bool isCaptureToolProcess(const std::wstring& exeName) {
    std::wstring n;
    for (wchar_t c : exeName) n += wchar_t(std::towlower(c));
    return n == L"snippingtool.exe" || n == L"screenclippinghost.exe" || n == L"screensketch.exe";
}

void ToolReveal::start(std::uint64_t now, bool veilExpected) {
    active_ = true;
    toolSeen_ = veilSeen_ = false;
    veilExpected_ = lowered_ = veilExpected;
    startedAt_ = keyAt_ = now;
}

bool ToolReveal::keep(std::uint64_t now, bool toolInFront, bool veilInFront) {
    if (active_ && now - startedAt_ >= kMaxMs) active_ = false;
    if (active_) {
        if (toolInFront) {
            toolSeen_ = true;
            toolSeenAt_ = now;
        } else if (toolSeen_ ? now - toolSeenAt_ >= kAfterToolMs : now - keyAt_ >= kMinMs) {
            active_ = false;
        }
    }
    if (veilInFront) veilSeen_ = true;
    lowered_ = active_ && (veilInFront || (veilExpected_ && !veilSeen_ && now - keyAt_ < kVeilWaitMs));
    return active_;
}

} // namespace md
