# Signature des versions de MacDock (plan 53). La clé ECDSA P-256 « MacDock Release » vit dans le magasin de clés
# Windows de ce compte (CNG), non exportable : elle ne quitte jamais ce PC. MacDock n'installe une mise à jour que si
# SHA256SUMS.txt porte sa signature (SHA256SUMS.txt.sig), vérifiée avec la clé publique de src\update\release_key.h.
#   ./tools/sign-release.ps1 -CreateKey      crée la clé (une seule fois) et écrit src\update\release_key.h
#   ./tools/sign-release.ps1 -Tag v0.53.0    après release.yml : télécharge la version publiée, revérifie l'empreinte
#                                            de l'installateur, signe SHA256SUMS.txt et joint SHA256SUMS.txt.sig
param(
    [switch]$CreateKey,
    [string]$Tag = '',
    [string]$Repo = 'titilyonnais/MacDock',
    [string]$KeyName = 'MacDock Release'
)
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
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
if (-not $Cng::Exists($KeyName)) { throw "La clé « $KeyName » n'existe pas sur ce PC : signature impossible." }
$dir = Join-Path ([IO.Path]::GetTempPath()) "macdock-sign-$Tag"
if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
New-Item -ItemType Directory -Path $dir | Out-Null
$version = $Tag.Substring(1)
$installer = "MacDock-Setup-$version.exe"
gh release download $Tag --repo $Repo --pattern $installer --pattern 'SHA256SUMS.txt' --dir $dir
if ($LASTEXITCODE -ne 0) { throw "Version $Tag introuvable sur $Repo." }
# On ne signe que ce qu'on a vérifié : l'installateur publié doit avoir l'empreinte annoncée.
$sumsPath = Join-Path $dir 'SHA256SUMS.txt'
$line = Get-Content $sumsPath | Where-Object { $_ -match "^([0-9a-f]{64})  $([regex]::Escape($installer))$" } | Select-Object -First 1
if (-not $line) { throw "SHA256SUMS.txt ne donne pas l'empreinte de $installer." }
$expected = $line.Substring(0, 64)
$actual = (Get-FileHash -Algorithm SHA256 (Join-Path $dir $installer)).Hash.ToLowerInvariant()
if ($actual -ne $expected) { throw "Empreinte de $installer différente ($actual) : version NON signée." }
$sums = [IO.File]::ReadAllBytes($sumsPath)
$signature = Sign-Bytes $sums
if (-not (Verify-Bytes $sums $signature)) { throw 'Signature invérifiable : rien n''est publié.' }
$sigPath = Join-Path $dir 'SHA256SUMS.txt.sig'
[IO.File]::WriteAllText($sigPath, [Convert]::ToBase64String($signature) + "`n", [Text.Encoding]::ASCII)
gh release upload $Tag $sigPath --repo $Repo --clobber
if ($LASTEXITCODE -ne 0) { throw 'Envoi de la signature impossible.' }
Write-Host "== $Tag signée : $installer ($expected)."
Remove-Item -Recurse -Force $dir
