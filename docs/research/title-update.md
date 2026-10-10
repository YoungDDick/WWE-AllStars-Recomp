# WWE All Stars (X360): Title Updates, DLC and Xenia status

Target: title ID `54510866` (THQ "TQ-2150"), disc media ID `60CEF505`, XEX base version `00000005`, built 2011-02-23.
Researched 2026-10-02. Metadata only; no files were downloaded.

## Title Updates

Yes. Two Title Updates exist, and both apply to media ID `60CEF505`, base version `00000005`. That is the same media ID as our disc image, so both target our build.

| TU | xboxunity TitleUpdateID | Version | Media ID | Base ver | Size (xboxunity) | SHA-1 (xboxunity) | xboxunity "UploadDate" |
|----|------|---|----------|----------|------|------------------------------------------|------------|
| TU1 | 21825 | 1 | 60CEF505 | 00000005 | 6496 B | `8BD99B63F0E7A18496360B26532CEEBD8D723C78` | 2011-08-20 |
| TU2 | 23145 | 2 | 60CEF505 | 00000005 | 6496 B | `624C19BC1739FFB528F3AF52C43A907CB76834B1` | 2013-08-19 |

Notes and caveats:
- The xboxunity "UploadDate" is the date the file entered their DB. It is not necessarily the Xbox Live release date. TU1 was in the DB by 2011-08-20, which fits the DLC waves of Apr–Aug 2011.
- Both entries list a size of 6496 bytes. That is unusually small for a TU containing an `.xexp` delta. It could be a header-only or metadata-level patch, or xboxunity could be reporting the size wrongly. This is unverified.
- digiex.net has a thread titled "WWE All Stars Title Update #2", posted 2013-06-24. It describes a "3.9 MB" rar to be injected with Horizon, with the filename reported as `tu00000005_00000000`. This contradicts the xboxunity size. The page summary may be unreliable, so treat it as unverified.
- **No changelog was found** for either TU. THQ, gamingnexus, IGN-style news, Wikipedia and the fan wiki did not mention any 360 patch notes.
- Xenia Canary's patch DB (`54510866 - WWE All Stars.patch.toml`) targets the **base disc XEX**: module hash `76C738F43A2E4F66` (default.xex), media `60CEF505`. It does not target a TU. That is the reference point for our recomp.

## DLC timeline and the 360 delay

| Pack | Contents | PS3 | X360 | Price (360) |
|------|----------|-----|------|-------------|
| Million Dollar Pack | Ted DiBiase, Ted DiBiase Jr. | 2011-04-16 | 2011-04-28 / early May | Free (GameStop pre-order) |
| American Dream Pack | Dusty Rhodes, Cody Rhodes | 2011-04-16 | 2011-04-28 / early May | 160 MSP |
| Honky Tonk Man | single character | 2011-04-16 (free) | 2011-04-28 / early May (free) | Free |
| R-Truth | single character | 2011-07-05/06 (free) | same | Free (compensation for 360 delay) |
| All-Time Greats | Chris Jericho, Road Warriors, Jerry Lawler | 2011-07-06 | 2011-07-06 | n/a |
| Southern Charisma | Big Boss Man, Michael "P.S." Hayes, Mark Henry | 2011-08-02 | 2011-08-02 | 240 MSP |

Why the 360 DLC was late:
- THQ's statement (via N4G) said that they "ran into some technical issues with our WWE All Stars downloadable content for Xbox 360". The release slipped past the promised "on or before April 19" date.
- Search summaries cite a THQ line about "unforeseen delays with Microsoft". Wikipedia gives the 360 date as 2011-04-28. gamingnexus "finally hits the 360" (May 2011) gives no reason.
- **No source explicitly says that a Title Update or TU certification caused the delay.** The timing is circumstantial: TU1 targets the launch disc and was in the DB by Aug 2011. The usual pattern of a TU shipping alongside or before DLC to support new roster slots is plausible but unconfirmed.
- DLC content package IDs and file names were not found in any public source checked. Whether the DLC requires a TU is **unconfirmed**. The Canary report below shows the DLC installed against the base XEX and getting past an intro-time error popup/crash. This suggests DLC loading paths exist in the base executable.

## Xenia compatibility reports

**xenia-project/game-compatibility #1536** "54510866 - WWE All Stars" (open)
- Labels: `state-menus`, `apu-garbage`, `tech-middleware-havok`, `marketplace-nodownload`, `requires_disassemble_functions_true`.
- 2020-04-27 (Beasker, build f7ad6310): crashes on startup (`state-crash-host`).
- 2021-03-29 (Etokapa, build 99024ae): **requires `disassemble_functions=true`**. Menus work. Starting a new game gives an infinite load after entrances, and the audio loops during entrances.
- 2022-05-14 / 2024-05-19 (users): the game crashes every few minutes during matches.
- 2024-01-18: a user asks about "increase the amount of labels in finalization pass". This points to a JIT/backend label-limit issue in some function.

**xenia-canary/game-compatibility #975** "54510866 - WWE All-Stars"
- Label: `state-gameplay`. Originally filed as `state-intro`.
- 2025-11-19 (MoxLogan, Canary 1bf98e9, D3D12): after installing DLC, an error dialog appears on every start, then Xenia freezes and crashes. The log shows unimplemented `XeKeysConsolePrivateKeySign` and `IoDismountVolumeByFileHandle` and `Stub XFileFsDeviceInformation` during early save/storage probing.
- 2026-05-08 (has207, collaborator): "Gets into a match at least."

**xenia-canary/game-patches**: `patches/54510866 - WWE All Stars.patch.toml`
- "Skip Audio Loop Stuck Code" (author Gliniak, disabled by default): `be32` at `0x833A6C98` = `0x38600001` (`li r3,1`). This likely forces a wait/poll function to return success, working around the entrance audio-loop / infinite-load hang.

Implications for the recomp:
- There is an audio-sync hang at `0x833A6C98`, likely an XMA/APU completion poll.
- A function at some address needs special handling. Xenia reports a label overflow, and the game needs `disassemble_functions=true`.
- Havok middleware is used.
- Kernel exports to stub: `XeKeysConsolePrivateKeySign`, `IoDismountVolumeByFileHandle`.

## Sources
- https://xboxunity.net/Resources/Lib/TitleUpdateInfo.php?titleid=54510866 (TU JSON)
- https://xboxunity.net/Resources/Lib/TitleList.php?search=54510866 (title entry: 2 updates, 1 media ID)
- https://digiex.net/threads/wwe-all-stars-title-update-2.12761/
- https://github.com/xenia-project/game-compatibility/issues/1536
- https://github.com/xenia-canary/game-compatibility/issues/975
- https://github.com/xenia-canary/game-patches/blob/main/patches/54510866%20-%20WWE%20All%20Stars.patch.toml
- https://github.com/xenia-canary/game-patches/issues/66
- https://www.gamingnexus.com/News/22473/WWE-All-Stars-DLC-finally-hits-the-360
- https://www.gamingnexus.com/FullNews/First-WWE-All-Stars-DLC-launches/Item22224.aspx
- https://gamingnexus.com/News/23696/New-WWE-All-Stars-content-bodyslams-its-way-on-to-XBL-and-PSN
- https://n4g.com/news/748032/wwe-all-stars-xbox-360-dlc-delayed
- https://en.wikipedia.org/wiki/WWE_All_Stars
- https://marketplace.xbox.com/en-gb/Product/WWE-All-Stars/66acd000-77fe-1000-9115-d80254510866
- Not reachable: operationsports forum thread (403), wweallstars.fandom.com DLC page (402), pastapadre 2011-04-29 (TLS error)
