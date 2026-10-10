# Stuck audio loop (Canary patch at 0x833A6C98)

Ticket: `.scratch/playable-build/issues/07-stuck-audio-loop.md`. Date 2026-10-03.
Addresses are base XEX v5. Disassembly is from `python scripts/disasm.py`
on `analysis/xex/default.pe`. ReXGlue paths are relative to
`.tools/rexglue-source` (`f5337cdc` plus `patches/`).

## Answer

- `0x833A6C10` returns how many decoded PCM samples one XMA context of a
  multi-context stream has ready to read. The Canary patch replaces its last
  instruction, so the function always returns 1.
- The stuck loop is the guest busy-wait at `0x82FD1E88`–`0x82FD1E9C`. It calls
  `0x833A6C10` until the result is non-zero, and it never yields, kicks the
  context or times out. The emulated XMA hardware decodes only when the game
  kicks a context, so if a context's ring is empty but still marked valid, the
  wait never ends.
- In ReXGlue today the wait cannot hang. `Work()` clears `output_buffer_valid`
  whenever the ring is empty, and the game's XMA code reads valid == 0 as a
  full ring. `0x833A6C10` then reports the whole ring as readable, so the
  game reads stale PCM. That probably explains "audio loops during
  entrances", but it is unverified: no audio has been captured.
- Classic in this recomp does not hang. The first exhibition run presented
  25,680 frames over about 7 minutes. It also logged 42
  `There are no bits to copy!` errors, in groups of three contexts.
- Fix: patch ReXGlue's `XmaContext::Work()`, not the guest code. Details under
  "Correct fix" below.

## Primary source: Canary game patch

`xenia-canary/game-patches`, `patches/54510866 - WWE All Stars.patch.toml` at
`01bff555f18e58dde570588449cf3477ce2ea0b8`, lines 6–13:

```
[[patch]]
    name = "Skip Audio Loop Stuck Code"
    author = "Gliniak"
    is_enabled = false
    [[patch.be32]]
        address = 0x833a6c98
        value = 0x38600001
```

- The patch was first added in commit `7ad667c26f` (2021-03-29, "WWE All Star -
  Skip freeze caused by looping audio code") as `patches/54510866.patch` with
  `is_enabled = true`. The same day, xenia-project/game-compatibility #1536
  recorded "infinite load after entrances, audio loops during entrances" on
  build 99024ae (`docs/research/title-update.md:44`).
- By `c5140397` (2022-02-19) the patch was disabled by default (line 8). The
  Canary baseline ran with no patches loaded and reached a match
  (`docs/research/xenia-baseline.md:27,33`), using `xma_decoder = "new"`
  (`.tools/xenia-canary/xenia-canary.config.toml:8`).
- `0x38600001` is `li r3,1`. It overwrites `0x833A6C98 srw r3,r8,r7`.

## The patched function: 0x833A6C10

Inputs: `r3` = playback object, `r4` = stream index. Each stream uses a
0x60-byte record at `[r3+8] + r4*0x60` (`0x833A6C18`–`0x833A6C2C`). The context
word layout below follows `include/rex/audio/xma/context.h:48-79`.

| Address | Meaning |
|---|---|
| `0x833A6C14`, `0x833A6C34`–`6C38` | If flag `0x20000` at `[r3+4]` is set, use the locked local copy at record+0. Otherwise use the hardware context through the pointer at record+0x40. |
| `0x833A6C3C`–`6C48` | dword1 bit 31 = `output_buffer_valid`. |
| `0x833A6C4C`–`6C78` | valid: `write_offset - read_offset` blocks (dword0 bits 27–31, dword9 bits 0–4), or `block_count - read + write` when it wraps. |
| `0x833A6C7C`–`6C80` | **not valid: `output_buffer_block_count` (dword0 bits 22–26), the whole ring.** |
| `0x833A6C84`–`6C90` | blocks × 256 bytes, minus the bytes already consumed in the current block (halfword at record+0x52). |
| `0x833A6C8C`–`6C98` | `>> (1 + is_stereo)` (dword1 bit 29) gives 16-bit samples per channel. |

The same readable-size logic is inlined in the consumer `0x833A6CA0`:
`0x833A6D90`–`6DB8` returns 0 when `write == read` and the context is valid, and
the remainder of the ring when it is not valid. The consumer sets
`output_buffer_valid` again each time it moves the read offset
(`0x833A6D4C oris r4,r9,0x8000`, `0x833A6D54 stw r4,4(r11)`). The guest's own
library therefore treats valid == 0 as "the hardware stopped because the ring
is full" and valid == 1 with equal offsets as "empty".

Neighbouring functions in the same library (behaviour taken from the
instructions):

- `0x833A6900` locks every context of the stream. It writes `1<<id` to
  `0x7FEA1A40 + 4*(id>>5)` (`0x833A6934`–`6964`), which is XMA register 0x690,
  `Lock` (`include/rex/audio/xma/register_table.inc:66`).
- `0x833A7070` polls `0x833A6998` until the lock is held. `0x833A6998` reads
  register 0x606 `CurrentContextIndex` (`0x833A69B0`–`69C8`). `0x833A7070`
  times out using the timebase (`0x833A7080`–`70A4`), then writes register 0x601
  (`0x833A70C8`–`70E8`).
- `0x833A6AE8` unlocks: it flushes the output (`dcbf`, `0x833A6B28`), copies the
  local contexts back, kicks register 0x650 `Kick` (`0x833A6BB0`–`6BE0`) and
  clears flags `0x30000` (`0x833A6BFC`–`6C08`).
- `0x833A6840` returns 1 unless both input buffers are valid (`0x833A6864`–`6870`).
- `0x833A68A0` returns 1 when neither input buffer is valid (`0x833A68C4`–`68D0`).

## The caller and the busy-wait: 0x82FD19D8

`DEFINE_REX_FUNC(sub_82FD19D8)` is at
`generated/default/wweallstars_recomp.248.cpp:25704`. It calls `sub_833A6C10` at
`:26047` (`0x82FD1C3C`) and at `:26378` (`0x82FD1E94`). `[r31+0x104]` holds the
number of contexts in the stream.

1. `0x82FD1A3C`: lock (`0x833A6900`) and wait for the lock (`0x833A7070`).
2. `0x82FD1C10`–`1DF4`: for each context, `n = 0x833A6C10()`. If `n == 0` and
   the input is idle (`0x833A68A0`), the stream is marked finished with -1
   (`0x82FD1DD8`). Otherwise the code reads `min(n, request, 0x200)` samples
   through `0x833A6CA0` and adds them to a running count per context (`r1+0x60`).
   `r22` holds the largest count.
3. `0x82FD1E00`: unlock and kick (`0x833A6AE8`).
4. With more than one context (`0x82FD1E08`):
   - `r22 != 0` and some context is behind: `Sleep`-like `0x82FAAC18(1)`, then
     go back to step 1 (`0x82FD1E48`–`1E50`).
   - **`r22 == 0`: for each context not marked -1, loop
     `0x82FD1E88`–`0x82FD1E9C` (`bl 0x833a6c10; cmplwi r3,0; beq 0x82fd1e88`).**
     The loop has no yield, no kick and no timeout.

The loop exits only when the hardware has decoded more PCM on its own after
the kick in step 3. On a real console an enabled context keeps decoding until
it is locked. Canary's patch forces `n = 1`, so `r22` becomes at least 1 and the
code never reaches the loop. The cost: each pass reads one possibly undecoded
sample per context, then sleeps and retries. That is a workaround, not a fix.

## What ReXGlue does at 0x833A6C98

- A kick decodes the context inline:
  `src/audio/xma_decoder.cpp:280-307`.
- `Work()` clears `is_enabled` on entry: `src/audio/xma_context.cpp:101`.
  `WorkerThreadMain` decodes only enabled contexts:
  `src/audio/xma_decoder.cpp:140-165`, `xma_context.cpp:96-98`.
  So one kick gives one decode pass, and polling never decodes anything.
- After the pass, `if (output_rb.empty()) data.output_buffer_valid = 0;`
  (`src/audio/xma_context.cpp:146-148`). `empty()` means `read == write`
  (`include/rex/memory/ring_buffer.h:34`).
- If the pass decoded nothing (input exhausted, or a decode error such as
  `There are no bits to copy!`, which drops the buffer: `xma_context.cpp:632-635`),
  the context ends up empty and marked not valid. `0x833A6C10` then takes the
  branch at `0x833A6C7C` and returns
  `(block_count*256 - consumed) >> (1+is_stereo)`. That is non-zero, so the wait at
  `0x82FD1E88` ends, and the game consumes a full ring of stale PCM.
- The decode loop also leaves as soon as no input buffer is valid, even when
  part of a frame has not been delivered (`xma_context.cpp:139-141`). Canary
  fixed the same gap (see below).
- Canary at the baseline build used the same empty-means-not-valid rule
  (`xma_context_new.cc@0efd3a9610:199-203`). This explains why the baseline
  reached a match without the patch.

Upstream Canary has since changed the rule (`src/xenia/apu/xma_context_new.cc`
at `e1f4a0d009`):

- `:232-237`: invalidate only when the ring is full
  (`remaining_subframe_blocks == 0 && empty`). Commit `505697f980`.
- `:218-230`: when input runs dry, set `write = read` and mark the context not
  valid. Commit `7e98ae6de3`, "Invalidate output buffer if there is nothing to
  process".
- `:189-198`: finish delivering the current frame before leaving the loop.
  Commit `6bef1fbed1`, whose message reads: "Work() clears is_enabled_ on
  entry and only XMAEnableContext sets it again … a game polling for it never
  issues the kick that would deliver it." That is the same deadlock class as
  `0x82FD1E88`.
- `:200-215`: break when the decoder makes no progress.

## Classic runs in this recomp

- `analysis/runtime-20261002-220750/runtime.log` is the first Classic
  exhibition (`docs/boot-investigation.md:48-60`). It holds 2,197 lines, and its
  final line at 22:15:13 reports `presents=25680`. The run did not hang.
- 42 `There are no bits to copy!` errors between 22:08:57 and 22:09:20 (lines
  559–732), always three consecutive contexts at once (for example 8/9/10). Three
  contexts failing together match one stream of several contexts read by
  `0x82FD19D8`, but nothing links them directly yet.
- Across all logs under `analysis/`, contexts 6–13 logged 111 such errors.
  There are no `Invalid packet`, `error_status` or `block_count` errors.
- Conclusion: in this recomp the busy-wait cannot hang, because of
  `xma_context.cpp:146-148`. The stale-read path is probably taken whenever a
  multi-context stream decodes nothing. Whether it is audible is unknown, because
  no run captured audio.

## Correct fix

No ISO patch and no guest stub. Write a ReXGlue runtime patch
(`patches/rexglue-xma-output-valid.patch`) against `src/audio/xma_context.cpp`:

1. Match what the guest library expects (`0x833A6C7C`, `0x833A6DA4`, `0x833A6D4C`).
   Clear `output_buffer_valid` only when the ring is full, as Canary does at
   `:232-237`. An empty ring keeps valid = 1, so `0x833A6C10` returns 0.
2. Port the frame drain (`6bef1fbed1`) and the stall break (`:200-215`).
3. Rule 1 brings back the wait at `0x82FD1E88`. An emptied context must
   therefore keep decoding without a new kick while input is valid and the ring
   has space, as the hardware does until `Lock`. For example, keep `is_enabled`
   set after a pass that leaves input, and clear it in `Disable()` or `Clear()`.
   Make `Work()` return progress, not `true`, so `WorkerThreadMain`
   (`xma_decoder.cpp:158-163`) does not spin. If the decoder genuinely cannot
   progress while input is still valid, mark the input exhausted, which makes
   `0x833A68A0` return 1 so the stream is marked finished, instead of leaving
   the guest waiting.
4. Check it test-first: a unit test on `XmaContext` with an empty ring and
   valid input expects valid = 1, readable 0 and more PCM after the worker runs.
   On the game, add a runtime counter (log only) for zero results at the
   `0x82FD1E94` call site. The first Classic exhibition, the entrances and a
   match should end with no hang and no stale-buffer reads.

## Measured on the exhibition path (2026-10-03, tickets 15 and 16)

- The busy-wait at `0x82FD1E88` is reached (9 queries per exhibition) and
  never sees a 0, with either rule.
- The game reads `output_buffer_valid == 0` contexts often (about 1 query in
  3), but they are full rings: probes in `Work()` counted 0 passes where the
  old empty-means-not-valid rule cleared valid on a ring that was not full.
  So "the game consumes a full ring of stale PCM" (above) does not happen on
  this path. Ticket 16's rule change is kept as the hardware behaviour.
- The 2021 "audio loops during entrances" therefore has another cause or was
  fixed elsewhere; 33 "There are no bits to copy!" errors per exhibition
  remain the only XMA errors (ticket 17 targets the frame drain).

## Open

- The 2021 hang ("infinite load after entrances") could not be reproduced from
  source. It was build 99024ae, and its decoder version was not checked.
- `0x82FD19D8` is probably FMOD's XMA codec (`.\src\fmod_codec_xma.cpp` string
  at `0x822F4C28`, `docs/research/tooling.md:298-300`). Not confirmed by xrefs.
