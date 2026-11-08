# Regression tests

One test per bug, written to **fail on the buggy code and pass with the fix**. Every test starts its own headless SimulIDE on a private socket (it never touches a GUI that is open), drives it through the CLI with `tools/simctl.py`, and checks values with `sim.check`.

```sh
tests/regress/run.sh            # all tests
tests/regress/run.sh bh-003     # only the tests whose file name contains bh-003
python3 tests/regress/test_000_harness_divider.py
SIMULIDE=/path/to/simulide tests/regress/run.sh
```

Exit codes: 0 pass, 1 a check failed (or the test raised), 2 harness error (no binary, server did not start).

## Writing a test

- File name `test_<bug id>_<slug>.py`, e.g. `test_bh-003_diode_reverse_leak.py`; the bug ids come from the downtime bug hunt ledger (`../bughunt/BUGS.md` in the electronics folder), or use a short descriptive id.
- A docstring that says what the bug is, what the right result is and where that number comes from (formula, datasheet, hand calculation).
- `def test(sim):` builds or opens a circuit, runs it and calls `sim.check(...)`; end the file with `run_test(test)` (see `test_000_harness_divider.py`).
- Circuits the test needs go next to it as `test_<id>_*.sim1`, or are built through the CLI in the test.
- Expected values are physics (Ohm's law, time constants, datasheet values), not whatever the simulator printed. SimulIDE favours fast models over accuracy: a tolerance that only fails on a real bug, and a note in the docstring when the model is simplified on purpose.

## Safety nets

- **Timeout:** every CLI call gets `$REGRESS_TIMEOUT` seconds (default 120, wall time). A call that runs out raises `SimHang`, so a solver hang fails the test instead of blocking forever; pass `timeout=` to `sim.cli` for a longer run.
- **`sim.finite(name, comp)`:** fails when a pin voltage of `comp` is missing, NaN, infinite or beyond 1e6 V (BH-025..031 were found by eye before this).
- **Private copy:** `SIMULIDE_COPY=1` runs a temporary copy of the app (removed afterwards), so building the next fix in `build_XX` does not break a long run.
- **Data sync:** before the server starts, files in `resources/data` that differ from the built app's `data/` folder are copied into it (`make` copies them only at link time, so an XML-only fix was invisible to tests).

## Sweeps (`sweeps/`)

Whole-library checks that find a class of bugs in one run; each finding is a `FIND` line, and a crash or hang restarts the server and goes on. They take minutes to an hour; narrow them with `--parts REGEX`, `--start-at NAME`, `--limit N`.

| Command | Finds | Found |
|---|---|---|
| `sweeps/sweep.py roundtrip [--factor 0.1] [--order rev]` | properties that do not survive save + reopen when set far from their defaults | BH-001..003, BH-017..019 |
| `sweeps/sweep.py strings` | malformed text in string properties (separators, XML characters, unicode, long) | BH-012, BH-020 |
| `sweeps/sweep.py garble` | crashes when a saved attribute holds junk | BH-022..024 |
| `sweeps/sweep.py extremes` | NaN / huge voltages, hangs, crashes with numeric properties at 0 and −1 | BH-025..031 |
| `sweeps/sweep.py examples` | examples that fail to load, crash, hang or log solver faults in 20 ms | |
| `sweeps/shadowed_members.py` | data members redeclared in a derived class, hiding the base one (static, instant) | BH-005 |

A finding is a candidate, not a bug: check it against the code's intent before writing a test.
