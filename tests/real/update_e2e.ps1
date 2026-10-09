# Essai de bout en bout des mises à jour automatiques (plan 53), sur de vraies préversions publiées sur GitHub.
# Prérequis :
#   - trois préversions publiées par release.yml depuis une branche jetable dont chaque commit ne change que
#     version_defs.h (ici 0.53.0-rc.4 à rc.6) ; les deux plus récentes signées (tools/sign-release.ps1 -Tag) ; attendre
#     une minute après la dernière signature (l'API de GitHub sans jeton est mise en cache 60 s) ;
#   - aucune installation de MacDock sur ce PC (le script le vérifie) ; le MacDock qui tourne est arrêté pendant
#     l'essai, puis relancé ;
#   - une copie compilée à la main dans build\Package (tools/make-installer.ps1), pour F.
# Ce qui est vérifié (Old : la plus ancienne, New : la plus récente signée) :
#   A. Old installée → recherche → New prête → notification → clic → installée, relancée ;
#   B. Old réinstallée → New prête → installateur altéré jamais lancé, démarrage normal → retéléchargé → installé au
#      démarrage suivant par le relais, avant le Dock ;
#   C. bouton « Installer » de Réglages (--install-update) : installe New et relance MacDock, sans toucher au démarrage
#      avec Windows (relecture, critique 1) ;
#   D. recherche bloquée (verrou tenu par l'essai) : « --quit » arrête le lanceur et une recherche de Réglages
#      (--check-update) en quelques secondes (relecture, importants 4 et 5) ;
#   E. installateur lancé pendant une recherche (verrou tenu) : il attend, abandonne proprement et MacDock revient ;
#   F. copie compilée à la main : aucune mise à jour, Réglages dit pourquoi.
# L'état et les téléchargements vont dans un dossier temporaire (MACDOCK_UPDATE_DIR), jamais dans %APPDATA%. Après
# l'essai : préversions et étiquettes supprimées (gh release delete … --cleanup-tag), branche jetable supprimée.
param(
    [string]$Old = '0.53.0-rc.4',
    [string]$New = '0.53.0-rc.6'
)
$ErrorActionPreference = 'SilentlyContinue'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$s = Join-Path ([IO.Path]::GetTempPath()) 'macdock-e2e'
New-Item -ItemType Directory -Force $s | Out-Null
$dir = "$s\MacDockE2E"
$upd = "$s\upd-e2e"
$dl = "$s\e2e-assets"
$repo = 'titilyonnais/MacDock'
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$realKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{7C2E4C1B-8A3D-4F6E-9B21-5D0A3C9E7F14}_is1'
$testKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{3B8F0E2A-6D47-4C19-A5E3-9F1C2B7D4E60}_is1'
$log = "$env:APPDATA\MacDock\logs\log.txt"
$failures = 0

function Procs { Get-Process MacDock, MacMenuBar, MacDockLauncher -ErrorAction SilentlyContinue }
function Check($ok, $what) { if ($ok) { "OK     $what" } else { "ÉCHEC  $what"; $script:failures++ } }
function WaitFor($cond, $seconds) { for ($i = 0; $i -lt $seconds * 10; $i++) { if (& $cond) { return $true }; Start-Sleep -Milliseconds 100 }; return $false }
function State { if (Test-Path "$upd\update.json") { Get-Content "$upd\update.json" -Raw | ConvertFrom-Json } }
function Installed { (Get-Item "$dir\MacDock.exe" -ErrorAction SilentlyContinue).VersionInfo.FileVersion }
function FromTest { $p = @(Procs); ($p.Count -eq 3) -and -not ($p | Where-Object { $_.Path -notlike "$dir\*" }) }
function RunValue { (Get-ItemProperty $runKey -ErrorAction SilentlyContinue).MacDock }
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
function InstallOld {
    $p = Start-Process -FilePath "$dl\MacDock-Setup-$Old.exe" -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/NOLAUNCH', '/NOSTARTUP', "/DIR=`"$dir`"" -PassThru
    $p.WaitForExit(180000) | Out-Null
    return $p.ExitCode
}
# Verrou des mises à jour tenu par l'essai, comme par une recherche en cours (un fil à lui : un mutex appartient à un fil).
function HoldLock {
    $ps = [PowerShell]::Create()
    $null = $ps.AddScript({
        $m = [Threading.Mutex]::new($false, 'Local\MacDockUpdate')
        $null = $m.WaitOne()
        $release = [Threading.EventWaitHandle]::new($false, 'ManualReset', 'Local\MacDockE2EReleaseLock')
        $null = $release.WaitOne()
        $m.ReleaseMutex()
        $m.Dispose()   # sinon le verrou existe encore : l'installateur et le désinstalleur croiraient MacDock en marche
        $release.Dispose()
    })
    $script:lockRun = @{ ps = $ps; handle = $ps.BeginInvoke() }
    Start-Sleep -Milliseconds 500
}
function ReleaseLock {
    [Threading.EventWaitHandle]::new($false, 'ManualReset', 'Local\MacDockE2EReleaseLock').Set() | Out-Null
    $script:lockRun.ps.EndInvoke($script:lockRun.handle) | Out-Null
    $script:lockRun.ps.Dispose()
}

Remove-Item -Recurse -Force $upd, $dl
New-Item -ItemType Directory -Force $upd, $dl | Out-Null
Check (-not (Test-Path $realKey) -and -not (Test-Path $testKey)) 'aucune installation de MacDock sur ce PC'
if ($failures) { 'Essai annulé.'; exit 1 }
$userLauncher = (Get-Process MacDockLauncher | Select-Object -First 1).Path
if ($userLauncher) {
    "Arrêt du MacDock de l'utilisateur ($userLauncher), relancé à la fin."
    (Start-Process $userLauncher -ArgumentList '--quit' -PassThru).WaitForExit(20000) | Out-Null
    WaitFor { -not (Procs) } 20 | Out-Null
}
$runBefore = RunValue

# Environnement de la copie d'essai (hérité par l'installateur, le relais et la relance).
$env:MACDOCK_UPDATE_DIR = $upd
$env:MACDOCK_UPDATE_PRERELEASE = '1'
$env:MACDOCK_UPDATE_DELAY = '3'
gh release download "v$Old" --repo $repo --pattern 'MacDock-Setup-*.exe' --dir $dl
Check (Test-Path "$dl\MacDock-Setup-$Old.exe") "installateur de $Old téléchargé"

"== A. Notification et clic"
$code = InstallOld
Check ($code -eq 0 -and (Installed) -eq $Old) "$Old installée (code $code, $(Installed))"
$mark = LogMark
Launch
Check (WaitFor { FromTest } 15) "$Old lancée"
Check (WaitFor { (State).readyVersion -eq $New } 120) "la plus récente signée est prête : « $((State).readyVersion) »"
Check (WaitFor { (LogSince $mark) -match 'annoncée' } 15) 'notification affichée'
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class N3 { [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowExW(IntPtr p, IntPtr a, string c, IntPtr t);
[DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l); }
"@
$notify = [N3]::FindWindowExW([IntPtr](-3), [IntPtr]::Zero, 'MacDockUpdateNotify', [IntPtr]::Zero)   # HWND_MESSAGE
Check ($notify -ne [IntPtr]::Zero) 'fenêtre de la notification trouvée'
$before = (Procs | Measure-Object StartTime -Maximum).Maximum
[N3]::PostMessageW($notify, 0x8001, [IntPtr]1, [IntPtr]0x0405) | Out-Null   # WM_APP + 1, NIN_BALLOONUSERCLICK
Check (WaitFor { ((Installed) -eq $New) -and (FromTest) -and ((Procs | Measure-Object StartTime -Minimum).Minimum -gt $before) } 120) "après le clic : $(Installed), relancée"
Check (WaitFor { (State).readyVersion -eq '' } 30) 'état nettoyé au démarrage de la nouvelle version'
Check (-not (Test-Path "$upd\updates\MacDock-Setup-$New.exe")) 'installateur téléchargé effacé'

"== B. Installateur altéré, puis installation au démarrage par le relais"
$code = InstallOld   # retour à Old (arrête la copie qui tourne)
Check ($code -eq 0 -and (Installed) -eq $Old) "$Old réinstallée (code $code, $(Installed))"
$code = CheckUpdate
Check ($code -eq 10) "$New prête (code $code)"
$installer = "$upd\updates\MacDock-Setup-$New.exe"
Add-Content -Path $installer -Value 'X' -NoNewline   # un octet de plus : altéré
$mark = LogMark
Launch
Check (WaitFor { FromTest } 20) 'démarrage normal malgré la version prête'
Check ((Installed) -eq $Old) "installateur altéré jamais lancé (toujours $(Installed))"
Check ((LogSince $mark) -match 'aucun installateur prêt et intact') 'journal : installateur refusé'
$code = CheckUpdate
Check ($code -eq 10) "retéléchargé et revérifié (code $code)"
Quit
$mark = LogMark
Launch   # installation au démarrage, avant le Dock, puis relance par le relais
Check (WaitFor { ((Installed) -eq $New) -and (FromTest) } 120) "installée au démarrage : $(Installed), relancée"
Check (WaitFor { (State).readyVersion -eq '' -and (State).attemptedVersion -eq '' } 30) 'état nettoyé après l''installation'
Check ((LogSince $mark) -match "installation de MacDock $([regex]::Escape($New)) lancée") 'journal : installation lancée au démarrage'
Check (WaitFor { -not (Get-CimInstance Win32_Process -Filter "Name = 'cmd.exe'" | Where-Object { $_.CommandLine -like '*MACDOCK_RELAY*' }) } 10) 'relais terminé'

"== C. Bouton « Installer » de Réglages (--install-update)"
$code = InstallOld
Check ($code -eq 0 -and (Installed) -eq $Old) "$Old réinstallée (code $code)"
Launch
Check (WaitFor { FromTest } 15) "$Old lancée"
$code = CheckUpdate
Check ($code -eq 10) "$New prête (code $code)"
$runMid = RunValue
$i = Start-Process -FilePath "$dir\MacDockLauncher.exe" -ArgumentList '--install-update' -PassThru
$i.WaitForExit(30000) | Out-Null
Check ($i.ExitCode -eq 0) "--install-update lance l'installation (code $($i.ExitCode))"
Check (WaitFor { ((Installed) -eq $New) -and (FromTest) } 120) "installée par le bouton : $(Installed), relancée"
Check ((RunValue) -eq $runMid) 'démarrage avec Windows pas touché'

"== D. Arrêt pendant une recherche bloquée"
Quit
$env:MACDOCK_UPDATE_DELAY = '1'
Remove-Item "$upd\update.json"   # recherche due tout de suite
HoldLock
Launch
Check (WaitFor { FromTest } 15) 'lancée, sa recherche bloquée sur le verrou'
$check = Start-Process -FilePath "$dir\MacDockLauncher.exe" -ArgumentList '--check-update' -PassThru
Start-Sleep -Seconds 3
Check (-not $check.HasExited) 'recherche de Réglages bloquée sur le verrou'
$t = [Diagnostics.Stopwatch]::StartNew()
(Start-Process -FilePath "$dir\MacDockLauncher.exe" -ArgumentList '--quit' -PassThru).WaitForExit(30000) | Out-Null
$stopped = WaitFor { -not (Procs) } 20
Check ($stopped -and $t.Elapsed.TotalSeconds -lt 15) ("lanceur arrêté en {0:N1} s malgré la recherche" -f $t.Elapsed.TotalSeconds)
Check ($check.WaitForExit(5000)) 'recherche de Réglages arrêtée par l''ordre d''arrêt'
Check (-not (Test-Path "$upd\update.json")) 'rien d''écrit par les recherches interrompues'

"== E. Installateur pendant une recherche (verrou toujours tenu)"
Launch
Check (WaitFor { FromTest } 15) 'relancée'
$t = [Diagnostics.Stopwatch]::StartNew()
$p = Start-Process -FilePath "$dl\MacDock-Setup-$Old.exe" -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/DIR=`"$dir`"" -PassThru
$p.WaitForExit(120000) | Out-Null
Check ($p.ExitCode -ne 0) ("installateur abandonné (code {0}, {1:N0} s) : la recherche n'est pas coupée en plein remplacement" -f $p.ExitCode, $t.Elapsed.TotalSeconds)
Check ((Installed) -eq $New) "rien de remplacé (toujours $(Installed))"
Check (WaitFor { FromTest } 30) 'MacDock revenu après l''abandon'
Quit   # sa recherche, bloquée sur le verrou, s'interrompt
ReleaseLock
$env:MACDOCK_UPDATE_DELAY = '3600'

"== F. Copie compilée à la main"
$hand = "$root\build\Package\MacDockLauncher.exe"
$c = Start-Process -FilePath $hand -ArgumentList '--check-update' -PassThru
$c.WaitForExit(30000) | Out-Null
Check ($c.ExitCode -eq 1) "aucune recherche depuis $hand (code $($c.ExitCode))"
Check ((State).lastError -like 'copie non installée*') "Réglages dit pourquoi : « $((State).lastError) »"

"== Nettoyage"
Quit
$env:MACDOCK_KEEP_THEME = '1'
$u = Start-Process -FilePath "$dir\unins000.exe" -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART' -PassThru
$u.WaitForExit(60000) | Out-Null
Check (WaitFor { -not (Procs) -and -not (Test-Path $dir) -and -not (Test-Path $realKey) } 60) 'copie d''essai désinstallée'
Check ((RunValue) -eq $runBefore) 'démarrage avec Windows comme avant l''essai'
Remove-Item -Recurse -Force $upd, $dl
Remove-Item Env:MACDOCK_UPDATE_DIR, Env:MACDOCK_UPDATE_PRERELEASE, Env:MACDOCK_UPDATE_DELAY, Env:MACDOCK_KEEP_THEME
if ($userLauncher) { Start-Process $userLauncher; Check (WaitFor { @(Procs).Count -ge 3 } 20) 'MacDock de l''utilisateur relancé' }
''
if ($failures) { "ÉCHECS : $failures" } else { 'tout est exact' }
