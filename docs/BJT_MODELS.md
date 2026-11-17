# BJT part models

The BJT's **Model** property picks a real transistor (2N2222A, 2N2907A, BC546C, BC556C, BC547B,
2N3904, BD139, TIP31C, ...). Choosing one sets PNP, Gain, IS (and so the Threshold), VA and the
junction capacitances Cje/Vje/Mje, Cjc/Vjc/Mjc. Editing one of those values afterwards switches
Model to **Custom**: the values stay, the name goes. BVceo and BVebo are not part of a model and
can be set on top of one. A new BJT, and every circuit saved before models existed, is **Custom**
with the old defaults (gain 100, IS 1e-13 A), so existing circuits simulate exactly as before.

## Where the values come from

`resources/data/bjts.model` is **generated** by `tools/bjt_models.py` (do not edit it by hand)
from verbatim SPICE lines of LTspice's `standard.bjt` kept in
`tools/bjt_models/standard_bjt_subset.lib`. To add a part: copy its `.model` line into that file,
add a row to `PARTS` in the script (a part with no published model gets a line derived from its
datasheet in `tools/bjt_models/datasheet_bjt.lib`, which says how it was made), run `python3 tools/bjt_models.py` (`--check` exits 1 when the
data file is out of date), rebuild (the build copies `resources/data`).

SimulIDE's BJT is Ebers-Moll with Early (`e-bjt.cpp`): one IS, one forward gain, a fixed reverse
gain of 1. It has no ISE/NE (gain falling at low current), IKF (gain falling at high current), NF,
base/emitter/collector resistances or transit time. A Gummel-Poon BF is the *peak ideal* gain, often
far above the real hFE (the Fairchild 2N4401 has BF = 4292), so BF and IS are not copied. The script
solves the full Gummel-Poon DC equations at a reference collector current (VBC = 0, 27 °C) and sets

- **Gain** = IC/IB there, the real hFE at that current;
- **IS** = IC/(exp(VBE/Vt) − 1) with VBE at the terminals (RB with IRB/RBM and RE drops included).

Reference current: 1 mA for small-signal parts (the bias of most book circuits), 10 mA for the
0.5–0.8 A switching parts (2N2222/A, 2N2907/A, 2N4401/4403, BC327/337), 100 mA for power parts
(BD13x, TIP3x/4x, 2N3055). Each line of `bjts.model` says its reference current, VBE there, the
SPICE BF and the Vceo rating. Away from the reference current the gain stays constant, as for any
SimulIDE BJT: the real part's hFE falls at very low and high currents.

VAF and the junction capacitances are copied (SPICE defaults VJ 0.75 V, MJ 0.33 when absent).
Diffusion capacitance (TF) is not modelled, so switching times and fT come out faster than the real
part. Vceo is a rating, not a breakdown voltage, so it is not applied to BVceo.

## Parts without a model of their own [INFERRED]

| Model | Values of | Why |
|---|---|---|
| 2N2907A | LTspice `2N2907P` (NSC-Philips) | the National/Fairchild 2N2907A process model, the complement of the `2N2222A` (Fairchild) line |
| BC546C | `BC547C` | BC546 is the 65 V selection of the BC547 die; LTspice's own BC546B is its BC547B with Vceo 65 |
| BC556C | `BC557C` | the same for BC556 / BC557 |
| 2N3055 | `2N3055H` (Motorola) | the plain `2N3055` line uses old SPICE2 names and an IS of 2.4e-8 A |
| 40250 | `2N3055H` scaled to a 4 A die (area 4/15), BF 341 | RCA hometaxial TO-66 part (VCEO 40 V, 4 A, 29 W) with no published SPICE model; BF chosen so hFE = 50 at 1.5 A, the geometric mean of the datasheet's 25–100 (Central 40250 datasheet). Gain is taken at 1.5 A (the datasheet's only hFE point), IS at 100 mA (VBE 0.68 V there); at 1.5 A the real VBE is near 1.0 V, SimulIDE gives about 0.75 V |

## Fitted values (reference current, hFE, VBE)

Run `python3 tools/bjt_models.py` to print them. Examples: BC547A/B/C 174 / 282 / 452 at 1 mA
(datasheet hFE groups 110–220 / 200–450 / 420–800), 2N2222A 174 at 10 mA (datasheet 100–300 at 150 mA),
2N4401 173 at 10 mA. Results with a model are [SIMULATED]; compare with a measurement keeping the
spread of the hFE group in mind.

## Limits

- Undo of a Model change restores only the name: undoing back to *Custom* keeps the model's values.
- Parts removed on purpose: MJE2955 (its LTspice line is the same old 2N3055-style model, VBE 0.40 V at 100 mA).
