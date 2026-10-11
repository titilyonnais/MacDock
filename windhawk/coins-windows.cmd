@echo off
rem Coins de Windows : désactive le mod « Custom Window Corner Radius » de Windhawk (autorisation administrateur).
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0coins-macos.ps1" -Off
