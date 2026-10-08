@echo off
rem Retire le mod « MacDock - macOS Look » de Windhawk (Windows demande l'autorisation administrateur).
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install-macdock-look.ps1" -Uninstall
