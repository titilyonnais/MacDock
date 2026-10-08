// Classement de la fenêtre au premier plan pour la barre de menus (logique pure).
#pragma once
#include <string_view>

namespace md {

// App : son nom et ses menus s'affichent. Explorer : bureau ou fenêtre de l'Explorateur (équivalent du Finder).
// Ignore : interface du système, du Dock ou de la barre ; l'app affichée ne change pas (comme un clic dans le Dock).
enum class ForegroundKind { App, Explorer, Ignore };

ForegroundKind classifyForeground(std::wstring_view className, std::wstring_view exeName, bool ownProcess);

// Le premier plan est passé au bureau. Comme macOS : un clic sur le bureau active le Finder (Explorateur) ; quand la
// fenêtre active se ferme, la dernière app utilisée qui a encore une fenêtre visible prend la main ; une app dont la
// seule fenêtre vient d'être réduite reste active. Windows, lui, donne souvent la main au bureau.
enum class DesktopFocus { ShowExplorer, KeepPrevious, ActivateNext };
struct DesktopFocusContext {
    bool clickedDesktop = false;       // bouton de la souris enfoncé, curseur sur le bureau
    bool previousGone = false;         // la fenêtre active d'avant est fermée ou cachée
    bool previousMinimized = false;    // elle vient d'être réduite
    bool otherWindowVisible = false;   // une autre fenêtre d'app est visible (ni réduite, ni sur un autre bureau)
};
DesktopFocus desktopFocus(const DesktopFocusContext& c);

} // namespace md
