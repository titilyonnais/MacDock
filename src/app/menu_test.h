// Diagnostic des menus en verre (MacDock.exe --menu-test) : menu de démonstration au bas de l'écran principal.
#pragma once
#include <windows.h>

namespace md {

int runMenuTest(HINSTANCE instance);   // journalise le choix ; 0 si un choix a été fait

} // namespace md
