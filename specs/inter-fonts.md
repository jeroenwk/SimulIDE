# Spec: Inter + JetBrains Mono, embedded in SVG exports

Status: **implemented** (2026-11-05), not yet committed. Target: SimulIDE 1.1.0-SR2 (this fork).
Font choice made with the mock page `https://claude.ai/artifact/X5GZ6Th6169AmCykrJuivV`
(specimens, the real `led_driver/sim/board_mains` export, a GUI mock).

## 1. Goal

- SimulIDE uses one sans and one mono font, bundled with the app, on Linux, macOS
  and Windows: the GUI, circuits and exports look the same everywhere.
- An SVG export shows the same fonts in any browser or viewer, whatever is
  installed there, because the fonts it uses are embedded in the file.

| Role | Font | Licence |
|---|---|---|
| GUI, component ids and values, Text component, meters, plots | **Inter** 4.1 | SIL OFL 1.1 |
| Pin and chip labels, code editor, serial console, memory tables, Messages, part-state badges | **JetBrains Mono** 2.304 | SIL OFL 1.1 |

Both licences allow bundling and embedding in documents. The licence texts go in
`resources/fonts/` next to the fonts.

### Out of scope

- Icons (`src/icons/**/*.svg`) that contain text in Ubuntu Condensed. They are
  drawn by QSvgRenderer and fall back to another font; they stay as they are.
- CJK text (zh_CN translation): Inter has no CJK glyphs, Qt falls back to a
  system font, as it did with Ubuntu.
- Letting the user pick fonts per role. The existing `fontName` setting stays
  as a GUI-font override (section 4).

## 2. What exists today

- `MainWindow` loads six Ubuntu / Ubuntu Mono TTFs from `application.qrc` and
  sets the app font from the `fontName` setting (default `Ubuntu`, always
  written back to `simulide.ini`). Component labels, plots, meters, serial
  widgets use `MainWindow::defaultFontName()`.
- `"Ubuntu Mono"` is hard-coded in 15 places (pins, chips, editor, console,
  memory widgets, Messages, three sensor parts) and in two `.ui` files;
  `"Ubuntu"` in `appdialog.ui`, `propdialog.ui`, `textval.ui`.
- Text component, Voltmeter/Ammeter and Frequency meter use
  `"Helvetica [Cronyx]"`, an X11 font name that exists nowhere today, so the
  result depends on the OS fallback. The Text component saves this name in the
  `.sim1` (`Font="Helvetica [Cronyx]"`).
- Part-state badges use `"Menlo"` (macOS only).
- `CircuitExport::write()` (SVG branch) uses QSvgGenerator, which writes the
  family name as is (`font-family="Ubuntu"`). A browser without Ubuntu shows a
  different font, and labels no longer fit where SimulIDE placed them.

## 3. Fonts in the app

- New `src/gui/appfonts.{h,cpp}`, namespace `AppFonts`:
  - `registerFonts()`: adds every bundled TTF with
    `QFontDatabase::addApplicationFont`, logs each family/file loaded or failed.
  - `sans()` = `"Inter"`, `mono()` = `"JetBrains Mono"`.
  - `normalize( family )`: maps legacy names to the bundled ones:
    `Ubuntu`, `Helvetica [Cronyx]`, `Helvetica`, `Sans`, `Sans Serif`, empty → sans;
    `Ubuntu Mono`, `Menlo`, `Monospace`, `Courier` → mono; anything else is kept.
- Every hard-coded family in C++ uses `AppFonts::sans()` / `AppFonts::mono()`;
  `.ui` files say `Inter` / `JetBrains Mono`.
- Text component: default font `Inter` for new ones. Existing circuits are **not**
  migrated on load: a Text component keeps the font saved in the `.sim1`. The
  canvas right-click menu has **Convert Fonts to Default** (CLI `circuit fonts`,
  `Circuit::convertFontsToDefault()`): every Text component whose font is not a
  bundled one gets Inter, or JetBrains Mono when `normalize()` maps its name to
  mono. One undo step. All other circuit text (ids, values, pins, chips, meters)
  comes from code and switches with the app.
- Weights bundled: Inter Regular, Medium, SemiBold, Bold; JetBrains Mono
  Regular, Medium, SemiBold, Bold, Italic (the old Ubuntu Mono set had italics).
- JetBrains Mono is about 20 % wider than Ubuntu Mono (0.6 em vs 0.5 em). To keep
  labels inside packages:
  - `Pin` scales every label size by 5/6, minimum 4 px (default 7 → 6 px,
    9 → 8 px), so the sizes passed by parts stay as they were.
  - The chip's vertical name drops its 120 % letter spacing on Unix/macOS
    (100 % gives the old width; SVG exports carry no letter spacing either).
  - DHT22 / DS18B20 / DS1621 displays: 10 → 8 px, 6 → 5 px.

## 4. The `fontName` setting

- Default becomes `Inter`. A saved value that `normalize()` maps to sans
  (e.g. the old default `Ubuntu`) is replaced by `Inter` at start, with a
  log line, so existing installs switch without editing `simulide.ini`.
- Any other saved value (a font the user picked in the App settings) is kept and
  used for the GUI and component labels, as today. Such a font is not embedded
  in SVG exports (section 5).

## 5. SVG export with embedded fonts

After QSvgGenerator has written the file, `CircuitExport` post-processes it:

0. QSvgGenerator writes Qt 5 weights times ten (Medium 57 → `570`, chip labels
   65 → `650`). Each is replaced by the CSS weight of the face Qt draws with
   (nearest of Qt's 0/12/25/50/57/63/75/81/87 → 100…900), else a browser picks
   another face (570 → SemiBold instead of Medium).
1. Collect the `font-family` / `font-weight` / `font-style` combinations used.
2. For each combination whose family is a bundled one, pick the closest
   bundled face (same style, nearest weight) and add one `@font-face` rule with
   the font as a base64 `data:font/woff2` URI, in a `<style>` element inside
   `<defs>`. Each face is embedded once.
3. Replace each `font-family="Inter"` by a stack
   `Inter, 'Helvetica Neue', Arial, sans-serif` and
   `font-family="JetBrains Mono"` by
   `'JetBrains Mono', Menlo, Consolas, 'DejaVu Sans Mono', monospace`, so a
   viewer that strips embedded fonts still shows a similar font.
4. Log the faces embedded and the byte sizes.

- The embedded files are WOFF2 copies of the bundled subsets (smaller than
  TTF); Qt itself uses the TTFs.
- Families that are not bundled are left as they are.
- New `ExportOptions::embedFonts` (default true). The Export dialog gets an
  "Embed fonts" checkbox, the CLI `export` command `--no-embed-fonts`. PNG
  export is not affected.

## 6. Font files and subsetting

- `tools/fonts_subset.sh` downloads the pinned releases (Inter 4.1, JetBrains
  Mono 2.304) to a cache outside the repo, subsets them with `pyftsubset`
  (fonttools, through `uv`) and writes:
  - `resources/fonts/ttf/*.ttf` (used by the app, listed in `application.qrc`)
  - `resources/fonts/woff2/*.woff2` (embedded in SVG, also in the qrc)
  - `resources/fonts/OFL-Inter.txt`, `OFL-JetBrainsMono.txt`
- Character set: Basic Latin, Latin-1, Latin Extended-A/B (cz, sk, tr, az),
  Greek (Ω, µ, α…), Cyrillic (ru), general punctuation, super/subscripts,
  currency, letterlike symbols (Ω ohm sign), arrows, mathematical operators,
  geometric shapes; JetBrains Mono also box drawing and block elements.
- OpenType features kept: `kern`, `ccmp`, `locl`, `mark`, `mkmk`. `calt` and
  `liga` are dropped so `->`, `!=`, `<=` stay plain characters in pin labels,
  code and the console.
- The Ubuntu TTFs are removed from `resources/fonts/` and `application.qrc`.

## 7. Tests

- Build, start the GUI: menus, component list, property editor, code editor and
  serial monitor show Inter / JetBrains Mono (`[Fonts]` log lines confirm the
  families loaded).
- Open `led_driver/sim/board_mains.sim1` and examples with ICs (e.g. an Arduino,
  a 74xx logic chip, an MCU): pin labels fit inside the chip outlines.
- An old circuit with Text components (`Font="Helvetica [Cronyx]"`) keeps that
  font; `circuit fonts` converts them to Inter, `undo` restores them.
- `simulide cli export --file x.svg`: the file contains `@font-face` rules for the
  faces used only, and `font-family` stacks; rendered by WebKit (QuickLook) and
  Chrome it matches `capture circuit`.
- `--no-embed-fonts`: no `<style>`, stacks still present.
- `tests/cli/smoke.sh` passes.

## 8. Order of work

1. Font files: `tools/fonts_subset.sh`, generated files, qrc, licences.
2. `AppFonts` + replace all hard-coded families (C++ and `.ui`), `fontName` migration.
3. Visual check of pins and chips; adjust sizes/spacing if needed.
4. SVG embedding + `embedFonts` option in dialog and CLI; `docs/CLI.md`.
5. Tests of section 7; `CLAUDE.md` note on fonts.

## 9. Results

- `led_driver/sim/board_mains.sim1`: export embeds Inter 400/700 and JetBrains
  Mono 400/700 (Inter 600 too after converting its Text components); the SVG
  grows from about 580 KB to 820 KB. WebKit (QuickLook) shows the same fonts
  and layout as `capture circuit`.
- `circuit fonts` on that circuit: 4 Text components converted; `undo` restores
  `Helvetica [Cronyx]`, `redo` applies Inter again.
- MT7833A, 7490, Arduino Uno, DHT22 checked at 6× zoom: labels fit.
- A saved `fontName=Ubuntu` in `simulide.ini` is read as Inter.
- `tests/cli/smoke.sh`: 275 passed, 0 failed.

## 10. Dialog sizes with Inter (follow-up)

- Inter is taller and wider than Ubuntu at the same size, which showed up in dialogs:
  - **Settings**: the tab widget had a fixed 300 px minimum from the `.ui` and the
    tabs had height caps (App 350 px), so Qt squeezed combos, buttons and the
    colour grid. Now each tab is sized by its own contents (hidden tabs ignored,
    no caps, no `adjustSize()` 2/3-screen cap). App 420 px, Circuit 731 px,
    Simulation 466 px on a 13 px font.
  - **Colors**: preset and Reset on one fixed row; the swatches in their own
    scroll area, about 6 rows high.
  - **Property dialogs and fields** derived their scale from the width of a
    space (`fm.width(" ")/2`). Inter's space is 0.281 em against Ubuntu's 0.231 em,
    so dialogs grew 33 % and fields (integer division: 3/2 → 4/2) 100 %. The six
    places now use `AppFonts::legacySpaceWidth()`: Ubuntu's space width at the
    font's pixel size, so sizes are as before. The row height is the larger of the
    old estimate and the rows' layout minimum.
- New CLI commands to check dialogs: `view props <comp> [--tab] [--close]`,
  `view export`, `capture window props <comp>`, `capture window dialog <title>`.

## 11. Component list colours (follow-up)

- Categories (component list and Manage Components) had dark-yellow text on a
  lavender background, which dark mode turned into yellow on purple. Now: no
  background, purple text (85,0,127, as the Settings headings; light purple in
  dark mode).
- The current row (the list's items are not selectable: it is the clicked one) and
  selected rows get a solid grey fill with strong contrast, as Xcode's navigator:
  dark 76,76,82 with focus, 58,58,62 without; light 200,200,206 / 222,222,226.
  No accent blue, no Fusion focus frame; text and icons keep their colours (the
  grey is drawn as the item's background, since a selected item's icon is tinted
  with the accent blue). Widgets with the property `uiGraySelection` get this in
  `UiStyle`: the component list, the file explorer and Manage Components.
- Other yellows in the GUI are kept on purpose: warnings in Messages, editor
  breakpoint/debug marks, scope channel 1, the canvas themes.
- New CLI: `lib select <name>` (select and focus an item in the component list),
  `capture window library`.
