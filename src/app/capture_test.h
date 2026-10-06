// Diagnostic de la capture de l'arrière-plan (MacDock.exe --capture-test out.png).
#pragma once
#include <string>

namespace md {

// 0 si une image non noire a été capturée et écrite ; code d'erreur sinon (détails dans le journal).
int runCaptureTest(const std::wstring& outPng);

} // namespace md
