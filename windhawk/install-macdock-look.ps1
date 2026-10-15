# Installe (ou retire) le mod « MacDock - macOS Look » dans Windhawk, exactement comme son éditeur le ferait
# (Créer un nouveau mod, coller, Compiler) : même compilateur, mêmes options, même enregistrement. La police SF Pro est
# téléchargée sur le site d'Apple et installée si elle manque (après accord sur sa licence), et Windhawk aussi
# (par winget) s'il n'est pas là.
#   Double-clic sur installer-macdock-look.cmd (ou retirer-macdock-look.cmd).
#   Windows demande l'autorisation administrateur : Windhawk garde ses mods dans HKLM et C:\ProgramData, et SF Pro
#   s'installe pour tout le PC.
# -OutDir : compile seulement, dans ce dossier (essai sans droits administrateur).
# -Restart / -NoRestart : redémarrer l'Explorateur ou non, sans poser la question ni attendre Entrée à la fin (lancé par
# l'app Réglages de MacDock, qui attend la fin de l'installation).
param([switch]$Uninstall, [string]$OutDir, [switch]$Restart, [switch]$NoRestart)
$quiet = $Restart -or $NoRestart
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'windhawk-common.ps1')

$id = 'local@macdock-look'
$source = Join-Path $PSScriptRoot 'macdock-look.wh.cpp'

if (-not (Test-Admin) -and -not $OutDir) {
    $extra = if ($Uninstall) { ' -Uninstall' } else { '' }
    if ($Restart) { $extra += ' -Restart' }
    if ($NoRestart) { $extra += ' -NoRestart' }
    Start-Process powershell.exe -Verb RunAs -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`"$extra"
    exit
}

if ($Uninstall) {
    $wh = Get-Windhawk
    if ($wh) {
        Remove-WindhawkMod $wh $id
        Write-Host 'Mod « MacDock - macOS Look » retiré. Les apps reprennent leur police à leur prochain lancement.'
    } else {
        Write-Host 'Windhawk est introuvable : le mod n''est pas installé.'
    }
    if (-not $quiet) { Read-Host 'Entrée pour fermer' }
    exit
}

if ($OutDir) {   # essai : compilation seule
    $wh = Get-Windhawk
    if (-not $wh) { throw 'Windhawk est introuvable.' }
    Write-Host ("Compilé : " + (Install-WindhawkMod -Wh $wh -Id $id -Source $source -OutDir $OutDir))
    exit
}

if (-not (Install-WindhawkIfMissing)) {
    if (-not $quiet) { Read-Host 'Entrée pour fermer' }
    exit 1
}
$wh = Get-Windhawk
$fonts = Install-SFPro
Install-WindhawkMod -Wh $wh -Id $id -Source $source `
    -DefaultSettings @{ textFont = 'SF Pro Text'; displayFont = 'SF Pro Display'; gdi = 1; directWrite = 1 }

Write-Host ''
Write-Host 'Mod « MacDock - macOS Look » installé et activé dans Windhawk.'
if ($fonts) { Write-Host 'Les apps prennent la police SF Pro à leur prochain lancement.' }
else {
    Write-Host 'SF Pro n''est pas installée : le mod ne change rien tant qu''elle manque (relance ce script pour l''installer).'
    if ($quiet) { Read-Host 'Entrée pour fermer' }   # lancé par Réglages : le message ne disparaît pas aussitôt
}
Write-Host 'Jeux en ligne : ajoute-les aussi à Windhawk > Paramètres > Avancé > Process exclusion list.'
Write-Host ''
# L'Explorateur (fenêtres de dossiers, bureau) garde ses polices jusqu'à son redémarrage ; ses fenêtres se rouvrent vides.
$answer = if ($Restart) { 'o' } elseif ($NoRestart) { 'n' } else { Read-Host "Redémarrer l'Explorateur maintenant pour qu'il prenne SF Pro ? (O/N)" }
if ($answer -match '^[oOyY]') {
    # Seulement l'Explorateur de cette session ; Windows le relance de lui-même. Jamais relancé d'ici : ce PowerShell
    # est administrateur, l'Explorateur le serait aussi.
    $session = (Get-Process -Id $PID).SessionId
    Get-Process explorer -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -eq $session } | Stop-Process -Force
    $back = $false
    foreach ($i in 1..20) {
        Start-Sleep -Milliseconds 250
        if (Get-Process explorer -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -eq $session }) { $back = $true; break }
    }
    if ($back) { Write-Host "Explorateur redémarré. Les autres apps prennent SF Pro à leur prochain lancement." }
    else { Write-Host "L'Explorateur ne s'est pas relancé tout seul : Ctrl+Maj+Échap, « Exécuter une nouvelle tâche », explorer." }
}
if (-not $quiet) { Read-Host 'Entrée pour fermer' }
