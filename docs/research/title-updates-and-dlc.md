# Title updates and DLC: running the latest version and installing DLC

Date 2026-10-10. Question: players keep asking about title updates (TUs) and
DLC. How can the recomp run the game at its latest version, and how can
players install DLC easily, ideally through the launcher? This is research
for a grilling session. It lists facts, options and open decisions, and
decides nothing. No code was changed, nothing was built, and no TU or DLC file
was downloaded or opened.

It builds on `docs/research/title-update.md` (TU list, DLC timeline, Xenia
compatibility reports; 2026-10-02), ADR-0009 (title update) and ADR-0010
(DLC). It does not repeat their tables.

Tags: **[P]** primary source (linked in Sources), **[S]** this repo, a sister
repo or the ReXGlue source (file:line), **[I]** my inference, **[U]**
unverified. Paths: `SDK:` is `.tools/rexglue-source` (`f5337cdc` plus
`patches/`). `SVR:` is `D:\Projects\SVR2011-RECOMP`. `SVRPC:` is
`github.com/KaikoClanworth1/wwe-svr2011-pc` at `de85161`, read with `gh api`.
Guest addresses are base XEX v5.

## Summary

- **Two TUs exist, both for our disc** (media `60CEF505`, base version 5), and
  no changelog has been found for either (`docs/research/title-update.md:8-18`)
  [S]. What they change is unknown. Nobody, us included, has opened one.
  Without a player-supplied TU file we cannot say whether the update is worth
  having at all.
- **ReXGlue already applies `.xexp` patches, both in codegen and at runtime.**
  `UserModule::LoadFromFile` looks for `default.xexp` next to `default.xex`
  and applies it (SDK:`src/system/user_module.cpp:105-126`) [S]. Codegen loads
  the XEX through the same path (SDK:`src/codegen/codegen_context.cpp:47-50`)
  [S]. A TU build is mechanically "put `default.xexp` beside `default.xex`
  and regenerate", if the prebuilt `rexglue.exe` behaves like the source
  [U].
- **A TU is a code change, so it means a separate build.** Recompiled code is
  fixed at build time. A TU build only runs correctly with the TU present, and
  the base build only runs correctly without it. Today nothing checks that
  the XEX loaded at runtime matches the one codegen saw
  (SDK:`include/rex/image_info.h:41-50`) [S]. A stray `default.xexp` in
  `gamedata` would patch data and headers under base-built code [I].
- **Moving to a TU is costly here.** About 150 hand-maintained guest
  addresses would have to be re-found: 122 manifest seeds, 30 mid-asm hooks
  (`wweallstars_manifest.toml:16-407`) and 63 unique addresses plus 80
  `sub_` references in `src/` [S]. ADR-0009 asked for byte signatures to make
  that move cheap, but the manifest still uses bare addresses [S].
- **DLC is mostly plumbing that ReXGlue already has.** `ContentManager::
  InstallContent` extracts an STFS package into
  `<user_data_root>/0000000000000000/54510866/00000002/<file>/` and writes the
  `.header` with the package's license bits
  (SDK:`src/system/xam/content_manager.cpp:572-644`) [S]. Nothing calls it
  yet. The sibling port `wwe-svr2011-pc` calls it at startup for packages the
  launcher has copied into a `DLC` folder, and forces a full license for
  marketplace content (SVRPC:`port/src/dlc.cpp:192-233`; SVRPC patch
  L3900-3901, L4030-4036) [S].
- **The game has its own DLC manager.** The image contains UE3
  `UOnlineSubsystemLive::execInitDLC`, a `GameDLCManager` that reads
  `InstalledPhases` and `DLCAvailableCharacters` from `Xe-Game.ini`, and a
  `POCMissingDLC` message (strings at `0x82130300`, `0x82197039`,
  `0x8222553A`) [S]. Disc config has `[DLCSettings] CheckNewDLCContent=0
  DataLoadDLCContent=0` in `Coalesced.ini` [S]. How these use the XContent
  APIs is not yet traced [U].
- **The game never imports `XamContentGetLicenseMask`.** It imports
  `XamContentCreateEx`, `XamContentCreateEnumerator`, `XamContentClose`,
  `XamContentDelete`, `XamContentGetCreator`, `XamContentGetDeviceData/State`,
  `XamShowMarketplaceUI`, `StfsCreateDevice/ControlDevice`,
  `XeKeysConsolePrivateKeySign` and `XeKeysConsoleSignatureVerification`
  (`generated/default/wweallstars_init.cpp`, import table around L109104) [S].
  Any license check therefore uses the mask that `XamContentCreateEx` returns
  [I].
- **Launcher precedent is simple and proven:** a "DLC" page that scans a
  folder (and archives), reads the STFS header (magic, content type at
  `0x344`, title ID at `0x360`, display name at `0x411`), copies matching
  packages, and lets the game unpack them at the next start
  (SVRPC:`port/launcher/svr2011_launcher.c:1991-2014`, L2065-2118) [S].
  SVR2011-Recomp has no TU or DLC support at all (see §B.5) [S].
- **Other recomps split two ways on TUs.** UnleashedRecomp recompiles the
  TU-patched XEX and requires players to supply the TU. Its installer
  re-applies `default.xexp` on the player's machine and hash-checks
  everything (`UnleashedRecomp/install/installer.cpp#L617-L632`, L70-135)
  [P]. Both ReXGlue WWE projects stay on the base XEX, and wwe-svr2011-pc
  tells players that TUs "are not needed and are skipped" [S]. Xenia Canary
  applies a TU at load time from `content/0000000000000000/<TID>/000B0000/`
  (`src/xenia/kernel/kernel_state.cc#L683-L840`) [P]. An emulator can do
  that; a static recomp cannot (§B).
- **Verification is cheap for DLC and expensive for TU.** DLC: the roster
  grows and the log shows the package opened with its license. TU: diffing
  the patched and base images tells us what changed before any codegen work.

## Findings from the real files (2026-10-10)

The owner supplied TU2 and the Million Dollar Pack. Both were read in a
scratch folder; neither enters `assets/` or Git. These facts replace the
matching [U] items below.

**TU2** (`54510866/000B0000/tu00000005_00000000`, 6,651,904 bytes = the 6496
KB xboxunity lists; SHA-1 `e190068dc5875d1e7ff62214a2444572c027c947`)
- Header: `LIVE`, type `0x000B0000`, title `54510866`, media `60CEF505`,
  display name "WWE All Stars Title Update #2".
- Contents (12 files + `PatchTOC.txt`): `default.xexp` (2,287,616 bytes),
  `Coalesced.ini` and the five localised `Coalesced.*`, and replacements for
  `Engine.xxx`, `Game.xxx`, `fe_templates.xxx`, `UI_LOADER.xxx`, `UI_ONLINE.xxx`,
  `UI_SHELL.xxx`. `Engine.xxx` and `Game.xxx` are the UnrealScript packages,
  so script code changes too.
- `default.xexp` delta descriptor: source version 5, target version `0x205`,
  `digest_source` `fd17f7d8…c7d9` = SHA-1 of our base XEX's RSA signature, so
  it targets exactly our disc. The image delta spans the whole image (source
  size `0x18C0000`) with 2.2 MB of LZX delta data. That much delta suggests
  wide code changes or a relink [I]; the function-level diff has not been run.
- Config (`Xe-Engine.ini`): `[DLCSettings] CheckNewDLCContent` and
  `DataLoadDLCContent` go from `0` (disc) to `1`. New `[DLCFrontEndPackages]
  package=Shared_Characters_DLC`. Every DLC wrestler's `AnimList_*`,
  `AnimSet_*_FIN`, chants and commentary packages are registered, and
  `AnimSet_Base_Template_DLC` and the DLC announcer packages are added.
- Config (`Xe-Game.ini`): `[AI.Variables.Character.*]` blocks for the DLC
  wrestlers.
- Text (`Coalesced.int`): "Before playing Xbox LIVE, you must update to the
  latest free Compatibility Pack", "This feature is disabled until you patch
  your WWE All Stars game", and Ted DiBiase renamed from MILLION DOLLAR MAN.

**Conclusion: DLC needs TU2.** The disc config switches DLC loading off and
TU2 switches it on, besides registering the packages DLC wrestlers use.

**TU2 code diff.** A scratch tool on the ReXGlue 0.10.0 SDK loaded
`default.xex` the way codegen does (`Runtime::LoadXexImage`, which applies a
sibling `default.xexp`, SDK:`src/system/user_module.cpp:105-126`) and dumped
both images. Scripts and dumps live in the session scratchpad, not the repo.
- Entry point moves `0x8241A8E0` → `0x8241A7E0`. `.text` grows by `0x3268`,
  `.rdata` by `0xDB0`, `.data` by `0xC0`, `.idata` by 8 bytes (one more import
  [I]). Section start addresses stay the same except `.pdata`, `BINKCONS` and
  `BINK`.
- Functions (from `.pdata`): 92,061 base, 92,171 TU2. Matched by code with
  branch and address immediates masked: 90,505, of which 90,492 moved. 486
  matched in place but changed, and about 1,600 per side are unmatched
  (changed length, new or removed). Roughly 2% of functions differ. The rest
  is a relink.
- Our hard-coded addresses (1,384 distinct `0x82…`/`0x83…`/`sub_` values in
  `wweallstars_manifest.toml` and `src/`): 666 sit in `.pdata` functions, and
  659 of those map through the function match. Of the 711 others, 691 map
  through data references learned from matched code (49,992 pairs) or a
  32-byte local match. 27 need a human: 7 code addresses (6 in
  `d3d_sweep.gen.cpp`, 1 seed at `0x82A5F000` in a changed function) and 20
  others, mostly manifest seeds. This mapping is heuristic and unverified
  until a TU2 codegen and a run confirm it.

**Million Dollar Pack** (`54510866/00000002/F5B3E0579C2BD2939FD88B0E0A936E8095E284D754`,
207,433,728 bytes)
- Header: `LIVE`, content type `0x00000002`, display name "WWE ALL STARS PACK 1".
- One folder `Phase1/` with 103 `.xxx` packages, 5 JumboTron `.bik` videos,
  `DLCTOC.txt`, `AI__Xe-Game.ini` and an `Xe-Game.ini` fragment:
  `[GameDLCManager] +InstalledPhases=1`, `[AvailableCharacters]
  CharacterData_110=1, _116=2, _117=4, _212=8, _219=16`. So "Pack 1" holds
  five wrestlers: Million Dollar Man (110), Honky Tonk Man (116), Dusty Rhodes
  (117), Ted DiBiase Jr (212) and Cody Rhodes (219). This confirms the ini
  fragment pattern guessed in C.2.

## A. Title updates

### A.1 Which TUs exist

- Title `54510866`, one media ID `60CEF505`, base version `00000005`. Our
  `default.xex` matches all three (`analysis/disc.json`, `executable`:
  `media_id 60CEF505`, `version 00000005`, `base_version 00000005`) [S]. The
  image is "WWE All Stars (USA, Europe)", so one disc and one media ID serve
  both regions [S]. No other regional media ID is listed on xboxunity [P].
- xboxunity lists TU1 (ID 21825, uploaded 2011-08-20) and TU2 (ID 23145,
  uploaded 2013-08-19). Both have base version 5 and `"Size":"6496"`, a
  figure with no stated unit (`title-update.md:10-13`; re-checked
  2026-10-10 by the TitleUpdateInfo query) [P]. What xboxunity's SHA-1 covers
  (the package or the `.xexp`) is **[U]**.
- digiex has a "TU #2" thread describing a 3.9 MB archive named
  `tu00000005_00000000` (`title-update.md:18`) [P/U].
- **What each TU changes: unknown.** No changelog has been found from THQ,
  the press or wikis (`title-update.md:19`). Canary's game patches target the
  base XEX, module hash `76C738F43A2E4F66` (:20). That suggests Canary users
  run the base game, not that the TU does nothing [I].

### A.2 How a TU ships

- A TU is an STFS package (usually `CON `/`LIVE`) with content type
  `0x000B0000`. Free60 calls the type "Installer", and Canary's content dialog
  calls it "Title Updates" (`xenia-canary` `src/xenia/xbox.h#L464`,
  `src/xenia/app/content_list_dialog.h#L90`) [P].
- Inside: `default.xexp`, an XEX2 file whose module flags set
  `PATCH_DELTA` (0x40), plus any replaced content files. UnleashedRecomp's TU,
  for example, also carries a `work/` folder (`UnleashedRecomp`
  `docs/DUMPING-en.md#L65-L72`) [P]. Whether ours holds anything beyond the
  `.xexp` is **[U]**. The digiex file name `tu00000005_00000000` fits the
  console cache name pattern [I].
- On a console the active TU sits in the HDD's `Content/Cache/TU_*` file
  (UnleashedRecomp dump guide, same lines) [P]. Some tools install it under
  `Content/0000000000000000/<TID>/000B0000/` instead, which is where Xenia
  Canary looks [P]. Which of the two our players will have is **[U]**.
- The `.xexp` header holds `XEX_HEADER_DELTA_PATCH_DESCRIPTOR` (`0x000005FF`)
  [P: Free60 XEX; xenia `src/xenia/kernel/util/xex2_info.h#L281`]. Its fields
  (xenia `xex2_info.h#L447-L460`) are: `target_version` (+0x04),
  `source_version` (+0x08), `digest_source` (+0x0C, SHA-1 of the base XEX's
  RSA signature), `image_key_source` (+0x20, the base session key encrypted
  with the new key), header-delta and image-delta offsets, then the LZX delta
  blocks (+0x4C) [P]. Free60 names the header but not its fields. The
  layout comes from Xenia's code [P].

### A.3 How Xenia applies it

- **Patch code.** `XexModule::ApplyPatch` (xenia master
  `src/xenia/cpu/xex_module.cc#L200-L466`; Canary `#L228`, same logic) [P]:
  1. SHA-1 of the base RSA signature vs. `digest_source`. A mismatch is only a
     warning (L211-222).
  2. Header bounds checks (errors 2-5, L224-248).
  3. Header LZX delta (L270-294).
  4. Key check. The decrypted `image_key_source` must equal the base session
     key, otherwise error 7 "image key doesn't match" (L330-353).
  5. AES-decrypt the patch body. For each block: SHA-1 check (error 9), then
     `lzxdelta_apply_patch` on the loaded image (L355-430). The log prints
     source and target versions (L452-458).

  ReXGlue carries the same code (B.2).
- **Where Xenia master looks.** `<module path> + "p"`, i.e. `default.xexp`
  beside `default.xex` in the game VFS, gated by `xex_apply_patches` (default
  true). A failed patch fails the load (xenia `src/xenia/kernel/user_module.cc#L23`,
  `#L104-L128`) [P].
- **Where Canary looks.** `Emulator::LaunchModule` calls
  `KernelState::ApplyTitleUpdate` (Canary `src/xenia/emulator.cc#L1619`;
  `src/xenia/kernel/kernel_state.cc#L683-L840`). `FindTitleUpdate` lists
  `content/0000000000000000/<TID>/000B0000/` and takes the first entry
  (L690, L735-744). It is not sorted by version: the agent read this from the
  code and did not test it [P/U]. It opens that entry as `UPDATE:` and patches
  with `UPDATE:<module path>p` (L746-787). A signature-digest mismatch skips
  the TU with a popup unless `allow_incompatible_title_update` is set. The
  cvars are `apply_title_update` (default true) and
  `allow_incompatible_title_update` (default false) (L33-36, L695-730) [P].
- **Installing it in Canary.** `Emulator::InstallContentPackage`
  (`emulator.cc#L916-L1000`) and `ContentManager::InstallContentPackage`
  (`src/xenia/kernel/xam/content_manager.cc#L678-L740`). A read-only STFS
  package (TU or DLC) is copied as one file, not extracted. The header goes to
  `Headers/<type>/<file>.header` (`emulator.cc#L849-L913`) [P].
- The reference emulator in `.tools/xenia-canary/` is a Canary binary, so this
  is the path our local baseline would use. Its content folder today holds
  only saves and the profile (`.tools/xenia-canary/content/E030000060FF33CA/54510866/`)
  [S].

## B. What a TU means for this static recomp

### B.1 The base XEX is what gets recompiled

- The manifest points codegen at `assets/default.xex`
  (`wweallstars_manifest.toml:6,9`), and its comment records the verified
  SHA-256 `521c2325…6892` (:15) [S].
- `build.ps1` refuses any other `default.xex`
  (`scripts/build.ps1:56-59`, `'Unsupported executable revision...'`) [S].
  It hashes only `default.xex`. A `default.xexp` beside it is neither
  checked nor rejected [S].
- The build runs the prebuilt SDK `rexglue.exe codegen
  wweallstars_manifest.toml` (`scripts/build.ps1:60`, :102) [S], not a binary
  built from `rexglue-source`.
- The launcher's setup checks the same SHA-256 and every disc file against
  the committed disc manifest (`launcher/src/Launcher.Core/GameSetup.cs:313-332`,
  `DiscManifest.cs:16-36`) [S]. A TU never touches disc files, so this check is
  unaffected [I].

### B.2 ReXGlue already has the XEX patch code

All of this is Xenia code carried into ReXGlue:

| What | Where | Behaviour |
|---|---|---|
| Patch lookup | SDK:`src/system/user_module.cpp:105-126` | After loading `X.xex`, resolves `X.xexp` on the same device. If present, loads it and calls `ApplyPatch`. A load or apply failure fails the whole module load (`X_STATUS_UNSUCCESSFUL`). |
| Patch apply | SDK:`src/system/xex_module.cpp:189-446` | Reads the delta patch descriptor (:197). Compares a SHA-1 of the base XEX RSA signature with `digest_source`, but a mismatch only logs a warning (:200-210). Patches the header with LZX delta (:273-275), checks keys (:327-334), then applies each LZX delta block to the loaded image (:383-413). |
| LZX delta | SDK:`src/system/lzx.cpp:150` (`lzxdelta_apply_patch`); struct `xex2_delta_patch` at SDK:`include/rex/system/util/xex2_info.h:445,453` | |
| `update:` device | SDK:`src/system/runtime.cpp:33` (cvar `update_data_root`), :325-337 | Mounts a host folder read-only as `\Device\Harddisk0\PartitionUpdate`, linked as `update:`. Empty by default. Nothing mounts an STFS TU package; it must be extracted first. |
| Stubs | SDK:`src/kernel/xam/xam_misc.cpp:756` (`XamUpdateGetTitleUpdateSizeThreshold`), SDK:`src/kernel/xboxkrnl/xboxkrnl_modules.cpp:232` (`XexPcToFileHeader`) | |

Two consequences:

1. **Codegen against a TU needs no new tooling.** `CodegenContext::Create`
   loads the XEX with `runtime.LoadXexImage("game:\\" + filename)`
   (SDK:`src/codegen/codegen_context.cpp:47-50`), which is the same
   `UserModule::LoadFromFile`. An `assets/default.xexp` would be applied
   before analysis. This assumes the prebuilt `rexglue.exe` matches the source
   [U].
2. **The runtime applies a patch too.** `ReXApp::ConstructRuntime` loads
   `game:\default.xex` (SDK:`src/ui/rex_app.cpp:267`, :291), so a
   `default.xexp` in `gamedata` is applied at every start. `PPCImageInfo` stores
   no image hash (SDK:`include/rex/image_info.h:41-50`), and nothing compares
   the runtime image with the codegen image [S]. Base-built code with a
   TU-patched data image, or TU-built code with a base data image, would run
   without warning. The result is undefined behaviour, from subtle data
   mismatches to crashes [I].

### B.3 What would change for a TU build

| Item | Today | Under a TU |
|---|---|---|
| SHA gate | `default.xex` must be `521c2325…` | Also gate `default.xexp` (or the patched image) on a known hash. Without the TU file, nobody can know that hash [I]. |
| Manifest seeds | 122 bare addresses in `[entrypoint.functions]` with evidence comments (`wweallstars_manifest.toml:16-163`) [S] | Every seed is re-checked. Where the TU moved code, seeds move [I]. |
| Mid-asm hooks | 30 `[[entrypoint.midasm_hook]]` blocks with absolute `address` and `jump_address_on_true` (`wweallstars_manifest.toml:165-407`; e.g. `WweHitPulse` 0x82AE392C → 0x82AE3958 at :165-169) [S] | Each is re-found. ADR-0009 required byte signatures for this ("Hook addresses must be recorded with byte signatures, not bare addresses", `docs/adr/0009-title-update.md`). None are recorded yet [S]. |
| Hand-written source | 74 guest-address literals (63 unique) in 14 files, plus 80 `sub_8xxxxxxx` references in 6 files. The largest are `src/d3d_census.cpp` (51 `sub_`), `src/game_tick_log.cpp` (16 literals + 10 `sub_`, e.g. `kCurrentTime = 0x8370BD20` at :47) and `src/vibration/enhanced_hooks.cpp` (13) [S] | Data addresses (globals in `.data`/`.bss`) move if the TU resizes sections. Code addresses move if the TU changes or relinks code [I]. |
| ReXGlue patches in `patches/` | No guest addresses [S] | Unchanged [I]. |
| Canary audio patch `0x833A6C98` | Not applied anywhere. It appears only in docs (`CLAUDE.md:52`, `docs/research/stuck-audio-loop.md`). The fix went into ReXGlue's `XmaContext::Work()` instead (`stuck-audio-loop.md`, "Answer") [S] | Nothing to move. If ever needed, its address is valid only for the base image [I]. |
| Native renderer / D3D9 function map | Matches statically linked XDK D3D9 functions by address (`d3d_census.cpp`) [S] | Likely unchanged if the TU only touches game code. A full relink would move all of it [I]. |
| Shader pack, disc manifest | Built from disc files [S] | Unchanged, unless the TU ships new content files [I]. |

How much moves depends on what the TU is. Xbox 360 delta patches are
LZX deltas over the header and the whole loaded image, so they can change
anything (xenia `xex_module.cc#L270-L430`) [P]. A 6,496-byte
TU (the xboxunity size, `title-update.md:12-13`) could only change a few
pages of code or data. A 3.9 MB package (the digiex report, :18) could relink
the whole executable. Both are **[U]** until someone has the file.

### B.4 Applying only the data parts without recompiling

- **Content files from the TU** (if the package holds any beyond `default.xexp`)
  could be offered through `update:` (`update_data_root`) or merged over
  `gamedata`. That only helps if the game actually reads `update:`. The image
  has no ASCII `update:` string (searched in `analysis/xex/default.pe`) [S],
  and whether UE3 on this title reads it is **[U]**.
- **Data changes inside the XEX** (tuning tables, strings, constants in
  `.rdata`/`.data`) are applied by the runtime when `default.xexp` is present
  (B.2). They are only safe if the TU leaves the code byte-identical, and every
  data address the hand-written code uses stays put. That can be checked by
  diffing the base and patched images (§E.1) before deciding [I].
- **Code changes** cannot be applied without codegen. Static recompilation
  turns each guest function into C++ at build time. Patching guest code bytes
  in memory at runtime does nothing, because the host never executes them [I].

### B.5 Precedents

- **SVR2011-Recomp:** no TU or DLC support. It targets the base disc XEX
  (version = base version = 1), gated by SHA-256
  (SVR:`scripts/build.ps1:14-16`, SVR:`analysis/disc.json:80-83`). The only
  STFS code reads `nxeart` for key art (SVR:`launcher/portable/KeyArt.cs:1-56`) [S].
- **wwe-svr2011-pc:** builds on the base XEX too. Its launcher sorts out TU
  packages and skips them, telling the player "Title updates are not needed
  and are skipped" (SVRPC:`port/launcher/svr2011_launcher.c:1890-1891`,
  L2091-2095, L2154) [S].
- **UnleashedRecomp / MarathonRecomp / XenonRecomp:** UnleashedRecomp **recompiles the TU-patched XEX**:
  - `SWA.toml` sets `patch_file_path = "../private/default.xexp"` and
    `patched_file_path = "../private/default_patched.xex"`
    (`UnleashedRecompLib/config/SWA.toml#L1-L5`).
  - XenonRecomp applies the patch once and recompiles the result
    (`XenonRecomp/recompiler.cpp#L17-L45`). Its patcher,
    `XenonUtils/xex_patcher.cpp#L225-L533`, is a port of Canary's code. It
    checks the image key but not `digest_source`.
  - The developer must obtain `default.xexp` "via the title update package"
    (`docs/BUILDING.md#L15-L20`).
  - Players need it too. The README says "Title Update required"
    (`README.md#L54-L57`).
  - The installer applies the same patch on the player's machine
    (`install/installer.cpp#L617-L632`), so the runtime data image matches
    the recompiled code.
  - The installer checks every file against XXH3-64 lists, including 4
    accepted `default.xexp` hashes (`install/hashes/*.cpp`,
    `installer.cpp#L70-L135`). A `skipData` dry run confirms that the game
    and the update belong together (`#L704-L732`).
  - TU data files under `game:\work\` are redirected to an `update` folder
    (`kernel/io/file_system.cpp#L388-L395`) [P].

  MarathonRecomp has no title update (`Marathon.toml` has no
  `patch_file_path`) [P]. XenonRecomp's README says `patch_file_path` is "not
  required if the game has no title updates" (L125-126) [P].

  So on the XenonRecomp side the precedent for a TU is "TU required, one
  build". The ReXGlue projects (SVR2011-Recomp, wwe-svr2011-pc) stay on the
  base XEX [S].

### B.6 Shipping a build made from TU code

- The release already ships a binary made from recompiled base-XEX code, and
  players supply the disc (ADR-0010 wording, launcher setup) [S]. A TU build
  is the same kind of derivative of Microsoft-distributed, publisher-owned
  code. The TU itself is not on the disc. It came from Xbox Live, so players
  must have dumped it from a console or obtained it elsewhere [I].
- Practical consequence: a TU-only build locks out every player who has only
  the disc. Supporting both means two codegen outputs, two executables (or one
  executable with two generated code sets), two sets of seeds and hooks, and
  twice the testing [I].
- `docs/research/title-update.md` already notes that TUs are copyrighted and
  cannot be redistributed (ADR-0009 "Context") [S]. Not legal advice. The
  owner's risk appetite decides this, not this document.

## C. DLC

### C.1 What exists

The pack list and dates are in `docs/research/title-update.md` ("DLC timeline")
[S]. In short: Million Dollar Pack (Ted DiBiase, Ted DiBiase Jr.), American
Dream Pack (Dusty and Cody Rhodes), Honky Tonk Man, R-Truth, All-Time Greats
(Jericho, Road Warriors, Lawler) and Southern Charisma (Big Boss Man, Michael
Hayes, Mark Henry).

What is on the disc already:

- **Character data and one attire package for the first wave**: CHR110
  Million Dollar Man, CHR212 DiBiase Jr, CHR117 Dusty Rhodes and CHR219 Cody
  Rhodes have `CharacterData` and one attire on disc, but no `LVSCHR_*` entry
  and no `HUDPortraits` package (`docs/research/base-roster.md:123-128`) [S].
  So the first-wave packs might be partly unlock-style: the pack ships the
  missing UI and select-screen entries, and the disc has the models [I/U].
- Later DLC (Honky Tonk Man, Jericho, Hawk, Animal, Lawler, Hayes, Mark Henry,
  R-Truth and others) are data stubs with no package on disc
  (`base-roster.md:130`) [S]. Those packs must ship the full assets [I].
- `Game/CookedXenon/Scripts_DLC.xxx` and `Triggers_DLC.xxx` (32 KB each) are on
  disc and listed in `[Engine.GlobalCharacterPackages]`
  (`launcher/disc-manifest.tsv:5323`, :5398; `Coalesced.ini`) [S].
- Disc config, in the decoded `Game/Config/Xenon/Cooked/Coalesced.ini`:
  `[DLCSettings] CheckNewDLCContent=0`, `DataLoadDLCContent=0`;
  `[Game.RunbotManager] RunbotGameMode=(index=7,display=Downloadable
  Content,value=DownloadableContent,...)`; `[Engine.DebugTextLog]
  TTY_DLC=Off` [S]. What the two `DLCSettings` flags do is **[U]**.
- Content IDs and file names of the packs: none found in public sources
  (`title-update.md:37`) [S]. A 2026-10-10 search (xboxunity, Microsoft's store catalog, the old
  marketplace URL) found none either. Pack contents and prices also disagree
  between sources. For example, Wikipedia leaves Big Boss Man out of Southern
  Charisma [P/U].

### C.2 How the game looks for DLC

From the image, with no tracing done yet:

- UE3 Live subsystem natives: `UOnlineSubsystemLive::execInitDLC` (UTF-16 at
  `0x821303EE`), `OnReadContentComplete`, `OnQueryAvailableDownloadsComplete`,
  `execCanPurchaseContent`, `execCanDownloadUserContent` (around
  `0x82130300`-`0x82131A75`) [S].
- A game-side `GameDLCManager` (UTF-16 `0x821970B8`) reading `InstalledPhases`
  (`0x82197018`) and `DLCAvailableCharacters` (`0x82197038`) from `Xe-Game.ini`
  (`0x82197068`). Code references: `0x829E8D24`-`0x829E92A4` and
  `0x82BF8808`-`0x82BF8838` (`lis`/`addi` pairs found by a scan of `.text`) [S].
  This reads like UE3's usual DLC pattern: each pack carries an ini fragment
  that is merged into `Xe-Game.ini`, listing its phase and characters [I/U].
- A UTF-16 `DLC` string at `0x82136F18`, next to `cache:\CAP_SAVE`, referenced
  from `0x8273FE14`, `0x82762D68`, `0x82765D18`, `0x82BDD7C4` and `0x82BDE618`
  [S]. It might be the XContent root name the packs are mounted under (`DLC:`)
  [U].
- UI strings: `AWTUI_DLC` (`0x821F0600`, the WWE-logo roster slot from
  `base-roster.md:34-37`), `OpenOSDLCStore` (`0x82220230`), `POCMissingDLC`
  (`0x82225530`, referenced at `0x82B98E1C`) [S].
- Content imports (`generated/default/wweallstars_init.cpp:109086-109152`):
  `XamShowMarketplaceUI`, `XamContentCreateEx`, `XamContentDelete`,
  `XamContentClose`, `XamContentGetCreator`, `XamContentCreateEnumerator`,
  `XamContentGetDeviceState`, `XamContentGetDeviceData`, `StfsControlDevice`,
  `StfsCreateDevice`, `XeKeysConsoleSignatureVerification`,
  `XeKeysConsolePrivateKeySign` [S]. **No `XamContentGetLicenseMask`.**
- One direct call to each of the `XamContentCreateEx` and
  `XamContentCreateEnumerator` thunks, at `0x82D9C7DC` and `0x82D9C818`. They
  sit in what looks like the XDK `XContentCreateEx` / `XContentCreateEnumerator`
  wrappers, which every caller goes through [S/I].
- In the Xenia baseline run without DLC, the game called
  `XamContentCreateEnumerator` 5 times and `XamContentCreateEx` twice (saves)
  (`docs/research/xenia-baseline.md:98`;
  `analysis/xenia-baseline/kernel_calls_run2.txt:48-63`) [S]. Which content
  types the 5 enumerations ask for was not logged [U].

### C.3 How Xbox 360 stores DLC

- DLC is an STFS package with `LIVE` (Microsoft-signed, Live downloads) or
  `PIRS` magic; `CON ` is console-signed (saves, local) [P: Free60 STFS, as summarised; U].
  ReXGlue accepts all three (`XContentPackageType` kCon/kPirs/kLive,
  SDK:`include/rex/filesystem/devices/stfs_xbox.h:71-73`, :464-467) [S].
- Header layout from ReXGlue's structs (SDK:`stfs_xbox.h:237-242`, :273-305,
  :457-469). Xenia Canary's `xcontent.h` gives the same offsets [P]:

  | Offset | Field |
  |---|---|
  | 0x000 | magic (`CON `/`LIVE`/`PIRS`) |
  | 0x004 | signature (0x228) |
  | 0x22C | 16 license entries × 0x10 (`licensee_id` u64, `license_bits` u32, `license_flags` u32) |
  | 0x32C | content ID (SHA-1, 0x14) |
  | 0x344 | content type (u32 BE). `0x00000002` marketplace, `0x000B0000` installer/TU (SDK:`include/rex/system/xcontent.h:40-63`) |
  | 0x348 | metadata version |
  | 0x34C | content size (u64) |
  | 0x354 | execution info: media ID, version, base version, title ID (0x360) … |
  | 0x411 | display name, UTF-16BE, 9 × 128 chars |
  | 0x1712 / 0x1716 | thumbnail / title thumbnail sizes (`TitleTile.cs` uses 0x1716) |

- On the console, marketplace content sits under
  `Content/0000000000000000/<TitleID>/00000002/<package>`, shared by all
  profiles [I; Xenia Canary uses the same layout, `content_manager.h#L42`, P]. ReXGlue copies that: marketplace content forces XUID 0
  (SDK:`src/system/xam/content_manager.cpp:102-115`) [S].

### C.4 ReXGlue's content implementation

- **Root:** `ContentManager` is built on `user_data_root` itself
  (SDK:`src/system/kernel_state.cpp:71-75`). Path =
  `<user_data_root>/<XUID>/<TitleID>/<ContentType>/<file_name>/`
  (SDK:`content_manager.cpp:88-115`) [S]. For DLC that is
  `userdata/0000000000000000/54510866/00000002/<file>/`. Our saves already sit
  at `userdata/B13EBABEBABEBABE/54510866/00000001/` (seen in
  `out/handcheck/userdata`) [S].
- **Packages are host folders, not STFS mounts.** `ContentPackage` registers a
  `HostPathDevice` on `\Device\Content\N\` (SDK:`content_manager.cpp:41-54`)
  [S]. An STFS reader exists (`StfsContainerDevice`), but the only callers
  are `InstallContent` and `vfs_dump.cpp:43` [S].
- **`InstallContent(path)`** (SDK:`content_manager.cpp:572-644`) mounts the
  STFS, extracts every entry to the marketplace path named after the package
  file, takes the English display name, ORs `license_bits` of every license
  entry with non-zero flags, and writes `Headers/00000002/<file>.header` [S].
  It does **not** check the header's content type or title ID: it forces type
  2 and the running title [S]. Nothing in ReXGlue or this repo calls it [S].
- **Header file:** `<root>/<xuid>/<title>/Headers/<type>/<file>.header` = the
  0x148-byte `XCONTENT_AGGREGATE_DATA`, optionally followed by a 4-byte license
  mask (SDK:`content_manager.cpp:117-137`, :191-256; reader
  `LoadPackageLicenseMask` :62-81) [S].
- **Enumerate:** `XamContentCreateEnumerator` lists HDD content for the
  signed-in XUID and again for XUID 0. Disc content is a TODO
  (SDK:`src/kernel/xam/xam_content.cpp:61-114`). A folder with no `.header`
  is still listed, with its folder name as display name
  (SDK:`content_manager.cpp:152-171`) [S].
- **Open:** `XamContentCreateEx` with an open disposition returns the license
  mask from the `.header` into `license_mask_ptr`, which is 0 if no mask was
  written (SDK:`xam_content.cpp:209-221`; `content_manager.cpp:290-305`) [S].
- **License mask export:** `XamContentGetLicenseMask` returns the cvar
  `license_mask` (default 0), not a per-package value
  (SDK:`xam_content.cpp:24`, :32-45) [S]. The game does not import it (C.2).
- **Other stubs** (SDK:`xam_content.cpp:434-468`): `XamContentMountPackage`,
  `XamContentInstall`, `XamContentGetAttributes` and others. Also
  `XamContentOpenFile` returns not-found (:264-269), and `XamContentResolve` is
  unimplemented (:47-57) [S].
- **Keys:** `XeKeysConsolePrivateKeySign` returns success without writing a
  signature (SDK:`src/kernel/xboxkrnl/xboxkrnl_crypt.cpp:641-644`).
  `IoDismountVolumeByFileHandle` warns and returns success
  (SDK:`xboxkrnl_io.cpp:740-743`) [S]. ADR-0010 named both as risks. Both are
  already non-fatal [S].
- **Device data:** `XamContentGetDeviceData` reports a dummy 20 GB HDD with 3
  GB free (SDK:`src/kernel/xam/xam_content_device.cpp:39-50`, :111-124) [S].
- **Marketplace UI:** `XamShowMarketplaceUI` and the purchase UIs are stubs
  (SDK:`src/kernel/xam/xam_ui.cpp:631-636`) [S]. The in-game store button
  (`OpenOSDLCStore`) would get a stub result [I].

### C.5 What this repo does today

Nothing for DLC or TUs. There is no reference in `src/`, `launcher/` or
`patches/` (the only "DLC" hits are the two disc files above). The launcher
passes only `--game_data_root`, `--user_data_root` and `--cache_root`
(`launcher/src/Launcher.Core/LaunchArguments.cs:15-17`). `update_data_root`
and `license_mask` are never set [S]. The launcher spec puts DLC/TU install
out of scope, as "a follow-up spec once M4's STFS work lands; the launcher's
setup pipeline is built so a step can be added"
(`.scratch/launcher/spec.md:391-393`) [S]. The launcher already reads STFS
headers (`TitleTile.cs:15-47`, magic check :76-77) and extracts a file from a
read-only STFS package (`KeyArt.cs:46-84`) [S].

### C.6 License and signature checks

- **Signatures:** ReXGlue never verifies STFS signatures. The game imports
  `XeKeysConsoleSignatureVerification` and `StfsCreateDevice`, but they were not
  called in the Xenia baseline (`xenia-baseline.md:74`) [S]. Whether the DLC
  path calls them is **[U]**.
- **License mask:** the game's only source is the mask `XamContentCreateEx`
  returns (C.2). With `InstallContent`, the mask is whatever license bits the
  dumped package carries. Packages bought on another account or console may
  carry bits that a different title license check rejects [I]. wwe-svr2011-pc
  hit exactly this for SvR 2011 and forced `0xFFFFFFFF` for marketplace
  content behind a `dlc_full_license` cvar, default on: "packages copied from a
  console carry that account's offers — e.g. SvR 2011's Fan Axxess key without
  its pre-order Bret Hart bit" (SVRPC patch L3900-3901, L4030-4038) [S].
  Whether WWE All Stars tests license bits at all is **[U]**.
- **Xenia report:** a Canary user with DLC installed got an error dialog on
  every start, then a freeze. The log showed `XeKeysConsolePrivateKeySign`
  and `IoDismountVolumeByFileHandle` (`title-update.md:50`) [S]. The cause was
  never pinned down. In ReXGlue both calls already return success (C.4) [S].
  The dialog could equally be a license, corrupt-content or missing-TU check
  [U].
- **Does DLC need a TU?** Unknown (`title-update.md:37`) [S]. The Canary report
  ran DLC on the base XEX and got past the intro after the dialog [S]. If the
  DLC needs TU code (for example, more roster slots), DLC support is blocked
  on the TU decision. This is the most important open fact [I].

## D. Easy install: launcher UX

### D.1 How others do it

| Project | Input | Validation | Where it goes | Shows |
|---|---|---|---|---|
| wwe-svr2011-pc (same SDK, same genre) | A "DLC" tab: pick a folder (default `UpdatesAndDLC` beside the game). It scans recursively and unpacks `.zip` (built-in) or `.rar`/`.7z` (Windows `tar.exe`) archives to a temp folder first (SVRPC:`port/launcher/svr2011_launcher.c:1880-1891`, :1938-1954, :2065-2140) [S] | Magic `LIVE`/`PIRS`/`CON `, title ID at `0x360`, content type at `0x344`; reads the display name at `0x411` (:1991-2014). TUs are counted and skipped (:2091-2095) [S] | Copies packages to `<game>\DLC`. At the next game start, `InstallDlc` calls `ContentManager::InstallContent` for each package not yet unpacked, skips other titles, and removes a half-unpacked folder so it is retried (SVRPC:`port/src/dlc.cpp:192-233`) [S] | Names of copied packages, counts of copied, present, skipped TUs and failed (:2154) [S] |
| Xenia Manager | An install dialog. It always installs under `<Xenia content>/0000000000000000`; a TODO in the code notes that profile XUIDs are not handled (`source/XeniaManager/ViewModels/Controls/InstallContentDialogViewModel.cs#L143-L175`) [P] | Its own STFS parser: content ID at 0x32C, a 0x344-byte fixed header, metadata up to 0x971A (`source/XeniaManager.Files/StfsFile.cs` around L385-440) [P] | Two modes. "PackageFile" copies the file to `<TID>/<Type>/<file>` (`XeniaManager.Core/Manage/ContentPackageManager.cs#L17-L41`). "Extract" writes `<TID>/<Type>/<name>/` plus `Headers/<Type>/<name>.header` (`StfsFile.cs#L1003-L1036`) [P] | TUs and DLC go through the same dialog [P] |
| UnleashedRecomp installer | "Add Files" takes CON/LIVE/PIRS packages, an `.iso` or a folder (`install/installer.cpp#L50-L68`; `README.md` L77) [P] | XXH3-64 hash lists for every file. The DLC type comes from `<Type>` in the pack's `DLC.xml` (`installer.cpp#L220-L272`). MarathonRecomp reads the display name at 0x412 (`MarathonRecomp/install/installer.cpp#L216-L258`) [P] | Extracts to `game/`, `update/` and `dlc/<Pack name>/`, then writes `patched/default.xex` (`installer.cpp#L18-L33`, L568-654). The runtime registers each `dlc/*` folder as content (`UnleashedRecomp/main.cpp#L70-L118`) [P] | Wizard pages: game+TU, DLC, space check, install. It can be reopened from the title screen to add DLC (`docs/research/launcher.md:290`) [S] |
| Xenia Canary itself | An install-content command in the UI (menu name [U]; `Emulator::InstallContentPackage`, `emulator.cc#L916-L1000`) [P] | `IsValidPackage()` while listing content (`content_manager.cc#L281-L382`) [P] | A read-only STFS package is copied as one file. Writable packages are extracted, with a `.header` (`content_manager.cc#L678-L740`) [P] | The content dialog lists content by type, including "Title Updates" [P] |

### D.2 Building blocks we already have

- STFS header reading in the launcher (`TitleTile.cs`, `KeyArt.cs`), with
  xUnit tests over synthetic packages (`launcher/tests/Launcher.Tests/TestNxeart.cs`,
  `TitleTileTests.cs`) [S].
- A setup pipeline designed as a state machine that "is built so a step can
  be added" (`.scratch/launcher/spec.md:205-222`, :391-393) [S].
- ReXGlue's `InstallContent` (C.4), which does the unpack and header write
  [S].
- The portable layout: `userdata` holds the content tree already
  (`PortableLayout.cs:26`; saves at `userdata/B13EBABEBABEBABE/54510866/`) [S].

### D.3 Design axes (each one is an open decision)

1. **Who unpacks.** (a) The launcher copies packages and the game unpacks
   them at startup through `InstallContent` (wwe-svr2011-pc). (b) The
   launcher unpacks them itself (a C# STFS reader, extending `KeyArt.cs` into a
   real hash-table walker) and writes the `.header`. (c) The game mounts STFS
   packages directly, with no extraction (needs a ReXGlue patch so
   `ContentPackage` can use `StfsContainerDevice`) [I].
2. **Input.** Single files, a folder scan, drag-and-drop onto the window,
   archives (zip/rar/7z), or a USB/console dump layout
   (`Content/0000000000000000/54510866/00000002/…`) [I].
3. **Validation level.** Magic + title ID + content type only (wwe-svr2011-pc),
   or also verify the STFS hash tables (catches truncated downloads), or
   match a list of known content IDs / SHA-1s per pack, if we ever learn them
   (C.1) [I].
4. **Known-pack list.** Showing "4 of 6 packs installed: missing Southern
   Charisma" needs content IDs or display names of the six packs. We do not
   have them yet [U].
5. **Uninstall.** Delete `00000002/<file>/` and its `.header` [I].
6. **License.** Write `0xFFFFFFFF` into the `.header` at install time
   (launcher-side), or patch `OpenContent` behind a cvar (wwe-svr2011-pc), or
   keep the package's own bits [I].
7. **TU handling in the same page.** Skip with a message
   (wwe-svr2011-pc), accept and stage for a TU build, or reject [I].
8. **Where the UI lives.** A launcher page (Avalonia, ADR-0018), a step in
   first-run setup ("Add DLC? You can do this later"), or both [I].

### D.4 Legality boundary

- We never ship, download or link to DLC or TU files. Players bring packages
  dumped from their own console or account. This is the line ADR-0009 and
  ADR-0010 already draw ("Nothing is bundled", `docs/adr/0010-dlc.md`) [S].
- Forcing a full license (axis 6) unlocks content items the dumped package
  lists, even if the dumping account lacked an entitlement bit (the SvR 2011
  Bret Hart case, C.6) [S]. Whether to do that is an owner decision [I].
- The launcher should not fetch hashes or metadata from xboxunity or similar
  at runtime. Hashes we commit are facts about files, not the files [I].

## E. Verification ideas (nothing run)

### E.1 Title update

1. **Header first.** Parse the player-supplied TU package: magic, content type
   `0x000B0000`, title `54510866`, media `60CEF505`, version/base version
   (header 0x354-0x363). Then the `.xexp` delta descriptor: `digest_source`
   must equal the SHA-1 of our base XEX RSA signature, the check
   `ApplyPatch` only warns about (SDK:`xex_module.cpp:200-210`) [I].
2. **Image diff before any codegen.** Produce the patched image (run the
   ReXGlue loader in a tool, or Xenia's) and diff it against
   `analysis/xex/default.pe` by section. Code-only changes, data-only
   changes and a relink each lead to a different option below [I].
3. **Function-level diff.** If code changed: list functions whose bytes differ
   (the codegen function list is `analysis/xex/functions.txt`). Map every
   manifest seed, hook and `src/` address through the diff [I].
4. **Behaviour.** Xenia Canary with the TU vs. without
   (`.tools/xenia-canary/`, portable; installed via its content installer).
   Compare the title screen version string, if any [U], and replay the same
   input script (`docs/research/scripted-input.md`) [I].
5. **Runtime guard.** Log the SHA-256 of the image the runtime actually loads
   (and whether a `.xexp` was applied) next to a hash baked in at codegen,
   and stop on mismatch [I].

### E.2 DLC

1. **Package level.** A unit test in `Launcher.Tests` over a synthetic `LIVE`
   package (the `TestNxeart` pattern) for header parsing, title and type
   rejection, and the install layout [I].
2. **Runtime log.** Log every `XamContentCreateEnumerator` call (device, content
   type, count) and every `XamContentCreateEx` (root name, file name, license
   returned). wwe-svr2011-pc logs exactly this line (SVRPC patch L4037-4038)
   [S].
3. **Game-level.** The roster's `DLC` slot (`base-roster.md:34-37`) fills in.
   A DLC wrestler is selectable in Exhibition and loads into a match
   (headless harness plus scripted input, per the run-owner-checks practice).
   Check the `POCMissingDLC` path does not fire [I].
4. **Xenia parity.** The same package in Xenia Canary as a reference for what
   the game shows (with the caveat of the Canary report's error dialog) [I].
5. **Saves.** A save made with a DLC wrestler loads with and without the pack
   (the game's missing-content path) [I].

## Options

### TU support

| # | Option | What it takes | For | Against |
|---|---|---|---|---|
| T0 | **Stay on base v5; skip TUs** (wwe-svr2011-pc does this) | A launcher message: "title updates are not needed and are skipped". Possibly a runtime guard against a stray `default.xexp` | Zero cost; every disc owner can play; all 150+ addresses stay valid | "Latest version" is not met. ADR-0009's target is unmet. Any TU fixes are lost (contents unknown) |
| T1 | **Investigate first** | One player-supplied TU2 → header + image diff (E.1 steps 1-3) | Cheap. Turns every unknown here into a fact. Decides between T0/T2/T3/T4 with evidence | Needs someone to supply the file. We cannot get it ourselves without a console dump |
| T2 | **Rebase onto TU2** (ADR-0009 as written) | `default.xexp` in `assets`, codegen, re-find all seeds/hooks/addresses, new SHA gates, launcher accepts and installs the TU (required) | One build, latest version | Players with only the disc cannot play. Large one-time port. Each rebase repeats it unless addresses become signatures |
| T3 | **Two builds, base and TU** | Two codegen outputs and manifests, or one exe with two generated code sets chosen at startup by image hash | Everyone can play; TU owners get the update | Doubles test matrix, binary size or release artefacts. The hand-written hooks need per-build addresses |
| T4 | **Data-only TU** | Only if E.1 shows the TU leaves code byte-identical: let the runtime apply `default.xexp` (it already does) behind a hash check, plus `update:` for any content files | No codegen. One build | Only valid for a very specific kind of TU; needs proof per address |
| T5 | **Port selected TU fixes as hooks** | Once the diff is known, re-implement the TU's changes as mid-asm hooks or host patches on the base build | Disc-only players get the fixes. No rebase | Hand work per change. Only fits a small TU |

### DLC install

| # | Option | For | Against |
|---|---|---|---|
| D1 | **Launcher copies, game unpacks at start** with `InstallContent` (wwe-svr2011-pc) | Smallest code; proven in a sister port on the same SDK; the launcher stays simple | The first game start after an install is slower. Unpack errors appear in the game log, not the launcher. Packages are stored twice (copy + extracted) unless deleted after |
| D2 | **Launcher unpacks** (C# STFS reader + `.header` writer) | Immediate feedback, progress bar and errors in the launcher; one copy on disk; testable in xUnit | A full STFS reader in C# (hash tables, multi-level blocks, fragmented files). `KeyArt.cs` only handles the simple case. Duplicates ReXGlue logic |
| D3 | **Launcher calls the game exe in a headless install mode** (like the existing pipeline-prep run, `LaunchArguments.cs:35`) | Reuses `InstallContent`; feedback in the launcher | A new game-exe mode; progress reporting across processes |
| D4 | **Mount STFS directly in the runtime** (no extraction) | No duplicate data; packages stay intact | ReXGlue patch to `ContentPackage`/enumeration; more SDK diff to carry |
| D5 | **Manual folder drop only** (document the `userdata/0000000000000000/54510866/00000002/` path) | No code | Not "easy". Players with LIVE packages must extract them themselves |

Orthogonal choices for any D option: the license policy (package bits /
forced full license / cvar), archive support, the known-pack list, and
whether DLC install is offered in first-run setup.

## Risks

- **DLC depends on the TU** [U]. If the packs need TU code, D-options alone
  do not deliver DLC, and the TU decision comes first.
- **Silent code/data mismatch.** A `default.xexp` in `gamedata` is applied by
  the runtime today with no check (B.2). A player following Xenia guides could
  create this state by hand [I].
- **License bits.** Packages from another account may lack a bit the game
  checks. Forcing a full license fixes that but unlocks items the player may
  not have bought (C.6) [I].
- **The game's DLC error path.** The Canary report's start-up error dialog with
  DLC is unexplained (C.6) [U].
- **Address drift.** ADR-0009's byte-signature requirement was never applied;
  every new hook adds rebase cost (B.3) [S].
- **Prebuilt `rexglue.exe` vs. source.** Codegen behaviour with a `.xexp` is
  inferred from source, not tested on the binary the build runs [U].
- **No files in hand.** Every TU claim and most DLC claims rest on metadata
  and code reading. One dumped TU2 and one dumped pack would settle most of
  them [I].
- **Save compatibility.** A TU build might change save formats, or saves made
  with DLC might not load without it [U].

## Open questions for the owner

1. Does "latest version" mean TU2 is a hard requirement for v1.0, or is base
   v5 acceptable if the TU turns out to be minor? (ADR-0009 says rebase once
   TU2 can be verified.)
2. Can you, or a trusted tester, supply TU2 (and TU1) dumped from a console, so
   T1 can run? Who supplies DLC packs for testing, and which ones?
3. If TU2 changes code: one TU-only build (T2), two builds (T3), or port the
   fixes as hooks (T5)? Is locking out disc-only players acceptable?
4. Should the runtime refuse to start when it finds a `default.xexp` that does
   not match the build (B.2), or ignore the file?
5. DLC unpack: launcher (D2), game at start (D1), headless game mode (D3) or
   direct STFS mount (D4)?
6. License policy: keep the package's license bits, or force a full license for
   marketplace content as wwe-svr2011-pc does? Behind a setting?
7. Which inputs must the launcher accept: single files, folders, drag-and-drop,
   zip/rar/7z, a USB `Content` tree?
8. Should the launcher show a known-pack checklist (needs pack IDs we do not
   have yet), or just list whatever is installed?
9. Should DLC install be a first-run setup step, a separate page, or both? Does
   it get uninstall?
10. Where should copied packages live (`userdata`, beside the launcher, or not
    kept after unpack)?
11. How should the launcher react to a TU package dropped into the DLC flow:
    skip with a message, stage it, or reject it?
12. Is ADR-0009's byte-signature requirement still wanted? If yes, should it
    be done before any TU work, to cut rebase cost?
13. ADR-0010 says DLC is required in v1.0. Does that still hold now that 0.9.0
    shipped without it, and does it become a 1.0 blocker if DLC needs the TU?

## Sources

Primary, read 2026-10-10 (the permalinks pin the commit read):

- Xenia master `95a5c3ee`: https://github.com/xenia-project/xenia/blob/95a5c3ee250f80c3b9d139658649d9ffb6db3eec/src/xenia/cpu/xex_module.cc ,
  `.../src/xenia/kernel/user_module.cc`, `.../src/xenia/kernel/util/xex2_info.h`,
  `.../src/xenia/kernel/xam/xam_content.cc`
- Xenia Canary `c3ec0ba2`: https://github.com/xenia-canary/xenia-canary/blob/c3ec0ba255e243c93b44ffd75e2f130957b20e22/src/xenia/emulator.cc ,
  `.../src/xenia/kernel/kernel_state.cc`, `.../src/xenia/kernel/xam/content_manager.cc`,
  `.../src/xenia/kernel/xam/xam_content.cc`, `.../src/xenia/kernel/xam/xcontent/xcontent.h`,
  `.../src/xenia/xbox.h`, `.../src/xenia/app/content_list_dialog.h`
- UnleashedRecomp `cf829a9e`: https://github.com/hedge-dev/UnleashedRecomp/blob/cf829a9eca8fb680fba4b0409ddeb6ca92f22e3c/UnleashedRecomp/install/installer.cpp ,
  `.../UnleashedRecompLib/config/SWA.toml`, `.../README.md`, `.../docs/BUILDING.md`,
  `.../docs/DUMPING-en.md`, `.../UnleashedRecomp/main.cpp`, `.../UnleashedRecomp/kernel/xam.cpp`,
  `.../UnleashedRecomp/kernel/io/file_system.cpp`
- XenonRecomp `ddd128bc`: https://github.com/hedge-dev/XenonRecomp/blob/ddd128bcca99fe8bfbb99bea583c972351fa6ace/XenonRecomp/recompiler.cpp ,
  `.../XenonUtils/xex_patcher.cpp`, `.../README.md`
- MarathonRecomp `4595cdd5`: https://github.com/sonicnext-dev/MarathonRecomp/blob/4595cdd5763218eb2956b0e160da81c3eb9795cf/MarathonRecomp/install/installer.cpp
- Xenia Manager `a905dfe9`: https://github.com/xenia-manager/xenia-manager/blob/a905dfe9bf0a1e2088f8ebcb660d2054e4d1602d/source/XeniaManager.Files/StfsFile.cs ,
  `.../source/XeniaManager.Core/Manage/ContentPackageManager.cs`,
  `.../source/XeniaManager/ViewModels/Controls/InstallContentDialogViewModel.cs`
- wwe-svr2011-pc `de85161`, read with `gh api`: `port/src/dlc.cpp`,
  `port/src/dlc.h`, `port/launcher/svr2011_launcher.c`,
  `port/patches/rexglue-sdk-svr2011.patch`
- Free60 STFS: https://free60.org/System-Software/Formats/STFS/ . The page's
  display-name encoding came to us as a summary ("UTF-8, 0x80 per locale"),
  and the page itself was not read. Xenia, ReXGlue (`char16_t[9][128]`) and
  MarathonRecomp all treat the name as UTF-16BE. The Free60 wording is
  **[U]**.
- Free60 XEX: https://free60.org/System-Software/Formats/XEX/ (header `0x5FF`
  "Delta Patch Descriptor"; module flags bits 4-6)
- xboxunity: http://xboxunity.net/Resources/Lib/TitleUpdateInfo.php?titleid=54510866
- Pack lists: https://www.gamingnexus.com/News/22473/WWE-All-Stars-DLC-finally-hits-the-360 ,
  https://www.wwe.com/inside/thq/wweallstars/wweallstars-dlc-all-time-greats ,
  https://en.wikipedia.org/wiki/WWE_All_Stars ,
  https://gamingnexus.com/News/23696/New-WWE-All-Stars-content-bodyslams-its-way-on-to-XBL-and-PSN

Local sources:

- `.tools/rexglue-source` (SDK) and `D:\Projects\SVR2011-RECOMP`.
- This repo:
  - `analysis/xex/default.pe` string and xref scans;
  - the `generated/default/` import table;
  - the decoded `Coalesced.ini`;
  - `docs/research/title-update.md`, `xenia-baseline.md`, `base-roster.md`,
    `launcher.md` and `svr2011-pc-port.md`;
  - ADR-0009, ADR-0010 and `.scratch/launcher/spec.md`.

consolemods.org returned 403 and could not be read. No TU or DLC file was
downloaded, because that is out of bounds for this project.

