# Convert the shader containers captured with --wwe_shader_dump_dir into the
# native renderer's shader pack, which the game loads at startup from its cache
# folder (default: cache/vulkan, the cache_root the dev scripts pass). Every
# folder and file here holds game-derived data and stays local.
param([string]$InputDirectory = 'cache/guest-shaders',
      [string]$OutputPath = 'cache/vulkan/wweallstars.shaderpack')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
Set-Location $projectRoot
$tool = 'out/build/xenosrecomp/XenosRecomp/XenosRecomp.exe'
if (!(Test-Path $tool)) { & "$PSScriptRoot/build-xenosrecomp.ps1" | Out-Null }
if (!(Get-ChildItem $InputDirectory -Filter '*.bin' -ErrorAction SilentlyContinue)) {
    throw "No shader containers in $InputDirectory. Run the game once with --wwe_shader_dump_dir=$InputDirectory."
}
New-Item -ItemType Directory -Force cache/shader-native | Out-Null
& $tool $InputDirectory cache/shader-native/shader_cache.cpp .tools/xenosrecomp/XenosRecomp/shader_common.h cache/shader-native/hlsl *> cache/shader-native/run.log
if ($LASTEXITCODE -ne 0) { throw "XenosRecomp failed; see cache/shader-native/run.log" }
# The XenosRecomp patch the shaders were converted with; a build configured
# with another patch treats the pack as stale and runs on Emulated.
$converterId = (Get-FileHash -Algorithm SHA256 patches/xenosrecomp-wweallstars.patch).Hash.Substring(0, 16).ToLower()
$outputDirectory = Split-Path $OutputPath -Parent
if ($outputDirectory) { New-Item -ItemType Directory -Force $outputDirectory | Out-Null }
# The pack also holds the texture slots each shader reads, so draws bind only those.
& python "$PSScriptRoot/shader_pack.py" cache/shader-native/shader_cache.cpp cache/shader-native/hlsl $converterId $OutputPath
if ($LASTEXITCODE -ne 0) { throw "shader_pack.py failed" }
Write-Output "Shader pack written: $OutputPath"
