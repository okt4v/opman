#include "help.hpp"

namespace om {

const std::vector<HelpTopic>& helpTopics() {
    static const std::vector<HelpTopic> topics = {
        {"Welcome", R"(opman is a notes editor where text and math live together. Type an expression and its value appears beside it; define a variable on one line and use it on the next; plot, tabulate and solve as you write.

New here? The "Open the tour" button above makes an example note that shows the main features (also :tour).

## The basics
- Every note is a .opman file. New notes go to your library folder (Settings > Files) and show in the sidebar.
- Notes save themselves as you type. Ctrl-s saves now; Save as (Ctrl-Shift-s) puts the note anywhere you like.
- Math is recognised as you type: x^2 + 1, a/b, sqrt, sin(x). Ctrl-m makes everything you type math.
- A faint "= 17" after a line is its live result. Tab writes it into the note.
- The popup under the cursor offers completions: symbols, functions, results, steps, solutions.

## Two ways to type
opman starts with vim keys: press i to type, Esc to stop. If you'd rather type straight away like in a word processor, choose Settings > Editor > Keys > Standard, or type :set standard.

## Where to look next
- Press Ctrl-Shift-p (or click ⌘ in the toolbar) for the command palette: every action, setting and note by name.
- Every topic on the left has the details. The search box above searches all of them.)"},

        {"Files and saving", R"(## Your notes are files
Each note is one .opman file holding its text, math, tables and pictures (pictures are packed inside the file, so a note can be copied, emailed or put in a cloud folder on its own).

`Ctrl-s` save now (:w)
`Ctrl-Shift-s` Save as: choose a folder and name (:saveas, :w path)
`Ctrl-Shift-o` open a note from anywhere (:open, :e path)
`Ctrl-Shift-n` new note (:new [title])
`:rename title` give the note a title
`:trash` move the note to the library's .trash folder
`:folder` show the note's folder in the file manager

## The library
New notes are created in the library folder (Documents/opman unless you change it in Settings > Files). Their file names follow their titles: a note that starts with "# Vectors" is saved as Vectors.opman. Notes you open or save elsewhere stay where they are and show in the sidebar under their title too.

## Opening from the file manager
The Windows installer and the Linux package register .opman files: double-click one to open it in opman, or run opman path/to/note.opman.

## Older notes
corca .json notes are converted to .opman the first time opman sees them; the original .json is kept in a .old folder beside it.)"},

        {"Writing and formatting", R"(## Text styles
`Ctrl-b` bold
`Ctrl-i` italic
`Ctrl-u` underline
`Space s` strikethrough (vim keys: Space is the formatting key in Normal mode)
`Space c` inline code
`Space h` highlight
`Space ^` superscript text
`Space _` subscript text
`Space R` `G` `B` red, green, blue text
`Space r` clear formatting
The toolbar buttons do the same; with a selection they format the selection, otherwise the word under the cursor or what you type next.

## Line styles
Type the prefix at the start of a line, or use the toolbar:
`# ` `## ` `### ` headings (Space 1 2 3)
`- ` bullet list (Space -)
`1. ` numbered list (Space n)
`> ` quote (Space >)
`- [ ] ` checklist (Space t); Space x or a click on the box ticks it
`---` a divider line (Space /)
Enter continues a list; Enter on an empty item ends it.

## Links between notes
Write [[Other note]] to link to a note by title. Ctrl-click it, or press Enter or gf on it in Normal mode, to open it (it is created if it doesn't exist). Ctrl-o goes back. The sidebar shows "Linked from" for notes that link to the open one.)"},

        {"Typing math", R"(Math is recognised as you type it: letters next to operators, numbers, x^2, a/b. Text stays text. Ctrl-m (or the ∑ button, or Space m) switches to math mode, where everything you type is math.

## Structures
`/` a fraction: 1/2 becomes ½ with the cursor in the denominator
`^` a power
`_` a subscript
`sqrt` a square root (type sqrt, or pick it from the popup)
`( [ { |` brackets that grow with their contents; |x| is an absolute value
`|` after an equation: a transformation step, 2x = 4 | :2 (also | −1, | ·3, | √). It is shown set apart and never calculated; write the result on the next line
`{` a set, for a solution set (Lösungsmenge): L = {2; 3; 5}. Inside braces | is "such that": L = {x | x > 0}. Type } at the end to leave them
`Tab` leave the current slot (fraction part, exponent…) and continue after it
`Shift-Space` `Shift-Enter` add a column / a row to a matrix
Arrow keys move in and out of slots; in Normal mode h and l do.

## Symbols by name
Type a name and pick it from the popup: alpha, beta, pi, theta, infinity, sum, integral, limit, matrix, determinant, union, in, subset, forall, exists, implies, approx, neq, leq… Greek letter names and function names (sin, ln, det…) convert on their own when you type past them.

## Typed shortcuts
`->` →
`<=` `>=` ≤ ≥
`!=` ≠
`~=` ≈
`+-` ±
`=>` ⇒
`:=` ≔
`...` …

## The popup
`Tab` take the highlighted entry when it's a result, step or program function
`Enter` take the highlighted entry for a math word (for a text word, press ↓ first, so Enter after a plain word still starts a new line)
`↑` `↓` `Ctrl-n` `Ctrl-p` move through it
`Ctrl-1` `…` `Ctrl-9` take entry 1 to 9
`Esc` close it)"},

        {"Live results and calculator", R"(Write an expression and its value shows faintly at the end of the line: 2^10 shows "= 1024". Tab writes the result into the note.

## Exact answers
Decimals that are really roots or multiples of π show their exact form first: √8 = 2√2 ≈ 2.828, arctan(1) = π/4, (1 + √5)/2. Tab writes the exact form; the popup also offers the decimal. Turn on Settings > Math > decimals to always get decimals.

## Equation numbers
End a line with #name to number it: E = mc^2 #energy shows (1) at the right edge. Write [#energy] anywhere to refer to it; it shows as (1), and Ctrl-click jumps there. Numbers follow the order of the note, and exports keep them.

## Results in the popup
Type = after an expression and the popup offers:
- the exact result (fractions stay fractions: 1/3 + 1/6 = 1/2)
- the decimal value
- "Fill in the steps": every step written out
- for algebra, "Expand and simplify": (x + 1)^2 = x² + 2x + 1
Type ⇒ (type =>) after an equation for its solutions: x^2 − 5x + 6 = 0 ⇒ x = 2 or x = 3.

## Calculus
- Derivatives: define f(x) = x^3, then f'(2) = 12, f''(x)…
- Integrals: pick "integral" from the popup and fill in the limits: ∫ from 0 to 1 of x^2 dx = 1/3
- Sums and products: ∑ from k=1 to 10 of k^2 = 385
- Limits: lim x→0 of sin(x)/x = 1

## Matrices
Type matrix and pick a size from the popup (2×2, 3×3, column or row vector; determinant 2×2 or 3×3). Shift-Space adds a column, Shift-Enter a row, and right-click offers add/delete row and column. Matrices add and multiply; det or |A| gives the determinant.

## Units
Numbers can carry units: 90 km/h * 2 h = 180 km. Known units: m km cm mm g kg s ms min h N J W Hz Pa A V K L.

## Settings that change results
Settings > Math: significant digits, degrees or radians, fractions or decimals. :set degrees, :set decimals, :set digits=6 do the same from the command line.)"},

        {"Functions", R"(## Built in
`sin cos tan` `cot sec csc` trigonometry (radians, or degrees: Settings > Math)
`arcsin arccos arctan` inverse trigonometry
`sinh cosh tanh` hyperbolic
`ln log exp` natural log, log base 10, e^x
`sqrt abs floor ceil round sign` the usual
`mod(a, b)` remainder: mod(17, 5) = 2
`nCr(n, k)` `nPr(n, k)` combinations and permutations: nCr(5, 2) = 10
`n!` factorial

## Lists and statistics
These take any number of arguments, or the name of a table column (see Tables):
`max min` largest, smallest
`sum count` total, how many
`mean median` average, middle value
`stdev var` sample standard deviation and variance
`gcd lcm` greatest common divisor, least common multiple
`fit(t, h)` the least-squares line through two columns, with R²: fit(t, h) = 2.32t + 0.5
`fit(t, h, 2)` `fit(t, h, exp)` a quadratic (or any degree up to 6), an exponential a·e^(bt)

## Your own
Define a function once and use it below: f(x) = x^2 + 1, then f(3) = 10. Several parameters work too: area(w, h) = w h.

## Constants
π (type pi) and e.)"},

        {"Variables and sections", R"(## Definitions
x = 5 defines x for every line below it. Functions too: f(x) = 3x − 2. Change the definition and every result that uses it updates.

## Sliders
A line that sets a name to a number (a = 2) gets a slider after it. Drag it and every result and plot that uses a follows. Turn sliders off in Settings > Editor.

## Sections: one set of variables per exercise
A section line starts a fresh set of definitions, so two exercises can both use x without clashing:
`=== Exercise 2` a section line (type it, use Space = or the § button, or :section [title])
Definitions above a section line are not seen below it. Each section is its own little world: results, plots and table columns only use what is defined inside it.

To insert one from the popup, type section alone on a line and press Tab.)"},

        {"Plots", R"(Type plot alone at the start of a line and press Tab (or click the plot button), then list what to draw:
`plot sin(x)` one curve
`plot sin(x), cos(x), x^2/4` several curves
`plot f(x), −3 < x < 3` with an x range
`plot x^2, −1 < y < 4` with a y range
`plot table` the nearest table above, as points (column 1 is x)
`plot table, 2x + 1` data and a model together
`plot table, fit` the best line through the table, its formula and R² in the legend; also fit 2, fit 3, fit exp
Without an x range plots use −10 to 10 (change it in Settings > Math); the y range fits the curves.

## Curves of every kind
`plot (cos(t), sin(t))` parametric: x and y in terms of t (0 < t < 2π unless you give a t range)
`plot r = 1 + cos(θ)` polar (type theta for θ)
`plot x^2 + y^2 = 4` implicit: any equation in x and y
Plots of only these shapes keep x and y at the same scale, so circles stay round.

## 3D
`plot sin(x) cos(y)` a surface: an expression with y in it (or z = …) is drawn in 3D over the x range, and the same range for y
`plot x y, −2 < x < 2, −1 < y < 1, −1 < z < 1` with ranges for x, y and the height z
`plot (cos(t), sin(t), t/5), 0 < t < 20` a space curve
Drag a 3D plot to turn it, wheel to zoom, double-click to reset. Exports draw the surface too.

## Moving around
Drag a plot to pan, use the mouse wheel to zoom, double-click to reset. Hover to read coordinates.

The word plot inside a sentence stays text: only a line that starts with the plot function (from the popup) is a plot.)"},

        {"Tables and data", R"(## Making a table
Type table alone on a line and press Tab, use Space | or the toolbar, or :table rows cols. A table is lines like | a | b |; the first row is the header.
`←` `→` the cell to the left / right (like a spreadsheet: always into a cell, after its text)
`↑` `↓` the same column in the row above / below (also j and k)
`Tab` `Shift-Tab` next / previous cell; Tab in the last cell adds a row
`Enter` a new row; Enter on an empty row ends the table
`|` a new cell boundary
`Shift-Enter` a row below the current one
Cells can hold math, and columns line up on their own. The table button lets you pick the size by hovering a grid. A click on a border lands in the nearest cell. With vim keys, h and l still move one character, to edit inside a cell.

## Rows and columns
Right-click a table to add a row above or below, a column left or right, or delete the row or column. The same commands: :row above, :row below, :row delete, :col left, :col right, :col delete. They work in matrices too.

## Columns are data
A header cell that is a name (t, h, mass) makes its column a list for the statistics functions in the same section:
| t | h |
| 1 | 10 |
| 2 | 20 |
mean(h) = 15, max(h) = 20, sum(t) = 3, stdev(h)…
plot table draws the columns as points.)"},

        {"Pictures", R"(Drag a picture (png, jpg, gif, bmp) onto the window, or type :image path. It becomes a line ![caption](media/name.png); write a caption between the brackets.

Pictures are stored inside the .opman file, so they travel with the note. Exports to HTML embed them; LaTeX, PDF and Markdown exports reference them.)"},

        {"Exporting and printing", R"(File menu > Export, or :export with a format or a path:
`:export md` Markdown, math as $…$
`:export html` one web page that works offline: math, pictures and plots inside
`:export tex` a LaTeX article (tables, lists, plots as pgfplots)
`:export pdf` a PDF through pdflatex, in the background
`:export ~/Desktop/notes.pdf` a path picks the format by its extension
Without a path the file goes to the export folder (Settings > Files, your home folder by default) named after the note.

PDF export needs a TeX installation (TeX Live, MiKTeX or MacTeX) with amsmath, pgfplots, soul and ulem; set the pdflatex command in Settings > Files if it isn't on your PATH.

## Printing
File menu > Print, :print, or Ctrl-p (standard keys) sends the note to your default printer as a PDF. Without pdflatex (or a printer command) the note opens in your browser with its print dialog instead.)"},

        {"Search and replace", R"(`/` search forward, `?` backward (vim keys); Ctrl-f in standard keys
`n` `N` next / previous match
`Esc` `:noh` clear the highlights
`:s/old/new/` replace on this line; add g for every match on it
`:%s/old/new/g` replace everywhere in the note
Searches are smart-case: lowercase matches any case, a capital letter makes it exact.

The sidebar's filter box searches every note's text, not just titles, and shows where it matched.)"},

        {"Outline and sidebar", R"(`Ctrl-\` show or hide the sidebar (the first toolbar button)
`Ctrl-w` move the keyboard to the note list: j/k move, Enter opens, / filters, Esc returns
The sidebar has two tabs:
- Notes: every note in the library and the ones you opened elsewhere, sortable by last edited, created or name.
- Outline: the open note's headings and sections. Click one to jump to it. :outline opens it.)"},

        {"Vim keys", R"(opman's default keys are vim's, on top of the math structure.

## Modes
`i a I A o O` start typing (Insert mode)
`Esc` `Ctrl-[` back to Normal mode (CapsLock too, if your system maps it to Escape)
`v V` select characters / lines (Visual)
`:` command line

## Moving
`h j k l` left, down, up, right; h and l step into and out of fractions, roots and exponents
`w b e` words
`0 ^ $` line start, first character, end
`gg G` first / last line
`%` matching bracket
`f t F T ; ,` find a character in the line
`/ ? n N` search
`Enter` `gf` follow a [[link]]
`Ctrl-o` back to the previous note

## Editing
`d c y` + a motion: delete, change, copy
`dd cc yy` whole lines
`x D C S J r` delete character, to end of line, change to end, change line, join lines, replace character
`p P` paste after / before
`u` `Ctrl-r` undo / redo
`.` repeat the last change
`Space` the formatting key: b i u s c h r 1 2 3 0 ^ _ R G B - n > t x | / = m e (the status bar lists them while it waits)
Copies go to the system clipboard as Markdown.)"},

        {"Standard keys", R"(Choose Settings > Editor > Keys > Standard (or :set standard) to type like a word processor: you are always typing, and Esc only closes the popup.
`Ctrl-z` `Ctrl-y` undo / redo (Ctrl-Shift-z redoes too)
`Ctrl-c` `Ctrl-x` `Ctrl-v` copy / cut / paste
`Ctrl-a` select all
`Shift-arrows` select
`Ctrl-f` find
`Ctrl-b` `Ctrl-i` `Ctrl-u` bold / italic / underline
Everything else (the popup, Tab, the toolbar, the palette) works the same in both.)"},

        {"Commands", R"(## Shortcuts that always work
`Ctrl-Shift-p` command palette: type to find any action, setting, note or help topic
`F1` this help
`Ctrl-,` settings
`Ctrl-=` `Ctrl--` `Ctrl-0` text size bigger / smaller / reset
`Ctrl-s` save
`Ctrl-Shift-s` save as
`Ctrl-Shift-o` open
`Ctrl-Shift-n` new note
`Ctrl-m` math mode
`Ctrl-\` sidebar

## Ex commands (type : in Normal mode)
`:w [path]` save, or save as path
`:saveas [path]` `:open [path]` with a dialog when no path is given
`:e title|path` open by title or path
`:new [title]` `:rename title` `:trash` `:folder`
`:export md|html|tex|pdf|path`
`:image path` `:table rows cols` `:rule` `:section [title]`
`:s/a/b/g` `:%s/a/b/g` replace
`:set dark|light|degrees|radians|decimals|fractions|digits=N|vim|standard`
`:help [topic]` `:settings` `:palette` `:outline` `:zoom in|out|reset` `:noh`
`:q` `:wq` `:q!` quit (notes are saved anyway, except with :q!)
`:12` go to line 12)"},

        {"Autocomplete", R"(The popup that opens as you type offers functions, symbols, Greek letters, units, your variables and, after =, results. Settings > Autocomplete changes how it behaves:
- Show suggestions while typing: turn it off and the popup only opens when you press Ctrl-Space
- Letters before it opens: 1 opens at once; 3 waits until you've typed "sin"
- Most entries shown: how long the list gets
- Accept with Enter, Tab or both. With Tab, Enter always starts a new line (unless you moved through the list with the arrows)
- Rank entries by what you use most, or keep a fixed order
- Math words turn into symbols: sin, sqrt, alpha, pi, infty become their function or symbol once you type past them (sqrt8 → √8)
- Symbol combinations: -> → , <= ≤ , != ≠ , => ⇒ , +- ± , ~= ≈ , ... …

`Ctrl-Space` open the popup now (even when it's off)
`↑` `↓` `Ctrl-n` `Ctrl-p` move through the list
`Ctrl-1` … `Ctrl-9` take that entry
`Esc` close the popup for this word)"},

        {"Settings", R"(Ctrl-, (or the gear in the toolbar) opens the settings; changes apply at once and are remembered.
- Appearance: theme, text size, page width, formatting toolbar.
- Editor: vim or standard keys, CapsLock as Escape, live results, sliders, autosave delay, what opens at start.
- Math: significant digits, degrees or radians, fractions or decimals, decimal point or comma (2,5, with ; between arguments), default plot range.
- Files: library folder, export folder, pdflatex command, system or built-in file dialogs.
Settings live in config.json in your user settings folder (%APPDATA%\opman, ~/Library/Application Support/opman, ~/.config/opman).)"},
    };
    return topics;
}

// typed with vi keys so the note is made by the editor itself (checked by tests/check.cpp). Ctrl-m toggles math
const char* tourKeys() {
    return "i# Welcome to opman<Esc>"
           "oText and math live side by side. Press Ctrl-m to switch to math.<Esc>"
           "oValues appear as you type, and Tab writes them in.<Esc>"
           "o## Calculate<Esc>"
           "o<C-m>12*7 + 1/3<C-m><Esc>"
           "o<C-m>sqrt8<Esc><C-m>"
           "oAnswers stay exact where they can (fractions, roots, π). Hover a value for more, or press Tab to keep it.<Esc>"
           "o## Variables and equation numbers<Esc>"
           "o<C-m>r = 3<C-m><Esc>"
           "o<C-m>A = pi r^2<C-m> #area<Esc>"
           "oEquation [#area] uses r from the line above: change r and everything below updates.<Esc>"
           "oEnd a line with #name to number it, and write [#name] to refer to it.<Esc>"
           "o## Tables, statistics and fits<Esc>"
           "o<Esc>cc| t | h |<Esc>o<Esc>cc| 1 | 3 |<Esc>o<Esc>cc| 2 | 5 |<Esc>o<Esc>cc| 3 | 7.2 |<Esc>o<Esc>cc| 4 | 10 |<Esc>"
           "o<Esc>cc<C-m>mean(h)<C-m><Esc>"
           "o<C-m>fit(t, h)<C-m><Esc>"
           "oRight-click a table or a matrix to add or remove rows and columns.<Esc>"
           "oplot<Tab>table, fit 2<Esc>"
           "o## Plots<Esc>"
           "oplot<Tab>sin(x), x^2/4, −5 < x < 5<Esc>"
           "oplot<Tab>(cos(3t), sin(2t)), x^2 + y^2 = 1<Esc>"
           "oDrag a plot to pan, scroll to zoom, double-click to reset.<Esc>"
           "oWith y in the formula the plot turns three-dimensional: drag to rotate it.<Esc>"
           "oplot<Tab>sin(x) cos(y), −3 < x < 3<Esc>"
           "oPress F1 for the full help, Ctrl-Shift-p for every command, and :print to print.<Esc>gg";
}

}  // namespace om
