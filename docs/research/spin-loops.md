# The two spin loops: the frame limiter and D3D's GPU wait

Low-end-preset ticket 09 (`.scratch/low-end-preset/issues/09-research-spin-loops.md`).
Question: where can the game's frame limiter and D3D's GPU wait be made to yield, with
instruction-level evidence? All listings are from the decrypted image
(`python scripts/disasm.py <addr> <count>`, base XEX v5).

## Summary

| Loop | Where it spins | Ends when | Proposed hook | Seed needed |
|---|---|---|---|---|
| Frame limiter | `0x82417E7C–0x82417EBC` in `FEngineLoop::Tick` (`0x82417890`): `appSleep(0.0)` until 1/60 s (times dilation) has passed since the last limiter exit | `GCurrentTime − frame_start ≥ dt` (`fcmpu cr6,f10,f30` / `blt`) | the existing timing-only hook on `appSleep` (`0x82608FD8`, `src/game_tick_log.cpp`): inside the tick and for `appSleep(0.0)`, sleep with a high-resolution timer until shortly before the deadline the loop is about to test, then return | No: `sub_82608FD8` and `sub_82417890` are already functions and hooked |
| GPU wait | Six D3D wait loops, all `bl 0x82DB6600` (begin) → loop { `bl 0x82DB6710` (one spin step), test } → `bl 0x82DB6630` (end) | each loop's own test of the GPU's progress (ring read pointer, fence, or a counter), or `0x82DB6710` returning 0 (device lost or hung) | a midasm hook right after each `bl 0x82DB6710` returns, which backs off (yield, then short high-resolution sleeps) by time since the wait began; a function hook on `0x82DB6600` marks the start | No seed: every containing function and `0x82DB6600` already exist; six `[[entrypoint.midasm_hook]]` entries |

`0x82DB6710` itself is never hooked (`native-renderer.md`, attribution sweep: about 885k calls
per present). The midasm hooks run in the loops that call it; they slow the loop down, so
`0x82DB6710` runs far fewer times, not more host code per call.

## 1. The frame limiter

### Listing

```
82417B64  stfs f31, 0x28(r25)        ; DeltaSeconds (1/60, or 1/30 at the game's half-rate)
82417B68  fmuls f30, f0, f31         ; dt = dilation (this+0x2C) * DeltaSeconds
...
82417BE4  lfs f31, 0xed0(r11)        ; f31 = 0.0 (float at 0x82000ED0)
...
82417E40  addi r3, r31, 0x50
82417E44  bl 0x8241a150              ; appCycles: mftb into r31+0x50
82417E48  lfd f13, 0x50(r31)
82417E4C  fcfid f12, f13
82417E50  lfd f0, -0x6b60(r23)       ; GSecondsPerCycle
82417E54  fmul f0, f12, f0           ; now (s)
82417E58  stfd f0, -0x42e0(r24)      ; GCurrentTime = now (0x8370BD20)
82417E5C  lwz r11, 0xc(r25)          ; frame_start, a double at this+0xC (r25 = this,
82417E60  lwz r10, 0x10(r25)         ;   mr r25,r3 at 0x824178B0), stored at 0x82417A58/5C
82417E64  stw r10, 0x54(r31)         ;   from the previous GCurrentTime
82417E68  stw r11, 0x50(r31)
82417E6C  lfd f11, 0x50(r31)
82417E70  fsub f10, f0, f11          ; now - frame_start
82417E74  fcmpu cr6, f10, f30
82417E78  bge cr6, 0x82417ec0        ; done already
82417E7C  fmr f1, f31                ; appSleep(0.0)
82417E80  bl 0x82608fd8
82417E84  ...                        ; 0x82417E84-0x82417EB4: the same now / GCurrentTime /
82417EB8  fcmpu cr6, f10, f30        ;   frame_start / compare
82417EBC  blt cr6, 0x82417e7c        ; loop
82417EC0  addi r1, r31, 0x200        ; epilogue
```

### What `appSleep(0.0)` does on the host

```
82608FE8  fmr f31, f1
82608FEC  bl 0x82c34f08              ; NaN/Inf test of f1 (exponent 0x7ff0)
...                                   ; f1 < 0.0 or f1 >= 5.0 (0x8206D750): return
82609020  lfd f0, -0x6b0(r11)        ; 1000.0 (0x8209F950)
82609028  fmul f0, f31, f0
8260902C  fctidz f13, f0             ; ms = seconds * 1000
82609038  bl 0x82d992d8              ; Sleep(ms) = SleepEx(ms, FALSE) (li r4,0; b 0x82d9d338)
```

`SleepEx` (`0x82D9D338`) turns `ms` into a relative 100 ns interval (`mulli r11,r11,-0x2710`)
and calls the kernel's `KeDelayExecutionThread` (`bl 0x835002c4`). The SDK's
`XThread::Delay` (`src/system/xthread.cpp:1000-1040` of `rexglue-source`) treats a zero
interval at normal priority as `rex::thread::MaybeYield()`, which is `SwitchToThread()`
(`src/core/threading_win.cpp:87`). `SwitchToThread` returns at once when no other thread is
ready on that core, so the limiter loop runs flat out: one host core at 100 % for the rest
of each 16.7 ms frame. The tick log measures this time as "limiter" (`src/game_tick_log.cpp`).

### Proposed wait

In the existing `appSleep` hook, when it runs inside the tick (`t_in_tick`) and `f1 == 0.0`
(the `GetMaxTickRate` path at `0x82417BD8–0x82417C54` calls `appSleep` with a positive
time and is left alone):

1. `remaining = frame_start + dt − GCurrentTime`, all guest values the loop just computed:
   `frame_start` = the double at `this+0xC`, `dt` = `this+0x28 × this+0x2C` (floats),
   `GCurrentTime` = the double at `0x8370BD20` (stored at `0x82417E58`/`0x82417E9C` just
   before the call).
2. If `remaining` is more than a margin (1 ms), sleep `remaining − margin` with
   `rex::thread::PreciseSleep` (a high-resolution waitable timer, the one the SDK's guest
   vblank thread uses, `src/graphics/graphics_system.cpp:210-217`). Otherwise yield as today.
3. Return. The guest loop recomputes `now` and runs its own compare, so the exit condition,
   the 1/60 s step and `GCurrentTime` are exactly the game's; only the number of spins
   changes. The last stretch (at most the margin) is still the game's yield loop, so the exit
   lands at the same granularity as before.

ADR-0016's guest vblank is produced by the SDK's vsync thread on its own timer and is not
touched. The pure part, "time left → sleep how long or yield", is a seam for a Catch2 test.

## 2. D3D's GPU wait

### The spin step `0x82DB6710` (not hooked)

```
82DB6728  li r11, 4 / mtctr r11
82DB6730  mr r31,r31 (x8) / bdnz      ; 32 PPC priority hints: no-ops on the host
82DB6758  lbz r11, 0x2abd(r29)        ; device lost/hung bit -> return 0
82DB6764  lwz r11, 0x2a90(r29)        ; the GPU's read pointer
...                                   ; progress since the last call resets a timestamp
82DB67B4  cmplwi cr6, r11, 0x1388     ; 5000 ms without progress:
82DB67C8  bl 0x82dc3d18               ;   the hang handler
82DB67BC  li r3, 1                    ; keep waiting
```

### The begin and end calls

`0x82DB6600` fills a wait record (`stw r4,0(r3)` device, `stw r5,4(r3)` reason, the read
pointer, a millisecond time and `mftb` at `0x82DB6624`) and returns; `0x82DB6630` reads it
back when the wait ends. Both run once per wait, not once per spin.

### The six loops

Each is `bl 0x82db6600`, then `addi r3,r1,<record>; bl 0x82db6710; cmpwi r3,0; beq <exit>`,
the loop's test, and a branch back.

| Call of `0x82DB6710` | In function | Waits for | Hook site (after the call) |
|---|---|---|---|
| `0x82DB460C` | `0x82DB45B8` | ring space: the read pointer (`+4` of `device+0x2A90`) passing the write position (`0x82DB4618–0x82DB462C`) | `0x82DB4610` |
| `0x82DB46B0` | `0x82DB4650` | the GPU passing a ring position (`+0x3C` of `device+0x2A90`, `0x82DB46BC–0x82DB46DC`) | `0x82DB46B4` |
| `0x82DB4D94` | `0x82DB4D00` | the read pointer (`device+0x2A90` → `+0`) against `device+0x2A9C` (`0x82DB4DA0–0x82DB4DB8`, `blt` back to `0x82DB4D90`) | `0x82DB4D98` |
| `0x82DBBB7C` | `0x82DBB558` | fewer than 15 between `device+0x4188` and `device+0x4190` (`0x82DBBB88–0x82DBBB98`) | `0x82DBBB80` |
| `0x82DC1CAC` | `0x82DC1C48` | a fence value at `*(entry+0x10)` reaching `r30`, six entries (`0x82DC1CB8–0x82DC1CC4`) | `0x82DC1CB0` |
| `0x82DC23C8` | `0x82DC2260` | the same for six entries at `device+0x2C48` (`0x82DC23D4–0x82DC23E0`) | `0x82DC23CC` |

Every hook site is the `cmpwi r3,0` right after the call. None is a branch target (each loop
branches back to the `addi r3,r1,<record>` before the call), and the hook needs no register:
it only waits on the host. `0x82DB6710`'s result and the loop's test then run as shipped.

### Proposed wait

- A function hook on `0x82DB6600` stores, per host thread, the time the wait began.
- The midasm hook backs off by time since then: the first 50 µs return at once (a GPU that
  is nearly done costs no latency), up to 1 ms `SwitchToThread()`, after that
  `PreciseSleep(100 µs)` per spin. The GPU's progress is the emulated command processor on
  another host thread, so a sleeping waiter frees the core it needs. The added latency per
  wait is bounded by one sleep (about 0.1–0.5 ms with the high-resolution timer), against a
  16.7 ms frame; the pacing bars (zero-stutter tickets 13 and 23) decide whether it holds.
- The decision (elapsed → return, yield or sleep) is pure and gets a Catch2 test.

### Seeds

None. The six containing functions, `0x82DB6600` and `0x82DB6630` are already functions in
the generated code (`DECLARE_REX_FUNC(sub_82DB6600)` etc.), and midasm hooks only need the
instruction addresses above plus a codegen run. Each manifest entry carries the evidence
from the table.

## 3. Prior art: wwe-svr2011-pc

Its counterpart, the XDK poll `sub_82919F18` (about 430k calls per frame), was hooked as a
function: yield, then sleep 100 µs (`port/src/perf_hooks.cpp` L1–97, commit `d26b3f7`;
`svr2011-pc-port.md`, Performance). Its render thread went from 98 % to 51 % of a core. This
proposal keeps the same back-off but puts it in the callers' loops, per this repo's rule for
`0x82DB6710`, and adds the 50 µs grace so a nearly finished GPU costs nothing.

## 4. What is not known yet

- Which of the six loops carries the 885k calls per present. The hooks cover all six, so it
  does not change the design; a Tracy or WPR capture of the render thread before and after
  shows it (tickets 10 and 11).
- How exact `PreciseSleep` is on the Deck under Proton (Wine maps high-resolution waitable
  timers to its own timer code). The limiter's 1 ms margin and the final yield loop absorb a
  late wake on the desktop; a Deck probe should check the tick log's "limiter" times.

## 5. The game thread's wait on the render thread (found in ticket 10)

The limiter in §1 does not run on the title screen: its `appSleep` call (return address
`0x82417E84`) was called 0 times per tick in three windowed starts. The tick reaches it with
1/60 s already gone, because earlier in the tick the game thread waits for the render
thread, and the render thread is paced by the presents. That wait is UE3's game-thread
fence:

```
82417C64  addi r30, r25, 0x20         ; the fence, at FEngineLoop+0x20
82417C6C  bl 0x8271c588              ; begin the fence
82417C70  li r4, 1                   ; the render thread may be one frame behind
82417C78  bl 0x8271c770              ; wait
...
8271C770  sub_8271C770(counter*, target):
8271C780  lwz r11, 0(r3)             ; counter
8271C78C  cmplw cr6, r11, r4         ; counter <= target: return
8271C7C4  lwz r11, 0x343c(r25)       ; loop: if rendering is not threaded,
8271C7CC  bne cr6, 0x8271c800        ;   run the pending commands here (0x829D2108)
8271C800  fmr f1, f31                ; appSleep(0.0)
8271C804  bl 0x82608fd8
8271C808  lwz r11, 0(r24)
8271C80C  cmplw cr6, r11, r23
8271C810  bgt cr6, 0x8271c7c4        ; loop while counter > target
```

Ten callers use it (FlushRenderingCommands-style waits during loads, among others); the
tick's is the one per frame. On the title screen the game thread spent about 14 ms of each
16.7 ms tick here, about 120 000 `appSleep(0)` calls per tick, one host core at 83–100 %.

### Wait

`sub_8271C770` is already a function (no seed). A function hook marks when the wait began;
the `appSleep` hook, for the call returning to `0x8271C808` with `f1 == 0.0`, backs off as
the GPU wait does (§2): the game's own yield for the first millisecond, then
`PreciseSleep(100 µs)` per pass. The loop's own compare still decides when it ends, so the
render thread's lead is unchanged; the added latency is at most one 100 µs sleep.

### Measured (title screen, windowed, `--wwe_gpu_wait_backoff=false`)

| Run | Game thread | Present interval median / p99 / max | Ticks over 20 ms (after boot) |
|---|---|---|---|
| sleep off (`lowend-diag10off-174453`) | 99.5 % | 16.666 / 16.861 / 17.115 ms | 1: 22.5 ms, 19.7 ms waiting on the render thread |
| sleep on (`lowend-t10on-174925`) | 12.0 % | 16.666 / 16.857 / 17.114 ms | 1: 21.8 ms of game work |
| default on (`lowend-t10default-175116`) | 12.2 % | 16.666 / 16.849 / 18.624 ms | 1: 25.4 ms, 24.9 ms waiting on the render thread |

The long tick about 65 s after start appears in every run, with the sleep on or off: the
render thread is late once there (the title's attract sequence). It is not the sleep. The one
18.6 ms present (frame 2811) is not at that tick and has no repeated or replaced frame; the
render thread still spins in the GPU wait in these runs (ticket 11).

The earlier report of 25–50 ms "limiter" ticks with the sleep on came from the old tick log,
which counted every `appSleep` in the tick as the limiter's. The log now splits it:
`limiter`, `render thread` (this fence) and `other waits`.

## 6. The GPU-wait back-off on its own (ticket 11)

Title screen, windowed, limiter sleep on (its default since ticket 10), 50 s samples after
40 s:

| Run | Process | Busiest thread | Present interval median / p99 / max | Present latency median / p99 |
|---|---|---|---|---|
| back-off off (`lowend-t11off-175526`) | 136 % of a core | 99.1 % (render thread) | 16.666 / 16.835 / 17.114 ms | 4.05 / 4.69 ms |
| back-off on (`lowend-t11on-175350`) | not recorded | 11.7 % | 16.666 / 16.871 / 17.204 ms | 4.10 / 4.68 ms |
| back-off on (`lowend-t11on2-175705`) | 48 % of a core | 10.7 % | 16.666 / 16.865 / 17.132 ms | 4.11 / 4.76 ms |

No repeated or replaced frames in any run. The back-off adds about 0.05 ms of present
latency at the median, within the 100 µs sleep it was designed around. It is on by
default.
