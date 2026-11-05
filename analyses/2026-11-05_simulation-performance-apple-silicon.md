# Simulation performance on Apple Silicon

Date: 2026-11-05. Machine: Apple M1 (4 performance + 4 efficiency cores), macOS 27, Apple clang, Homebrew Qt 5.15.19.
SimulIDE 1.1.0-SR2 fork, `main` at 7441632.

Questions: how does the simulator use the CPU cores, and can a simulation run faster on Apple Silicon,
by compiling differently or by changing the engine?

## How the CPU is used

- `Simulator::timerEvent()` (GUI thread, every frame, 50 ms by default) stops the worker, runs the
  `Updatable::updateStep()` calls, and starts `runCircuit()` again with `QtConcurrent::run`
  (`src/simulator/simulator.cpp:123`). The worker runs the event loop up to the next frame time.
- The engine has no other threads. Running a circuit flat out, one thread sat at about 97 % of a core
  and the GUI thread at about 8 %: about 1.1 of the 8 cores.
- One simulation cannot usefully be split across cores: a time step costs about 1 to 2 µs and depends on
  the previous one, and handing work between cores costs about as much.
- App Nap (headless instances are hidden apps): no clear effect in a quick test, which was noisy.

### Several simulations at once

Separate headless instances (`--headless --socket N`), each running the same 200 ms of a switching
supply circuit:

| Instances | Seconds each | Total throughput |
|---|---|---|
| 1 | 5.2 | 1× |
| 2 | 5.3 | 1.8× |
| 4 | 6.2 | 3.1× |
| 6 | 8.2 | 3.5× |
| 8 | 9.7 to 12.5 | 3.2× |

Up to 4 at once (the performance cores) is the useful limit for sweeps and variants.

## Compiler options

Benchmark: five circuits, each run for a fixed span of simulated time at maximum speed, best of 3 runs,
wall-clock seconds:

| Circuit | Current (-O2) | -O3 -mcpu=apple-m1 | Thin LTO | PGO + thin LTO |
|---|---|---|---|---|
| Op-amp wave generator (`Active/OP_Amp/wave_gen`, 40 s) | 3.62 | 3.57 | 3.52 | **3.37** (−7 %) |
| JK flip-flop counter, clock set to 100 kHz (`Logic/counter-60_FF`, 150 s) | 3.61 | 3.57 | 3.62 | **3.06** (−15 %) |
| Arduino (`Micro/Arduino/oscope8544`, 20 s) | 3.27 | 3.27 | 3.27 | **2.81** (−14 %) |
| AVR + scripted SPI part (`Scripted/SPI_Digipot/SPIDigipot-test`, 25 s) | 3.27 | 3.32 | 3.36 | **2.91** (−11 %) |
| Switching supply with scripted ICs (`led_driver/sim/board_mains`, 150 ms) | 3.97 | 4.07 | 3.86 | **3.62** (−9 %) |

- **Profile-guided optimisation (PGO) is the only option that pays: 7 to 15 % on every kind of circuit.**
  It helps most in branchy code: the AVR core, the digital pin logic, the AngelScript interpreter.
  The profile was trained on shortened runs of the same five circuits, so the numbers are slightly
  optimistic.
- `-O3`, `-mcpu=apple-m1` and thin LTO alone are within run-to-run noise.
- `SimulIDE.pro` asks for `-Ofast`, but qmake appends `-O2` after it and the last `-O` wins: the release
  build is `-O2` without fast-math. `-ffast-math` was not tried: the engine relies on exact `==`
  comparisons and zero checks.

## Where the time goes (sampling profiles of the simulation thread)

| Kind of circuit | Main costs |
|---|---|
| Analog (op amps, RLC) | About 55 % in the solver path: `CircMatrix::luSolve`, `eNode::setVolt` (callbacks to the parts of each node), `eNode::stampMatrix`, a `std::vector::resize` on every solve |
| Switching supply (50 nodes, 50 ns step) | Matrix about 37 % (a 50-node solve takes about 200 ns, a factorization about 300 ns), AngelScript about 30 %, `setVolt` about 15 % |
| Digital logic | Digital pin machinery (`IoPin::getInpState`, `IoComponent::scheduleOutPuts`, `IoPin::setOutState`), plus `stampMatrix` / `stampCurrent`: digital outputs are analog sources in the matrix |
| Microcontrollers | About 50 % CPU emulation (`AvrCore::runStep`, `eMcu::stepCpu`, `DataSpace::writeReg`), **12 to 13 % in `Simulator::addEvent`** |

## Possible improvements, most promising first

1. **PGO build**: 7 to 15 %, measured, no code change. Chosen: see `specs/pgo-build.md`.
2. **Event queue**: `addEvent` inserts into a sorted linked list by walking it (O(n)), and an MCU
   schedules an event per instruction. A fast path for an event that goes first, or a binary heap,
   would remove most of the 12 % on MCU circuits.
3. **Solver data layout**: flat compressed arrays instead of `vector<vector<double>>` reached through
   pointers into a full-size matrix, a work buffer allocated once, node pointers next to the values.
   Partial refactorization from the first changed row when one diode or switch changes state.
   Estimated 10 to 20 % on analog circuits.
4. **Digital fast path**: nodes that only connect digital pins pass logic levels without the matrix.
   Biggest potential gain for logic circuits (maybe 2×), and the biggest refactor.
5. **Threshold callbacks for scripted parts**: scripts are interpreted on arm64 (the AngelScript JIT is
   x86-only) and run on every voltage change of a watched pin. A callback only when a pin crosses a
   threshold would remove most of those calls.
6. **Parallel instances** for sweeps: about 3× total throughput on the 4 performance cores.

Not worth it: splitting one simulation across threads, `-O3`, `-mcpu`, LTO alone.
