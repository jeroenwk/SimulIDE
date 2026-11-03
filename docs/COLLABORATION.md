# Working together on circuits (user + Claude)

This is how the user and Claude design circuits together in SimulIDE.

- **The user** watches the SimulIDE window and gives instructions in chat.
  They may also select or edit parts in the GUI.
- **Claude** builds and runs the circuit by driving that same window with
  `simulide cli`. It checks the result by capturing images of the window and
  looking at them, and by reading measurements.

The command reference is in [CLI.md](CLI.md). This page covers the workflow and
the lessons learned so far.

## 1. Start the session (do this first)

The `/collab` command does all of this: it runs `sh .claude/skills/collab/start.sh`,
which finds the binary, reuses or launches the GUI, and prints the circuit
state plus a capture. The manual steps are:

```sh
cd /Users/jeroen.dezwart/perso/SimulIDE
B=$PWD/build_XX/executables/SimulIDE_1.1.0-SR2/simulide.app/Contents/MacOS/simulide
$B cli ping            # "pong" means a GUI is already running: use it
```

If nothing answers, launch the GUI with a visible window, detached so it
outlives the command:

```sh
nohup "$B" > /tmp/simulide-gui.log 2>&1 & disown; sleep 4; $B cli ping
```

- **Use the GUI, not `--headless`.** The user must see the window. Headless is
  only for tests or private checks, on a separate socket (section 6).
- **If the binary is missing**, build it first. See CLAUDE.md, which needs
  `export PATH=/opt/homebrew/opt/qt@5/bin:$PATH`, then `qmake && make`.
- **If the build changed** while a GUI was running, restart it (`$B cli quit`,
  then relaunch) so new commands are available. Ask the user first if the
  circuit has unsaved work.
- **Shell:** zsh doesn't word-split variables. Call `$B cli ...` directly,
  with `$B` holding only the path. Don't put `cli --socket x` inside a
  variable.
- **Long builds:** write them as a heredoc script with `$B cli - <<'EOF' ... EOF`.
  It stops at the first error.

## 2. The loop for each instruction

1. **Look before acting.** The user may have changed things in the GUI. Run:
   - `$B cli comp list`
   - `$B cli select`, which shows what the user selected and often what they
     are referring to
   - `$B cli capture circuit`, then read the PNG.
2. **Plan values and show the maths in one line.** For example: "ripple ≈
   I/(f·C) ≈ 0.85 V → 220 µF". Pick standard values (E12) and leave margin
   against ratings.
3. **Add parts.** Use `comp add <Name> --label X --at x,y [--rot 90] Prop=value`.
   - Find library names with `lib list --search`.
   - The reply lists every pin's position and the side it points to: plan the
     wires from it. `comp get X` shows the same pins, plus properties and units.
   - Add all parts first, then wire. Put them all in one `$B cli - <<'EOF'`
     script.
4. **Wire.** Use `wire add A.pin B.pin [--via x,y,...]`.
   - **Choose each junction yourself:** `wire add U1.Trg @160,64 --via ...`
     joins the wire through 160,64 at that point. Without `@`, connecting to a
     pin that already has a wire puts the junction in the middle of the segment
     next to that pin, which is rarely where you want it.
   - Draw the main line of a net first (e.g. the capacitor node from top to
     bottom), then join the branches to it with `@x,y`.
   - Act on every `note:`. It means a wire lies on another wire or runs over a
     pin. That looks connected but isn't.
5. **Check the result visually.** Run `capture circuit` and read the PNG. Look
   for crossing wires, wires running over parts, and polarity. Tidy up if it
   looks messy: the user is looking at it too.
6. **Run and measure.**
   - `sim run --for <time>` runs for a span of simulated time, then pauses.
   - Read results with `read A B`, `probe X.pin`, or `scope data SCOPE --last 40ms`.
   - Capture the scope with `capture circuit SCOPE`.
7. **Leave it live.** Run `sim resume` after measuring, so the user sees the
   circuit running. Only stop it when asked (`sim stop`).
8. **Report briefly:**
   - what changed,
   - a before/after table of the key numbers,
   - whether the result matches the prediction,
   - any surprise or hazard (ratings exceeded, polarity),
   - then ask what's next.

Point at things instead of describing them:

- `view show R1 D1` selects and zooms to parts.
- `view fit` shows everything.
- `select --none` clears the selection.
- Ask the user to select parts in the GUI, then run `select` to read them.

## 3. Layout conventions

- **Coordinates:** the grid is 8 px. Place parts on multiples of 8, and keep the
  signal flowing left to right with ground at the bottom.
- **Pin positions:**
  - **2-pin parts** (resistor, diode, capacitor) have pins at x ± 16 from the
    centre.
  - **`--rot 90`** puts `lPin` on top (y − 16) and `rPin` at the bottom (y + 16).
  - **Ground:** the `Gnd` pin is 16 px above the symbol centre.
- **Wire placement:** put series parts on the existing straight line between two
  points, and parallel parts 48 px above or beside.
- **Scope:** place it to the right (e.g. `--at 320,0`). Its input pins are on
  its left edge (`Pin0` … `Pin3`, and `PinG` for ground).
- **Don't over-plan.** Pick positions on the grid so pins line up (same x or
  y), add the parts, read the pin positions from the replies, and wire. Fix
  details after the capture. A crossing that the pinout forces is fine: a
  crossing without a dot isn't a connection.
- **Inserting a part into an existing wire:** remove that one wire with
  `wire remove Connector-N`, add the part, and wire both sides. Removing a wire
  can dissolve a junction: the two remaining pieces merge into one new wire,
  which `wire remove` reports under `merged`. Use that new uid from then on.
  Removing several wires in one command is safe.
- **After moving parts**, re-check the picture. Re-routing is often cleaner than
  keeping the old wires.

## 4. Part-specific knowledge (learned the hard way)

| Part | Note |
|---|---|
| Any add/remove | Stops the simulation. Restart it afterwards (`sim start` / `sim run`) |
| Push | Pins `pinP0`, `switch0pinN`. Use `ui press` and `ui release`. A held press survives a simulation restart |
| Switch | Use `ui set S on\|off` or `ui click` |
| Current flow | `settings set currentFlow true` shows moving dots on the wires (direction = conventional current; `currentMin` sets the smallest current shown). Confirm with numbers via `wire current` |
| LED | `MaxCurrent` defaults to 30 mA. SimulIDE flags `overloaded` only well above that, so check the current yourself. A dark (black) LED means it's off, not a drawing bug |
| Wave Generator (`WaveGen`) | Its ground is internal unless `Floating=true`, so only `outnod` is visible. It has an on/off button: set `Always_On=true`. A sine is set with `Freq=50Hz Semi_Ampli=10V` |
| Electrolytic (`elCapacitor`) | `lPin` is + (the straight plate) and `rPin` is − (the curved plate). With `--rot 90`, + is on top. Verify with `capture circuit C1 --scale 6 --margin 12` |
| Oscope | The default display (1 ms/div, 1 V/div) clips most signals. Set it with `scope set SCOPE --timediv 5ms --voltdiv 5 --trigger 0`, then check the trace in `capture circuit SCOPE`. `scope get SCOPE` shows the settings |
| LM555 (`Lm555`) | DIP outline. At `--at x,y`: Gnd, Trg, Out, Rst on the left at x−8, y+8 … y+32; Vcc, Dis, Thr, CV on the right at x+40, y+8 … y+32. Its output high is about Vcc − 1.3 V. A crossing between the reset (Rst→Vcc) wire and the trigger/threshold wiring can't be avoided |
| BJT | At `--at x,y`: base x−16,y (left), collector x+8,y−16 (up), emitter x+8,y+16 (down). The default `Gain` is 100, the low end for small-signal NPNs (2N3904 typ. 200). Set it to match the part |
| Rail | `--rot 0` puts the pin on the right at x+16. The default rotation (90) puts it below at y+16 |
| Labels with spaces (e.g. "Arduino Uno") | Use the uid instead (`Uno-1`) |
| Values | Use engineering notation (`220u`, `4k7`, `1M`). A wrong unit is an error, which is good. Choice properties list their `options` in `comp get` |
| Hidden properties | `comp get X --all` lists them; `comp set` changes them |

## 5. Saving

- Save to `samples/<descriptive_snake_case>.sim1`, for example
  `samples/half_wave_rectifier_filtered.sim1`. Use `circuit save <path>`
  (relative to the shell's directory).
- **Verify a saved file** without touching the user's window: open it in a
  private headless instance (section 6), run it, and compare a key reading.
- Commit or push only when the user asks.

## 6. Private checks (without disturbing the user)

```sh
$B --headless --socket verify >/dev/null 2>&1 & P=$!; sleep 2
$B cli --socket verify circuit open samples/x.sim1
$B cli --socket verify sim run --for 300ms
$B cli --socket verify read RL
$B cli --socket verify quit --discard; wait $P
```

## 7. Known CLI gaps (fix candidates)

None known. Add any you find here, with a workaround.
