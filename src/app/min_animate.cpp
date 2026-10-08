#include "min_animate.h"

#include <windows.h>

namespace md {

MinAnimateApi realMinAnimateApi() {
    MinAnimateApi api;
    api.get = []() -> std::optional<bool> {
        ANIMATIONINFO ai{sizeof ai};
        if (!SystemParametersInfoW(SPI_GETANIMATION, sizeof ai, &ai, 0)) return std::nullopt;
        return ai.iMinAnimate != 0;
    };
    api.set = [](bool on) {
        ANIMATIONINFO ai{sizeof ai, on ? 1 : 0};
        return SystemParametersInfoW(SPI_SETANIMATION, sizeof ai, &ai, 0) != FALSE;   // pas d'écriture dans le profil
    };
    api.stored = []() -> std::optional<bool> {
        wchar_t value[8] = {};
        DWORD size = sizeof value;
        if (RegGetValueW(HKEY_CURRENT_USER, L"Control Panel\\Desktop\\WindowMetrics", L"MinAnimate", RRF_RT_REG_SZ, nullptr,
                         value, &size) != ERROR_SUCCESS)
            return std::nullopt;
        return value[0] != L'0';
    };
    return api;
}

bool MinAnimateGuard::original() const {
    const auto v = api_.stored ? api_.stored() : std::nullopt;
    return v.value_or(true);
}

void MinAnimateGuard::apply(MinimizeEffect) {
    const bool want = original();
    const auto now = api_.get ? api_.get() : std::nullopt;
    if (now && *now != want && api_.set) api_.set(want);
}

void MinAnimateGuard::restore() {}

} // namespace md
