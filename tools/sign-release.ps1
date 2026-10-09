# Signature des versions de MacDock (plan 53). La clé ECDSA P-256 « MacDock Release » vit dans le magasin de clés
# Windows de ce compte (CNG), non exportable : elle ne quitte jamais ce PC. MacDock n'installe une mise à jour que si
# SHA256SUMS.txt porte sa signature (SHA256SUMS.txt.sig), vérifiée avec la clé publique de src\update\release_key.h.
#   ./tools/sign-release.ps1 -CreateKey      crée la clé (une seule fois) et écrit src\update\release_key.h
#   ./tools/sign-release.ps1 -Tag v0.53.0    après release.yml : vérifie d'où vient la version publiée (étiquette
#                                            d'ici, fichiers envoyés par le run de release.yml, aucun autre run d'un
#                                            commit inconnu), revérifie l'empreinte de l'installateur et que
#                                            SHA256SUMS.txt ne contient que sa ligne, signe et joint SHA256SUMS.txt.sig
#   ./tools/sign-release.ps1 -Tag v0.53.0 -DryRun   tous les contrôles, sans rien signer ni envoyer
param(
    [switch]$CreateKey,
    [string]$Tag = '',
    [string]$Repo = 'titilyonnais/MacDock',
    [string]$KeyName = 'MacDock Release',
    [switch]$DryRun
)
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'release-checks.ps1')
Add-Type -AssemblyName System.Security
$Cng = [System.Security.Cryptography.CngKey]

function Sign-Bytes([byte[]]$data) {
    $ecdsa = [System.Security.Cryptography.ECDsaCng]::new($Cng::Open($KeyName))
    try { return $ecdsa.SignData($data, [System.Security.Cryptography.HashAlgorithmName]::SHA256) } finally { $ecdsa.Dispose() }
}

function Verify-Bytes([byte[]]$data, [byte[]]$signature) {
    $pub = $Cng::Import($Cng::Open($KeyName).Export([System.Security.Cryptography.CngKeyBlobFormat]::EccPublicBlob),
                        [System.Security.Cryptography.CngKeyBlobFormat]::EccPublicBlob)
    $ecdsa = [System.Security.Cryptography.ECDsaCng]::new($pub)
    try { return $ecdsa.VerifyData($data, $signature, [System.Security.Cryptography.HashAlgorithmName]::SHA256) } finally { $ecdsa.Dispose() }
}

if ($CreateKey) {
    if ($Cng::Exists($KeyName)) {
        Write-Host "== La clé « $KeyName » existe déjà : elle est gardée."
    } else {
        $p = [System.Security.Cryptography.CngKeyCreationParameters]::new()
        $p.ExportPolicy = [System.Security.Cryptography.CngExportPolicies]::None   # la clé privée ne sort jamais
        $p.KeyUsage = [System.Security.Cryptography.CngKeyUsages]::Signing
        $Cng::Create([System.Security.Cryptography.CngAlgorithm]::ECDsaP256, $KeyName, $p).Dispose()
        Write-Host "== Clé « $KeyName » créée dans le magasin de clés de ce compte."
    }
    $blob = $Cng::Open($KeyName).Export([System.Security.Cryptography.CngKeyBlobFormat]::EccPublicBlob)
    # Échantillon signé par la vraie clé : un test vérifie avec la clé publique que la signature de .NET (P1363) et la
    # vérification de CNG s'entendent.
    $sample = 'MacDock : échantillon signé par la clé des versions'
    $sampleSig = [Convert]::ToBase64String((Sign-Bytes ([Text.Encoding]::UTF8.GetBytes($sample))))
    $bytes = ($blob | ForEach-Object { '0x{0:x2}' -f $_ }) -join ', '
    $header = @"
// Clé publique des versions de MacDock (plan 53) : ECDSA P-256 « $KeyName », créée sur le PC de l'auteur par
// tools/sign-release.ps1 -CreateKey (la clé privée, non exportable, n'en sort jamais). Format BCRYPT_ECCPUBLIC_BLOB.
// Fichier écrit par ce script : ne pas modifier à la main.
#pragma once
#include <vector>

namespace md {

inline const std::vector<unsigned char>& releasePublicKey() {
    static const std::vector<unsigned char> key = {$bytes};
    return key;
}

// Échantillon signé par la vraie clé (tests : la signature de .NET et la vérification de CNG s'entendent).
inline constexpr char kReleaseKeySample[] = "$sample";
inline constexpr char kReleaseKeySampleSig[] = "$sampleSig";

} // namespace md
"@
    $out = Join-Path $Root 'src\update\release_key.h'
    [IO.File]::WriteAllText($out, $header.Replace("`r`n", "`n"), [Text.UTF8Encoding]::new($false))
    Write-Host "== $out écrit ($($blob.Length) octets de clé publique)."
    exit 0
}

if (-not $Tag) { throw 'Préciser -Tag vX.Y.Z (ou -CreateKey).' }
if ($Tag -notmatch '^v\d+\.\d+\.\d+(-[0-9A-Za-z.-]+)?$') { throw "Étiquette « $Tag » : vX.Y.Z attendu." }
if (-not $DryRun -and -not $Cng::Exists($KeyName)) { throw "La clé « $KeyName » n'existe pas sur ce PC : signature impossible." }
$version = $Tag.Substring(1)
$installer = "MacDock-Setup-$version.exe"

# D'où vient la version : rien n'est téléchargé tant que ce n'est pas établi.
$localSha = git -C $Root rev-parse --verify --quiet "$Tag^{commit}"
if ($LASTEXITCODE -ne 0) { $localSha = '' }
$remoteSha = gh api "repos/$Repo/commits/$Tag" --jq .sha
if ($LASTEXITCODE -ne 0) { throw "Étiquette $Tag introuvable sur $Repo." }
$release = gh api "repos/$Repo/releases/tags/$Tag" | ConvertFrom-Json -DateKind String
if ($LASTEXITCODE -ne 0) { throw "Version $Tag introuvable sur $Repo." }
$runs = (gh api "repos/$Repo/actions/runs?per_page=100" | ConvertFrom-Json -DateKind String).workflow_runs
if ($LASTEXITCODE -ne 0) { throw 'Runs de GitHub Actions illisibles.' }
$isKnown = { param($sha) git -C $Root cat-file -e "$sha^{commit}" 2>$null; $LASTEXITCODE -eq 0 }
$problem = Test-Provenance $release $runs $Tag $localSha $remoteSha $isKnown
if ($problem) { throw "$problem Version NON signée." }
if (-not $version.Contains('-')) {   # une version (pas une préversion d'essai) vient de main, comme le vérifie release.yml
    git -C $Root merge-base --is-ancestor $localSha refs/remotes/origin/main
    if ($LASTEXITCODE -ne 0) { throw "L'étiquette $Tag ne vise pas un commit de main : version NON signée." }
}
Write-Host "== Provenance vérifiée : $Tag ($localSha), fichiers envoyés par le run de release.yml."

$dir = Join-Path ([IO.Path]::GetTempPath()) "macdock-sign-$Tag"
if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
New-Item -ItemType Directory -Path $dir | Out-Null
gh release download $Tag --repo $Repo --pattern $installer --pattern 'SHA256SUMS.txt' --dir $dir
if ($LASTEXITCODE -ne 0) { throw "Version $Tag introuvable sur $Repo." }
# On ne signe que ce qu'on a vérifié : l'installateur publié a l'empreinte annoncée, et SHA256SUMS.txt ne contient
# que sa ligne (la signature ne vaut que pour lui).
$sumsPath = Join-Path $dir 'SHA256SUMS.txt'
$sums = [IO.File]::ReadAllBytes($sumsPath)
$actual = (Get-FileHash -Algorithm SHA256 (Join-Path $dir $installer)).Hash.ToLowerInvariant()
$problem = Test-SumsContent $sums $actual $installer
if ($problem) { throw "$problem Version NON signée." }
if ($DryRun) {
    Write-Host "== Essai à blanc réussi : $installer ($actual) serait signé ; rien n'est signé ni envoyé."
    Remove-Item -Recurse -Force $dir
    exit 0
}
$expected = $actual
$signature = Sign-Bytes $sums
if (-not (Verify-Bytes $sums $signature)) { throw 'Signature invérifiable : rien n''est publié.' }
$sigPath = Join-Path $dir 'SHA256SUMS.txt.sig'
[IO.File]::WriteAllText($sigPath, [Convert]::ToBase64String($signature) + "`n", [Text.Encoding]::ASCII)
gh release upload $Tag $sigPath --repo $Repo --clobber
if ($LASTEXITCODE -ne 0) { throw 'Envoi de la signature impossible.' }
Write-Host "== $Tag signée : $installer ($expected)."
Remove-Item -Recurse -Force $dir
