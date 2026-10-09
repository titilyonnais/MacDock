# Installateur de MacDock (plan 52) : compile la version Release puis Inno Setup. Écrit
# build\installer\MacDock-Setup-X.Y.Z.exe et build\installer\SHA256SUMS.txt (empreinte SHA-256, lue par les mises à
# jour). La version est celle de src\core\version_defs.h ; -Tag (release.yml) doit lui correspondre exactement.
# Avant de fabriquer, les exécutables sont contrôlés : version, aucun runtime Visual C++ à installer à part (/MT),
# aucun chemin du dossier de l'utilisateur.
#   ./tools/make-installer.ps1                       compile dans build\Package (MacDock peut tourner pendant ce temps)
#   ./tools/make-installer.ps1 -SkipBuild -BinDir build\Release
#   ./tools/make-installer.ps1 -Tag v0.52.0          vérifie l'étiquette avant de fabriquer
#   ./tools/make-installer.ps1 -Test                 variante d'essai (autre identifiant, « MacDock (essai) »,
#                                                    dossier MacDock-essai) dans build\installer-essai
param(
    [switch]$SkipBuild,
    [string]$BinDir = 'build\Package',
    [string]$Tag = '',
    [switch]$Test
)
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot

$defs = Get-Content -Raw (Join-Path $Root 'src\core\version_defs.h')
$parts = foreach ($n in 'MAJOR', 'MINOR', 'PATCH') {
    $m = [regex]::Match($defs, "#define MACDOCK_VERSION_$n (\d+)")
    if (-not $m.Success) { throw "MACDOCK_VERSION_$n introuvable dans version_defs.h" }
    $m.Groups[1].Value
}
$suffix = [regex]::Match($defs, '#define MACDOCK_VERSION_SUFFIX "([^"]*)"')
if (-not $suffix.Success) { throw 'MACDOCK_VERSION_SUFFIX introuvable dans version_defs.h' }
$version = ($parts -join '.') + $suffix.Groups[1].Value
if ($Tag -and $Tag -cne "v$version") { throw "L'étiquette $Tag ne correspond pas à la version $version (version_defs.h)." }

$bin = if ([IO.Path]::IsPathRooted($BinDir)) { $BinDir } else { Join-Path $Root $BinDir }
if (-not $SkipBuild) {
    foreach ($t in 'dock', 'menubar', 'launcher', 'settings') {
        & (Join-Path $Root 'build.ps1') -Target $t -Config Release -OutDir $bin
        if ($LASTEXITCODE -ne 0) { throw "Compilation de $t impossible" }
    }
}

$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
$dumpbin = Get-ChildItem (Join-Path $vs 'VC\Tools\MSVC') -Directory | Sort-Object Name -Descending |
    ForEach-Object { Join-Path $_.FullName 'bin\Hostx64\x64\dumpbin.exe' } | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $dumpbin) { throw 'dumpbin.exe introuvable (outils C++ de Visual Studio)' }
foreach ($exe in 'MacDock.exe', 'MacMenuBar.exe', 'MacDockLauncher.exe', 'MacDockSettings.exe') {
    $f = Join-Path $bin $exe
    if (-not (Test-Path $f)) { throw "$f manquant" }
    $fv = (Get-Item $f).VersionInfo.FileVersion
    if ($fv -cne $version) { throw "$exe porte la version $fv, pas $version : recompile." }
    # Un Windows neuf n'a pas forcément le redistribuable Visual C++ : le runtime doit être dans l'exécutable (/MT).
    $crt = & $dumpbin /nologo /dependents $f | Where-Object { $_ -match '(?i)(vcruntime|msvcp|ucrtbase|api-ms-win-crt)' }
    if ($crt) { throw "$exe dépend de $(($crt | ForEach-Object { $_.Trim() }) -join ', ') : compiler en /MT." }
    # Aucun chemin du dossier de l'utilisateur (pdb…) dans ce qui sera publié.
    if (Select-String -Path $f -Pattern $env:USERPROFILE -SimpleMatch -Quiet) { throw "$exe contient le chemin $env:USERPROFILE" }
    if (Select-String -Path $f -Pattern $env:USERPROFILE -SimpleMatch -Quiet -Encoding unicode) { throw "$exe contient le chemin $env:USERPROFILE" }
}

$iscc = @("$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe", "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
          "$env:ProgramFiles\Inno Setup 6\ISCC.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { throw 'Inno Setup 6 introuvable : winget install JRSoftware.InnoSetup --version 6.7.3' }
$outDir = Join-Path $Root $(if ($Test) { 'build\installer-essai' } else { 'build\installer' })
$defines = @("/DAppVersion=$version", "/DBinDir=$bin", "/O$outDir")
if ($Test) { $defines += @('/DAppGuid=3B8F0E2A-6D47-4C19-A5E3-9F1C2B7D4E60', '/DAppTitle=MacDock (essai)', '/DAppFolder=MacDock-essai') }
& $iscc /Q @defines (Join-Path $Root 'installer\MacDock.iss')
if ($LASTEXITCODE -ne 0) { throw "Inno Setup a échoué ($LASTEXITCODE)" }

$name = "MacDock-Setup-$version.exe"
$setup = Join-Path $outDir $name
$hash = (Get-FileHash -Algorithm SHA256 $setup).Hash.ToLowerInvariant()
Set-Content -Path (Join-Path $outDir 'SHA256SUMS.txt') -Value "$hash  $name" -Encoding ascii
Write-Host "== $setup"
Write-Host "== SHA-256 $hash"
