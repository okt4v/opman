// Modal vi keys on top of the structured editor. Normal mode moves through the item tree (h/l step into and out of
// slots, j/k between numerator/denominator and lines); Insert mode is corca's typing model unchanged.
#pragma once
#include "edit.hpp"

namespace om {

enum class Mode { Normal, Insert, Visual, VisualLine, Command };

struct Key {
    enum Sp : uint8_t { None, Esc, Enter, Tab, Backspace, Delete, Left, Right, Up, Down, Home, End } sp = None;
    std::string ch;  // typed character (UTF-8) for None; with ctrl, the lowercase letter/digit of the chord
    bool ctrl = false, shift = false;
};

struct Vi {
    Mode mode = Mode::Normal;
    std::string cmdline;              // text after ":" in Command mode
    std::string message;              // shown in the status line until the next key
    std::vector<std::string> cmds;    // ex commands for the app to run (w, q, wq, e <name>, new, rename <t>, trash, sidebar)
    std::vector<Entry> items;         // the completion popup (Insert mode only)

    void key(Editor& ed, const Key& k);
    void refresh(Editor& ed);         // recompute the popup after the document or cursor changed
    void mouse(Editor& ed, Cur p, bool drag);
    void pickItem(Editor& ed, size_t j);  // take popup entry j  // a click places the cursor, dragging selects
    static std::vector<Key> parse(const std::string& s);  // "if(x)<Esc>dd": chars, <Esc> <CR> <Tab> <BS> <S-CR> <C-r>
    std::optional<std::pair<int, int>> visualLines(const Editor& ed) const;  // VisualLine: first, last line

private:
    std::string pending;  // count, operator, g/r prefixes
    Cur anchor;           // Visual: where the selection started
    Cur dragFrom;
    int anchorLine = 0;   // VisualLine
    struct {
        bool lines = false;
        Doc doc;
        Row items;
    } reg;

    void insertKey(Editor& ed, const Key& k);
    void normalKey(Editor& ed, const std::string& c, const Key& k);
    void visualKey(Editor& ed, const std::string& c, const Key& k);
    void commandKey(Editor& ed, const Key& k);
    bool motion(Editor& ed, const std::string& m, int n, bool hasCount, bool structural, bool& linewise, bool& inclusive);
    void deleteLines(Editor& ed, int a, int b, bool yankOnly);
    void toInsert(Editor& ed, bool snapped = false);
    void visualSelect(Editor& ed);
};

}  // namespace om
