# Coins de macOS 26 sur toutes les fenêtres (plan 72) : active le mod « Custom Window Corner Radius » de Windhawk (m417z,
# à installer depuis le catalogue de Windhawk) avec les valeurs de macOS : rayon de 12 pt au lieu de 8, fenêtres
# ancrées (moitiés, quarts) arrondies elles aussi. Ce mod accroche le calcul du rayon dans dwm.exe et se retire tout
# seul si DWM plante à répétition ; rien n'est modifié dans les fichiers de Windows.
#   Double-clic sur coins-macos.cmd (ou coins-windows.cmd pour revenir aux coins de Windows).
#   Windows demande l'autorisation administrateur : Windhawk garde ses réglages dans HKLM.
# -Off : désactive le mod. -DryRun : montre seulement ce qui serait écrit (sans droits administrateur).
param([switch]$Off, [switch]$DryRun)
$ErrorActionPreference = 'Stop'

$engine = 'HKLM:\SOFTWARE\Windhawk\Engine'
$key = "$engine\Mods\custom-corner-radius"
$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin -and -not $DryRun) {
    $extra = if ($Off) { ' -Off' } else { '' }
    Start-Process powershell.exe -Verb RunAs -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`"$extra"
    exit
}

if (-not (Test-Path $key)) {
    Write-Host 'Le mod « Custom Window Corner Radius » n''est pas installé.'
    Write-Host 'Windhawk > Explorer : cherche « Custom Window Corner Radius », Installer, puis relance ce script.'
    if (-not $DryRun) { Read-Host 'Entrée pour fermer' }
    exit 1
}

# Liste d'inclusion de Windhawk : sans dwm.exe, le mod n'est jamais chargé dans DWM.
$writes = [System.Collections.Generic.List[object]]::new()
$include = [string](Get-ItemProperty "$engine\Settings" -ErrorAction SilentlyContinue).Include
$list = @($include -split '\|' | ForEach-Object { $_.Trim() } | Where-Object { $_ })
if (-not $Off -and -not ($list | Where-Object { $_ -ieq 'dwm.exe' })) {
    $writes.Add(@("$engine\Settings", 'Include', (($list + 'dwm.exe') -join '|'), 'String'))
}
$writes.Add(@($key, 'Disabled', $(if ($Off) { 1 } else { 0 }), 'DWord'))
if (-not $Off) {
    $writes.Add(@("$key\Settings", 'radius', 12, 'DWord'))
    $writes.Add(@("$key\Settings", 'smallRadius', 6, 'DWord'))
    $writes.Add(@("$key\Settings", 'roundMaximizedAndSnapped', 'snapped', 'String'))
}
# Horodatage du changement : Windhawk recharge le mod dans DWM.
$writes.Add(@($key, 'SettingsChangeTime', [int][DateTimeOffset]::UtcNow.ToUnixTimeSeconds(), 'DWord'))

foreach ($w in $writes) {
    if ($DryRun) {
        Write-Host ("{0} : {1} = {2}" -f $w[0], $w[1], $w[2])
        continue
    }
    if (-not (Test-Path $w[0])) { New-Item -Path $w[0] | Out-Null }
    New-ItemProperty -Path $w[0] -Name $w[1] -Value $w[2] -PropertyType $w[3] -Force | Out-Null
}
if ($DryRun) { exit 0 }

Write-Host ''
if ($Off) { Write-Host 'Coins de Windows rétablis (mod « Custom Window Corner Radius » désactivé).' }
else { Write-Host 'Coins de macOS activés : rayon de 12 pt sur toutes les fenêtres, ancrées comprises.' }
Write-Host 'Les fenêtres déjà ouvertes changent au prochain redessin (survol, déplacement).'
Read-Host 'Entrée pour fermer'
