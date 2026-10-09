# Contrôles de tools/sign-release.ps1 avant de signer (plan 53, relecture : importants 2 et 3), sur des données
# fabriquées à la forme de l'API de GitHub : contenu exact de SHA256SUMS.txt, et provenance de la version (étiquette,
# auteur et date des fichiers, run de release.yml, autres runs). Sans réseau ; code de sortie 1 en cas d'échec.
#   pwsh -NoProfile -File tests/release_checks.tests.ps1
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\tools\release-checks.ps1')
$failures = 0
function Expect($ok, $what) { if ($ok) { "OK     $what" } else { "ÉCHEC  $what"; $script:failures++ } }

$hash = 'd3e3c2a044962a450db1a6b4c66999507d319d2ac887baf314beadba00300257'
$exe = 'MacDock-Setup-0.53.0.exe'
function Ascii([string]$s) { [Text.Encoding]::ASCII.GetBytes($s) }
Expect ($null -eq (Test-SumsContent (Ascii "$hash  $exe`r`n") $hash $exe)) 'SHA256SUMS.txt tel qu''écrit par make-installer.ps1 : accepté'
Expect ($null -ne (Test-SumsContent (Ascii "$hash  $exe`r`n$('a' * 64)  MacDock-Setup-9.0.0.exe`r`n") $hash $exe)) 'une ligne glissée en plus : refusé'
Expect ($null -ne (Test-SumsContent (Ascii "$('a' * 64)  MacDock-Setup-9.0.0.exe`r`n$hash  $exe`r`n") $hash $exe)) 'une ligne glissée avant : refusé'
Expect ($null -ne (Test-SumsContent (Ascii "$hash  $exe`n") $hash $exe)) 'autre fin de ligne : refusé (octet pour octet)'
Expect ($null -ne (Test-SumsContent (Ascii "$hash  $exe`r`n") ('0' * 64) $exe)) 'autre empreinte : refusé'

# Provenance : une version telle que release.yml la publie (dates comme les donne l'API).
$sha = '1111111111111111111111111111111111111111'
function Run($path, $branch, $headSha, $created, $started, $updated, $conclusion = 'success') {
    [pscustomobject]@{ path = $path; head_branch = $branch; head_sha = $headSha; event = 'push'; status = 'completed'
                       conclusion = $conclusion; created_at = $created; run_started_at = $started; updated_at = $updated }
}
$release = Run '.github/workflows/release.yml' 'v0.53.0' $sha '2026-10-09T12:00:00Z' '2026-10-09T12:00:05Z' '2026-10-09T12:06:00Z'
$ci = Run '.github/workflows/ci.yml' 'main' $sha '2026-10-09T11:59:58Z' '2026-10-09T12:00:01Z' '2026-10-09T12:08:00Z'
$old = Run '.github/workflows/ci.yml' 'vieille' ('4' * 40) '2026-10-09T09:00:00Z' '2026-10-09T09:00:05Z' '2026-10-09T09:06:00Z'
function Asset($name, $login = 'github-actions[bot]', $at = '2026-10-09T12:05:30Z') {
    [pscustomobject]@{ name = $name; updated_at = $at; uploader = [pscustomobject]@{ login = $login } }
}
function Published($assets) { [pscustomobject]@{ tag_name = 'v0.53.0'; draft = $false; assets = $assets } }
$known = { param($s) $s -eq $sha }
$ok = Published @((Asset $exe), (Asset 'SHA256SUMS.txt'))
Expect ($null -eq (Test-Provenance $ok @($ci, $release, $old) 'v0.53.0' $sha $sha $known)) 'version publiée par release.yml : acceptée (vieux run d''ailleurs ignoré)'
Expect ($null -ne (Test-Provenance $ok @($ci, $release) 'v0.53.0' $sha ('2' * 40) $known)) 'étiquette distante sur un autre commit : refusée'
Expect ($null -ne (Test-Provenance $ok @($ci, $release) 'v0.53.0' '' '' $known)) 'étiquette absente ici : refusée'
Expect ($null -ne (Test-Provenance (Published @((Asset $exe 'titilyonnais'), (Asset 'SHA256SUMS.txt'))) @($ci, $release) 'v0.53.0' $sha $sha $known)) 'installateur envoyé à la main : refusé'
Expect ($null -ne (Test-Provenance (Published @((Asset $exe), (Asset 'SHA256SUMS.txt' 'github-actions[bot]' '2026-10-09T12:30:00Z'))) @($ci, $release) 'v0.53.0' $sha $sha $known)) 'empreintes remplacées après le run : refusées'
Expect ($null -ne (Test-Provenance (Published @((Asset $exe 'github-actions[bot]' '2026-10-09T11:00:00Z'), (Asset 'SHA256SUMS.txt'))) @($ci, $release) 'v0.53.0' $sha $sha $known)) 'installateur d''avant le run : refusé'
Expect ($null -ne (Test-Provenance (Published @((Asset $exe))) @($ci, $release) 'v0.53.0' $sha $sha $known)) 'SHA256SUMS.txt absent : refusé'
$failed = Run '.github/workflows/release.yml' 'v0.53.0' $sha '2026-10-09T12:00:00Z' '2026-10-09T12:00:05Z' '2026-10-09T12:06:00Z' 'failure'
Expect ($null -ne (Test-Provenance $ok @($ci, $failed) 'v0.53.0' $sha $sha $known)) 'run de release.yml en échec : refusé'
Expect ($null -ne (Test-Provenance $ok @($ci) 'v0.53.0' $sha $sha $known)) 'aucun run de release.yml : refusé'
$elsewhere = Run '.github/workflows/release.yml' 'v0.53.0' ('5' * 40) '2026-10-09T12:00:00Z' '2026-10-09T12:00:05Z' '2026-10-09T12:06:00Z'
Expect ($null -ne (Test-Provenance $ok @($ci, $elsewhere) 'v0.53.0' $sha $sha $known)) 'run de release.yml sur un autre commit : refusé'
$intruder = Run '.github/workflows/ci.yml' 'piege' ('3' * 40) '2026-10-09T12:02:00Z' '2026-10-09T12:02:05Z' '2026-10-09T12:03:00Z'
Expect ($null -ne (Test-Provenance $ok @($intruder, $ci, $release) 'v0.53.0' $sha $sha $known)) 'autre run au même moment, sur un commit inconnu ici : refusé'

''
if ($failures) { "ÉCHECS : $failures"; exit 1 } else { 'tout est exact' }
