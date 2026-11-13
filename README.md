# opman

Math notes that calculate as you type. A native C++20 / Dear ImGui editor: type text and math together, get live
results (`f(4) =` offers `17`), plot, tabulate and solve, with vim keys or ordinary ones. Notes are `.opman` files you
can keep anywhere; F1 in the app explains everything.

## Files

Each note is one `.opman` file (JSON: text, math, tables, and pictures packed in as base64), so it can be copied,
mailed or synced on its own. A new note is *Untitled* until the first **Save** (Ctrl-s, `:w`) asks for a name and a
folder; from then on it autosaves to that file (Settings can turn that off) and its file name is its name. **Save as**
(Ctrl-Shift-s, `:saveas`, `:w path`) puts a note anywhere, **Open** (Ctrl-Shift-o, `:open`) opens one from anywhere,
and the sidebar lists the notes folder (`~/Documents/opman`, File > Open folder changes it) plus recently opened notes.
Closing with unsaved changes asks first, and unsaved changes are recovered after a crash. The installers
register `.opman`, so a double-click opens it. Older corca `.json` notes are converted on first sight (the original
goes to `.old/`). Open/save dialogs are the system's (Linux: zenity or kdialog), else a built-in browser.

## Settings, help, commands

- **Settings** (Ctrl-, or the gear): theme, text size, page width, vim or standard keys, CapsLock-as-Escape, live
  results, sliders, autosave delay, significant digits, degrees/radians, exact/decimal results, decimal point or comma, default plot range,
  library and export folders, the pdflatex command, system or built-in dialogs.
- **Help** (F1 or ?): every feature, searchable.
- **Command palette** (Ctrl-Shift-p): any action, setting, note or help topic by name.

## Build

```sh
nix develop -c cmake -B build -G Ninja
nix develop -c cmake --build build
./build/opman          # the app
./build/opman_check    # self-checks
```

Windows installer: `packaging/build-windows.sh` (NSIS, cross-built with mingw).

## Keys

| | |
|---|---|
| `i a I A o O` | Insert; `Esc` / `Ctrl-[` / CapsLock (caps:escape) back to Normal |
| `h j k l w b e 0 ^ $ gg G %` | motions; `h`/`l` step into and out of fractions, roots, scripts |
| `f t F T ; ,` | find a character in the line |
| `/ ? n N` | search (smartcase) |
| `d c y` + motion, `dd cc yy x D C S J r p P u Ctrl-r .` | editing, as in vim |
| `v V` | Visual, Visual line |
| `Space` | formatting leader: `b i u s c h` style, `^ _` super/subscript, `R O G T B P A` colour, `S +` small/large, `[ \\ ]` align, `r` clear, `1 2 3` headings, `- n > t` lists, `x` tick, `\|` table, `/` divider, `=` section, `m` math |
| `Enter`, `gf`, `Ctrl-o` | follow a `[[link]]`, go back |
| `Ctrl-m` | math mode: everything typed is math. Outside it everything is plain text (`:set automath` brings back math recognised in text) |
| `Shift-Enter` | continue a long calculation (or sentence) in the next line; it stays one line with one result. Lines too long for the page wrap by themselves, math before an operator |
| `Tab` `Shift-Tab`, `>>` `<<` | indent a list item or line |
| `Ctrl-Shift-l` `e` `r` | align left, centre, right |
| `Ctrl-v` (Insert), `p` | paste from the system clipboard; yanks are copied to it as Markdown |
| `Ctrl-w`, `Ctrl-\` | focus / toggle the file list |
| `Ctrl-Shift-p`, `F1`, `Ctrl-,` | command palette, help, settings |
| `Ctrl-=` `Ctrl--` `Ctrl-0` | text size |

**Standard keys** (Settings, or `:set standard`): always typing, Esc only closes the popup, Ctrl-z/y undo/redo,
Ctrl-c/x/v clipboard, Ctrl-f find.

Ex commands: `:w [path] :saveas [path] :open [path] :q :q! :wq :e <title|path> :new [title] :rename <title> :trash
:folder :export [md|html|tex|pdf|path] :image <path> :table [rows] [cols] :rule :section [title] :s/a/b/g :%s/a/b/g :noh
:set dark|light|degrees|radians|decimals|fractions|comma|point|digits=N|vim|standard :help [topic] :settings :palette :outline
:zoom in|out|reset`.

In Insert mode the popup offers completions: Tab takes a computed result or a program function, Enter takes the entry
for a math word (after ↓ / Ctrl-n for a text word, so Enter after a plain word still starts a new line), Ctrl-1…9 any.

## Blocks

Notes stay plain text lines (corca-compatible JSON). Type `plot`, `table`, `section` or `divider` alone at the start of a
line and take it from the popup with Tab (they're the *Program* functions there), or use the toolbar or Space leader:

| Line | Renders as |
|---|---|
| **plot** `sin(x), f(x), x^2/4, −3 < x < 3, −1 < y < 1` | a graph of each expression; x and y ranges optional. `plot table` draws the nearest table above (column 1 is x), `fit` / `fit 2` / `fit exp` a least-squares curve through it. Parametric `(cos(t), sin(t))`, polar `r = 1 + cos(θ)` and implicit `x^2 + y^2 = 4` curves too. Drag pans, the wheel zooms, a double-click resets |
| | Zeros, minima, maxima and intersections are marked on `y = f(x)` curves; hover over a mark or a curve to read its values (Settings > Math > Plots, `:set marks`, `:set nomarks`) |
| **plot** `sin(x) cos(y)`, `z = x y`, `(cos(t), sin(t), t)` | 3D: a surface over x and y, or a space curve; drag to turn it |
| `=== Exercise 2` (Space =, `:section`) | a section banner. **Definitions don't cross it**: two exercises can both use `x` |
| `\| a \| x^2 \| b \|` (Space \|, `:table`) | a table row; consecutive rows form a table, the first is the header. Tab / Shift-Tab move between cells, Enter adds a row, Enter on an empty row ends it. Right-click (or `:row above\|below\|delete`, `:col left\|right\|delete`) adds and removes rows and columns, in matrices too |
| `---` (Space /) | a divider. Definitions don't cross it either |
| `![caption](path)` | a picture: drag one onto the window, Insert > Picture, paste a screenshot with Ctrl-v, or `:image path`. Drag its corner to resize, align it like text |

Also:
- **Sliders**: a line `a = 2` gets a drag handle; every result and plot using `a` follows as you drag.
- **Links**: `[[Other note]]` opens that note (Enter or `gf` on it, Ctrl-click), creating it if needed; Ctrl-o goes
  back. The sidebar lists the notes that link to the open one.
- **Search**: `/` highlights every match as you type (Esc or `:noh` clears); the sidebar filter searches note text too.
- **Outline**: the sidebar's Outline tab lists headings and sections; click to jump.
- **Autocomplete**: Settings > Autocomplete turns the popup off (Ctrl-Space opens it on demand), sets how many
  letters open it and how many entries it shows, whether Enter, Tab or both accept, and whether math words (`sqrt` → √)
  and combos (`->` → →) convert.
- **Tables** move like a spreadsheet: ←/→ go to the next cell, ↑/↓ keep the column, clicks on borders land in a cell.
- **Transformation steps**: `2x = 4 | :2` in math, a space then `|` after an equation, marks the step
  (`\qquad|\;` in LaTeX); it's shown dimmed and never calculated. `2 = |x|` stays an absolute value.
  On the empty line under a step, Tab writes what it gives (`x = 2`). A line that doesn't follow from the step above is
  flagged in red; a line about other variables isn't compared. After `3x + 1 = 7 ⇒` (type `=>`) the popup also offers
  **Show the working**: the steps written out, for linear and quadratic equations (pq formula). `f′(x) =` offers it too,
  term by term, for polynomials.
- **Sets**: `{` in math makes growing braces, for a solution set `L = {2; 3; 5}` or `L = {x | x > 0}` (`\{ … \}`,
  `\mid` in LaTeX).
- **Decimal comma**: Settings > Math > Numbers (`:set comma`): `2,5` is a number and `;` separates arguments, as in
  `max(1; 2,5)`. A comma with a space after it still separates, so plot lines and `max(1, 2,5)` keep working.
- **Exact answers**: `√8 = 2√2`, `arctan(1) = π/4`; Tab writes the exact form.
- **Equation numbers**: end a line with `#name` to number it `(1)`; `[#name]` refers to it (Ctrl-click jumps).
- **Tour**: `:tour` (or Help > Open the tour) types an example note.
- **Spreadsheets**: paste cells copied from Excel to get a table; `:import file.csv` reads CSV or tab-separated files
  (Excel's `;` and old encoding too), `:export csv` writes the table at the cursor, `:copytable` copies it for pasting
  into a spreadsheet.
- **Functions**: `sin … arctan`, `ln log exp sqrt abs floor ceil round sign`, `mod(a, b)`, `nCr(n, k)`, `nPr(n, k)`, and over
  lists `max min sum count mean median stdev var gcd lcm`. A table column's header name is its list: `mean(h)`;
  `fit(t, h)`, `fit(t, h, 2)`, `fit(t, h, exp)` give a regression formula with R².

## Export

`:export` takes a file name or just a format: `md` (default), `html`, `tex`, `pdf` or `csv` (the table at the cursor). Without a path it writes
`~/<title>.<ext>`.
- **HTML**: one shareable file that works offline; math as MathML (browsers draw it themselves), images inlined, plots as SVG.
- **PDF**: runs `pdflatex` in the background when it's installed (a TeX distribution with amsmath, pgfplots, soul and
  ulem). Otherwise, or with Settings > Files > PDF export (`:set pdf=builtin`), the app draws A4 pages itself: nothing to
  install and it looks like the app, but pages are pictures, so text can't be selected.
- **LaTeX**: lists, quotes and tables become environments, lines of only math display math, plots pgfplots.
- **Print**: `:print` (File > Print) sends a PDF to the default printer (`lp`; on Windows the PDF viewer's Print).

## Releases

GitHub Actions (`.github/workflows/release.yml`) builds Windows, macOS and Linux packages and runs the self-checks on
every push; `git tag v0.2.0 && git push origin v0.2.0` publishes a release with the downloads.

## License

GPL-3.0, see `LICENSE`. Fonts: Inter, JetBrains Mono, STIX Two Math (OFL).
