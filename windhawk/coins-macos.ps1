# Coins de macOS 26 sur toutes les fenêtres (plans 72 et 74) : installe et active le mod « Custom Window Corner Radius »
# de Windhawk (m417z) avec les valeurs de macOS. Grandes fenêtres : 26 pt au lieu de 8 (celui des fenêtres à barre
# d'outils de Tahoe : Safari, Finder) ; petites fenêtres et boîtes de dialogue (Exécuter, alertes) : 12 pt, que MacDock
# leur donne (un grand rayon y rend mal) ; fenêtres ancrées arrondies elles aussi. Ce mod accroche le calcul du rayon
# dans dwm.exe et se retire tout seul si DWM plante à répétition ; rien n'est modifié dans les fichiers de Windows.
#   Double-clic sur coins-macos.cmd (ou coins-windows.cmd pour revenir aux coins de Windows).
#   Windows demande l'autorisation administrateur : Windhawk garde ses mods et ses réglages dans HKLM.
# Absent, le mod est téléchargé (dépôt officiel des mods de Windhawk, version figée et vérifiée par son empreinte) puis
# compilé comme le fait Windhawk ; Windhawk lui-même est proposé par winget s'il manque.
# -Off : désactive le mod. -DryRun : montre seulement ce qui serait fait (sans droits administrateur).
param([switch]$Off, [switch]$DryRun)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'windhawk-common.ps1')

$id = 'custom-corner-radius'
# Version 1.3 du mod (commit du dépôt ramensoftware/windhawk-mods, 11 août 2026) : elle tourne dans dwm.exe, d'où
# l'empreinte vérifiée.
$sourceUrl = 'https://raw.githubusercontent.com/ramensoftware/windhawk-mods/16834cd873954cf8b2e19612c4841dc905f6e148/mods/custom-corner-radius.wh.cpp'
$sourceSha256 = 'BB084BA83EF640428EDB41A7EA5778A964A8B6428982FA5AB8AE304C93AF985D'
$settings = @{ radius = 26; smallRadius = 12; tooltipRadius = -1; roundMaximizedAndSnapped = 'snapped' }

if (-not (Test-Admin) -and -not $DryRun) {
    $extra = if ($Off) { ' -Off' } else { '' }
    Start-Process powershell.exe -Verb RunAs -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`"$extra"
    exit
}

$wh = Get-Windhawk
if ($DryRun) {
    if (-not $wh) { Write-Host 'Windhawk absent : proposé par winget.'; exit 0 }
    $key = "$($wh.Key)\Mods\$id"
    if ($Off) { Write-Host "$key : Disabled = 1"; exit 0 }
    $lib = (Get-ItemProperty $key -ErrorAction SilentlyContinue).LibraryFileName
    if (-not ($lib -and (Test-Path (Join-Path $wh.Mods $lib)))) {
        Write-Host "Mod absent : téléchargé ($sourceUrl), vérifié, compilé et enregistré."
    }
    foreach ($k in $settings.Keys) { Write-Host "$key\Settings : $k = $($settings[$k])" }
    Write-Host "$key : Disabled = 0"
    if (Add-DwmToWindhawk $wh -DryRun) { Write-Host 'dwm.exe ajouté à la liste d''inclusion de Windhawk.' }
    exit 0
}

try {
    if ($Off) {
        if ($wh -and (Test-Path "$($wh.Key)\Mods\$id")) {
            New-ItemProperty -Path "$($wh.Key)\Mods\$id" -Name Disabled -Value 1 -PropertyType DWord -Force | Out-Null
            Update-WindhawkMod $wh $id
            Write-Host 'Coins de Windows rétablis (mod « Custom Window Corner Radius » désactivé).'
        } else {
            Write-Host 'Le mod « Custom Window Corner Radius » n''est pas installé : les coins sont déjà ceux de Windows.'
        }
    } else {
        if (-not (Install-WindhawkIfMissing)) { Read-Host 'Entrée pour fermer'; exit 1 }
        $wh = Get-Windhawk
        $key = "$($wh.Key)\Mods\$id"
        $installed = (Test-Path $key) -and ($lib = (Get-ItemProperty $key).LibraryFileName) -and (Test-Path (Join-Path $wh.Mods $lib))
        $wasDisabled = -not $installed -or ([int](Get-ItemProperty $key -ErrorAction SilentlyContinue).Disabled -ne 0)
        if (-not $installed) {
            Write-Host 'Mod « Custom Window Corner Radius » : téléchargement et compilation…'
            $work = New-WorkDir 'coins'   # vérifié puis compilé dans un dossier des seuls administrateurs
            try {
                $tmp = Join-Path $work "$id.wh.cpp"
                Get-Download $sourceUrl $tmp
                Assert-Sha256 $tmp $sourceSha256
                Install-WindhawkMod -Wh $wh -Id $id -Source $tmp -ForceSettings $settings
            } finally {
                Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
            }
        } else {
            Set-WindhawkSettings "$key\Settings" $settings
            New-ItemProperty -Path $key -Name Disabled -Value 0 -PropertyType DWord -Force | Out-Null
            Update-WindhawkMod $wh $id
        }
        $added = Add-DwmToWindhawk $wh
        # Windhawk ne se charge dans dwm.exe (déjà lancé) qu'en redémarrant : seulement si dwm.exe vient d'être ajouté
        # ou si le mod était absent ou coupé ; un simple changement de rayon est pris à chaud par le mod.
        if ($added -or $wasDisabled) { Restart-Windhawk }
        Write-Host ''
        Write-Host 'Coins de macOS activés : 26 pt sur les fenêtres, 12 pt sur les petites (MacDock lancé), ancrées comprises.'
        Write-Host 'Les fenêtres déjà ouvertes changent au prochain redessin (survol, déplacement, activation).'
    }
} catch {
    Write-Host "Échec : $($_.Exception.Message)"
}
Read-Host 'Entrée pour fermer'
