---
name: collab
description: Start a collaborative circuit session. The user watches the SimulIDE GUI while Claude builds, runs and measures circuits in it via `simulide cli`, checking every step with image captures. Use when the user wants to build, try or explore circuits together.
---

# Collaborative circuit session

1. **Start or reuse the GUI and get the state** in one call:
   ```sh
   sh .claude/skills/collab/start.sh
   ```
   - It prints `B=<binary path>`. Use `$B cli ...` for every command afterwards: set `B=...` at the start of each Bash call.
   - If it says NOT BUILT, build first (see CLAUDE.md) and rerun.
   - It launches the GUI with a visible window if none is running, or reuses the open one. Never use `--headless` for the shared window.

2. **Read the workflow spec** `docs/COLLABORATION.md` if it isn't already in context. It covers:
   - the per-instruction loop,
   - layout conventions,
   - part-specific gotchas (WaveGen, electrolytic polarity, scope `TimDiv`/`VolDiv`, ...),
   - saving to `samples/`.

   The command reference is `docs/CLI.md`; use `$B cli help <cmd>` for single commands.

3. **Look at the capture** the script saved (the path is on its `file:` line) with the Read tool, so you see what the user sees.

4. **Greet the user briefly.** Say what's on screen: the circuit name, the parts, whether the simulation is running, and anything they selected. Then ask what to build or change. If they already gave an instruction with `/collab`, start on it right away.

For each instruction, follow the loop in the spec:
- look first (`comp list`, `select`, capture);
- plan the values;
- add and wire the parts;
- capture and check;
- run and measure;
- leave the simulation running;
- report with a before/after table.
