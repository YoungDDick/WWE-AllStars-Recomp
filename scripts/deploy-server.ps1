# Deploys the game server to the VPS in one command (ticket 14): uploads the committed
# server/ to /opt/allstars, rebuilds the container there and installs its nginx site
# (server/deploy/remote-deploy.sh). The access-key config goes up over SSH only, made
# here from your own copy of the game; it never enters Git or the image.
#
#   pwsh ./scripts/deploy-server.ps1 -AccessKeyFrom assets/Game/Config/Xenon/Cooked/Coalesced.ini
#   pwsh ./scripts/deploy-server.ps1     # redeploy, keeping the config on the VPS
#   pwsh ./scripts/deploy-server.ps1 -RequiredGameVersion tu2   # release 1.0.0: only TU2 clients
#   pwsh ./scripts/deploy-server.ps1 -RequiredGameVersion ''    # take every client again
# The SSH target (user@host) comes from -Target or the ALLSTARS_DEPLOY_TARGET environment
# variable; the repository names no host.
param(
    [string]$Target = $env:ALLSTARS_DEPLOY_TARGET,
    # A Coalesced.ini to make a new config from (allstars-server init); needs -Action Server.
    [string]$AccessKeyFrom = '',
    # Sets the config's required_game_version before the redeploy (tu2-dlc ticket 06); left
    # alone when not given. An empty value takes every client.
    [AllowEmptyString()][string]$RequiredGameVersion = $null
)
$ErrorActionPreference = 'Stop'
if (-not $Target) { throw 'No SSH target: pass -Target user@host or set ALLSTARS_DEPLOY_TARGET.' }
$projectRoot = Split-Path $PSScriptRoot -Parent
Set-Location $projectRoot
$ssh = @('-o', 'BatchMode=yes', '-o', 'ConnectTimeout=15', '-o', 'ServerAliveInterval=15', '-o', 'ServerAliveCountMax=4')
# The address players reach the server at, the compose network's gateway (nginx's requests
# come from there) and the auth port it publishes: all fixed in server/deploy/compose.yaml.
$publicAddress = '46.36.38.4'
$trustedProxy = '172.29.48.1'
$authPort = 32480

function Invoke-Native([string]$Name, [string[]]$Arguments) {
    & $Name @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Name failed (exit $LASTEXITCODE)" }
}

if (git status --porcelain -- server) {
    Write-Warning 'server/ has uncommitted changes; the deploy uses the last commit (HEAD).'
}
$work = Join-Path ([IO.Path]::GetTempPath()) "allstars-deploy-$PID"
New-Item -ItemType Directory -Force $work | Out-Null
try {
    Invoke-Native ssh ($ssh + @($Target, 'mkdir -p /opt/allstars && chmod 700 /opt/allstars'))
    if ($AccessKeyFrom) {
        $server = "$projectRoot/out/server/allstars-server.exe"
        if (!(Test-Path -LiteralPath $server)) { throw "Missing $server (run scripts/build.ps1 -Action Server)" }
        $configPath = Join-Path $work 'allstars-server.json'
        $ini = (Resolve-Path -LiteralPath $AccessKeyFrom).Path
        # The server's logger creates ./log at startup: keep it out of the repo.
        Push-Location $work
        try { Invoke-Native $server @('init', '-config', $configPath, $ini) } finally { Pop-Location }
        $config = Get-Content -Raw -LiteralPath $configPath | ConvertFrom-Json
        if ($config.auth_port -ne $authPort) {
            throw "$AccessKeyFrom names auth port $($config.auth_port); compose.yaml publishes $authPort to $($authPort + 2)"
        }
        $config | Add-Member -Force public_address $publicAddress
        $config | Add-Member -Force trusted_proxies @($trustedProxy)
        [IO.File]::WriteAllText($configPath, ($config | ConvertTo-Json) + "`n")
        Invoke-Native scp ($ssh + @('-q', $configPath, "${Target}:/opt/allstars/allstars-server.json"))
        Write-Output 'Uploaded the access-key config.'
    }
    if ($PSBoundParameters.ContainsKey('RequiredGameVersion')) {
        if ($RequiredGameVersion -notmatch '^[a-z0-9]*$') { throw "Not a game version: $RequiredGameVersion" }
        Invoke-Native ssh ($ssh + @($Target, "cd /opt/allstars && jq --arg v '$RequiredGameVersion' " +
                "'if `$v == `"`" then del(.required_game_version) else .required_game_version = `$v end' " +
                'allstars-server.json > allstars-server.json.new && mv allstars-server.json.new allstars-server.json'))
        Write-Output "required_game_version set to '$RequiredGameVersion'."
    }
    $archive = Join-Path $work 'server.tar'
    Invoke-Native git @('archive', '--format=tar', "--output=$archive", 'HEAD:server')
    Invoke-Native scp ($ssh + @('-q', $archive, "${Target}:/opt/allstars/server.tar"))
    Invoke-Native ssh ($ssh + @($Target, 'cd /opt/allstars && rm -rf server.new && mkdir server.new && ' +
            'tar -xf server.tar -C server.new && rm server.tar && bash server.new/deploy/remote-deploy.sh'))
    if ($PSBoundParameters.ContainsKey('RequiredGameVersion')) {
        # compose keeps an unchanged container running, so it would not read the new config.
        Invoke-Native ssh ($ssh + @($Target, 'cd /opt/allstars && docker compose restart server'))
    }
} finally {
    Remove-Item -Recurse -Force -LiteralPath $work
}
