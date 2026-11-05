# Profile-guided optimisation (PGO) build

Status: implemented on `feature/pgo-build` (2026-11-05). Background and measurements:
`analyses/2026-11-05_simulation-performance-apple-silicon.md`.

## Goal

Make simulations faster on Apple Silicon without changing the engine. Of the compiler options measured,
only PGO gave a gain (7 to 15 %); `-O3`, `-mcpu=apple-m1` and LTO on their own did not.

## Design

- **`SimulIDE.pro`**, clang only:
  - `CONFIG+=pgo_gen` builds an instrumented binary (`-fprofile-instr-generate`). It writes a `.profraw`
    file per process when it exits, named by `LLVM_PROFILE_FILE`.
  - A normal release build adds `-flto=thin -fprofile-instr-use=pgo/simulide.profdata` when that file
    exists. Without it, the build is unchanged. qmake prints a `PGO:` message in both cases.
  - Out-of-date and unprofiled warnings are off: a stale profile still builds, and the functions that
    changed are optimised normally.
- **`tools/sim_bench_circuits.txt`**: the training and benchmark set, five circuits from
  `resources/examples`: op amps, a BJT LC oscillator, flip-flop logic (clock set to 100 kHz through a CLI
  setup command), an Arduino, an AVR with a scripted SPI part. This covers the solver, the digital pins,
  the AVR core and AngelScript.
- **`tools/sim_bench.sh`**: runs the set on a private headless instance (socket `simbench-<pid>`) at full
  speed for a fixed span of simulated time, and prints the wall time, best of N runs. It quits the
  instance with `quit --discard`, so an instrumented binary writes its profile. Log lines are prefixed
  `[sim_bench]` and go to stderr; the table goes to stdout.
- **`tools/pgo_build.sh`**: four steps, logged with `[pgo_build]`:
  1. Instrumented build in `build_pgo_gen/`.
  2. Training: `sim_bench.sh --runs 1 --scale 0.3`, with `LLVM_PROFILE_FILE=pgo/raw/%p.profraw`. The CLI
     client processes also write profiles; they are small and harmless.
  3. `xcrun llvm-profdata merge` into `pgo/simulide.profdata`.
  4. `qmake`, `make clean` and a full rebuild of `build_XX`. A changed profile is not a Makefile
     dependency, so the clean rebuild is needed.

  The script refuses to run while a SimulIDE from `build_XX` is running.
- `build_pgo_gen/` and `pgo/` are git-ignored. The profile depends on the source version, so it is not
  committed; rerun `tools/pgo_build.sh` after larger source changes.

## Result (M1, best of 3, wall seconds)

| Circuit | -O2 before | PGO |
|---|---|---|
| analog_opamp (40 s) | 3.62 | 3.37 (−7 %) |
| analog_bjt_lc (15 s) | 3.07 | 3.17 (+3 %, noise level) |
| logic_ff (150 s) | 3.67 | 3.06 (−17 %) |
| avr_arduino (20 s) | 3.27 | 2.77 (−15 %) |
| scripted_spi (25 s) | 3.27 | 2.87 (−12 %) |
| led_driver `board_mains` (150 ms, not in the training set) | 3.97 | 3.72 (−6 %) |

`tests/cli/smoke.sh` passes on the PGO build (275 checks).

## Out of scope

Engine changes (event queue, solver layout, digital fast path, script threshold callbacks): see the
analysis. Linux/Windows builds: the `.pro` block only applies to clang; GCC would need
`-fprofile-generate` / `-fprofile-use`.
