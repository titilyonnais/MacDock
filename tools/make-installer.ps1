# Installateur de MacDock (plan 52) : compile la version Release puis Inno Setup. Écrit
# build\installer\MacDock-Setup-X.Y.Z.exe et build\installer\SHA256SUMS.txt (empreinte SHA-256, lue par les mises à
# jour). La version est celle de src\core\version_defs.h ; -Tag (release.yml) doit lui correspondre.
#   ./tools/make-installer.ps1                       compile dans build\Package (MacDock peut tourner pendant ce temps)
#   ./tools/make-installer.ps1 -SkipBuild -BinDir build\Release
#   ./tools/make-installer.ps1 -Tag v0.52.0          vérifie l'étiquette avant de fabriquer
param(
    [switch]$SkipBuild,
    [string]$BinDir = 'build\Package',
    [string]$Tag = ''
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
if ($Tag -and $Tag -ne "v$version") { throw "L'étiquette $Tag ne correspond pas à la version $version (version_defs.h)." }

$bin = if ([IO.Path]::IsPathRooted($BinDir)) { $BinDir } else { Join-Path $Root $BinDir }
if (-not $SkipBuild) {
    foreach ($t in 'dock', 'menubar', 'launcher', 'settings') {
        & (Join-Path $Root 'build.ps1') -Target $t -Config Release -OutDir $bin
        if ($LASTEXITCODE -ne 0) { throw "Compilation de $t impossible" }
    }
}
foreach ($exe in 'MacDock.exe', 'MacMenuBar.exe', 'MacDockLauncher.exe', 'MacDockSettings.exe') {
    $f = Join-Path $bin $exe
    if (-not (Test-Path $f)) { throw "$f manquant" }
    $fv = (Get-Item $f).VersionInfo.FileVersion
    if ($fv -ne $version) { throw "$exe porte la version $fv, pas $version : recompile." }
}

$iscc = @("$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe", "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
          "$env:ProgramFiles\Inno Setup 6\ISCC.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { throw 'Inno Setup 6 introuvable : winget install JRSoftware.InnoSetup' }
& $iscc /Q "/DAppVersion=$version" "/DBinDir=$bin" (Join-Path $Root 'installer\MacDock.iss')
if ($LASTEXITCODE -ne 0) { throw "Inno Setup a échoué ($LASTEXITCODE)" }

$name = "MacDock-Setup-$version.exe"
$setup = Join-Path $Root "build\installer\$name"
$hash = (Get-FileHash -Algorithm SHA256 $setup).Hash.ToLowerInvariant()
Set-Content -Path (Join-Path $Root 'build\installer\SHA256SUMS.txt') -Value "$hash  $name" -Encoding ascii
Write-Host "== $setup"
Write-Host "== SHA-256 $hash"
