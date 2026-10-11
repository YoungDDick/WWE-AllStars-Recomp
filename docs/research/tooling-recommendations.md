# Tooling recommendations: reverse engineering, debugging, verification

Date: 2026-10-11. Question: which tools would make this project much more
efficient for reverse engineering, debugging and verifying the recompiled
game, and what is clearly missing. This note follows up `tooling.md`
(2026-10-03), which planned the M2–M6 verification loop. Most of that plan now
exists: the scene runner uses NVIDIA FLIP (`scripts/scene_runner.py` L8–11,
L148–151), input is scripted (`scripts/session.ps1`, `src/script_input.h`),
PresentMon and Tracy are pinned (`scripts/capture.ps1` L36,
`.tools/tracy-0.13.1`), and the Native renderer emits debug-utils labels
(`src/native/debug_labels.h` L2). This note covers what is still missing.

**Status, 2026-10-11 (after this note):** items 1 and 2 are done.
`disasm.py` decodes with the Xenon objdump `-M cell,vmx128` and falls back to
Capstone; it matched codegen's instruction comments on 40,460 of 40,740
instructions in 400 sampled vector functions (the rest are aliases such as
`vmr`/`vor`). Plain `-M vmx128` misreads `lvx128` as `vcmpequd`, and drops
`vsubshs` (0x10C86F40), hence the fallback. TU2 is in Ghidra 12.1.4 with
Xenon360 v0.7.0 (project `ghidra/WWEAllStarsTU2`, 97,925 functions, analysis
438 s with a 10 GB heap); Xenon360 does not decode `vpk*128`. Version Tracking
from `default_v5` (§1.3) was not run. See CLAUDE.md "Reverse engineering".

Local state was read on 2026-10-11. Web facts were read the same day from
primary sources: GitHub repos, releases and source (through `gh api`), and
vendor docs. Each one carries a link. **Unverified** marks anything not
confirmed at the source. Nothing was installed, built or run against the game
for this note. The only runs were Capstone and the bundled objdump on
`analysis/xex/tu2.pe` (§1.1).

**One correction to the brief.** WWE All Stars is not a Yuke's game. It is
THQ San Diego's Unreal Engine 3 (`docs/ROADMAP.md` "Target";
`svr2011-pc-port.md` Summary). Its data is UE3 packages: `assets/` holds 5,414
`.xxx` files and 164 `.bik` files (file count, 2026-10-11). Yuke's PAC/PACH
tools from the SmackDown scene do not apply. UE3 tools do (§4).

## Summary: ranked top list (highest impact first)

| # | Recommendation | Present? | Effort |
|---|---|---|---|
| 1 | **Give `disasm.py` a VMX128 fallback through the Xenon binutils objdump already in `.tools`.** Capstone cannot decode about 36,000 vector words in TU2 `.text`. The bundled objdump with `-M vmx128` decodes them. | Present, unused | 1 h |
| 2 | **Import TU2 into Ghidra with a VMX128-capable language, then port the names from `default_v5` with Version Tracking.** The only program in the project is the base image, while every address in the project is TU2 (ADR-0022). | Missing (no XEX or Xenon extension installed) | ½–1 day |
| 3 | **WinDbg Time Travel Debugging plus `mcp-windbg`.** Record the recomp once, then let an agent ask "who wrote guest address X, and when". The PDB already names every guest function `sub_<addr>`. | WinDbg present; `ttd.exe` CLI and MCP missing | ½ day |
| 4 | **Install RenderDoc 1.46 and a RenderDoc MCP.** The Native labels and the SDK's RenderDoc API hook exist, but RenderDoc is not installed. | Missing | 2–3 h |
| 5 | **Install the Vulkan SDK 1.4.363 (validation layers).** `--vulkan_validation_enabled` exists, but no Khronos validation layer is registered on this machine. | Missing | 1 h |
| 6 | **Install the AMD Radeon Developer Tool Suite (RGP, RGD, RMV).** The dev GPU is an RX 7900 XT, so Nsight and PIX do not apply. RGD explains GPU crashes and TDRs. | Missing | 1–2 h |
| 7 | **Update Canary and build its GPU trace viewer and dump** for per-draw Xenos ground truth. Local Canary is from 2026-03; the tools are never shipped prebuilt. | Old binary; source present without submodules | 1 day |
| 8 | **Tracy 0.14.1, for its built-in MCP server**, so agents can query and compare `.tracy` captures. The project pins 0.13.1. | 0.13.1 present | ½ day (protocol/client bump) |
| 9 | **UModel (`-game=tna`) for asset ground truth.** It names WWE All Stars as supported (textures, meshes, sounds). It lets agents check texture formats and contents without the renderer. | Missing | 1 h |
| 10 | **XDK library naming: build an XDK 11775 FunctionID database** with `x360-fidb-generator`, or fall back to BSim (as in `tooling.md` §6). The public FIDBs skip 11775. | Missing | 1–2 days, needs XDK libs |

Lower priority or not recommended (§6): IDA/idaxex, Binary Ninja, BinDiff,
Diaphora, Nsight, PIX, Superluminal, x64dbg, Cheat Engine, ReClass.NET, Frida,
GFXReconstruct, ImHex nightly MCP, Kaitai.

**Present but underused:** the Xenon objdump (§1.1), the Xenia source in
`.tools/xenia-netplay` (§3.4), WinDbg 1.2606 (§2.1), the `sub_<addr>` names in
the RelWithDebInfo PDBs (§2.1), the ghidra MCP's `ghidra_script` for BSim and
Version Tracking (§1.3), and Canary's `trace_functions*` cvars (§5).

---

## 1. Static reverse engineering

### 1.1 Xenon binutils objdump for VMX128 (present, unused)

- **What it does.** `.tools/rexglue-source/tools/binutils/` ships
  `powerpc-none-elf-objdump.exe`. Its `-M` list includes `vmx128` (it prints
  its own options under `--help`).
- **Why it helps.** `scripts/disasm.py` prints `.long 0x…` for every word
  Capstone cannot decode (L23). Over TU2 `.text`
  (0x82410000–0x83504018), 67,425 words have primary opcode 4, 5 or 6, and
  Capstone 5.0.9 decodes none of 36,371 of them (20,779 / 10,901 / 4,691; this
  note's scan). Some may be data inside `.text`. A sample of the first 200
  failing words past 0x82500000 went through
  `objdump -D -b binary -m powerpc:common64 -EB -M vmx128`. All 200 decoded,
  with 0 `.long` (`vmsum4fp128`, `vmrghw128`, `stvx128`, `lvx128`,
  `vperm128`…). Havok and the D3D math code are vector-heavy, so agents
  currently read holes in exactly the code that matters for the renderer and
  for physics timing.
- **How to use it.** When Capstone returns nothing, fall back to one objdump
  call per range in `disasm.py`. Pass POSIX paths, because objdump is a Cygwin
  build and warns about `C:/` paths. The generated C++ already carries the
  same mnemonics as comments (for example `// lvx128 v63,r0,r7` in
  `generated/default/wweallstars_recomp.1*.cpp`). That is a second source,
  usable only for functions codegen knows about.
- **Maturity and license.** It is part of the rexglue toolchain at the pinned
  `f5337cdc`. GNU binutils is GPL.

### 1.2 Ghidra: XEX loader and VMX128 language (missing)

Local state: Ghidra 12.1.2 lives at `D:\tools\ghidra-mcp\ghidra_12.1.2_PUBLIC`.
The user extensions are only `ghidra_psx_ldr` and
`ghidra-emotionengine-reloaded`
(`%APPDATA%\ghidra\ghidra_12.1.2_PUBLIC\Extensions`). There is no XEX loader
and no Xenon language. Stock 12.1.2 defines no VMX128 language: `ppc.ldefs`
lists 4xx, e500, QUICC, A2/A2ALT and VLE variants, and no `.sinc` mentions
vmx128 or xenon (local grep). The project
`ghidra/WWEAllStars.rep` holds a single program, **`default_v5`** (the base
image, imported 2026-10-02). ADR-0022 makes every address TU2. The project was
locked by another session at 17:45, so this note could not open it read-only.
**Unverified**: which language that program uses. Since no Xenon language is
installed, it must be a stock one, which leaves VMX128 code undecoded.

Options, verified 2026-10-11:

| Tool | What it adds | Ghidra build | Status | License |
|---|---|---|---|---|
| [XEXLoaderWV](https://github.com/zeroKilo/XEXLoaderWV) | Decrypts and decompresses XEX (`XEXHeader.java`, LZX), names xboxkrnl/xam imports (`ImportRenamer.java`). The README lists "Supports XEXP delta patches" and "Supports PDB/XDB files". | **12.1.2 release exists** (2026-08-01); latest 12.1.3 (2026-09-20) | 178★, last commit 2026-09-20 | none declared |
| [Xenon360](https://github.com/EveryoneEverybody/Xenon360) | XEX0–XEX2 + XEXP loader **and** language `PowerPC:BE:64:Xenon-VMX128-32addr`; PDB/XDB; smoke test covers VMX128 decompilation | **12.1.4 only** ("other versions not packaged or validated") | v0.7.0 (2026-09-30), 1★, new | Apache-2.0 |
| [GhidraXenon](https://github.com/freeqaz/ghidra-xenon-extension) | Language `PowerPC:BE:64:Xenon` (AltiVec + 77 VMX128 opcodes), `RecoverMSVCSwitchTables.java`, `ApplyMapSymbols.java`; no loader (README says use XEXLoaderWV) | 12.1 | v12.1 (2026-02-11), 2★ | none |

XEXLoaderWV alone does not fix VMX128. It hard-codes the stock language
`PowerPC:BE:64:A2ALT-32addr` (`XEXLoaderWVLoader.java` L43), and Ghidra's
source has no "vmx128" (code search).

**Recommendation.**

1. Install Ghidra 12.1.4 (latest, 2026-09-21,
   https://github.com/NationalSecurityAgency/ghidra/releases) side by side with
   12.1.2, plus Xenon360.
2. Import `assets/default.xex` with `assets/default.xexp` as a new program
   `tu2` in a copy of the project. The ISO stays untouched; both files are
   already on disk.
3. Check its decode against §1.1's objdump on a few VMX128 functions before
   trusting it. Xenon360 is one month old and has one star.
4. Fallback: XEXLoaderWV 12.1.2 for loading, and set the language by hand
   after rebuilding GhidraXenon for 12.1.2. **Unverified** that the rebuild
   works.
5. Point the ghidra MCP's `--ghidra-install-dir` at 12.1.4 once it works. The
   server is pyghidra-based and not tied to 12.1.2 (`~/.claude.json` mcpServers
   entry; `D:\tools\ghidra-mcp\ghidra-headless-mcp\README.md`).

The memory note `feedback-use-ghidra.md` says the owner pre-authorised
installing Xbox 360 Ghidra extensions from the projects' own releases.

### 1.3 Porting names base v5 → TU2: Ghidra Version Tracking (built in, unused)

- Ghidra's Version Tracking is meant to "version track two different versions
  of the same binary… to port comments and labels into the new context"
  (`Ghidra/Features/VersionTracking/.../Version_Tracking_Intro.html`,
  https://github.com/NationalSecurityAgency/ghidra).
- **Why here.** Any names and types in `default_v5` are at base addresses.
  `tu_addrmap.py` already maps addresses in text files (ADR-0022), but it does
  not carry Ghidra markup. VT moves labels, comments and signatures into the
  `tu2` program from §1.2 in one session.
- The ghidra MCP has no VT or BSim tools in its catalogue, but it exposes
  `ghidra_script`/`ghidra_eval` (README "Scripting access"). A headless VT
  script driven by an agent is possible. **Unverified**: that VT's headless API
  is usable from pyghidra without the GUI.

### 1.4 Library naming: FunctionID, BSim

- **Public XDK FIDBs:** https://github.com/tomcl7/ghidra-fidb-xenonsdk covers
  XDK 3529, 5426, 6274, 6534, 7645, 7776, 9328 and 20764.6. **There is none
  for 11775**, our XDK (`xenia-baseline.md` Setup table). The generator is
  https://github.com/tomcl7/x360-fidb-generator. It needs the 11775 `.lib`
  files, which the project does not have. Neither repo has a license, and the
  last push was 2025-06-28.
- FunctionID matches exact bytes, so a near version (9328 or 20764) will
  name little. **Unverified** how much.
- **BSim** (fuzzy matching) runs on a local H2 file with no server: Ghidra
  help `BSim/DatabaseConfiguration.html`. `tooling.md` §6 already plans BSim
  from the Fable Anniversary PDB build. That plan still stands, and is
  cheaper than finding XDK 11775 libs.
- No public Xbox 360 FLIRT or BSim sets were found (**unverified** that none
  exist).

### 1.5 Ghidra MCP alternatives

The current server (`ghidra-headless-mcp`, 212 tools, pyghidra) covers what
agents need. Alternatives, for reference only:

- [GhidrAssistMCP](https://github.com/symgraph/GhidrAssistMCP) 3.0.0
  (2026-10-09, MIT): multi-program via `program_name`, headless via
  `analyzeHeadless`, needs Ghidra 12.0/12.1 and Java 25+. The machine has
  Java 21 (`java` on PATH is Temurin 21.0.12).
- [pyghidra-mcp](https://github.com/clearbluejar/pyghidra-mcp) v0.2.7
  (2026-09-25, Apache-2.0): project-wide analysis of every binary in a
  project, semantic search. This would let agents query `tu2` and `default_v5`
  side by side.
- [LaurieWired/GhidraMCP](https://github.com/LaurieWired/GhidraMCP) 2.0
  (2026-10-10, Apache-2.0): built for 12.1.4.

Switching servers is not worth it now. One friction point is worth fixing:
the project lock. `WWEAllStars.lock` was held at 17:45 today, and a second
session's open failed with `LockException`. Agents should share the single
MCP session, or open a copy of the project.

---

## 2. Runtime debugging of the recomp (x64 host)

### 2.1 WinDbg + Time Travel Debugging (WinDbg present, TTD CLI and MCP missing)

- **Local:** the `Microsoft.WinDbg` 1.2606.22001.0 Appx is installed
  (`windbgx` on PATH). `ttd.exe` is not on PATH.
- **TTD.exe** is a standalone install from https://aka.ms/ttd/download. It
  needs admin and `-accepteula` for automation. It can record with `-launch`,
  `-attach <PID>` or `-monitor`. Other options are `-ring`/`-maxFile` (ring
  buffer up to 32,768 MB), `-module <name>` (record only that module) and
  `-recordmode Manual` (in-process start and stop API). Recording runs about
  5–20x slower
  ([Microsoft Learn, TTD.exe](https://learn.microsoft.com/en-us/windows-hardware/drivers/debuggercmds/time-travel-debugging-ttd-exe-command-line-util)).
- **Queries:**
  - `dx @$cursession.TTD.Memory(begin, end, "w")` lists every access to a
    range, with IP, value and time position
    ([memory objects](https://learn.microsoft.com/en-us/windows-hardware/drivers/debuggercmds/time-travel-debugging-memory-objects)).
  - `dx @$cursession.TTD.Calls("mod!sym")` lists every call, with parameters
    and return value
    ([calls objects](https://learn.microsoft.com/en-us/windows-hardware/drivers/debuggercmds/time-travel-debugging-calls-objects)).
- **Why it fits this project.**
  - The guest arena sits at a fixed host base: `1 << n`, the first n ≥ 32 that
    maps, logged as "Guest memory arena mapped: virtual base…"
    (`.tools/rexglue-source/src/system/xmemory.cpp` L161–179).
  - Every guest function is a host function named `sub_<guestaddr>`
    (`DEFINE_REX_FUNC(sub_82410000)` in `generated/default`).
  - RelWithDebInfo builds have PDBs (`out/build/win-amd64-relwithdebinfo-v3o2-vulkan/wweallstars.pdb`).
  - So "which guest function last wrote guest 0x8xxxxxxx before the crash" is
    one `TTD.Memory` query on `base + addr`. It needs no rebuild and no new
    `write_watch` instrumentation (`src/native/write_watch.cpp`).
  - `TTD.Calls("wweallstars!sub_82…")` gives a guest call log with arguments
    (r3–r10 live in the context struct, so read them through the context
    pointer, not the x64 parameters).
- **Cost.** At a 5–20x slowdown a recording cannot follow a live match, and
  the owner drives the game by hand. Use `-ring` plus a scripted
  `session.ps1` route to the bug (seconds, not minutes).
- **Agent integration:** [mcp-windbg](https://github.com/svnscha/mcp-windbg)
  v1.5.1 (2026-10-10, MIT) puts WinDbg/CDB behind MCP. **Unverified**:
  whether it drives TTD traces or only dumps and live sessions. Check before
  relying on it.

### 2.2 x64dbg, Cheat Engine, ReClass.NET, Frida (low priority)

- [x64dbg](https://github.com/x64dbg/x64dbg) 2026.05.27, GPLv3. It has several
  MCP servers, for example
  [x64DbgMCPServer](https://github.com/AgentSmithers/x64DbgMCPServer) v1.6
  (no license) and [x64dbg-mcp](https://github.com/SetsunaYukiOvO/x64dbg-mcp)
  v1.0.11 (MIT). WinDbg+TTD covers the same ground, plus history, so skip it.
- [Cheat Engine](https://www.cheatengine.org/downloads.php) 7.7, no license
  stated. It is useful for ad-hoc live poking of guest variables: add the
  guest base, and mind that guest data is big-endian. A custom big-endian
  4-byte type is needed. **Unverified**: the exact CE setting.
  [ReClass.NET](https://github.com/ReClassNET/ReClass.NET) is dormant (last
  release 2019-04-15).
- [Frida](https://github.com/frida/frida) 17.23.3 (2026-10-09). Hooks are
  compiled into the recomp (`midasm_hook`, `tooling.md` §1), so Frida adds
  little.

---

## 3. GPU and frame debugging

The dev GPU is an **AMD RX 7900 XT** (`xenia-baseline.md` Setup), which
rules some tools out.

### 3.1 RenderDoc (missing)

- Not installed: no `C:\Program Files\RenderDoc`, and no `renderdoc*` on PATH.
  `tooling.md` §3 built the capture plan around it, and the Native labels exist
  "so RenderDoc, RGP and Nsight captures are readable"
  (`src/native/debug_labels.h` L2).
- [RenderDoc](https://github.com/baldurk/renderdoc) v1.46 (2026-08-31, MIT).
  It has an in-application API (`StartFrameCapture`, `EndFrameCapture`,
  `TriggerMultiFrameCapture`;
  https://renderdoc.org/docs/in_application_api.html) and
  `qrenderdoc --python` (`qrenderdoc/Code/qrenderdoc.cpp` L346–353).
- **AMD caveat:** the v1.46 release notes report an AMD driver bug that can
  break shader debugging across multiple threads.
- **Agent integration:**
  [renderdoc-mcp](https://github.com/JiaboLi-GitHub/renderdoc-mcp) v0.3.1
  (2026-04-10, MIT, 357★) wraps the replay API. **Unverified**: its tool count
  and its Vulkan coverage on AMD.

### 3.2 Vulkan validation layers (missing)

- No `VK_LAYER_KHRONOS_validation` is registered:
  `HKLM\SOFTWARE\Khronos\Vulkan\ExplicitLayers` lists only the Social Club
  layer, and there is no `C:\VulkanSDK`. `--vulkan_validation_enabled`
  therefore has nothing to load.
- Latest SDK: **1.4.363.0** (https://vulkan.lunarg.com/sdk/latest.json). Layer
  docs cover sync validation, GPU-AV (off by default), debug printf and
  `gpu_dump`
  ([Vulkan-ValidationLayers docs](https://github.com/KhronosGroup/Vulkan-ValidationLayers)).
- **Effort:** install the SDK (or only the layer), then use the switch planned
  in `tooling.md` §3.

### 3.3 AMD Radeon Developer Tool Suite (missing)

- One download, `RadeonDeveloperToolSuite-2026-10-02-1872.zip`, linked from
  the [RGP release notes](https://github.com/GPUOpen-Tools/radeon_gpu_profiler).
- **RGP** v2.7.1 (2026-10-06, binary-only): Vulkan, RX 7000 (RDNA3). It shows
  per-pass GPU time with the Native labels. `performance-and-low-end-hardware.md`
  calls the GPU side "the gap".
- **RGD** ([Radeon GPU Detective](https://github.com/GPUOpen-Tools/radeon_gpu_detective))
  v1.6.3 (2026-06-11, MIT): crash analysis for DX12 and **Vulkan**, RX 7000.
  It needs Adrenalin 25.10.2 or later and Windows 11
  (https://gpuopen.com/radeon-gpu-detective/). This is the only tool here
  that explains a `VK_ERROR_DEVICE_LOST`.
- **RMV** v1.15.1 (MIT): VRAM use, relevant to Deck-class targets.
- **Not applicable:**
  - [Nsight Graphics](https://docs.nvidia.com/nsight-graphics/InstallationGuide/windows.html)
    needs an NVIDIA driver. NVIDIA testers could use it at M6.
  - [PIX](https://devblogs.microsoft.com/pix/requirements/) is Direct3D 12
    only.

### 3.4 Xenia Canary GPU trace and debugger (binary stale, tools never built)

- **Local:** `.tools/xenia-canary` is release `02d2cb5` (2026-03-24,
  `xenia-baseline.md`). Canary now releases from the main repo: the latest is
  tag `103257c`, 2026-10-11, asset `xenia_canary_windows.7z`
  (https://github.com/xenia-canary/xenia-canary/releases). The old
  `xenia-canary-releases` repo stopped at `02d2cb5`. No `.xtr` trace exists
  under `analysis/` or `.tools/xenia-canary`.
- **Trace viewer and dump are never shipped.** They build only with
  `XENIA_BUILD_MISC`, which is OFF by default (`CMakeLists.txt` L38). Windows
  CI packages only `xenia_canary.exe` and `LICENSE`
  (`.github/workflows/Windows_x86.yml` L77).
- **Already on disk:** `.tools/xenia-netplay` is a Canary fork (HEAD
  `6dbaa1fef`, 2026-09-10). It contains `trace_writer`, `trace_viewer`,
  `trace_dump`, `vulkan_trace_viewer_main.cc`, `vulkan_trace_dump_main.cc` and
  `src/xenia/debug/ui/debug_window.cc`. But `third_party/` is only 1.8 MB, so
  the submodules are not fetched and it cannot build as is.
- **Guest debugger:** Canary has an ImGui guest debugger: the menu entry
  "Break and Show Guest Debugger" (`src/xenia/app/emulator_window.cc` L907)
  and the `debug` cvar (`src/xenia/cpu/processor.cc` L49). **Unverified**: that
  the release build enables it. There is **no GDB stub upstream** (source and
  code search).
- **Recommendation:** update the reference Canary to `103257c` and re-baseline
  `xenia-baseline.md`. Build `xenia-gpu-vulkan-trace-viewer` and `-trace-dump`
  once, from upstream at the same commit rather than the netplay fork, and pin
  them under `.tools/`.
- **Second step:** port a trace writer into rexglue-source.
  [skate3-recomp](https://github.com/portingpete/skate3-recomp) carries
  `include/rex/graphics/trace_{writer,reader,player,viewer,dump,protocol}.h`.
  These are a GPU command trace, not a CPU trace.

### 3.5 GFXReconstruct (optional)

[GFXReconstruct](https://github.com/LunarG/gfxreconstruct) is tagged per SDK
(`vulkan-sdk-1.4.363.0`, 2026-09-28). It captures and replays Vulkan on
Windows (README capability table). It is useful only for handing a
self-contained repro of a driver bug to AMD; RenderDoc covers frame debugging.

---

## 4. Asset and format tooling (UE3, not Yuke's)

| Tool | Relevance here | Status | License |
|---|---|---|---|
| [UE Viewer / UModel](https://github.com/gildor2/UEViewer) | **Names WWE All Stars as supported**: `G("WWE All Stars", tna, GAME_TNA)`, detected as ArVer 398 / Licensee 37 (`Unreal/GameDatabase.cpp`). The 2014-10-08 changelog: "everything except the animation (use -noanim)" (`readme.txt`). Exports Xbox 360 textures and XMA audio. Use: decode the textures behind a Native texture-format bug, check mesh data. | Last commit 2024-02-23; Win32 build 2023-07-07 (https://www.gildor.org/en/projects/umodel) | MIT (per GitHub) |
| Gildor `decompress` / `extract` | Decompress `.xxx` packages for other tools | 2020-09-30 (https://www.gildor.org/downloads) | freeware |
| [UE Explorer](https://github.com/UE-Explorer/UE-Explorer) | UnrealScript and package decompiler; big-endian packages supported (`BigEndianEncodedStream`), but packages must be decompressed first (README). Useful to read game logic in UnrealScript rather than PPC. **Unverified** for ArVer 398.37. | 1.6.2 (2026-06-03) | GPL-3.0 |
| [JPEXS FFDec](https://github.com/jindrapetrik/jpexs-decompiler) | Scaleform GFx tags and XBT textures (changelog). Menus are GFx 2.x (`tooling.md` §5). | 26.3.0 (2026-09-14) | GPL-3.0 |
| [vgmstream](https://github.com/vgmstream/vgmstream) | FSB1–4 with XMA1/XMA2 (`doc/FORMATS.md`). FMOD Ex is FSB4. Reference decode for the audio-loop checks (`stuck-audio-loop.md`). | r2117 (2026-05-19) | NOASSERTION |
| [ImHex](https://github.com/WerWolv/ImHex) | Built-in MCP server (`open_file`, `read_data`, `execute_pattern_code`…) since 2025-12-16, **nightly only**, not in v1.38.1. The `upk-ue3.hexpat` pattern is little-endian only, and `xex.hexpat` is the Atari 8-bit format. | v1.38.1 (2025-12-21) | GPL-2.0 |
| 010 Editor | Template `XEX.bt` exists; no UE3, Bink or FSB templates (https://www.sweetscape.com/010editor/repository/templates/) | v16.0.4, commercial | commercial |
| Kaitai Struct | No XEX, Xbox or Unreal specs among 189 formats (https://github.com/kaitai-io/kaitai_struct_formats) | n/a | n/a |
| Noesis | **Unverified** support for Xbox 360 untiling and UE3 (official page silent) | v4474 | freeware |

No dedicated WWE All Stars modding tools were found (**unverified**).

---

## 5. Differential testing (Xenia ↔ recomp)

- **No off-the-shelf tool** for lockstep or state diffing between Xenia and a
  recompiled binary was found (**unverified** that none exists). rexglue has
  no CPU trace either: `tools/` is only `binutils`, and the CLI commands are
  `codegen`, `init`, `project_scan` and `test_recompiler`
  (https://github.com/rexglue/rexglue-sdk, `src/rexglue/commands`).
  v0.10.0 is still the latest stable; the newest nightly is
  `nightly-20261002-bd833a2a`.
- **Building blocks that already exist:**
  - Canary's CPU cvars `trace_functions`, `trace_function_coverage`,
    `trace_function_references` and `trace_function_data` with
    `trace_function_data_path` (`src/xenia/cpu/cpu_flags.cc` L23–31,
    `processor.cc` L51 in `.tools/xenia-netplay`). **Unverified**: that
    release builds honour them.
  - The guest-function view of the recomp comes from TTD `Calls`/`Memory`
    queries (§2.1).
  - Game-level state already diffs: `syncsim_log.py compare` (Quazal
    checksums), the draw census and the scene runner.
- **Suggested shape, if a CPU divergence ever needs it:**
  1. Pick a handful of guest functions at the divergence.
  2. Log their r3–r5 and return value in Canary (function trace) and in the
     recomp (a `midasm_hook` or TTD `Calls`).
  3. Diff the two sequences.
  This is days of work; build it only when a bug demands it.

---

## 6. Evaluated, lower priority

- **IDA Pro + idaxex.** IDA 9.1 decodes "Xenon (Xbox 360) instructions,
  including VMX128 extension"
  (https://docs.hex-rays.com/9.1/user-guide/disassembler/supported-processors).
  [idaxex](https://github.com/emoose/idaxex) 0.44 (2026-06-04, BSD-3) loads
  XEX, names imports and marks `.pdata` functions, but its README does not
  mention XEXP. [ida-pro-mcp](https://github.com/mrexodia/ida-pro-mcp) exists
  (MIT). It is better than stock Ghidra only on VMX128, which §1.2 closes for
  free. Worth it only if the owner already has a licence.
  [xenon-jumptables](https://github.com/xdzleo/xenon-jumptables) (IDA, emits
  `[[switch_tables]]`) stays in reserve, as in `tooling.md`.
- **Binary Ninja.** [binja-xex2](https://github.com/landaire/binja-xex2) is a
  loader with no license (pushed 2026-10-08). VMX128 is unverified. No reason
  to switch.
- **BinDiff / BinExport.** [BinDiff 8](https://github.com/google/bindiff) is
  open source (Apache-2.0). The latest [BinExport](https://github.com/google/binexport)
  release targets Ghidra 11.0.3 (2024-04-17), with no 12.x build. Base↔TU2 is
  already solved by `tu_addrmap.py` (ADR-0022), and Ghidra VT (§1.3) covers
  the markup.
- **Diaphora.** [Diaphora](https://github.com/joxeankoret/diaphora) is IDA-only
  (README: IDA ≥ 7.4). AGPL-3.0.
- **Matching with SVR2011.** The engines differ (Yuke's vs THQ UE3,
  `svr2011-pc-port.md` Summary), so the shared code is the XDK, mostly D3D.
  `match_functions.py` already matched 349 of 563 D3D functions
  (`tooling.md` §6). A diffing tool would add little.
- **Superluminal** (€59 personal, €289 per seat per year,
  https://superluminal.eu/pricing/). Tracy is already integrated.
- **Tracy upgrade.** [Tracy](https://github.com/wolfpld/tracy) v0.14.0 added
  a built-in MCP server that loads `.tracy` captures and compares traces
  (release notes); the latest is v0.14.1 (2026-08-22). The project ships
  `TracyClientrd.dll` built from the SDK's Tracy client. **Unverified**: whether
  a 0.14 profiler reads 0.13.1 captures or connects to a 0.13.1 client. If it
  does not, the client must be bumped in the SDK patch set. Ranked #8 for that
  reason.

## Open questions

- Which language the `default_v5` Ghidra program uses (project locked during
  this note).
- Whether Xenon360's VMX128 decode matches the Xenon objdump across the image
  (a scripted cross-check is cheap once installed).
- Whether `mcp-windbg` can open and query TTD traces.
- Whether Canary release builds honour `trace_function*` and `debug`.
- Whether Tracy 0.14 reads 0.13.1 captures and clients.
