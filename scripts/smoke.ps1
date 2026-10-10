# Bounded startup probe: run the game for N seconds, keep logs and screenshots,
# and report the outcome together with any unregistered guest call targets.
param(
    [ValidateRange(1, 600)][int]$Seconds = 30,
    [ValidateSet('info', 'debug', 'trace')][string]$LogLevel = 'info',
    # Window screenshots every N seconds (0 = none); needs a visible window.
    [ValidateRange(0, 600)][int]$ScreenshotEvery = 0,
    [ValidateSet('fsi', 'rtv')][string]$RenderTargetPath = 'fsi',
    [string[]]$ExtraArguments = @(),
    [switch]$Headless,
    [switch]$PerfCapture
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$executable = "$projectRoot/out/build/win-amd64-relwithdebinfo-vulkan/wweallstars.exe"
if (!(Test-Path $executable)) { throw "Build first: $executable" }
$run = Join-Path $projectRoot ("analysis/runtime-" + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force $run | Out-Null

$info = [System.Diagnostics.ProcessStartInfo]::new()
$info.FileName = $executable
$info.WorkingDirectory = Split-Path $executable -Parent
$info.UseShellExecute = $false
$info.RedirectStandardOutput = $true
$info.RedirectStandardError = $true
$arguments = @('--gpu_plugin=xenos', '--vulkan_device=-1',
    "--render_target_path_vulkan=$RenderTargetPath",
    "--game_data_root=$projectRoot/assets", "--update_data_root=$projectRoot/updatedata", "--user_data_root=$projectRoot/userdata/vulkan",
    "--cache_root=$projectRoot/cache/vulkan",
    "--log_file=$run/runtime.log", "--log_level=$LogLevel", '--log_flush_interval=1')
if ($Headless) { $arguments += '--headless' }
if ($PerfCapture) { $arguments += "--perf_log_csv=$run/perf.csv" }
foreach ($argument in $ExtraArguments) {
    if ($argument -notmatch '^--[a-z0-9_]+=') { throw "Invalid runtime argument: $argument" }
    $arguments += $argument
}
foreach ($argument in $arguments) { $info.ArgumentList.Add($argument) }

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class SmokeWin {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
}
'@
function Save-Screenshot([System.Diagnostics.Process]$p, [string]$path) {
    $p.Refresh()
    $handle = $p.MainWindowHandle
    if ($handle -eq [IntPtr]::Zero) { return }
    $rect = [SmokeWin+RECT]::new()
    if (![SmokeWin]::GetWindowRect($handle, [ref]$rect)) { return }
    $w = $rect.R - $rect.L; $h = $rect.B - $rect.T
    if ($w -le 0 -or $h -le 0) { return }
    $bitmap = [System.Drawing.Bitmap]::new($w, $h)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    # Ask the window to render itself (PW_RENDERFULLCONTENT) instead of copying
    # the screen, so other windows on the desktop never end up in a capture.
    $hdc = $graphics.GetHdc()
    $ok = [SmokeWin]::PrintWindow($handle, $hdc, 2)
    $graphics.ReleaseHdc($hdc)
    if ($ok) { $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png) }
    $graphics.Dispose(); $bitmap.Dispose()
}

$process = [System.Diagnostics.Process]::new()
$process.StartInfo = $info
$started = $false
$timer = [System.Diagnostics.Stopwatch]::StartNew()
try {
    $started = $process.Start()
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    $shot = 0
    while (!$process.HasExited -and $timer.Elapsed.TotalSeconds -lt $Seconds) {
        $step = if ($ScreenshotEvery -gt 0) { $ScreenshotEvery } else { $Seconds }
        $remaining = [math]::Max(0, $Seconds - $timer.Elapsed.TotalSeconds)
        $null = $process.WaitForExit([int]([math]::Min($step, $remaining) * 1000))
        if ($ScreenshotEvery -gt 0 -and !$process.HasExited) {
            $shot++
            Save-Screenshot $process ("$run/shot-{0:D3}.png" -f $shot)
        }
    }
    $timedOut = !$process.HasExited
    if ($timedOut) { $process.Kill($true); $process.WaitForExit() }
    $stdout.GetAwaiter().GetResult() | Set-Content "$run/stdout.txt"
    $stderr.GetAwaiter().GetResult() | Set-Content "$run/stderr.txt"
    $fatalTargets = @()
    if (Test-Path "$run/runtime.log") {
        $fatalTargets = @(Select-String -Path "$run/runtime.log" -Pattern '\[FATAL\].*guest address (0x[0-9A-Fa-f]+)' |
            ForEach-Object { $_.Matches[0].Groups[1].Value })
    }
    $outcome = if ($timedOut) { 'alive_at_deadline' } elseif ($process.ExitCode -eq 0) { 'exited' } else { 'crashed' }
    $result = [ordered]@{
        seconds = $Seconds; elapsed_seconds = [math]::Round($timer.Elapsed.TotalSeconds, 2)
        exit_code = $(if ($timedOut) { $null } else { $process.ExitCode }); outcome = $outcome
        fatal_guest_targets = $fatalTargets; screenshots = $shot; logs = $run
    }
    $result | ConvertTo-Json | Set-Content "$run/result.json"
    $result | ConvertTo-Json
} finally {
    if ($started -and !$process.HasExited) { $process.Kill($true); $process.WaitForExit() }
    $process.Dispose()
}
