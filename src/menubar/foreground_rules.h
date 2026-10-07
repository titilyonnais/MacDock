// Classement de la fenêtre au premier plan pour la barre de menus (logique pure).
#pragma once
#include <string_view>

namespace md {

// App : son nom et ses menus s'affichent. Explorer : bureau ou fenêtre de l'Explorateur (équivalent du Finder).
// Ignore : interface du système, du Dock ou de la barre ; l'app affichée ne change pas (comme un clic dans le Dock).
enum class ForegroundKind { App, Explorer, Ignore };

ForegroundKind classifyForeground(std::wstring_view className, std::wstring_view exeName, bool ownProcess);

} // namespace md
