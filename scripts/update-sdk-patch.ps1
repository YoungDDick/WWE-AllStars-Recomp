# Rewrite patches/<Name>.patch (default rexglue-smooth-presentation) from the SDK tree and restamp the tree.
# The diff is taken against a private index that holds the tree with every earlier patch applied
# (written once by -Baseline before the first edit), so the patch carries only this work.
param([switch]$Baseline, [string]$Name = 'rexglue-smooth-presentation')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$source = "$projectRoot/.tools/rexglue-source"
$patch = "$projectRoot/patches/$Name.patch"
# The smooth-presentation baseline keeps its original file name.
$indexName = if ($Name -eq 'rexglue-smooth-presentation') { 'wwe-sdk-baseline.index' } else { "wwe-sdk-baseline-$Name.index" }
$env:GIT_INDEX_FILE = Join-Path ([IO.Path]::GetTempPath()) $indexName
if ($Baseline) {
    git -C $source read-tree HEAD
    git -C $source add -A
    return
}
if (!(Test-Path $env:GIT_INDEX_FILE)) { throw 'No baseline index; run with -Baseline on a tree without this patch.' }
# git add -N makes new files show up in the diff without staging their content.
git -C $source add -N -- src include
$diff = git -C $source diff --binary -- . ':!thirdparty' ':!.wwe-patches'
[IO.File]::WriteAllText($patch, (($diff -join "`n") + "`n"))
$stamp = "$source/.wwe-patches"
$lines = (Get-Content $stamp) | Where-Object { $_ -notmatch "^$([regex]::Escape($Name))\.patch " }
$hash = (Get-FileHash $patch -Algorithm SHA256).Hash
Set-Content -Path $stamp -Value (($lines + "$Name.patch $hash") -join "`n") -NoNewline
Write-Output "Wrote $patch ($hash)"
