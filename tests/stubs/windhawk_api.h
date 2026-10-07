// Faux Windhawk : compile le mod (windhawk/*.wh.cpp) dans tests.exe pour tester ses fonctions pures.
// Aucun crochet n'est posé, aucune valeur n'est stockée.
#pragma once
#include <windows.h>

inline void Wh_Log(const wchar_t*, ...) {}
inline int Wh_GetIntValue(const wchar_t*, int defaultValue) { return defaultValue; }
inline BOOL Wh_SetIntValue(const wchar_t*, int) { return TRUE; }
inline void Wh_DeleteValue(const wchar_t*) {}
inline int Wh_GetIntSetting(const wchar_t*, ...) { return 0; }
inline BOOL Wh_SetFunctionHook(void*, void*, void**) { return TRUE; }
