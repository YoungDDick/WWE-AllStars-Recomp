# ADR-0023: The public repository gets its own history, built without internal files

Status: accepted (2026-10-10, the owner's decision for the GitHub launch).
Amends ADR-0014 (release policy): the repo goes public, but not its private history.

## Context

The repo goes public on GitHub as `shaneomac1337/WWE-AllStars-Recomp`, the name the
shipped launcher already checks for updates. Its local history holds what was never meant
to be published (`docs/research/github-and-reddit-launch.md`, section A):

- `.scratch/`, the issue tracker, which CLAUDE.md said is never pushed;
- the launch research, which analyses the owner's Reddit account;
- the author's private email on every commit, and the VPS's root SSH host in two scripts.

Rewriting `main` would break every commit hash the ADRs, tickets and release zips name.

## Decision

- Local `main` stays the full, private history. Nothing in it is rewritten.
- The public history lives on the local branch `public`, pushed as `main` of the GitHub
  repo. `scripts/publish-tree.ps1 -Message <text>` adds one commit to it: the tree of
  `HEAD` without `.scratch/` and the launch research, authored and committed as
  `shaneomac1337 <shaneomac1337@users.noreply.github.com>`, its parent the previous public
  commit. It refuses a commit that changes nothing.
- Scripts name no SSH host; `deploy-server.ps1` and `sync-test.ps1` take it from
  `-Target`/`-ServerSsh` or `ALLSTARS_DEPLOY_TARGET`.

## Consequences

- Public commits are coarse, one per publish (a release or a batch of fixes), and their
  hashes differ from `main`'s. A release zip is built from `main`; its name carries
  `main`'s short hash, and the release notes say which public commit holds the same tree.
- Public docs keep their links into `.scratch/`; those paths don't exist on GitHub. The
  ADRs and research stand on their own without them.
- `.scratch/` stays the tracker and keeps being committed to `main`.
