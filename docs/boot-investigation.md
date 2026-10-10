# Boot investigation

A dated log. Earlier sections stay as written. Runtime captures live under
`analysis/` on the test PC (Windows 11, RX 7900 XT) and are not committed.

## 2026-10-02: M0 and M1 in one evening

### Baseline

- XEX SHA-256 `521c2325d89470ff490efee257885a465360ffb64f9db2491eda6af7ab3a6892`.
  Title `54510866`, media `60CEF505`, version 5; entry `0x8241A8E0`.
- 5,632 disc files extracted and hashed into `assets/` (`analysis/disc-full.json`).
- ReXGlue v0.10.0 (source `f5337cdc`) with SVR2011's 8 runtime patches,
  LLVM 21.1.8, VS 18 BuildTools. Vulkan, RelWithDebInfo.
- Xenia Canary `02d2cb5` reaches gameplay with no crash
  (`docs/research/xenia-baseline.md`).

### Function discovery

1. The first strict codegen failed on 3 unresolved branches. Each came from a
   leaf getter/setter whose shared `li r3,0; blr` tail was left orphaned. Seeded
   0x83046098, 0x83060EE8 and 0x83061000.
2. Runtime miss `0x83402460`: a C++ static initializer (VMX128 constant
   setup). It was the only undiscovered code pointer in the 27k-word table
   around 0x83524418.
3. Runtime miss `0x83039FC8`: `li r5,1; b …`, a thunk referenced only from a
   vtable. Instead of one rebuild per crash, `scripts/find_table_targets.py`
   lists every code pointer in data that sits next to a known function start
   and follows a block end (`blr`, `b`, `bctr`, padding). It skips the SEH
   scope records at 0x82357000–0x82358000, whose handlers lie inside their
   functions. Run to a fixpoint, since each new seed exposes neighbours.
4. Runtime miss `0x82778B58`: preceded by `bctr`, which the first version of the
   scanner did not count. After the fix, the fixpoint took 6 passes:
   58 + 9 + 5 + 4 + 1 + 1 seeds.

There are 121 seeds in total. Every codegen ran strict, with no stubs.

### Result

The game boots through the logos straight to the **main menu** (Classic
renderer, Vulkan FSI). It ran for 90 s with no fatal error. Screenshots:
`analysis/runtime-20261002-214535/shot-004.png` and `shot-008.png`. Character
model, Scaleform UI and lighting look correct.

M1's exit criterion is met. Next: M2 (native bring-up) and Classic exhibition
stability.

## 2026-10-02: first exhibition match on Classic

The project owner played a 1v1 exhibition with an Xbox controller. Command:
`scripts/smoke.ps1 -PerfCapture` plus `--mnk_mode=true --input_backend=xinput`.
There was no crash in about 7 minutes of menus and match.

- Frame timing (`scripts/analyze_perf.py`): 25,679 frames, 58.1 fps average;
  median 16.6 ms, p95 18.5 ms, p99 19.0 ms. 99 frames exceeded two frame
  budgets, 31 exceeded 100 ms and the worst was 933 ms. These are mostly
  loading and first-seen shader compiles; to examine with
  `scripts/analyze_hitches.py`.
- 42 `XmaContext N: There are no bits to copy!` errors from XMA audio
  decoding. Possibly related to Xenia's stuck-audio-loop report; to check.
- Missing optional files (`Game/Movies/*_Xenon.txt`, `Splash/Splash_UK.bmp`)
  are expected probes.
- The pad only works with `--input_backend=xinput`; the default SDL backend did
  not pick up the Xbox controller.
