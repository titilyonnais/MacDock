# Installe (ou retire) le mod « MacDock - macOS Look » dans Windhawk, exactement comme son éditeur le ferait
# (Créer un nouveau mod, coller, Compiler) : même compilateur, mêmes options, même enregistrement.
#   Double-clic sur installer-macdock-look.cmd (ou retirer-macdock-look.cmd).
#   Windows demande l'autorisation administrateur : Windhawk garde ses mods dans HKLM et C:\ProgramData.
# -OutDir : compile seulement, dans ce dossier (essai sans droits administrateur).
param([switch]$Uninstall, [string]$OutDir)
$ErrorActionPreference = 'Stop'

$id = 'local@macdock-look'
$version = '1.0.0'   # remplacée plus bas par le @version de la source
$wh = Join-Path $env:ProgramFiles 'Windhawk'
$compiler = Join-Path $wh 'Compiler'
$source = Join-Path $PSScriptRoot 'macdock-look.wh.cpp'
$modsDir = Join-Path $env:ProgramData 'Windhawk\Engine\Mods\64'
$sourcesDir = Join-Path $env:ProgramData 'Windhawk\ModsSource'
$key = "HKLM:\SOFTWARE\Windhawk\Engine\Mods\$id"

$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin -and -not $OutDir) {
    $extra = if ($Uninstall) { ' -Uninstall' } else { '' }
    Start-Process powershell.exe -Verb RunAs -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`"$extra"
    exit
}

if (-not (Test-Path (Join-Path $compiler 'bin\clang++.exe'))) { throw "Windhawk est introuvable ($wh)." }
$found = [regex]::Match((Get-Content $source -Raw), '(?m)^// @version\s+(\S+)')
if ($found.Success) { $version = $found.Groups[1].Value }

if ($Uninstall) {
    $old = (Get-ItemProperty $key -ErrorAction SilentlyContinue).LibraryFileName
    Remove-Item $key -Recurse -Force -ErrorAction SilentlyContinue
    if ($old) { Remove-Item (Join-Path $modsDir $old) -Force -ErrorAction SilentlyContinue }
    Remove-Item (Join-Path $sourcesDir "$id.wh.cpp") -Force -ErrorAction SilentlyContinue
    Write-Host 'Mod « MacDock - macOS Look » retiré. Les apps reprennent leur police à leur prochain lancement.'
    Read-Host 'Entrée pour fermer'
    exit
}

# Moteur le plus récent (sa bibliothèque d'import) ; options de compilation de l'éditeur de Windhawk.
$engine = Get-ChildItem (Join-Path $wh 'Engine') -Directory | Sort-Object { [version]$_.Name } | Select-Object -Last 1
$lib = Join-Path $engine.FullName '64\windhawk.lib'
$target = if ($OutDir) { $OutDir } else { $modsDir }
New-Item -ItemType Directory -Force $target | Out-Null
$dllName = "{0}_{1}_{2}.dll" -f $id, $version, (Get-Random -Minimum 100000 -Maximum 999999)
$dll = Join-Path $target $dllName
# Identifiant et version par un en-tête inclus d'abord (pas de guillemets à faire passer dans la ligne de commande).
$ids = Join-Path $env:TEMP 'macdock-look-ids.h'
Set-Content -Path $ids -Encoding ASCII -Value "#define WH_MOD_ID L`"$id`"`r`n#define WH_MOD_VERSION L`"$version`""
$compileArgs = @('-std=c++23', '-O2', '-shared', '-DUNICODE', '-D_UNICODE', '-DWINVER=0x0A00', '-D_WIN32_WINNT=0x0A00',
    '-D_WIN32_IE=0x0A00', '-DNTDDI_VERSION=0x0A000008', '-D__USE_MINGW_ANSI_STDIO=0', '-DWH_MOD', $lib, '-x', 'c++',
    $source, '-include', $ids, '-include', 'windhawk_api.h', '-target', 'x86_64-w64-mingw32', '-Wl,--export-all-symbols',
    '-o', $dll, '-lgdi32', '-ldwrite', '-luser32')
Push-Location $compiler
try { & (Join-Path $compiler 'bin\clang++.exe') @compileArgs } finally { Pop-Location }
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $dll)) { throw 'La compilation du mod a échoué (voir ci-dessus).' }
if ($OutDir) {
    Write-Host "Compilé : $dll"
    exit
}

# Bibliothèques du compilateur, comme l'éditeur les dépose à côté des mods.
foreach ($pair in @(@('libc++.dll', 'libc++.whl'), @('libunwind.dll', 'libunwind.whl'), @('windhawk-mod-shim.dll', 'windhawk-mod-shim.dll'))) {
    $from = Join-Path $compiler "x86_64-w64-mingw32\bin\$($pair[0])"
    $to = Join-Path $modsDir $pair[1]
    if ((Test-Path $from) -and -not (Test-Path $to)) { Copy-Item $from $to }
}

# Enregistrement : celui qu'écrit l'éditeur (listes séparées par |, réglages par défaut du mod).
$old = (Get-ItemProperty $key -ErrorAction SilentlyContinue).LibraryFileName
$text = Get-Content $source -Raw
$excludes = [regex]::Matches($text, '(?m)^// @exclude\s+(.+?)\s*$') | ForEach-Object { $_.Groups[1].Value }
New-Item -Path $key -Force | Out-Null
New-ItemProperty -Path $key -Name LibraryFileName -Value $dllName -PropertyType String -Force | Out-Null
New-ItemProperty -Path $key -Name Disabled -Value 0 -PropertyType DWord -Force | Out-Null
New-ItemProperty -Path $key -Name LoggingEnabled -Value 0 -PropertyType DWord -Force | Out-Null
New-ItemProperty -Path $key -Name Include -Value '*' -PropertyType String -Force | Out-Null
New-ItemProperty -Path $key -Name Exclude -Value ($excludes -join '|') -PropertyType String -Force | Out-Null
New-ItemProperty -Path $key -Name Architecture -Value 'x86-64' -PropertyType String -Force | Out-Null
New-ItemProperty -Path $key -Name Version -Value $version -PropertyType String -Force | Out-Null
$settings = Join-Path $key 'Settings'
if (-not (Test-Path $settings)) {
    New-Item -Path $settings -Force | Out-Null
    New-ItemProperty -Path $settings -Name textFont -Value 'SF Pro Text' -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $settings -Name displayFont -Value 'SF Pro Display' -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $settings -Name gdi -Value 1 -PropertyType DWord -Force | Out-Null
    New-ItemProperty -Path $settings -Name directWrite -Value 1 -PropertyType DWord -Force | Out-Null
}
# Horodatage du changement : Windhawk recharge le mod dans les processus.
New-ItemProperty -Path $key -Name SettingsChangeTime -Value ([int][DateTimeOffset]::UtcNow.ToUnixTimeSeconds()) -PropertyType DWord -Force | Out-Null
New-Item -ItemType Directory -Force $sourcesDir | Out-Null
Copy-Item $source (Join-Path $sourcesDir "$id.wh.cpp") -Force
if ($old -and $old -ne $dllName) { Remove-Item (Join-Path $modsDir $old) -Force -ErrorAction SilentlyContinue }

Write-Host ''
Write-Host 'Mod « MacDock - macOS Look » installé et activé dans Windhawk.'
Write-Host 'Les apps prennent la police SF Pro à leur prochain lancement (il faut que SF Pro soit installée).'
Write-Host 'Jeux en ligne : ajoute-les aussi à Windhawk > Paramètres > Avancé > Process exclusion list.'
Write-Host ''
# L'Explorateur (fenêtres de dossiers, bureau) garde ses polices jusqu'à son redémarrage ; ses fenêtres se rouvrent vides.
$answer = Read-Host "Redémarrer l'Explorateur maintenant pour qu'il prenne SF Pro ? (O/N)"
if ($answer -match '^[oOyY]') {
    Stop-Process -Name explorer -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 2
    if (-not (Get-Process explorer -ErrorAction SilentlyContinue)) { Start-Process explorer.exe }
    Write-Host "Explorateur redémarré. Les autres apps prennent SF Pro à leur prochain lancement."
}
Read-Host 'Entrée pour fermer'
