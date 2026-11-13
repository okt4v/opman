// Structured editing (port of corca core.js): typing with text/math classification, slots, navigation, undo,
// selection, matrices and the completion catalog.
#pragma once
#include "doc.hpp"
#include <functional>
#include <map>
#include <optional>
#include <unordered_map>

namespace om {

struct Cur {
    Row* row = nullptr;
    int i = 0;
};
struct Range {
    Row* row = nullptr;
    int a = 0, b = 0;      // b is the cursor end
    Row* row2 = nullptr;   // set: the selection runs over top-level lines, from (row, a) to (row2, b)
};
struct Parent {
    Item* node;
    Row* row;
    int slot;
};

// a completion: catalog entry (make) or computed result (insert)
struct Entry {
    std::string name;
    std::vector<std::string> kw;
    Item make;
    std::string cat;
    std::optional<Row> insert;
    int eraseBack = 0;       // items before the cursor it replaces
    std::vector<Row> below;  // whole lines it adds under this one (showing the working)
};

// how completion behaves (Settings > Autocomplete)
struct CompleteOpts {
    bool popup = true;        // suggestions open as you type; off: only Ctrl-Space opens them
    int minLetters = 1;       // letters of a word before the popup opens
    int maxItems = 9;         // entries shown
    int accept = 0;           // key that takes the first suggestion: 0 Enter (math words), 1 Tab, 2 both
    bool convert = true;      // math words (sin, sqrt, alpha…) become their symbol once you type past them
    bool combos = true;       // -> <= != … become → ≤ ≠ in math
    bool learn = true;        // entries you pick often move up
};
extern CompleteOpts completeOpts;

extern const std::vector<Entry> CATALOG;
extern const std::map<std::string, unsigned> CATS;  // category colour, 0xRRGGBB

struct Editor {
    Doc doc{Row{}};
    Cur cur{&doc[0], 0};
    std::optional<Range> range;
    uint16_t style = 0;     // Fmt bits given to typed text (Insert mode: Ctrl-b/i/u, toolbar)
    bool mathMode = false;  // Ctrl-m: what is typed is math. Off, a line takes plain text and numbers
    static inline bool autoMathDefault = false;
    bool autoMath = autoMathDefault;  // setting: text mode still reads "x = 5" or "2 m" as math, as corca did
    int gen = 0;            // bumped by reindex(): rows may have moved, so pointers kept from before are stale
    std::function<bool(int)> visualLine;  // the app's: move the cursor a wrapped line up/down, false if it can't
    std::unordered_map<const Row*, Parent> parents;
    struct Snap {
        Doc doc;
        Path path;
    };
    std::vector<Snap> undoStack, redoStack;
    int changes = 0;  // bumped by every snap(), so callers can tell whether a command changed the doc
    std::string lastSnapKind;
    double lastSnapAt = -1e9;
    std::string dismissed;  // popupKey() the popup was dismissed at
    bool forced = false;    // Ctrl-Space opened the popup: it stays open for the word being typed
    int pick = 0;
    std::map<std::string, int> usage;

    // model
    void reindex();
    Row* topOf(Row* row) const;
    int lineOf(const Row* row) const;  // index in doc, -1 if not a line
    Path curPath() const;
    void setPath(const Path& p);
    void load(Doc d, const Path& p);
    void snap(const std::string& kind = "");  // call before a change
    void undo();
    void redo();
    bool inSlot() const { return parents.count(cur.row) > 0; }

    // completion
    std::string pendingWord() const;
    std::string popupKey() const;
    std::vector<Entry> search(const std::string& q) const;
    std::vector<Entry> suggestions();
    void applySuggestion(const Entry& e);
    void insertNode(Item n);

    // typing and navigation
    void insertChar(std::string c);
    void exitSlot();
    void right();
    void left();
    void vertical(int d, bool visual = true);  // visual: wrapped lines count as lines
    void backspace();
    void del();    // the Delete key: the item after the cursor, or the line break
    void enter();
    int lineStart() const;  // the first place the cursor can be in its row: after a line's hidden prefix
    void clampCur();        // keep the cursor out of a line's hidden prefix and tail
    void syncMode();        // math mode follows the cursor: on next to math, off next to text
    void softBreak();       // Shift-Enter: a long calculation goes on in the next line, still one line to the math
    void toggleMath();      // Ctrl-m; turning it off inside a fraction, root… steps out to the line first
    void indent(int a, int b, int d);  // lines a..b one level in (d = 1) or out (-1)
    void setAlign(int a, int b, int align);
    // formatting
    void setLine(Row& line, Line kind, int num = 1);           // replace a top-level line's Markdown prefix
    void toggleFmt(Row& row, int a, int b, uint16_t flag);       // on text chars in [a,b): set, or clear if all have it; 0 clears all
    bool listEnter();
    void tableRow(const Row& from, Row& to);  // an empty row with from's cells, cursor in the first                                          // Enter on a list line: continue the list, or end it when empty
    void home(bool end);
    bool addMatrixRow();
    bool addMatrixCol();
    // rows and columns of the table or matrix the cursor is in: 'k' row above, 'j' row below, 'h' column left,
    // 'l' column right, 'r' delete row, 'c' delete column. False when the cursor is in neither
    bool gridOp(char op);
    // tables move like a spreadsheet: a cell left/right (dx) or the same column up/down (dy). False when the cursor
    // isn't on a table row (or there's no cell that way: stays put and returns true for dx)
    bool cellMove(int dx, int dy);
    void snapToCell();  // a cursor on a border, in padding or past the last | goes into its cell
    bool inGrid() const;

    // selection: a range of one row (selRange), or of several top-level lines (range->row2 set, selRange empty)
    std::optional<std::pair<int, int>> selRange() const;
    std::optional<std::pair<int, int>> selOn(const Row& row) const;  // the items of this row that are selected
    Doc selectedLines() const;  // what's selected, a row per line
    void insertLines(const Doc& parts);  // at the cursor: the first joins this line, the last joins what follows it
    void collapseSelection(bool toEnd);
    void selectBetween(Cur a, Cur b);
    void extendSelection(int d);
    void selectAll();
    Row selectedItems() const;
    bool deleteSelection();
    void insertItems(const Row& items);

private:
    Item* autoConvert();
    bool openIntegral(const Row& row, int i) const;
    void combine();
    std::optional<std::tuple<Item*, int, int>> matrixCell();
    void matrixBackspace(const Parent& p);
};

}  // namespace om
