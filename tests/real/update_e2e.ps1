# Essai de bout en bout des mises à jour automatiques (plan 53), sur de vraies préversions publiées sur GitHub.
# Prérequis :
#   - trois préversions v0.53.0-rc.1 à rc.3 publiées par release.yml (une branche jetable dont chaque commit ne change
#     que MACDOCK_VERSION_SUFFIX), rc.2 et rc.3 signées (tools/sign-release.ps1 -Tag) ; attendre une minute après la
#     dernière signature (l'API de GitHub sans jeton est mise en cache 60 s) ;
#   - aucune installation de MacDock sur ce PC (le script le vérifie) ; MacDock qui tourne est arrêté pendant l'essai ;
#   - la variante d'essai de l'installateur : tools/make-installer.ps1 -Test.
# Ce qui est vérifié :
#   T. désinstallation avec MACDOCK_KEEP_THEME=1 : le thème de Windows n'est pas touché ;
#   A. rc.1 installée → recherche → la plus récente signée (rc.3) prête → notification → clic → installée, relancée ;
#   B. rc.1 réinstallée → rc.3 prête → installateur altéré jamais lancé, démarrage normal → retéléchargé → installé au
#      démarrage suivant (/RELAUNCH), avant le Dock.
# L'état et les téléchargements vont dans un dossier temporaire (MACDOCK_UPDATE_DIR), jamais dans %APPDATA%. Après
# l'essai : préversions et étiquettes supprimées (gh release delete … --cleanup-tag), branche jetable supprimée.
$ErrorActionPreference = 'SilentlyContinue'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$s = Join-Path ([IO.Path]::GetTempPath()) 'macdock-e2e'
New-Item -ItemType Directory -Force $s | Out-Null
$dir = "$s\MacDockE2E"
$upd = "$s\upd-e2e"
$dl = "$s\e2e-assets"
$repo = 'titilyonnais/MacDock'
$realKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{7C2E4C1B-8A3D-4F6E-9B21-5D0A3C9E7F14}_is1'
$testKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{3B8F0E2A-6D47-4C19-A5E3-9F1C2B7D4E60}_is1'
$log = "$env:APPDATA\MacDock\logs\log.txt"
$failures = 0

function Procs { Get-Process MacDock, MacMenuBar, MacDockLauncher -ErrorAction SilentlyContinue }
function Check($ok, $what) { if ($ok) { "OK     $what" } else { "ÉCHEC  $what"; $script:failures++ } }
function WaitFor($cond, $seconds) { for ($i = 0; $i -lt $seconds * 10; $i++) { if (& $cond) { return $true }; Start-Sleep -Milliseconds 100 }; return $false }
function State { if (Test-Path "$upd\update.json") { Get-Content "$upd\update.json" -Raw | ConvertFrom-Json } }
function Installed { (Get-Item "$dir\MacDock.exe" -ErrorAction SilentlyContinue).VersionInfo.FileVersion }
function FromTest { $p = Procs; ($p.Count -eq 3) -and -not ($p | Where-Object { $_.Path -notlike "$dir\*" }) }
function Quit {
    $q = Start-Process -FilePath "$dir\MacDockLauncher.exe" -ArgumentList '--quit' -PassThru
    $q.WaitForExit(20000) | Out-Null
    WaitFor { -not (Procs) } 15 | Out-Null
}
function Launch { Start-Process -FilePath "$dir\MacDockLauncher.exe" }
function CheckUpdate {
    $c = Start-Process -FilePath "$dir\MacDockLauncher.exe" -ArgumentList '--check-update' -PassThru
    $c.WaitForExit(180000) | Out-Null
    return $c.ExitCode
}
function LogMark { if (Test-Path $log) { (Get-Content $log).Count } else { 0 } }
function LogSince($mark) { if (Test-Path $log) { Get-Content $log | Select-Object -Skip $mark } }
function InstallRc1 {
    $p = Start-Process -FilePath "$dl\MacDock-Setup-0.53.0-rc.1.exe" -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/NOLAUNCH', '/NOSTARTUP', "/DIR=`"$dir`"" -PassThru
    $p.WaitForExit(180000) | Out-Null
    return $p.ExitCode
}

Remove-Item -Recurse -Force $upd, $dl
New-Item -ItemType Directory -Force $upd, $dl | Out-Null
Check (-not (Test-Path $realKey) -and -not (Test-Path $testKey)) 'aucune installation de MacDock sur ce PC'

"== T. Désinstallation d'essai : le thème de Windows n'est pas touché"
$p = Start-Process -FilePath "$root\build\installer-essai\MacDock-Setup-0.52.0.exe" -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/NOLAUNCH', '/NOSTARTUP' -PassThru
$p.WaitForExit(180000) | Out-Null
Check ($p.ExitCode -eq 0) 'variante d''essai installée (ton MacDock s''arrête pendant l''essai)'
$mark = LogMark
$env:MACDOCK_KEEP_THEME = '1'
$u = Start-Process -FilePath "$env:LOCALAPPDATA\Programs\MacDock-essai\unins000.exe" -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART' -PassThru
$u.WaitForExit(60000) | Out-Null
Check (WaitFor { -not (Test-Path $testKey) -and -not (Test-Path "$env:LOCALAPPDATA\Programs\MacDock-essai") } 30) 'désinstallée'
Check (-not ((LogSince $mark) -match 'Thème Windows')) 'thème de Windows jamais touché (MACDOCK_KEEP_THEME=1)'

# Environnement de la copie d'essai des mises à jour (hérité par l'installateur et par la relance).
$env:MACDOCK_UPDATE_DIR = $upd
$env:MACDOCK_UPDATE_PRERELEASE = '1'
$env:MACDOCK_UPDATE_DELAY = '3'
gh release download v0.53.0-rc.1 --repo $repo --pattern 'MacDock-Setup-*.exe' --dir $dl
Check (Test-Path "$dl\MacDock-Setup-0.53.0-rc.1.exe") 'installateur de la rc.1 téléchargé'

"== A. Notification et clic"
$code = InstallRc1
Check ($code -eq 0 -and (Installed) -eq '0.53.0-rc.1') "rc.1 installée (code $code, $(Installed))"
$mark = LogMark
Launch
Check (WaitFor { FromTest } 15) 'rc.1 lancée'
Check (WaitFor { (State).readyVersion -eq '0.53.0-rc.3' } 120) "la plus récente signée est prête : « $((State).readyVersion) »"
Check (WaitFor { (LogSince $mark) -match 'annoncée' } 15) 'notification affichée'
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class N2 { [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowExW(IntPtr p, IntPtr a, string c, IntPtr t);
[DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l); }
"@
$notify = [N2]::FindWindowExW([IntPtr](-3), [IntPtr]::Zero, 'MacDockUpdateNotify', [IntPtr]::Zero)   # HWND_MESSAGE
Check ($notify -ne [IntPtr]::Zero) 'fenêtre de la notification trouvée'
$before = (Procs | Measure-Object StartTime -Maximum).Maximum
[N2]::PostMessageW($notify, 0x8001, [IntPtr]1, [IntPtr]0x0405) | Out-Null   # WM_APP + 1, NIN_BALLOONUSERCLICK
Check (WaitFor { ((Installed) -eq '0.53.0-rc.3') -and (FromTest) -and ((Procs | Measure-Object StartTime -Minimum).Minimum -gt $before) } 120) "après le clic : $(Installed), relancée"
Check (WaitFor { (State).readyVersion -eq '' } 30) 'état nettoyé au démarrage de la nouvelle version'
Check (-not (Test-Path "$upd\updates\MacDock-Setup-0.53.0-rc.3.exe")) 'installateur téléchargé effacé'

"== B. Installateur altéré, puis installation au démarrage"
$code = InstallRc1   # retour à la rc.1 (arrête la copie qui tourne)
Check ($code -eq 0 -and (Installed) -eq '0.53.0-rc.1') "rc.1 réinstallée (code $code, $(Installed))"
$code = CheckUpdate
Check ($code -eq 10) "rc.3 prête (code $code)"
$installer = "$upd\updates\MacDock-Setup-0.53.0-rc.3.exe"
Add-Content -Path $installer -Value 'X' -NoNewline   # un octet de plus : altéré
$mark = LogMark
Launch
Check (WaitFor { FromTest } 20) 'démarrage normal malgré la version prête'
Check ((Installed) -eq '0.53.0-rc.1') "installateur altéré jamais lancé (toujours $(Installed))"
Check ((LogSince $mark) -match 'aucun installateur prêt et intact') 'journal : installateur refusé'
$code = CheckUpdate
Check ($code -eq 10) "retéléchargé et revérifié (code $code)"
Quit
$mark = LogMark
Launch   # installation au démarrage, avant le Dock (/RELAUNCH)
Check (WaitFor { ((Installed) -eq '0.53.0-rc.3') -and (FromTest) } 120) "installée au démarrage : $(Installed), relancée"
Check (WaitFor { (State).readyVersion -eq '' -and (State).attemptedVersion -eq '' } 30) 'état nettoyé après l''installation'
Check ((LogSince $mark) -match 'installation de MacDock 0.53.0-rc.3 lancée') 'journal : installation lancée au démarrage'

"== Nettoyage"
$env:MACDOCK_KEEP_THEME = '1'
$u = Start-Process -FilePath "$dir\unins000.exe" -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART' -PassThru
$u.WaitForExit(60000) | Out-Null
Check (WaitFor { -not (Procs) -and -not (Test-Path $dir) -and -not (Test-Path $realKey) } 60) 'copie d''essai désinstallée'
Remove-Item -Recurse -Force $upd, $dl
Remove-Item Env:MACDOCK_UPDATE_DIR, Env:MACDOCK_UPDATE_PRERELEASE, Env:MACDOCK_UPDATE_DELAY, Env:MACDOCK_KEEP_THEME
""
if ($failures) { "ÉCHECS : $failures" } else { 'tout est exact' }
