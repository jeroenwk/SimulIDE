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
