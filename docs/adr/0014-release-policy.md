# ADR-0014: Releases: private until Native is the default

Status: accepted (2026-10-02)

## Decision

The repo stays private through M5. The repo and first binaries go public when Native becomes the default. Releases are built by hand; CI builds only what needs no game data.

## Consequences

- The launcher's update check is off while the repo is private:
  `UpdateChecker.ProjectRepository` (`launcher/src/Launcher.Core/UpdateCheck.cs`)
  is empty, so no request is sent and the setting is hidden. Set it to the
  public `owner/name` in the release that goes public (launcher ticket 22).
- **2026-10-10:** going public with 0.9.0. `ProjectRepository` is
  `shaneomac1337/WWE-AllStars-Recomp`; release tags are `v<version>` (`v0.9.0`).
- **2026-10-10 (later):** the first public release is 1.0.0 (`v1.0.0`); 0.9.0 was
  never published. The public history is built without internal files (ADR-0023).
