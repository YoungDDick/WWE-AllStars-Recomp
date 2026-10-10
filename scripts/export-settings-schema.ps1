# Regenerates the launcher's committed copy of the game's player settings list
# (launcher/src/Launcher.Core/SettingsSchema.json) from a built game exe. The
# exe's --print-settings-schema prints JSON and exits before any window opens.
# Launcher.Tests' drift test fails while the two differ.
param(
    [string]$Game = "$PSScriptRoot/../out/build/win-amd64-relwithdebinfo-vulkan/wweallstars.exe"
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$exe = (Resolve-Path -LiteralPath $Game).Path
$info = [Diagnostics.ProcessStartInfo]::new($exe)
$info.ArgumentList.Add('--print-settings-schema')
$info.UseShellExecute = $false
$info.RedirectStandardOutput = $true
$info.RedirectStandardError = $true
$info.StandardOutputEncoding = [Text.UTF8Encoding]::new($false)
$info.WorkingDirectory = Split-Path $exe -Parent
$process = [Diagnostics.Process]::Start($info)
$errors = $process.StandardError.ReadToEndAsync()
$json = $process.StandardOutput.ReadToEnd()
if (!$process.WaitForExit(30000)) { $process.Kill(); throw "$exe --print-settings-schema did not exit" }
if ($process.ExitCode -ne 0) { throw "$exe --print-settings-schema exited with $($process.ExitCode): $($errors.Result)" }
if (!$json.StartsWith('{')) { throw "$exe --print-settings-schema printed no schema: $json" }
$target = "$projectRoot/launcher/src/Launcher.Core/SettingsSchema.json"
[IO.File]::WriteAllText($target, $json, [Text.UTF8Encoding]::new($false))
Write-Output "Settings schema: $target"
