# Test circuits

Circuits built while developing or checking a feature, kept so they can be
opened in the GUI and looked at (`tests/cli/smoke.sh` builds its own copies
through the CLI). Turn on Show Capacitor Charge and Inductor Current
(`settings set capCharge true`, `capLabel both`) for the charge / current ones.

| File | What it shows | Expected |
|---|---|---|
| `rc_charge.sim1` | 10 V, 1 kΩ, 100 µF (τ = 100 ms) | C1 63 % at τ, 99 % at 5τ ([capacitor-charge.md](../../specs/capacitor-charge.md)) |
| `cap_own_scale.sim1` | Two caps on one node, C2 with `ChargeScale=20` | Same voltage, C2 at half C1's percentage |
| `rl_step.sim1` | 10 V, 100 Ω, 1 H (τ = 10 ms), circuit `currentScale` 0.1 A | L1 63 % at τ ([inductor-current.md](../../specs/inductor-current.md)) |
| `lc_tank.sim1` | 1000 µF from 10 V into 10 H: 1.6 Hz, 0.1 A peak | Gauges trade places every quarter period; their squares add up to 1 |
| `reactive_bandpass.sim1` | 50 mH / 120 nF / 54 Ω series bandpass, 2055 Hz; 10 ns reactive step | Resistor/input RMS gain ≈0.99845; at 100 ns ≈0.98479, at 1 µs ≈0.86629 (expected backward-Euler damping, not physical winding resistance) |
| `transformer_bandpass.sim1` | Same bandpass, one transformer primary (`Primary=1`, `Secondary=0`); 10 ns reactive step | Gain ≈0.99841, including the winding's 2 mΩ; also checks the transformer's separate companion path |
| `transformer_1to2_load.sim1` | 10 V 50 Hz into a 1:2 transformer, 1 kΩ load | Secondary 19.8 mA at the sine peak; primary 2× that plus ~30 mA magnetising |
| `transformer_layouts.sim1` | Six secondary coils, and a rotated transformer | Gauge placement: labels never overlap; above/below when rotated |
| `relay_and_rl_decay.sim1` | Relay coil, and 10 mH / 10 Ω decaying from 0.5 A | Relay coil shows nothing; L1 at 37 % (e⁻¹) after 1 ms |
| `chip_theme.sim1` | LM555, 74HC00 as DIP and as logic symbol, ATtiny25, MT7833A, a resistor | IC bodies, pin names and pin-1 markers per theme (`export --theme Classic|Light|Dark`) |
| `text_labels.sim1` | Chip-theme circuit plus three Text labels: defaults, own background/border/white text, own red text | New defaults (no background, no border, name colour); own colours stay |
| `scope_trigger_level.sim1` | ±2.5 V 50 Hz sine on a scope, trigger on Ch1 at a fixed 1.5 V | Rising 1.5 V crossing on the centre line, dashed level line with an arrow; `--triglevel 4` free runs; `auto` triggers at 0 V |
| `led_strip.sim1` | Led Strip (12 × 2.75 V, 1 Ω, 300 mA per LED) from 38 V through 10 Ω | 227.3 mA, state ON; 40 V → 318 mA, OVER; 20 V → 0 (below the 33 V knee); `Strings` 2 → 437.5 mA |
| `lm334_current_source.sim1` | Current Source IC, model LM334: 12 V, 4.7 kΩ load on the high side, R_SET 68 Ω ([CURRENT_SOURCE_IC.md](../../docs/CURRENT_SOURCE_IC.md)) | 1.000 mA (67.7 mV/68 Ω = 0.996), V_R 64.0 mV, I_BIAS 59 µA, state REG; RL 15 kΩ → 0.94 mA, LOW V |
| `wire_colors.sim1` | Two resistors, a capacitor and ground; two nets with bands, one green wire ([wire-colors.md](../../specs/wire-colors.md)) | DSEN band 40 % / 12 px, ground band 60 % / 8 px, even at corners and dots (no darker overlaps); the top wire green; colours kept after save / reopen |
| `border_styles.sim1` | Rectangle, Ellipse, Line and Text with each line type (Solid, Dash, Dot, DashDot, DashDotDot) in a border colour, an Image with a dash-dot-dot border, and the same items as they were, black and solid ([gfx-border-style.md](../../specs/gfx-border-style.md)) | Each row left to right: the border in its colour with that line type (Line Type under Border, Border Color next to the fill colour; a Line's colour is its Color); the defaults as before: a black border, which shows on a light canvas only |
| `fuse_states.sim1` | Four Fuses (1 A) on 30 V ([fuse.md](../../specs/fuse.md)): T idle (1000 Ω), T at 2.75 In, F at 10 In, T in resistor mode at 2.75 In; Show Part States on | After 1.5 s: OK (green), HOT (amber, element thicker as θ rises; 93 % heat), BLOWN at 10.8 ms (red, element broken), resistor mode (dashed element, no state, never blows) |
| `history_ternary_and_debouncer.sim1` | Copy of Scherz Fig. 12.57b (switch debouncer with the built-in T_AND ternary AND), for `test_simctl_history_parts.py` | Going back (`sim history check`) replays bit for bit: the restore used to re-solve the restored step, and the diodes and zeners settled on another point within their tolerance |
| `photo_sensors.sim1` | Photodiode BPW34 reverse-biased at 5 V into 10 kΩ, phototransistor BPW85B with 1 kΩ from 5 V, both at 500 lx ([PHOTO.md](../../docs/PHOTO.md)) | D1 35 µA (0.35 V on R1); Q1 1.83 mA (collector 3.17 V); turn the dials to change the light |
| `parts_pane.sim1` | R1, R2, R10 (10 kΩ, Yageo MPN), R3 (4.7 kΩ, Cp 2 pF, locked), BJT Q1 2N2222, 74HC00, a scope, a fixed voltage, a rectangle and a tunnel ([parts-pane.md](../../specs/parts-pane.md)) | Parts tab: 6 of 10 rows (no scope, source, rectangle or tunnel; Meters shows the scope and the tunnel), order IC1 Q1 R1 R2 R3 R10, R3 with 🔒 and Tab —; Export BOM groups "R1, R2, R10"; a click on R3 on the canvas does not select it |
| `rotary_switch_lights.sim1` | Rotary Switch (2 poles, 4 positions: off, LED 1, LED 2, both) feeding two white LEDs through 330 Ω from 12 V ([ROTARY_SWITCH.md](../../docs/ROTARY_SWITCH.md)) | Position 1: both dark; 2: LED1 26.7 mA; 3: LED2 26.7 mA; 4: both. Drag or click the knob, or `ui dial SW 0..3` |
