# Oscilloscope redesign

Status: **framework**, collecting subjects (2026-11-16). No design chosen yet; nothing here
is decided until it is in the Decision log. The create-circuit issues of the scope are
labelled `task:sim-scope` (`jeroenwk/create-circuit`); this page groups them by the part of
the scope they would change.

## Why

The book builds of the create-circuit skill measure almost everything through the scope
(`cc.py run --pp`, `cc.py wave`, `cli scope data`). Most of their scope problems are not bugs
in one place but follow from how the scope acquires and keeps samples: resolution and history
both depend on the timebase and on how often the signal changes, each channel keeps its own
instants, and there is no acquisition mode for peaks or a.c. signals on a d.c. level. Builders
work around it (short runs, small timebases, part readings instead of the scope) and report
the same limits again and again.

## The scope as built (2026-11-16)

- **Acquisition** (`OscopeChannel::voltChanged`, simulation thread): called when a probed
  node changes, not at a fixed rate. A sample is the pin voltage difference at that instant.
  Changes closer than one *slot* (time/div × 10 / 50000 = time/div / 5000) overwrite the
  current slot (the last change wins, with its time); a later change starts the next slot.
  So a slowly changing or held signal stores few samples, a busy one one per slot.
- **Storage**: a ring of `BufferSize` slots per channel (default 600 000; `PlotBase`), values
  and times. History = BufferSize × max(slot, interval between changes): about 12 screens
  for a busy signal, much more for a quiet one. Each channel has its own times.
- **Edges, frequency, auto trigger** (`findEdges`, `findTrigEdge`): from the value each time
  step settled at (not the intermediate non-linear iterations); fixed-level trigger with the
  Filter as hysteresis (`specs/scope-trigger-level.md`).
- **Display** (`PlotDisplay`): draws the ring for the screen around the trigger, per channel
  volts/div and position (`VolPos` in volts), time position.
- **Logic analyzer** (`LaChannel`): stores every change, ring of 100 000.
- **Export** (`cli scope data`, `cli_measure.cpp`): `[time, value]` per channel, a value holding
  until the next sample (create-circuit `wave.py` reads it that way, #101).

## Subjects that could benefit from a redesign

Each: the issues, what goes wrong, the cause in the current design, and directions (not
decisions).

### S1. Resolution follows the timebase

- Issues: #178 (event-driven samples, sub-µs events only in short runs), #207 (≈100 ns floor,
  p-p under-read above 200 kHz), #226 (a 24 ns discharge inside one sample), #296 (±30 mV
  readout skew from 0.8 µs spacing), #174 (the first ms of a flyback edge between samples).
- Cause: one slot per time/div / 5000; a measurement made after a run at a coarse timebase
  has coarse samples, whatever the simulation step was.
- Directions: acquisition independent of the display timebase (store every settled step up to
  a memory budget, or a sample rate set like a bench scope's, with the timebase only for
  display); a record length setting; report the effective sample rate.

### S2. History length

- Issues: #183 (a 1–2 Hz sweep window longer than the stored history read high), #267 (a long
  run overwrites its start), #174 (sparse samples at the end of a 26 s run).
- Cause: the ring holds BufferSize slots; how much time that is depends on the timebase and
  the signal.
- Directions: a record length in time, not in slots; segmented memory (keep the start, or keep
  around triggers); decimation that keeps envelopes (min/max per interval) for old data.

### S3. Channels sampled at different instants

- Issues: #202 (channels looked phase-shifted in a dump), #296 (held level vs input at the
  sampling instant).
- Cause: each channel stores its own change instants; comparing two channels means
  interpolating one at the other's times.
- Directions: one common time axis for all channels of a scope (every channel sampled when
  any changes, or at a common rate), as on a bench scope.

### S4. Fast events and peaks

- Issues: #226, #174, #256 (edges dated between distant samples), the coil kickback spike of
  create-circuit lessons.md (one reactive step wide, usually missed).
- Cause: the last change in a slot wins; a spike shorter than a slot is lost.
- Directions: a peak-detect acquisition mode (min and max per slot, as bench scopes do), glitch
  capture, or min/max kept per slot always.

### S5. Front end: coupling and probes

- Issues: #133 (no a.c. coupling: a 1.2 mV ripple on 7.8 V needs `VolPos` set to the exact d.c.
  level, and drifts when the load changes).
- Directions: per-channel coupling DC / AC (a high-pass at a few Hz, or the running mean
  subtracted for display and readouts), with controls in the Expand panel and the CLI; maybe
  a bandwidth limit and probe attenuation shown on screen (`InputImped` already models a ×10
  probe, #102).

### S6. Measurements in the scope

- Issues: #198 (period of a 99 % duty square read as 1.2 µs: the 555 chattered within each
  1 µs reactive step; create-circuit `wave.py` now reports `chatter`), and every builder
  re-measuring p-p, mean, period with its own scripts.
- Directions: bench-scope style measurements per channel (p-p, mean, RMS, period, frequency,
  duty, rise time) computed by the scope over the record, shown on screen and given by the
  CLI, so `cc.py wave` and the GUI agree; flags for chatter and too few samples.

### S7. Export contract

- Issues: #256, #217 (digital channels exported sparsely, interpolation mis-dates edges).
- Interim (2026-11-16, create-circuit 1.20.0): `scope data` thins by time buckets (first,
  lowest, highest, last per bucket, the same buckets for every channel) instead of every Nth
  sample, reports `thinned`, `sampleSlot` and each channel's `oldest` sample; `scope get` gives
  `sampleSlot` and `historyMin`; cc.py notes a window longer than the history.
- Directions: whatever acquisition is chosen, the export states its sample rate, mode and
  record span, and digital channels say they hold values.

## Constraints

- Speed: acquisition runs in the simulation thread on every node change. A simple circuit must
  not run slower by any measurable amount (the user's limit: 10 % is unacceptable); measure with
  `tools/sim_bench.sh` against main built the same way.
- Memory: four channels × record length × (value + time); 600 000 slots today.
- Old circuits load unchanged (`BufferSize`, `TrigAuto`, ... stay readable).
- GUI and CLI parity; headless works; every behaviour has a regression test in
  `tests/regress/`.
- The logic analyzer shares `PlotBase`: decide whether it follows.

## Open questions

1. Event-driven acquisition (store changes) or a fixed sample rate, or both (a rate for
   display, every settled step kept for measurements)?
2. Record length: in samples or in time; what default; what memory budget?
3. One time axis for all channels?
4. Acquisition modes to offer: normal, peak detect, average, roll / single shot?
5. A.c. coupling: a modelled high-pass (affects the waveform like a real input, with a
   settling time) or a display offset (the running mean)?
6. Measurements in SimulIDE (C++) or kept in create-circuit's `wave.py`?
7. What happens to `BufferSize` and the 1/5000 slot when the design changes?

## Decision log

(empty)

## Issues

| Issue | Subject | Summary |
|---|---|---|
| #133 | S5 | no a.c. coupling |
| #174 | S1, S2, S4 | flyback edge lost between sparse samples after a long run |
| #178 | S1 | event-driven samples, spacing = run length / events |
| #183 | S2 | low-frequency sweep window longer than the history |
| #198 | S6 | period of a chattering 99 % square (step artefact; `wave.py` flags it) |
| #202 | S3 | channels exported at different instants |
| #207 | S1 | ≈100 ns sampling floor |
| #217 | S7 | digital channels exported sparsely |
| #226 | S1, S4 | a 24 ns transient inside one sample |
| #256 | S4, S7 | decimated export dated edges wrong |
| #267 | S2 | rolling buffer loses the start of a long run |
| #296 | S1, S3 | readout skew from the sample spacing |
