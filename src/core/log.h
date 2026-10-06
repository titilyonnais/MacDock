// Journal fichier : %APPDATA%\MacDock\logs\log.txt, rotation à 1 Mo, 3 fichiers.
#pragma once
#include <string>

namespace md::log {

void init(const std::wstring& dir);
void info(const wchar_t* fmt, ...);
void warn(const wchar_t* fmt, ...);
void error(const wchar_t* fmt, ...);

} // namespace md::log
