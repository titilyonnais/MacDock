# Fonctions communes aux installateurs de MacDock pour Windhawk (install-macdock-look.ps1, coins-macos.ps1), chargées
# par « . (Join-Path $PSScriptRoot 'windhawk-common.ps1') ». Windows PowerShell 5.1 : pas de ?? ni d'opérateur ternaire.

# 7-Zip (sait ouvrir les .dmg et .pkg d'Apple) : son installateur MSI officiel, extrait sans être installé.
$script:SevenZipUrl = 'https://www.7-zip.org/a/7z2501-x64.msi'
$script:SevenZipSha256 = 'E7EB0B7ED5EFA4E087B7B17F191797F7AF5B7F442D1290C66F3A21777005EF57'
# SF Pro, sur le site des polices d'Apple (developer.apple.com/fonts).
$script:SFProUrl = 'https://devimages-cdn.apple.com/design/resources/download/SF-Pro.dmg'

function Test-Admin {
    ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
}

# Chemins de Windhawk ; $null s'il n'est pas installé.
function Get-Windhawk {
    $root = Join-Path $env:ProgramFiles 'Windhawk'
    $clang = Join-Path $root 'Compiler\bin\clang++.exe'
    if (-not (Test-Path $clang)) { return $null }
    $engine = Get-ChildItem (Join-Path $root 'Engine') -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^\d+(\.\d+)+$' } | Sort-Object { [version]$_.Name } | Select-Object -Last 1
    if (-not $engine) { return $null }
    [pscustomobject]@{
        Root     = $root
        Compiler = Join-Path $root 'Compiler'
        Clang    = $clang
        Lib      = Join-Path $engine.FullName '64\windhawk.lib'
        Mods     = Join-Path $env:ProgramData 'Windhawk\Engine\Mods\64'
        Sources  = Join-Path $env:ProgramData 'Windhawk\ModsSource'
        Key      = 'HKLM:\SOFTWARE\Windhawk\Engine'
    }
}

# Windhawk absent : proposé par winget (paquet RamenSoftware.Windhawk, celui de windhawk.net). $true s'il est là ensuite.
function Install-WindhawkIfMissing {
    if (Get-Windhawk) { return $true }
    Write-Host "Windhawk n'est pas installé (c'est lui qui charge les mods de MacDock dans les apps)."
    $answer = Read-Host "L'installer maintenant avec winget (source officielle, windhawk.net) ? (O/N)"
    if ($answer -notmatch '^[oOyY]') { return $false }
    $winget = Get-Command winget -ErrorAction SilentlyContinue
    if (-not $winget) {
        Write-Host 'winget est introuvable : installe Windhawk depuis https://windhawk.net, puis relance ce script.'
        return $false
    }
    & $winget.Source install --id RamenSoftware.Windhawk --exact --silent --accept-package-agreements --accept-source-agreements |
        Out-Host
    if (-not (Get-Windhawk)) { Write-Host "Windhawk ne s'est pas installé (voir ci-dessus)."; return $false }
    return $true
}

# Dossier de travail. En administrateur : à la racine de ProgramData, modifiable par les seuls administrateurs et
# SYSTEM ; ce qui y est vérifié puis exécuté ou compilé en administrateur ne peut pas être remplacé entre-temps par un
# programme de la session, comme dans %TEMP%. Sans droits (essais) : %TEMP%.
function New-WorkDir([string]$Name) {
    $leaf = "macdock-$Name-" + [guid]::NewGuid().ToString('N')
    if (-not (Test-Admin)) {
        $dir = Join-Path $env:TEMP $leaf
        New-Item -ItemType Directory -Force $dir | Out-Null
        return $dir
    }
    $dir = Join-Path $env:ProgramData $leaf   # à la racine : un dossier parent créé par la session ne le renomme pas
    New-Item -ItemType Directory -Force $dir | Out-Null
    # Héritage coupé : Administrateurs (S-1-5-32-544) et SYSTEM (S-1-5-18) seuls, contrôle total.
    & icacls.exe $dir /inheritance:r /grant:r '*S-1-5-32-544:(OI)(CI)F' '*S-1-5-18:(OI)(CI)F' | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Droits du dossier de travail impossibles à poser ($dir)." }
    # Rien n'a pu y être glissé avant la pose des droits (un fichier d'un autre garderait les siens).
    if (@(Get-ChildItem -LiteralPath $dir -Force).Count -ne 0) { throw "Dossier de travail inattendu ($dir)." }
    return $dir
}

# Téléchargement (curl.exe de Windows : rapide, avec sa barre de progression ; sinon Invoke-WebRequest).
function Get-Download([string]$Url, [string]$OutFile) {
    $curl = Join-Path $env:windir 'System32\curl.exe'
    if (Test-Path $curl) {
        & $curl -L --fail --progress-bar -o $OutFile $Url
        if ($LASTEXITCODE -ne 0) { throw "Téléchargement impossible : $Url" }
    } else {
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        $ProgressPreference = 'SilentlyContinue'
        Invoke-WebRequest -Uri $Url -OutFile $OutFile -UseBasicParsing
    }
    if (-not (Test-Path $OutFile)) { throw "Téléchargement impossible : $Url" }
}

function Assert-Sha256([string]$Path, [string]$Expected) {
    $got = (Get-FileHash $Path -Algorithm SHA256).Hash
    if ($got -ne $Expected) { throw "Fichier inattendu ($Path) : empreinte $got au lieu de $Expected." }
}

# Compile un mod comme l'éditeur de Windhawk (même compilateur, mêmes options) et l'enregistre comme lui : liste
# d'inclusion et d'exclusion lues dans la source, réglages par défaut si le mod n'en a pas encore, réglages imposés
# chaque fois. -OutDir : compile seulement (essai sans droits administrateur), rend le chemin de la DLL.
function Install-WindhawkMod {
    param([Parameter(Mandatory)] $Wh, [Parameter(Mandatory)] [string]$Id, [Parameter(Mandatory)] [string]$Source,
          [hashtable]$DefaultSettings = @{}, [hashtable]$ForceSettings = @{}, [string]$OutDir)
    $text = [IO.File]::ReadAllText($Source)
    $version = '1.0.0'
    $found = [regex]::Match($text, '(?m)^// @version\s+(\S+)')
    if ($found.Success) { $version = $found.Groups[1].Value }
    $target = if ($OutDir) { $OutDir } else { $Wh.Mods }
    New-Item -ItemType Directory -Force $target | Out-Null
    $dllName = "{0}_{1}_{2}.dll" -f $Id, $version, (Get-Random -Minimum 100000 -Maximum 999999)
    $dll = Join-Path $target $dllName
    # Identifiant et version par un en-tête inclus d'abord (pas de guillemets à faire passer dans la ligne de commande),
    # dans un dossier de travail sûr (New-WorkDir), supprimé ensuite.
    $idsDir = New-WorkDir 'wh-ids'
    $ids = Join-Path $idsDir 'ids.h'
    Set-Content -Path $ids -Encoding ASCII -Value "#define WH_MOD_ID L`"$Id`"`r`n#define WH_MOD_VERSION L`"$version`""
    $options = @()
    $opt = [regex]::Match($text, '(?m)^// @compilerOptions\s+(.+?)\s*$')
    if ($opt.Success) { $options = $opt.Groups[1].Value -split '\s+' | Where-Object { $_ } }
    $compileArgs = @('-std=c++23', '-O2', '-shared', '-DUNICODE', '-D_UNICODE', '-DWINVER=0x0A00', '-D_WIN32_WINNT=0x0A00',
        '-D_WIN32_IE=0x0A00', '-DNTDDI_VERSION=0x0A000008', '-D__USE_MINGW_ANSI_STDIO=0', '-DWH_MOD', $Wh.Lib, '-x', 'c++',
        $Source, '-include', $ids, '-include', 'windhawk_api.h', '-target', 'x86_64-w64-mingw32', '-Wl,--export-all-symbols',
        '-o', $dll) + $options
    Push-Location $Wh.Compiler
    try { & $Wh.Clang @compileArgs | Out-Host } finally {
        Pop-Location
        Remove-Item $idsDir -Recurse -Force -ErrorAction SilentlyContinue
    }
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $dll)) { throw "La compilation du mod $Id a échoué (voir ci-dessus)." }
    if ($OutDir) { return $dll }

    # Bibliothèques du compilateur, comme l'éditeur les dépose à côté des mods.
    foreach ($pair in @(@('libc++.dll', 'libc++.whl'), @('libunwind.dll', 'libunwind.whl'), @('windhawk-mod-shim.dll', 'windhawk-mod-shim.dll'))) {
        $from = Join-Path $Wh.Compiler "x86_64-w64-mingw32\bin\$($pair[0])"
        $to = Join-Path $Wh.Mods $pair[1]
        if ((Test-Path $from) -and -not (Test-Path $to)) { Copy-Item $from $to }
    }
    $key = "$($Wh.Key)\Mods\$Id"
    $old = (Get-ItemProperty $key -ErrorAction SilentlyContinue).LibraryFileName
    $includes = [regex]::Matches($text, '(?m)^// @include\s+(.+?)\s*$') | ForEach-Object { $_.Groups[1].Value }
    $excludes = [regex]::Matches($text, '(?m)^// @exclude\s+(.+?)\s*$') | ForEach-Object { $_.Groups[1].Value }
    # Sans -Force sur la clé : -Force la recréerait et effacerait les réglages du mod.
    if (-not (Test-Path $key)) { New-Item -Path $key -Force | Out-Null }
    New-ItemProperty -Path $key -Name LibraryFileName -Value $dllName -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $key -Name Disabled -Value 0 -PropertyType DWord -Force | Out-Null
    New-ItemProperty -Path $key -Name LoggingEnabled -Value 0 -PropertyType DWord -Force | Out-Null
    New-ItemProperty -Path $key -Name Include -Value ($includes -join '|') -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $key -Name Exclude -Value ($excludes -join '|') -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $key -Name Architecture -Value 'x86-64' -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $key -Name Version -Value $version -PropertyType String -Force | Out-Null
    $settings = Join-Path $key 'Settings'
    if (-not (Test-Path $settings)) {
        New-Item -Path $settings -Force | Out-Null
        Set-WindhawkSettings $settings $DefaultSettings
    }
    Set-WindhawkSettings $settings $ForceSettings
    Update-WindhawkMod $Wh $Id
    New-Item -ItemType Directory -Force $Wh.Sources | Out-Null
    Copy-Item $Source (Join-Path $Wh.Sources "$Id.wh.cpp") -Force
    if ($old -and $old -ne $dllName) { Remove-Item (Join-Path $Wh.Mods $old) -Force -ErrorAction SilentlyContinue }
}

# Réglages d'un mod : entiers en DWORD, le reste en texte (comme l'éditeur de Windhawk).
function Set-WindhawkSettings([string]$Path, [hashtable]$Values) {
    if (-not (Test-Path $Path)) { New-Item -Path $Path -Force | Out-Null }
    foreach ($name in $Values.Keys) {
        $v = $Values[$name]
        $type = if ($v -is [int]) { 'DWord' } else { 'String' }
        New-ItemProperty -Path $Path -Name $name -Value $v -PropertyType $type -Force | Out-Null
    }
}

# Horodatage du changement : Windhawk recharge le mod dans les processus.
function Update-WindhawkMod($Wh, [string]$Id) {
    New-ItemProperty -Path "$($Wh.Key)\Mods\$Id" -Name SettingsChangeTime -Value ([int][DateTimeOffset]::UtcNow.ToUnixTimeSeconds()) `
        -PropertyType DWord -Force | Out-Null
}

function Remove-WindhawkMod($Wh, [string]$Id) {
    $key = "$($Wh.Key)\Mods\$Id"
    $old = (Get-ItemProperty $key -ErrorAction SilentlyContinue).LibraryFileName
    Remove-Item $key -Recurse -Force -ErrorAction SilentlyContinue
    if ($old) { Remove-Item (Join-Path $Wh.Mods $old) -Force -ErrorAction SilentlyContinue }
    Remove-Item (Join-Path $Wh.Sources "$Id.wh.cpp") -Force -ErrorAction SilentlyContinue
}

# Liste d'inclusion globale de Windhawk (Paramètres > Avancé) : sans dwm.exe, aucun mod n'est chargé dans DWM.
# $true si dwm.exe vient d'y être ajouté (Windhawk doit alors redémarrer). -DryRun : rien n'est écrit.
function Add-DwmToWindhawk($Wh, [switch]$DryRun) {
    $include = [string](Get-ItemProperty "$($Wh.Key)\Settings" -ErrorAction SilentlyContinue).Include
    $list = @($include -split '\|' | ForEach-Object { $_.Trim() } | Where-Object { $_ })
    if ($list | Where-Object { $_ -ieq 'dwm.exe' }) { return $false }
    if (-not $DryRun) {
        if (-not (Test-Path "$($Wh.Key)\Settings")) { New-Item -Path "$($Wh.Key)\Settings" -Force | Out-Null }
        New-ItemProperty -Path "$($Wh.Key)\Settings" -Name Include -Value (($list + 'dwm.exe') -join '|') -PropertyType String -Force | Out-Null
    }
    return $true
}

# Windhawk ne se charge dans un processus déjà lancé (dwm.exe) qu'en redémarrant. Les autres mods sont rechargés au
# passage (la barre des tâches peut réapparaître un instant).
function Restart-Windhawk {
    try { Restart-Service -Name Windhawk -ErrorAction Stop }
    catch { Write-Host "Le service Windhawk n'a pas redémarré ($($_.Exception.Message)) : redémarre Windhawk (ou la session)." }
}

# ---- SF Pro ----

function Test-SFPro {
    Add-Type -AssemblyName System.Drawing
    $names = (New-Object System.Drawing.Text.InstalledFontCollection).Families | ForEach-Object { $_.Name }
    return ($names -contains 'SF Pro Text') -and ($names -contains 'SF Pro Display')
}

# « SF-Pro-Text-SemiboldItalic.otf » → « SF Pro Text Semibold Italic (OpenType) » (nom de la police dans le registre).
function Get-FontRegistryName([string]$File) {
    $base = [IO.Path]::GetFileNameWithoutExtension($File)
    $m = [regex]::Match($base, '^SF-Pro-(Text|Display)-(.+)$')
    if (-not $m.Success) { return "$base (OpenType)" }
    $style = $m.Groups[2].Value -creplace '(?<=[a-z])(?=[A-Z])', ' '
    return "SF Pro $($m.Groups[1].Value) $style (OpenType)"
}

# SF Pro Text et Display téléchargées sur le site d'Apple puis installées pour tout le PC (administrateur), après
# accord sur la licence d'Apple. -Yes : accord déjà donné. $true si elles sont installées ensuite. -ExtractOnly
# (essai, sans droits administrateur) : polices extraites dans ce dossier, rien d'installé.
function Install-SFPro([switch]$Yes, [string]$ExtractOnly) {
    if (-not $ExtractOnly -and (Test-SFPro)) { Write-Host 'SF Pro (Text et Display) est déjà installée.'; return $true }
    Write-Host ''
    Write-Host "La police SF Pro (Text et Display) n'est pas installée. Ce script peut la télécharger sur le site"
    Write-Host "d'Apple (developer.apple.com/fonts, environ 220 Mo) et l'installer pour tout le PC."
    Write-Host ''
    Write-Host "ATTENTION, licence : SF Pro appartient à Apple. Son contrat (texte complet dans le paquet téléchargé)"
    Write-Host "la réserve à la création de maquettes d'interfaces pour les systèmes d'Apple : l'utiliser comme police"
    Write-Host "de Windows n'est pas prévu par ce contrat. MacDock ne la fournit pas ; c'est toi qui choisis de la"
    Write-Host "télécharger chez Apple et de l'installer, en connaissance de cause. Sans elle, le mod ne change rien."
    if (-not $Yes) {
        $answer = Read-Host 'Télécharger et installer SF Pro malgré tout ? (O/N)'
        if ($answer -notmatch '^[oOyY]') { return $false }
    }
    $work = New-WorkDir 'sfpro'
    try {
        Write-Host '7-Zip (pour ouvrir le paquet d''Apple), sans l''installer…'
        $msi = Join-Path $work '7z.msi'
        Get-Download $script:SevenZipUrl $msi
        Assert-Sha256 $msi $script:SevenZipSha256
        $p = Start-Process msiexec.exe -ArgumentList "/a `"$msi`" /qn TARGETDIR=`"$work\7zip`"" -Wait -PassThru
        $z = Get-ChildItem "$work\7zip" -Recurse -Filter 7z.exe -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($p.ExitCode -ne 0 -or -not $z) { throw "7-Zip n'a pas pu être extrait (msiexec $($p.ExitCode))." }
        Write-Host 'SF Pro, depuis le site d''Apple…'
        $dmg = Join-Path $work 'SF-Pro.dmg'
        Get-Download $script:SFProUrl $dmg
        Write-Host 'Extraction…'
        $steps = @(
            @{ From = $dmg; To = "$work\dmg"; Next = { Get-ChildItem "$work\dmg" -Recurse -Filter *.pkg | Select-Object -First 1 } },
            @{ To = "$work\pkg"; Next = { Get-ChildItem "$work\pkg" -Recurse -Filter Payload | Select-Object -First 1 } },
            @{ To = "$work\payload"; Next = { Get-ChildItem "$work\payload" -File | Select-Object -First 1 } },
            @{ To = "$work\fonts"; Next = { $null } })
        $from = $dmg
        foreach ($s in $steps) {
            & $z.FullName x $from "-o$($s.To)" -y | Out-Null
            if ($LASTEXITCODE -ne 0) { throw "Extraction impossible ($from)." }
            $next = & $s.Next
            if ($s.To -ne "$work\fonts") {
                if (-not $next) { throw "Paquet d'Apple inattendu (rien après $($s.To))." }
                $from = $next.FullName
            }
        }
        $fonts = Get-ChildItem "$work\fonts" -Recurse -File | Where-Object { $_.Name -match '^SF-Pro-(Text|Display)-.+\.otf$' }
        if ($fonts.Count -lt 10) { throw "Polices SF Pro introuvables dans le paquet d'Apple ($($fonts.Count))." }
        if ($ExtractOnly) {
            New-Item -ItemType Directory -Force $ExtractOnly | Out-Null
            $fonts | ForEach-Object { Copy-Item $_.FullName $ExtractOnly }
            Write-Host "SF Pro extraite ($($fonts.Count) polices) dans $ExtractOnly."
            return $true
        }
        Add-Type -Namespace MacDock -Name Fonts -MemberDefinition @'
[DllImport("gdi32.dll", CharSet = CharSet.Unicode)] public static extern int AddFontResourceW(string file);
[DllImport("user32.dll")] public static extern System.IntPtr SendMessageTimeoutW(System.IntPtr h, uint m, System.IntPtr w,
    System.IntPtr l, uint flags, uint timeout, out System.IntPtr result);
'@ -ErrorAction SilentlyContinue
        $dir = Join-Path $env:windir 'Fonts'
        $reg = 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Fonts'
        foreach ($f in $fonts) {
            $dest = Join-Path $dir $f.Name
            if (-not (Test-Path $dest)) { Copy-Item $f.FullName $dest }
            New-ItemProperty -Path $reg -Name (Get-FontRegistryName $f.Name) -Value $f.Name -PropertyType String -Force | Out-Null
            [void][MacDock.Fonts]::AddFontResourceW($dest)
        }
        $r = [IntPtr]::Zero
        [void][MacDock.Fonts]::SendMessageTimeoutW([IntPtr]0xffff, 0x001D, [IntPtr]::Zero, [IntPtr]::Zero, 2, 1000, [ref]$r)   # WM_FONTCHANGE
        Write-Host "SF Pro installée ($($fonts.Count) polices)."
        return $true
    } catch {
        Write-Host "SF Pro n'a pas pu être installée : $($_.Exception.Message)"
        return $false
    } finally {
        Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
    }
}
