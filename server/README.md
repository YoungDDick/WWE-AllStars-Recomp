# allstars-server

The WWE All Stars recomp's game server (ADR-0021, `.scratch/online-play/spec.md`): the
Rendez-Vous stand-in the game logs in to, and the players and sessions the SDK's online
client registers and searches (the subset of the Xenia Web Services API it calls). One Go
program; state lives in memory.

```
pwsh ./scripts/build.ps1 -Action Server       # -> out/server/allstars-server.exe
pwsh ./scripts/build.ps1 -Action ServerTest   # Go tests
allstars-server init <game>/Game/Config/Xenon/Cooked/Coalesced.ini
allstars-server serve
```

`init` reads the title's `[Quazal]` access key and port from your own copy of the game
into `allstars-server.json` (never committed). The server listens on UDP 32480
(Rendez-Vous authentication), UDP 32481 (the secure server), UDP 32482 (the relay
for peers that cannot reach each other, `internal/relay`) and TCP 36000 (the XWS HTTP
API the game's online client calls). `public_address` in the config is the address
players reach it at (loopback by default).

Go is pinned by `scripts/bootstrap.ps1` (`.tools/go`). nex-go is vendored with a small
fix in `third_party/nex-go` (see its `WWE-ALLSTARS.md`); nex-go is AGPL-3.0, which the
owner accepted for this friends-only server (ADR-0021), so `server/` is AGPL-3.0
(`LICENSE`); the game and the SDK stay BSD-3.

## The VPS

`allstars.komplexaci.cz` runs the server in Docker (`Dockerfile`, `deploy/compose.yaml`,
256 MB limit) in `/opt/allstars`. Rendez-Vous UDP is published on the VPS address; the
XWS API listens on loopback and the host nginx serves it over HTTPS
(`deploy/allstars.komplexaci.cz`, certbot). The server believes `X-Forwarded-For` only
from `trusted_proxies` (the compose network's gateway). Players start the game with
`--wwe_online_server=https://allstars.komplexaci.cz`.

```
pwsh ./scripts/deploy-server.ps1 -AccessKeyFrom <game>/Game/Config/Xenon/Cooked/Coalesced.ini
pwsh ./scripts/deploy-server.ps1    # redeploy the last commit, keeping the config
```

The first form makes the config here and uploads it over SSH. The certificate is
requested once `allstars.komplexaci.cz` resolves to the VPS (a DNS-only record:
Cloudflare's proxy carries no UDP).
