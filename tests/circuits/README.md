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
| `transformer_1to2_load.sim1` | 10 V 50 Hz into a 1:2 transformer, 1 kΩ load | Secondary 19.8 mA at the sine peak; primary 2× that plus ~30 mA magnetising |
| `transformer_layouts.sim1` | Six secondary coils, and a rotated transformer | Gauge placement: labels never overlap; above/below when rotated |
| `relay_and_rl_decay.sim1` | Relay coil, and 10 mH / 10 Ω decaying from 0.5 A | Relay coil shows nothing; L1 at 37 % (e⁻¹) after 1 ms |
| `chip_theme.sim1` | LM555, 74HC00 as DIP and as logic symbol, ATtiny25, MT7833A, a resistor | IC bodies, pin names and pin-1 markers per theme (`export --theme Classic|Light|Dark`) |
| `text_labels.sim1` | Chip-theme circuit plus three Text labels: defaults, own background/border/white text, own red text | New defaults (no background, no border, name colour); own colours stay |
| `led_strip.sim1` | Led Strip (12 × 2.75 V, 1 Ω, 300 mA per LED) from 38 V through 10 Ω | 227.3 mA, state ON; 40 V → 318 mA, OVER; 20 V → 0 (below the 33 V knee); `Strings` 2 → 437.5 mA |
