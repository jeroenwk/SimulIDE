# Oscilloscope trigger level

Status: implemented (branch `feature/scope-trigger-level`).

## Why

The scope triggered only at an automatic level: the middle of the last cycle,
±15 % hysteresis. A signal such as a rectified mains bus or a gate drive could
not be triggered where the user wanted (a set voltage), as on a bench scope.

## Behaviour

- Per scope, saved in the circuit: `TrigAuto` (bool, default true, so old
  circuits are unchanged) and `TrigLevel` (V).
- Auto: unchanged.
- Fixed level, on the channel chosen as Trigger: a rising edge is armed once the
  signal is below level − hysteresis (the Filter setting, or 1 nV when it is 0)
  and fires when it reaches the level. The crossing time is interpolated
  between the two settled time steps around it.
- Display: the latest crossing with half a screen of data after it goes in the
  middle of the screen. No such crossing in the last 10 screens (the buffer
  holds about 12): free running.
- A dashed line at the level in the trigger channel's colour, with an arrow at
  the right edge (clamped to the edge when the level is off screen).
- Frequency readout and Auto scale keep using the automatic edges.

## Interfaces

- Expand panel: row "Trig level" with an Auto checkbox and a value box
  (disabled while Auto is ticked).
- CLI: `scope set <comp> --triglevel V|auto`; `scope get` reports `trigLevel`
  (`"auto"` or the number).
- Debug log, prefix `[ScopeTrig]`: level and mode changes, triggered / free
  running transitions. With `SIMULIDE_SCOPE_DEBUG` set also every arm and
  stored edge.

## Implementation

- `OscopeChannel::findTrigEdge` (simulation thread) keeps a ring of 64
  crossings at least 1/16 screen apart, so it spans 4 screens or more at any
  signal frequency. `trigEdgeBefore` returns the latest one at or before a time.
- `Oscope::fixedLevelEnd` (GUI frame) picks the display end from it.
- Changing the level, the mode or the trigger channel clears the ring.

## Not done

- Falling edge, and a trigger position other than the screen centre (the
  per-channel Time Pos still shifts a trace).
- Single / Normal sweep (holding the last trigger forever).

## Check

`tests/circuits/scope_trigger_level.sim1`; `tests/cli/smoke.sh` (trigger level
checks).
