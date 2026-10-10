# Run an input script in a background session and capture shots until the
# screen stops changing (for example the result screen) or -Seconds pass.
#   pwsh scripts/run-input.ps1 -Name t1 -Script scripts/input/exhibition-1v1.txt
# Shots and shots.tsv (shot -> guest present count) go to analysis/session-<Name>/.
param([Parameter(Mandatory)][string]$Name, [int]$Seconds = 600, [int]$Every = 10, [Parameter(Mandatory)][string]$Script,
      [string[]]$ExtraArguments = @('--wwe_d3d_census=true'),
      # Stop early once this many consecutive shots barely change (a static
      # screen such as the result screen), but not before MinSeconds (loading
      # screens before a match sit still for about 20 s).
      [int]$StaticShots = 3, [int]$MinSeconds = 240)
Set-Location (Split-Path $PSScriptRoot -Parent)
Add-Type -AssemblyName System.Drawing
function Get-Thumb([string]$path) {
    $img = [System.Drawing.Bitmap]::FromFile($path)
    $small = New-Object System.Drawing.Bitmap $img, 64, 36
    $img.Dispose()
    $px = foreach ($y in 0..35) { foreach ($x in 0..63) { $c = $small.GetPixel($x, $y); [int](($c.R + $c.G + $c.B) / 3) } }
    $small.Dispose(); , $px
}
& ./scripts/session.ps1 start -Name $Name -ExtraArguments $ExtraArguments
pwsh ./scripts/session.ps1 input -Name $Name (Get-Content $Script) | Out-Null
$log = "analysis/session-$Name/runtime.log"
$index = "analysis/session-$Name/shots.tsv"
"stem`ttime`tpresents" | Set-Content $index
$t = [Diagnostics.Stopwatch]::StartNew()
$prev = $null; $still = 0
for ($i = 0; $t.Elapsed.TotalSeconds -lt $Seconds; $i++) {
    Start-Sleep $Every
    $stem = 's{0:D3}' -f $i
    $file = pwsh ./scripts/session.ps1 shot $stem -Name $Name
    $m = Select-String -Path $log -Pattern 'presents=(\d+)' | Select-Object -Last 1
    $p = if ($m) { $m.Matches[0].Groups[1].Value } else { '' }
    "$stem`t$([int]$t.Elapsed.TotalSeconds)`t$p" | Add-Content $index
    try { $cur = Get-Thumb $file } catch { $cur = $null }
    if ($prev -and $cur) {
        $diff = 0; for ($k = 0; $k -lt $cur.Count; $k++) { $diff += [math]::Abs($cur[$k] - $prev[$k]) }
        $still = if (($diff / $cur.Count) -lt 2) { $still + 1 } else { 0 }
    }
    $prev = $cur
    if ($still -ge $StaticShots - 1 -and $t.Elapsed.TotalSeconds -ge $MinSeconds) { "static screen at $stem, stopping early"; break }
}
pwsh ./scripts/session.ps1 status -Name $Name
pwsh ./scripts/session.ps1 stop -Name $Name
