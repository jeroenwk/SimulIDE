# Rotary switch

A native part (`src/gui/circuitwidget/components/switches/rotaryswitch.{h,cpp}`), Switches in the component list: 1 to 4 poles, 3 to 12 positions, 30° per detent. It models the deck of the SR25 / SR26 family (Alpha SR26 datasheet: changeover angle 30°, 1 to 4 poles with 3 to 12 positions set by the maker, contact resistance 50 mΩ max.), which the light switch of a microscope belongs to (`electronics/microscope/specs/SPEC.md` §4). Poles × positions is at most 12: one contact per pole and position, as on the deck.

The `Rotary_SW4`, `Rotary_SW8` and `Rotary_SW16` of Logic / Tools are something else: a keypad selects which of 4, 8 or 16 inputs reaches one output.

| Property | Default | |
|---|---|---|
| `Poles` | 1 | 1 to 4. Each pole has its common (`A`, `B`, `C`, `D`) and one throw per position. |
| `Positions` | 4 | 3 to 12. Clamped so that poles × positions is at most 12: 2 poles go up to 6, 3 poles to 4, 4 poles to 3. |
| `Position` | 1 | Where the knob stands, 1 to `Positions`. Saved with the circuit; it is the part's shown value. |
| `Shorting` | no | Yes: make before break. No: break before make. See "Timing of a step". |
| `Key` | none | A capital letter. Shift + that letter turns the switch one detent clockwise while the simulation runs. |
| `Contact_Res` | 50 mΩ | Resistance of a closed contact. |
| `Transit_Time` | 0 ms | Not shorting: how long the pole touches nothing. |
| `Overlap_Time` | 1 ms | Shorting: how long both throws are connected. |
| `Bounce_Time` | 0 ms | Each contact chatters for this long when it changes. 0: a clean change. |
| `Bounces` | 5 | Changes of the chatter. |

## Pins and look

A round body with the knob in the middle. The commons are the pins on the left. The throws are named after their pole and position (`A1`, `A2`, ... `B1`, ...) and placed, in that order, on the right, the top and the bottom: as even as the count allows, the right taking the remainder first. Each name lies inside the rim next to its pin, horizontal whatever side the pin is on.

| Poles × positions | Right | Top | Bottom | Body |
|---|---|---|---|---|
| 1 × 3 | A1 | A2 | A3 | 80 px |
| 1 × 4 | A1, A2 | A3 | A4 | 80 px |
| 1 × 5 | A1, A2 | A3, A4 | A5 | 80 px |
| 1 × 6, 2 × 3 | 2 | 2 | 2 | 80 px |
| 2 × 4 | A1, A2, A3 | A4, B1, B2 | B3, B4 | 80 px |
| 2 × 5 | A1–A4 | A5, B1, B2 | B3, B4, B5 | 96 px |
| 1 × 12, 2 × 6, 3 × 4, 4 × 3 | 4 | 4 | 4 | 96 px |

Pins on a side are 16 px apart; a wire connects 8 px outside the body. The body is 80 px across, or 96 when a side carries 4 pins (also with 4 poles): then the names of two neighbouring sides do not meet in their corner.

The knob is a dial widget of 40 px. Its pointer stands at −(positions − 1) × 15° + 30° × (position − 1), clockwise from 12 o'clock: the sweep is centred on 12 o'clock, with a tick per detent. However the part is turned or flipped, the knob stays in the middle of the body, upright and unmirrored, and the names stay horizontal and unmirrored (they sit where their pins are). An item in Qt turns about its `transformOriginPoint` and mirrors about its corner; the part undoes its own turn and mirror on each of them (`placeLabels()`, `placeKnob()`).

## Using it

- Drag the knob round: it stops at the detents.
- Click it: one detent clockwise; Shift + click: one detent the other way.
- Turn the mouse wheel over it: down is clockwise.
- Arrow keys when it has the focus; Shift + `Key` while the simulation runs.
- `Position` in its properties.
- CLI: `ui dial SW <0..positions−1>` (the value is the position minus one; a value past the last position is refused), `comp set SW Position=2`, `ui key S tap` (its `Key`), `ui click <x of the knob>,<y of it>`. `ui controls SW` lists it as a dial.

Changing `Poles` or `Positions` rebuilds the pins, as on the Switch, and stops a running simulation. If the knob stood beyond the new count it comes back to the last position.

## Contacts

Each contact is a resistor between its pole's common and its throw: 1 / `Contact_Res` when made, 0 when open. In position n every pole's common is connected to its throw n: `A` to `A3`, `B` to `B3`, ... The commons are not connected to each other.

### Timing of a step

A step made while the simulation runs takes the switch's timing; when the simulation is stopped the contacts follow the knob at once.

- **Break before make** (`Shorting` no): every contact opens at once; after `Transit_Time` the new throw makes.
- **Make before break** (`Shorting` yes): the new throw makes at once; after `Overlap_Time` the other one opens. A pole that had no contact made (coming from an open position) has nothing to open.
- **Chatter**: when `Bounce_Time` is more than 0, the contact that changes chats: it is closed, then changes `2 × Bounces` times (ending closed, at a make) or `2 × Bounces + 1` times (ending open, at a break), within `Bounce_Time`, the gaps shrinking by 0.7 each time, as on the Switch. The other contacts change cleanly.
- A second step while one is under way cuts it short and starts from the contacts as they are then.

The simulator keeps one event per element, so the part has one timer: it is cancelled when a step is cut short.

## State (keyframes, going back)

`simState()` saves the position asked for, the position the mechanism is at, what it is waiting for and the chatter's count; the contacts save their own admittance. A turn while the circuit runs is an input, as a click on a button: after going back to a time before it, the knob is where it was then, and running on does not turn it again.

## Tests and demo

- `tests/regress/test_rotary_switch.py` (186 checks): the contacts in every position for 1 × 3, 1 × 4, 1 × 12, 2 × 6, 3 × 4 and 4 × 3; the limits of poles and positions; the contact resistance; the timing of a step (transit, overlap, chatter at the make and at the break, two quick steps); save and reopen; a `Key`; saved key frames and `sim history check`; the knob's place and the pins' grid in 24 turn and flip states.
- `tests/circuits/rotary_switch_lights.sim1`: the lights of the microscope (12 V, two LEDs of 330 Ω each, off / LED 1 / LED 2 / both).
