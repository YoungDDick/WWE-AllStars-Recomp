# ADR-0021: Online play: revive the game's own mode against a self-hostable server

Status: accepted (2026-10-05, the owner's go after the online spike).
Supersedes ADR-0008.

## Context

ADR-0008 stubbed online for v1 and left an investigation for later.
`docs/research/online-mode-feasibility.md` found that online play stacks
Xbox Live (sign-in, sessions, leaderboards), Quazal Net-Z (an input-lockstep
match transport with checksums) and Quazal Rendez-Vous (a login service THQ
hosted behind the Live security gateway). All of the servers are gone, and
ReXGlue stubs the network and session calls. The owner decided the direction
in a grilling session on 2026-10-05.

## Decision

- **Revive the game's own online mode**: its menus, lobby, Player Match and
  later Ranked Match, with Net-Z carrying the match. Recomp-level netplay over
  local multiplayer was rejected: it would have to keep the whole program in
  sync, not only the match, and it loses the lobby and created-superstar
  sharing.
- **The port has its own game server**: one Go service built on
  PretendoNetwork's nex-go, in `server/` in this repo. It lists sessions,
  answers the Rendez-Vous login, and relays traffic between players who can't
  connect directly. Rejected alternatives: running Xenia Web Services, which
  means two stacks and MongoDB, and a C# service, which would mean writing
  PRUDP from scratch.
- **A Rendez-Vous stand-in, not a host hook.** The game runs its real Quazal
  login against the game server, so no game logic is patched.
- **Self-hostable.** The launcher has a server field. It defaults to the
  owner's VPS at `allstars.komplexaci.cz`, and anyone can run the same binary.
- **Direct connection first, relay as the fallback.**
- **No accounts.** Each install generates a random XUID, and the player sets
  their name in the launcher.
- **Scope.** First Player Match, including 4-player matches and
  created-superstar sharing. Ranked Match comes later, if at all. Voice chat
  is out. Original leaderboards, Marketplace and Live system UI stay gone.
- **Opt-in.** Online play is an experimental launcher toggle, off by default.
  The server only matches players running identical builds.
- **Timing.** The spike ran first (`.scratch/online-spike/`). Amended
  2026-10-05: the sync test, the online build's first milestone, runs before
  v1.0, behind the off-by-default `--wwe_online` cvar. The rest waits until
  after v1.0, first in the post-v1 order. v1.0 ships with online off.
- **Amended 2026-10-05 (grilling): the client follows the Xenia netplay fork.**
  The SDK ports the fork's XNet and XSession client, and the game server
  implements the subset of the Xenia Web Services REST API that client calls.
  XWS itself still isn't run. Plan: `.scratch/online-play/spec.md`.
- **Amended 2026-10-07: nex-go's license.** nex-go and nex-protocols-go are AGPL-3.0,
  which this ADR didn't account for and ADR-0007 (BSD-3, no GPL) would rule out. The
  owner chose to keep them: online play is for friends, and what works best matters
  more than the license. The server in `server/` is a separate program and carries
  nex-go's AGPL; the game and the SDK stay BSD-3. nex-go is vendored in
  `server/third_party/nex-go` with local fixes (it dropped the game's 11-byte PINGs).

## Consequences

- The spike could not run the NETZRECORD/NETZPLAYBACK test: SyncSim starts
  only in a hosted or joined Net-Z session (`docs/research/online-mode-feasibility.md`
  §6.4). That test is the first milestone of the online build. A desync it finds
  blocks all other network work until it's fixed.
- The spike's probe (`patches/rexglue-online-probe.patch`, branch `online-spike`;
  retired 2026-10-07, replaced by `--wwe_online`)
  shows what the SDK has to provide for real: the Live LSP server list, XNet
  connect, socket byte order and overlapped receives (feasibility §6.3).
- The owner's VPS hosts a public UDP service, in a container with a memory
  limit.
