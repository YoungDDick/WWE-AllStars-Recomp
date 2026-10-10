# Shader containers from the disc

Ticket: `.scratch/launcher/issues/02-research-shader-containers-from-disc.md`.
Date 2026-10-04. Addresses are base XEX v5. Disassembly is from
`python scripts/disasm.py` on `analysis/xex/default.pe`. Package facts come
from the extracted disc (`assets/Game/CookedXenon`), read by a throwaway
prototype (LZO1X decoder, package table reader, shader-cache walker) that
stayed in the session scratchpad. No game data or prototype output is in
this repo.

## Answer

- **The containers are on the disc.** Every cooked package that renders
  anything carries a `SeekFreeShaderCache` export (class `ShaderCache`). Each
  record in it holds the shader's complete Xenos container (header and
  microcode back to back), as the register hooks rebuild it.
- **A static reader finds 1,720 of the 1,726 captured containers,
  byte-identical**, by the dump's own name (`ps_`/`vs_` + FNV-1a-64 of the
  bytes). That covers 1,412 of 1,416 pixel shaders and 308 of 310 vertex
  shaders in `cache/guest-shaders`. Vertex microcode matches too: the dump
  reads it at register time, before D3D patches the vfetch placeholders.
- **The disc has more than any capture.** The reader returns 2,019 unique
  containers (1,706 PS, 313 VS) from 410 packages. 299 of them were never
  captured, because they belong to content no capture session visited.
- **All 2,019 convert.** XenosRecomp, with the current
  `patches/xenosrecomp-wweallstars.patch`, converts 2,019 of 2,019 in one run
  (17 s, no `reason=` failures). The five quarantined containers in
  `cache/guest-shaders-rejected` are not on disc. They were the garbage
  rebuilds of `Create*Shader` objects (ticket 29 of playable-build), not
  shaders.
- **Six shaders are not on disc.** The game compiles them at runtime from
  HLSL text in the XEX, using the D3DX compiler linked into the XEX (see
  "The six runtime-compiled shaders"). No static reader can produce their
  bytes without running that compiler.
- **Verdict for ticket 10: static reader, plus a small runtime top-up for
  the six. No scripted capture run.** Details are in "Verdict" below.

## Where the hooked containers come from

UE3's in-place path for vertex shaders is `0x828E6608` (`this`, r4 =
container). Pixel shaders use the same shape at `0x828E67B0`.

| Address | What it does |
| --- | --- |
| `0x828E6698` | `bl 0x82DA9658`: splits the container at r4 into header size, microcode pointer and microcode size (stack `+0x64`, `+0x68`, `+0x6C`). |
| `0x828E66A0`, `0x828E66B0` | Allocate the shader object (`0x824DBE50`) and copy the header into it (`0x82DAB210`). |
| `0x828E66C0`–`0x828E66EC` | Allocate physical memory for the microcode (size rounded up to 32) through the allocator at `0x83701478`, vtable slot `+0x14`. |
| `0x828E6700` | `memcpy` (`0x82C307A0`) of the microcode into that memory. |
| `0x828E670C` | `bl 0x82DAB250`: the hooked vertex shader register function, r4 = the copied microcode. |

The register hook's rebuild (header copy plus microcode) therefore equals the
container that UE3 handed in. That container is the `Code` array of a
`FShader` loaded from a package's shader cache, which is why the bytes match
exactly.

## Package format (what a reader needs)

Packages are UE3, big-endian, file version `0x018E` (398) with licensee
`0x25`. 5,414 `.xxx` files are packages. `AIFSM_Strategies.xxx` (tag
`2923BE84`) is not a package and holds no shaders.

### Summary and compression

| Offset | Field |
| --- | --- |
| `0x00` | Tag `9E2A83C1` |
| `0x04` | Version `0x0025018E` |
| `0x08` | Total header size |
| `0x0C` | `20414E54` (constant, licensee field) |
| `0x10` | `0x0F` (constant, licensee field) |
| `0x14` | FolderName (`FString`: i32 length including the NUL, then chars; a negative length means UTF-16) |
| then | PackageFlags, NameCount, NameOffset, ExportCount, ExportOffset, ImportCount, ImportOffset (7 × u32; no DependsOffset at this version) |
| then | GUID (16), generation count n, n × 12 bytes, EngineVersion (`0xA2D`), CookerVersion (`0x21`) |
| then | CompressionFlags (2 = LZO), chunk count, chunks of {UncompressedOffset, UncompressedSize, CompressedOffset, CompressedSize} |

- **380 of the 410 shader-cache packages** are compressed through that chunk
  table. The summary stays plain. The body, from the first
  UncompressedOffset on, is in chunks.
- **30 are compressed as whole files** (first bytes `C1 83 2A 9E`). The file
  is a sequence of chunks with little-endian headers, and the decompressed
  result is an ordinary package (for example `Engine.xxx`, `Game.xxx`,
  `CombinedGlobalCharacters.xxx`).
- **Chunk layout.** Each chunk is {Tag, BlockSize, CompressedSize,
  UncompressedSize}, then {CompressedSize, UncompressedSize} per block, then
  the blocks. Blocks are LZO1X, with up to `0x20000` uncompressed bytes each.
  In every chunk on this disc the BlockSize field holds the tag value
  (`9E2A83C1`) rather than `0x20000`, so a reader must not trust it.

The packages that hold shaders total 678 MB on disc. The prototype decodes
and parses all 410 of them in 8 s (Python with the LZO loop in C).

### Tables

- **Name entry:** `FString`, then u64 flags.
- **Import entry:** 28 bytes. ClassPackage (FName), ClassName (FName), Outer
  (i32), ObjectName (FName). An FName is {i32 index, i32 number}.
- **Export entry:** 40 fixed bytes: Class, Super, Outer, ObjectName (FName),
  Archetype, ObjectFlags (u64), SerialSize, SerialOffset. A variable tail
  follows: ExportFlags, the generation net-object array, GUID and flags.
  Forced exports carry a longer tail. The prototype found the next entry
  reliably by its SerialOffset, which continues the previous entry's
  SerialOffset + SerialSize.

### `SeekFreeShaderCache` export

| Relative offset | Field |
| --- | --- |
| `+0x00` | FName `None` (end of the property list) |
| `+0x08` | u32 `4` in all 410 packages |
| `+0x0C` | Record count |
| `+0x10` | Records |

Each record:

| Offset | Field |
| --- | --- |
| `+0x00` | Shader type (FName), e.g. `FSD_UberPixelShader`, `TEmissiveVertexShader<FNoLightMapPolicy>` |
| `+0x08` | GUID (16) |
| `+0x18` | Absolute package offset of the record's end (the next record starts there) |
| `+0x1C` | Target: platform byte (2 = Xbox 360) and frequency byte (0 = vertex, 1 = pixel) |
| `+0x1E` | `Code`: u32 length, then the container (`102A1100` pixel or `102A1101` vertex, header size, microcode size, …) |
| … | Type-specific parameters, up to the end offset |

Two shader types, `FBinkYCrCbAToRGBAPixelShader` and
`FBinkYCrCbToRGBNoPixelAlphaPixelShader`, put type-specific bytes (6 and 34)
before the target in every package. A reader that searches the record (from
`+0x1C` to the end offset) for a `102A110x` container preceded by a matching
length and target handles them too.

Checks over all 410 caches: 100,965 records, 0 walk errors, every length
prefix equals header size + microcode size, and every target is platform 2
with the frequency that matches the container flag. A blind byte-pattern scan
of every decompressed package found no container outside these exports. 222
shader types appear. Each package carries its own copy of the shaders it
uses, so 100,965 records reduce to 2,019 unique containers. A greedy cover
needs 31 packages (`Arena_002` 625, `Engine` 403,
`CombinedGlobalCharacters` 305, `Story_ArcA_FMC04` 110,
`CombinedStartup_int` 91, …). A reader should still walk all 410 caches,
because the cover is a property of this disc and not a rule.

## The six runtime-compiled shaders

Not on disc: `ps_69d28119d5fe8475`, `ps_70a9b839a616c29a`,
`ps_ba4217b1c1c1a40f`, `ps_e9bf247d29508b28`, `vs_309cb027862dbde6`,
`vs_d8617cc8f9c7e195`. Their headers carry compiler version `2.0.11775.3`,
while all 2,019 package shaders carry the cooker's, `2.0.9328.0`. They are the `Create*Shader`
objects from playable-build ticket 29, whose draws include the autosave
indicator. The call sites below are every runtime compile in the XEX. Which
site produced which of the six hashes was not traced, because the verdict
does not depend on it.

- `0x82DCD040` is a D3DX-style compile function linked into the XEX. Its
  arguments are r3 = HLSL text, r4 = length, r7 = entry point,
  r8 = profile, and r10 = the out buffer. Each caller then calls the buffer's
  `GetBufferPointer` (vtable `+0xC`) and passes the result to
  `D3DDevice_CreateVertexShader` (`0x82DAB348`) or
  `D3DDevice_CreatePixelShader` (`0x82DAB120`).
- **Call sites of `0x82DCD040`:**
  - `0x82904CF8`/`0x82904D7C`, `0x82906418`/`0x8290649C`,
    `0x82907810`/`0x82907894`: source from the pointer at `0x835363D8`.
    That pointer is a static `.data` initialiser (no code stores to it) to
    the text at `0x82170408`. Entry points are `SplashVertexShader`
    (`0x82170A08`) and `SplashPixelShader` (`0x821709EC`), plus
    `BusyIconVertexShader`/`BusyIconPixelShader` (`0x821714E4`,
    `0x821714D0`).
  - `0x82888F98`/`0x8288901C`: a depth-restore pair. The text is copied from
    `0x8215CB18`/`0x8215CA58` to the stack, and the profiles are
    `vs_3_0`/`ps_3_0`.
  - `0x8257B288`, `0x8257B320`, `0x8257B3AC`: a YUV-to-RGB movie set (two
    pixel shaders and one vertex shader; text at `0x82071C70`, `0x82071F00`
    and `0x82071C00`).
- Inputs (text, profile, compiler code) are constant, so the output is
  deterministic. The same hashes recur across capture sessions
  (`guest-shaders-t41`, `-capture` and the current set). Their constant
  tables differ from any package shader (for example the constant-table
  offset `0x84` against `0x88` in the closest disc match). Only the compiler
  produces these bytes.

## Containers in the XEX itself

`.rdata` holds 68 more containers (3 PS, 65 VS) from `0x822AD8F0` to
`0x822F165C`, compiled with `2.0.9407.0` (XDK library shaders). Three of them
(`0x822AD8F0`, `0x822AD9E0`, `0x822ADBC0`) are created at `0x82EA3090`. None
appears in any capture, and 64 of 68 fail XenosRecomp (undeclared temp
registers, as with the old rejects). The reader should skip them.

## Verdict

**Ticket 10 uses a static reader.** During setup, the launcher walks the
`SeekFreeShaderCache` of every package in `gamedata/Game/CookedXenon`, writes
each unique container named `ps_`/`vs_` + FNV-1a-64 (the same naming as
`--wwe_shader_dump_dir`, so `convert-shaders.ps1` works unchanged), runs
XenosRecomp and builds the pack. The game does not run. Expected cost on the
owner's machine: about 10 s to read and about 20 s to convert.

**The six runtime-compiled shaders come from a top-up, not a capture run.**
A scripted headless capture just for six small shaders would start the game
during setup, with its window, audio and timing risks. The owner's rule
against long scripted runs argues against it too. Instead:

- The game already has each `Create*Shader` container in hand
  (`ShaderCreated` in `src/d3d_census.cpp`). When one is missing from the
  loaded pack, it writes it to `cache/shader-topup/`. This is always on
  and writes misses only; every session so far has had the same six.
- When that folder is not empty, the launcher's "prepare shaders" step (or
  the stale-pack offer from story 54) converts the extra containers and
  rebuilds the pack.
- Until then, the six draws behave as today's misses: Native skips them as
  `no_shader` (a few UI draws such as the autosave indicator), and the rest
  of the frame is native.

The top-up is new game-side work: a few lines in `ShaderCreated`, plus a
pack lookup. Ticket 10, or a follow-up ticket it creates, must own it.

**What a capture run would look like (rejected).** The six register during
boot. In playable-build ticket 29, a headless boot run
(`analysis/scenes/t29-boot`) saw them by frame 1,500, about 25 s at 60 fps.
Once they were registered, a boot to frame 3,000 had no `no_shader` skips
at all. So a capture run
would be `--wwe_shader_dump_dir` plus a scripted boot to the title screen,
about 30 s plus game start-up. It would capture only boot and menu shaders
(the main menu registers 981 containers), so on its own it would cover far
less than the static reader.

If the owner wants those six right from the first session, the alternative
is a game-side export mode that runs only `0x82DCD040` on the known sources
and writes the containers. That needs the guest runtime (heap, kernel
imports) without booting the game. It is unproven, and it is not needed for
ticket 10.

**Constraints for the C# reader (ticket 10):**

- **LZO1X decoder.** liblzo2 and minilzo are GPL (ADR-0007), so the decoder
  must be written from the format description or taken from a permissively
  licensed source. The decoder is about 60 lines. The prototype's decoder was
  written from the format description.
- **Byte order.** Big-endian everywhere except whole-file chunk headers.
- **Corrupt input.** Treat a bad package as a setup error that names the
  file, since the disc was hash-verified first.

## Evidence summary

| Check | Result |
| --- | --- |
| Captured containers (`cache/guest-shaders`) | 1,726 (1,416 PS, 310 VS) |
| Found on disc byte-identical | 1,720 |
| Not on disc | 6, all runtime-compiled (above) |
| Older capture sets on disc | `-capture` 1,632/1,638, `-t41` 1,424/1,435, `-new` 624/624, `-new2` 33/33; every miss is one of the six runtime-compiled shaders or one of the five pre-ticket-29 garbage rebuilds |
| Quarantined rejects on disc | 0/5 (garbage, not shaders) |
| Unique containers on disc | 2,019 (1,706 PS, 313 VS) in 410 packages |
| XenosRecomp on the disc set | 2,019/2,019 converted |
| Read + parse time (prototype) | 8 s for 410 packages |
