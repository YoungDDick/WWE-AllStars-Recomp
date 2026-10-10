param([string]$SevenZipPath)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/Invoke-Hidden.ps1"
$projectRoot = Split-Path $PSScriptRoot -Parent
Set-Location $projectRoot
New-Item -ItemType Directory -Force .tools | Out-Null
$artifacts = @(
    @{
        File = 'rexglue-sdk-0.10.0-win-amd64.zip'
        Url = 'https://github.com/rexglue/rexglue-sdk/releases/download/v0.10.0/rexglue-sdk-0.10.0-win-amd64.zip'
        Sha256 = 'ad4485da853fbe932e9f5dbb643f9b5fe85dc73775ebbad4df6879fe6df0c11b'
        Destination = '.tools/rexglue-0.10.0'
        Executable = '.tools/rexglue-0.10.0/win-amd64/bin/rexglue.exe'
    },
    @{
        File = 'LLVM-21.1.8-win64.exe'
        Url = 'https://github.com/llvm/llvm-project/releases/download/llvmorg-21.1.8/LLVM-21.1.8-win64.exe'
        Sha256 = '7a5386c26497db1691f320121e5b113364dd0274b98e55f15f4dbc00c0450113'
        Destination = '.tools/llvm-21.1.8'
        Executable = '.tools/llvm-21.1.8/bin/clang++.exe'
    },
    @{
        # The launcher's SDK, pinned by launcher/global.json. Microsoft publishes SHA-512 only
        # (https://builds.dotnet.microsoft.com/dotnet/release-metadata/10.0/releases.json).
        File = 'dotnet-sdk-10.0.401-win-x64.zip'
        Url = 'https://builds.dotnet.microsoft.com/dotnet/Sdk/10.0.401/dotnet-sdk-10.0.401-win-x64.zip'
        Sha512 = '24b670ad3d923bfcf47df6c3b034152398b42f6dbc388e10d783aee1cfb5e5817d399fc0ae2a12cfa822a55e61d34830ccb15c50ef6efee437ab874bb7c79430'
        Destination = '.tools/dotnet-sdk'
        Executable = '.tools/dotnet-sdk/dotnet.exe'
    },
    @{
        # The game server's Go (server/go.mod names the same version). The zip holds a go/ folder.
        File = 'go1.27.1.windows-amd64.zip'
        Url = 'https://go.dev/dl/go1.27.1.windows-amd64.zip'
        Sha256 = 'a3911b5e0e1b1053f25ed0675f4c1c6aad1e2bfcf253df2b9be4caabd2edd95d'
        Destination = '.tools'
        Executable = '.tools/go/bin/go.exe'
    },
    @{
        # The SDK's online client (patches/rexglue-online.patch): libcurl and rapidjson at the
        # commits the Xenia netplay fork pins (6dbaa1f). The zips hold a <name>-<commit>/ folder.
        File = 'curl-a05f349.zip'
        Url = 'https://github.com/curl/curl/archive/a05f34973e6c4bb629d018f7cb51487be1c904d8.zip'
        Sha256 = '7e43e784d0a66573d82405cd85e594afed845955cfb0c4838f0fc5ff3c3d26a4'
        Destination = '.tools'
        Executable = '.tools/curl-a05f34973e6c4bb629d018f7cb51487be1c904d8/CMakeLists.txt'
    },
    @{
        File = 'rapidjson-7c73dd7.zip'
        Url = 'https://github.com/Tencent/rapidjson/archive/7c73dd7de7c4f14379b781418c6e947ad464c818.zip'
        Sha256 = '9aa224aea970ac2cd8edf21d8215c70ef66f2e4ad29ae127c1e0c72729c1733f'
        Destination = '.tools'
        Executable = '.tools/rapidjson-7c73dd7de7c4f14379b781418c6e947ad464c818/include/rapidjson/rapidjson.h'
    },
    @{
        # UPnP port mapping for online play (--wwe_online_upnp): miniupnpc 2.3.3 at the
        # commit the Xenia netplay fork pins (6dbaa1f, third_party/miniupnp).
        File = 'miniupnp-b5e5d2e.zip'
        Url = 'https://github.com/miniupnp/miniupnp/archive/b5e5d2eb069822b7f00d56c8e61033b9d500e60c.zip'
        Sha256 = 'fce90c333069569cfe46feb69bc2e03c91033693202cbcd6dc9197786770f9ee'
        Destination = '.tools'
        Executable = '.tools/miniupnp-b5e5d2eb069822b7f00d56c8e61033b9d500e60c/miniupnpc/CMakeLists.txt'
    }
)
foreach ($artifact in $artifacts) {
    $archive = Join-Path '.tools' $artifact.File
    if (!(Test-Path -LiteralPath $archive)) {
        Invoke-WebRequest $artifact.Url -OutFile $archive
    }
    $algorithm = if ($artifact.Sha512) { 'SHA512' } else { 'SHA256' }
    $expected = if ($artifact.Sha512) { $artifact.Sha512 } else { $artifact.Sha256 }
    if ((Get-FileHash $archive -Algorithm $algorithm).Hash -ne $expected) {
        throw "Checksum mismatch: $archive. No files were extracted."
    }
    if (!(Test-Path -LiteralPath $artifact.Executable)) {
        if ($archive.EndsWith('.zip')) {
            Expand-Archive $archive $artifact.Destination
        } else {
            if (!$SevenZipPath) {
                $SevenZipPath = @((Get-Command 7z -ErrorAction SilentlyContinue).Source,
                    "$env:ProgramFiles/7-Zip/7z.exe", "${env:ProgramFiles(x86)}/7-Zip/7z.exe") |
                    Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1
            }
            if (!$SevenZipPath -or !(Test-Path -LiteralPath $SevenZipPath)) {
                throw '7-Zip not found. Install it from https://www.7-zip.org or pass -SevenZipPath with the path to 7z.exe.'
            }
            Invoke-Hidden $SevenZipPath @('x', $archive, "-o$($artifact.Destination)", '-y')
        }
    }
    Write-Output "Verified $($artifact.File)"
}
Write-Output 'Local toolchain is ready. No system installation or PATH changes were made.'
