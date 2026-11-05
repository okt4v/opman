# opman

Math notes with vi keys. A native C++20 / Dear ImGui editor: type text and math together, get live results
(`f(4) =` offers `17`), and keep notes as plain JSON files (corca-compatible) in `~/Documents/opman`.

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
| `Space` | formatting leader: `b i u s c h` style, `^ _` super/subscript, `R G B` colour, `r` clear, `1 2 3` headings, `- n > t` lists, `x` tick, `\|` table, `/` divider, `=` section, `m` math |
| `Enter`, `gf`, `Ctrl-o` | follow a `[[link]]`, go back |
| `Ctrl-m` | math mode: everything typed is math |
| `Ctrl-v` (Insert), `p` | paste from the system clipboard; yanks are copied to it as Markdown |
| `Ctrl-w`, `Ctrl-\` | focus / toggle the file list |

Ex commands: `:w :q :q! :wq :e <name> :new [title] :rename <title> :trash :set dark|light :export [path]
:image <path> :table [rows] [cols] :rule :section [title] :noh`.

In Insert mode the popup offers completions: Tab takes a computed result or a program function, Enter takes the entry
for a math word (after ↓ / Ctrl-n for a text word, so Enter after a plain word still starts a new line), Ctrl-1…9 any.

## Blocks

Notes stay plain text lines (corca-compatible JSON). Type `plot`, `table`, `section` or `divider` alone at the start of a
line and take it from the popup with Tab (they're the *Program* functions there), or use the toolbar or Space leader:

| Line | Renders as |
|---|---|
| **plot** `sin(x), f(x), x^2/4, −3 < x < 3, −1 < y < 1` | a graph of each expression; x and y ranges optional. `plot table` draws the nearest table above (column 1 is x). Drag pans, the wheel zooms, a double-click resets |
| `=== Exercise 2` (Space =, `:section`) | a section banner. **Definitions don't cross it**: two exercises can both use `x` |
| `\| a \| x^2 \| b \|` (Space \|, `:table`) | a table row; consecutive rows form a table, the first is the header. Tab / Shift-Tab move between cells, Enter adds a row, Enter on an empty row ends it |
| `---` (Space /) | a divider |
| `![caption](path)` | an image: drag a picture onto the window or `:image path` (copied into `media/` next to the notes) |

Also:
- **Sliders**: a line `a = 2` gets a drag handle; every result and plot using `a` follows as you drag.
- **Links**: `[[Other note]]` opens that note (Enter or `gf` on it, Ctrl-click), creating it if needed; Ctrl-o goes
  back. The sidebar lists the notes that link to the open one.
- **Search**: `/` highlights every match as you type (Esc or `:noh` clears); the sidebar filter searches note text too.

## Export

`:export` takes a file name or just a format: `md` (default), `html`, `tex` or `pdf`. Without a path it writes
`~/<title>.<ext>`.
- **HTML**: one shareable file; math by KaTeX (loaded from a CDN), images inlined, plots as SVG.
- **PDF**: runs `pdflatex` in the background (needs a TeX distribution with amsmath, pgfplots, soul and ulem).
- **LaTeX**: lists, quotes and tables become environments, lines of only math display math, plots pgfplots.

## Releases

GitHub Actions (`.github/workflows/release.yml`) builds Windows, macOS and Linux packages and runs the self-checks on
every push; `git tag v0.2.0 && git push origin v0.2.0` publishes a release with the downloads.

## License

GPL-3.0, see `LICENSE`. Fonts: Inter, JetBrains Mono, STIX Two Math (OFL).
