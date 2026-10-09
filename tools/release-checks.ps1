# Contrôles faits par tools/sign-release.ps1 avant de signer une version (plan 53, relecture : importants 2 et 3).
# Fonctions sans réseau (données de l'API de GitHub en entrée, message d'erreur ou $null en sortie), essayées par
# tests/release_checks.tests.ps1. Dates : chaînes ISO 8601, comme les donne l'API (ConvertFrom-Json -DateKind String).

function ConvertTo-Instant($Value) {
    [datetimeoffset]::Parse([string]$Value, [Globalization.CultureInfo]::InvariantCulture)
}

# SHA256SUMS.txt doit être, octet pour octet, la ligne de l'installateur vérifié telle que make-installer.ps1 l'écrit :
# la signature ne vaut alors que pour lui (une ligne glissée en plus, pour une autre version, ne serait jamais signée).
function Test-SumsContent([byte[]]$Bytes, [string]$Hash, [string]$Installer) {
    $expected = [Text.Encoding]::ASCII.GetBytes("$Hash  $Installer`r`n")
    if ([Convert]::ToBase64String($Bytes) -cne [Convert]::ToBase64String($expected)) {
        return "SHA256SUMS.txt ne contient pas exactement la ligne de $Installer ($Hash) : rien n'est signé."
    }
    return $null
}

# La version vient bien de release.yml, sur le commit de l'étiquette d'ici :
# - l'étiquette vise le même commit sur GitHub qu'ici ;
# - le run de release.yml pour cette étiquette, sur ce commit, a réussi ;
# - l'installateur et SHA256SUMS.txt ont été envoyés par ce run (github-actions[bot], pendant le run) ;
# - aucun autre run, depuis une demi-heure avant, ne vient d'un commit inconnu ici (un autre workflow aurait pu
#   envoyer des fichiers sous le même nom de github-actions[bot]).
# $Runs : workflow_runs de l'API ; $IsKnownCommit : { param($sha) … } vrai si ce commit existe dans le dépôt d'ici.
function Test-Provenance($Release, $Runs, [string]$Tag, [string]$LocalSha, [string]$RemoteSha, [scriptblock]$IsKnownCommit) {
    if (-not $LocalSha) { return "L'étiquette $Tag n'existe pas ici." }
    if ($LocalSha -ne $RemoteSha) { return "L'étiquette $Tag vise $RemoteSha sur GitHub, $LocalSha ici." }
    $run = @($Runs | Where-Object {
            $_.path -eq '.github/workflows/release.yml' -and $_.event -eq 'push' -and $_.head_branch -eq $Tag -and
            $_.head_sha -eq $LocalSha -and $_.status -eq 'completed'
        } | Sort-Object { ConvertTo-Instant $_.updated_at }) | Select-Object -Last 1
    if (-not $run) { return "Aucun run terminé de release.yml pour $Tag sur ce commit." }
    if ($run.conclusion -ne 'success') { return "Le run de release.yml pour $Tag a fini en « $($run.conclusion) »." }
    $start = ConvertTo-Instant $run.run_started_at
    $end = (ConvertTo-Instant $run.updated_at).AddMinutes(1)
    foreach ($name in @("MacDock-Setup-$($Tag.Substring(1)).exe", 'SHA256SUMS.txt')) {
        $asset = @($Release.assets | Where-Object { $_.name -eq $name }) | Select-Object -First 1
        if (-not $asset) { return "$name absent de la version $Tag." }
        if ($asset.uploader.login -ne 'github-actions[bot]') { return "$name a été envoyé par $($asset.uploader.login), pas par release.yml." }
        $at = ConvertTo-Instant $asset.updated_at
        if ($at -lt $start -or $at -gt $end) { return "$name date de $($at.ToString('u')), hors du run de release.yml ($($start.ToString('u')) à $($end.ToString('u')))." }
    }
    $since = (ConvertTo-Instant $run.created_at).AddMinutes(-30)
    foreach ($r in $Runs) {
        if ((ConvertTo-Instant $r.created_at) -lt $since) { continue }
        if (-not (& $IsKnownCommit $r.head_sha)) {
            return "Run de $($r.path) sur « $($r.head_branch) » ($($r.head_sha)) : commit inconnu ici, version non signée."
        }
    }
    return $null
}
