# Essai réel non intrusif : la barre de menus suit une fenêtre non éligible au Dock (bureau, dialogue…) qui passe
# au premier plan. L'événement EVENT_SYSTEM_FOREGROUND est émis par NotifyWinEvent pour une fenêtre jamais affichée
# de ce script (Windows ne relaie l'événement que pour les fenêtres du processus qui l'émet) : le vrai premier plan
# ne change pas, aucune souris ni clavier n'est piloté.
# Usage : powershell -NoProfile -ExecutionPolicy Bypass -File tests\real\menubar_unlisted_foreground.ps1 [-Exe chemin\MacMenuBar.exe]
param([string]$Exe = "$PSScriptRoot\..\..\build\Debug\MacMenuBar.exe")
$ErrorActionPreference = 'Stop'
$dir = "$env:APPDATA\MacDock"
$log = "$dir\logs\menubar\log.txt"
$hadJson = Test-Path "$dir\menubar.json"
if ($hadJson) { Copy-Item "$dir\menubar.json" "$env:TEMP\menubar.json.essai" -Force }
$before = if (Test-Path $log) { (Get-Content $log).Count } else { 0 }

Add-Type -Namespace Essai -Name W -MemberDefinition @'
[DllImport("user32.dll")] public static extern void NotifyWinEvent(uint ev, IntPtr hwnd, int idObject, int idChild);
'@
Add-Type -AssemblyName System.Windows.Forms

Start-Process $Exe -ArgumentList '--trace'
Start-Sleep -Seconds 3
$form = New-Object System.Windows.Forms.Form   # jamais affichée : non éligible au Dock
[Essai.W]::NotifyWinEvent(0x0003, $form.Handle, 0, 0)   # EVENT_SYSTEM_FOREGROUND, OBJID_WINDOW, CHILDID_SELF
Start-Sleep -Seconds 1
& $Exe --quit
Start-Sleep -Seconds 2
$form.Dispose()

$lines = Get-Content $log -Encoding UTF8 | Select-Object -Skip $before
$ok = [bool]($lines | Select-String -Pattern 'app active .*PowerShell')
if ($hadJson) { Copy-Item "$env:TEMP\menubar.json.essai" "$dir\menubar.json" -Force; Remove-Item "$env:TEMP\menubar.json.essai" }
else { Remove-Item "$dir\menubar.json" -ErrorAction SilentlyContinue }
if ($ok) { 'REUSSI : fenetre non eligible suivie'; exit 0 } else { 'ECHEC : fenetre non eligible ignoree'; exit 1 }
