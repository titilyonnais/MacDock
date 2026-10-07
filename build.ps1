# Construction de MacDock avec MSVC (Visual Studio 2022), sans CMake.
#   ./build.ps1 -Target tests -Run
#   ./build.ps1 -Target all -Config Release
param(
    [ValidateSet('tests', 'dock', 'launcher', 'all')] [string]$Target = 'all',
    [ValidateSet('Debug', 'Release')] [string]$Config = 'Debug',
    [switch]$Run
)
$ErrorActionPreference = 'Stop'
$Root = $PSScriptRoot

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio avec les outils C++ est introuvable.' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'

$Common = @('/nologo', '/std:c++latest', '/W4', '/permissive-', '/EHsc', '/utf-8', '/MP',
            '/DUNICODE', '/D_UNICODE', '/DNOMINMAX', '/DWIN32_LEAN_AND_MEAN', '/D_WIN32_WINNT=0x0A00')
if ($Config -eq 'Debug') { $Common += @('/Zi', '/Od', '/MDd', '/D_DEBUG') }
else { $Common += @('/O2', '/MD', '/DNDEBUG', '/Zi') }

function Get-Sources([string[]]$Patterns) {
    $files = foreach ($p in $Patterns) { Get-ChildItem -Path (Join-Path $Root $p) -ErrorAction SilentlyContinue }
    $files | Where-Object { $_ } | ForEach-Object { $_.FullName } | Sort-Object -Unique
}

# Modules logiques (sans dépendance graphique) partagés par les tests.
$LogicSources = @('src\core\*.cpp', 'src\config\*.cpp', 'src\geom\*.cpp', 'src\layout\*.cpp', 'src\anim\*.cpp',
                  'src\model\*.cpp', 'src\ipc\*.cpp', 'src\launcher\crash_policy.cpp',
                  'src\icons\*.cpp', 'src\tracker\app_identity.cpp', 'src\shell\*.cpp',
                  'src\app\dock_controller.cpp', 'src\app\dock_menus.cpp', 'src\app\visibility.cpp', 'src\app\monitor_choice.cpp', 'src\app\thumbnails.cpp',
                  'src\interact\*.cpp', 'src\popup\menu_model.cpp', 'src\stack\*.cpp',
                  'src\menubar\bar_layout.cpp', 'src\menubar\bar_color.cpp', 'src\menubar\clock_format.cpp',
                  'src\menubar\shortcut.cpp', 'src\menubar\foreground_rules.cpp', 'src\menubar\menubar_settings.cpp')

$Targets = @{
    tests    = @{ Exe = 'tests.exe'; Sources = @('tests\*.cpp') + $LogicSources + @('src\render\*.cpp', 'src\calib\*.cpp', 'src\glass\*.cpp'); Subsystem = 'CONSOLE';
                  Libs = @('user32.lib', 'shell32.lib', 'ole32.lib', 'advapi32.lib', 'windowscodecs.lib', 'gdi32.lib', 'dwmapi.lib', 'propsys.lib', 'version.lib',
                      'd3d11.lib', 'dxgi.lib', 'd2d1.lib', 'dwrite.lib', 'dcomp.lib', 'dxguid.lib') }
    dock     = @{ Exe = 'MacDock.exe'; Sources = @('src\core\*.cpp', 'src\config\*.cpp', 'src\geom\*.cpp', 'src\layout\*.cpp',
                      'src\anim\*.cpp', 'src\model\*.cpp', 'src\ipc\*.cpp', 'src\icons\*.cpp', 'src\tracker\*.cpp',
                      'src\shell\*.cpp', 'src\render\*.cpp', 'src\calib\*.cpp', 'src\glass\*.cpp', 'src\popup\*.cpp', 'src\interact\*.cpp', 'src\stack\*.cpp', 'src\app\*.cpp'); Subsystem = 'WINDOWS';
                  Libs = @('d3d11.lib', 'dxgi.lib', 'dcomp.lib', 'd2d1.lib', 'dwrite.lib', 'windowscodecs.lib',
                      'dwmapi.lib', 'shell32.lib', 'shlwapi.lib', 'ole32.lib', 'oleaut32.lib', 'user32.lib',
                      'gdi32.lib', 'advapi32.lib', 'propsys.lib', 'uxtheme.lib', 'version.lib', 'dbghelp.lib', 'shcore.lib', 'dxguid.lib') }
    launcher = @{ Exe = 'MacDockLauncher.exe'; Sources = @('src\launcher\*.cpp', 'src\core\*.cpp'); Subsystem = 'WINDOWS';
                  Libs = @('user32.lib', 'shell32.lib', 'advapi32.lib', 'ole32.lib') }
}

# Shaders HLSL (src\glass\shaders) compilés par le fxc du SDK en en-têtes (g_<nom>) dans build\<Config>\shaders.
$ShaderDir = Join-Path $Root "build\$Config\shaders"
function Build-Shaders {
    New-Item -ItemType Directory -Force -Path $ShaderDir | Out-Null
    $files = Get-ChildItem -Path (Join-Path $Root 'src\glass\shaders\*.hlsl') -ErrorAction SilentlyContinue
    if (-not $files) { return }
    $cmds = foreach ($f in $files) {
        $name = $f.BaseName
        $shaderProfile = if ($name.EndsWith('_vs')) { 'vs_5_0' } else { 'ps_5_0' }
        "fxc /nologo /O3 /T $shaderProfile /E main /Vn g_$name /Fh `"$ShaderDir\$name.h`" `"$($f.FullName)`" >nul"
    }
    Write-Host "== shaders : $($files.Count) fichiers"
    cmd /c "`"$vcvars`" 10.0.26100.0 >nul && $($cmds -join ' && ')"
    if ($LASTEXITCODE -ne 0) { throw 'Echec de compilation des shaders' }
}

function Build-Target([string]$Name) {
    $t = $Targets[$Name]
    $out = Join-Path $Root "build\$Config"
    $obj = Join-Path $out "obj\$Name"
    New-Item -ItemType Directory -Force -Path $obj | Out-Null
    $sources = Get-Sources $t.Sources
    if (-not $sources) { throw "Aucune source pour $Name" }
    $rsp = Join-Path $out "$Name.rsp"
    $lines = $Common + @("/I`"$ShaderDir`"", "/Fo`"$obj\\`"", "/Fd`"$obj\\vc.pdb`"", "/Fe`"$out\$($t.Exe)`"") +
             ($sources | ForEach-Object { "`"$_`"" })
    $link = (@("/SUBSYSTEM:$($t.Subsystem)", '/DEBUG', '/INCREMENTAL:NO') + $t.Libs) -join ' '
    Set-Content -Path $rsp -Value $lines -Encoding ascii
    Write-Host "== $Name ($Config) : $($sources.Count) fichiers"
    cmd /c "`"$vcvars`" 10.0.26100.0 >nul && cl @`"$rsp`" /link $link"
    if ($LASTEXITCODE -ne 0) { throw "Echec de compilation : $Name" }
}

$names = if ($Target -eq 'all') { @('tests', 'dock', 'launcher') } else { @($Target) }
if ($names -contains 'tests' -or $names -contains 'dock') { Build-Shaders }
foreach ($n in $names) { Build-Target $n }

if ($Run -and ($names -contains 'tests')) {
    & (Join-Path $Root "build\$Config\tests.exe")
    exit $LASTEXITCODE
}
