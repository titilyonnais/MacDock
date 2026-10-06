// Utilitaires de test : fichiers temporaires.
#pragma once
#include <string>

namespace md {

std::wstring testTempDir();                                         // dossier temporaire neuf et unique
void testWriteFile(const std::wstring& path, const std::string& bytes);
bool testFileExists(const std::wstring& path);

} // namespace md
