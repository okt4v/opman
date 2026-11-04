// Structured editing (port of corca core.js): typing with text/math classification, slots, navigation, undo,
// selection, matrices and the completion catalog.
#pragma once
#include "doc.hpp"
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
    int a = 0, b = 0;  // b is the cursor end
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
};

extern const std::vector<Entry> CATALOG;
extern const std::map<std::string, unsigned> CATS;  // category colour, 0xRRGGBB

struct Editor {
    Doc doc{Row{}};
    Cur cur{&doc[0], 0};
    std::optional<Range> range;
    uint8_t style = 0;      // Fmt bits given to typed text (Insert mode: Ctrl-b/i/u, toolbar)
    bool mathMode = false;  // Ctrl-m: everything typed is math, so "(a+b)/2" at a line start isn't text
    std::unordered_map<const Row*, Parent> parents;
    struct Snap {
        Doc doc;
        Path path;
    };
    std::vector<Snap> undoStack, redoStack;
    std::string lastSnapKind;
    double lastSnapAt = -1e9;
    std::string dismissed;  // popupKey() the popup was dismissed at
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
    void vertical(int d);
    void backspace();
    void enter();
    // formatting
    void setLine(Row& line, Line kind, int num = 1);           // replace a top-level line's Markdown prefix
    void toggleFmt(Row& row, int a, int b, uint8_t flag);       // on text chars in [a,b): set, or clear if all have it; 0 clears all
    bool listEnter();                                          // Enter on a list line: continue the list, or end it when empty
    void home(bool end);
    bool addMatrixRow();
    bool addMatrixCol();

    // selection: always a range of one row
    std::optional<std::pair<int, int>> selRange() const;
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
