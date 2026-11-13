// Modal vi keys on top of the structured editor. Normal mode moves through the item tree (h/l step into and out of
// slots, j/k between numerator/denominator and lines); Insert mode is corca's typing model unchanged.
#pragma once
#include "edit.hpp"
#include <functional>

namespace om {

enum class Mode { Normal, Insert, Visual, VisualLine, Command };

struct Key {
    enum Sp : uint8_t { None, Esc, Enter, Tab, Backspace, Delete, Left, Right, Up, Down, Home, End, F1 } sp = None;
    std::string ch;  // typed character (UTF-8) for None; with ctrl, the lowercase letter/digit of the chord
    bool ctrl = false, shift = false;
};

struct Vi {
    Mode mode = Mode::Normal;
    std::string cmdline;              // text after the prompt in Command mode
    char prompt = ':';                // Command mode: ':' ex command, '/' or '?' search
    std::string message;              // shown in the status line until the next key
    std::vector<std::string> cmds;    // ex commands for the app to run (w, q, wq, e <name>, new, rename <t>, trash, sidebar)
    // Space is the leader in Normal/Visual: see leaderKey in vi.cpp and LEADER_HELP
    std::vector<Entry> items;         // the completion popup (Insert mode only)
    bool navigated = false;           // the popup's selection was moved (arrows, Ctrl-n/p): Enter and Tab take it
    bool hlsearch = false;            // highlight the last search's matches (until Esc or :noh)
    // standard keys instead of vim: always typing (Esc only closes the popup), Ctrl-z/y undo/redo, Ctrl-c/x copy/cut,
    // Ctrl-f find
    bool standard = false;
    // system clipboard, set by the app (empty in tests): yanks and deletes are copied out as Markdown; p/P and
    // Insert Ctrl-v paste text that was copied elsewhere
    std::function<std::string()> getClip;
    std::function<void(const std::string&)> setClip;

    void key(Editor& ed, const Key& k);
    void refresh(Editor& ed);         // recompute the popup after the document or cursor changed
    void mouse(Editor& ed, Cur p, bool drag);  // a click places the cursor, dragging selects
    void pickItem(Editor& ed, size_t j);  // take popup entry j
    // formatting, for the keys and the toolbar: the selection, else the word under the cursor (Normal), else the
    // style of what is typed next (Insert)
    void format(Editor& ed, uint16_t flag);  // 0 clears
    void setLine(Editor& ed, Line kind);    // toggles the line style of the current/selected lines
    void toggleDone(Editor& ed);
    void align(Editor& ed, int a);   // 0 left, 1 centre, 2 right: the current/selected lines
    void indent(Editor& ed, int d);  // nest them a level (1) or bring them back out (-1)
    bool leader() const { return pending == " "; }
    static std::vector<Key> parse(const std::string& s);  // "if(x)<Esc>dd": chars, <Esc> <CR> <Tab> <BS> <S-CR> <C-r>
    std::optional<std::pair<int, int>> visualLines(const Editor& ed) const;  // VisualLine: first, last line
    bool enterPicks(const Editor& ed) const;  // Enter takes the popup's entry: after navigating, or for a math word
    std::string highlight() const;            // the pattern to highlight: typed after "/", or the last search
    struct Match {
        int line, a, b;  // items [a, b) of a top-level line
    };
    static std::vector<Match> matches(const Editor& ed, const std::string& pattern);  // smartcase

private:
    std::string pending;  // count, operator, g/r prefixes
    Cur anchor;           // Visual: where the selection started
    Cur dragFrom;
    int anchorLine = 0;   // VisualLine
    struct {
        bool lines = false;
        Doc doc;
        Row items;
        Doc parts;  // a charwise selection over several lines
    } reg;
    std::string copied;               // what we last put on the clipboard, so p knows whether it came from elsewhere
    std::vector<Key> rec, last;       // keys of the command being typed, and of the last one that changed the doc (.)
    int recFrom = 0;
    bool replaying = false;
    std::string lastFind;             // f/t/F/T and its target, for ; and ,
    std::string lastSearch;
    bool lastBack = false;            // last search was ?

    void yanked();
    void pasteText(Editor& ed, const std::string& s);
    void searchNext(Editor& ed, bool reverse);

    void key1(Editor& ed, const Key& k);
    void insertKey(Editor& ed, const Key& k);
    void normalKey(Editor& ed, const std::string& c, const Key& k);
    void visualKey(Editor& ed, const std::string& c, const Key& k);
    void commandKey(Editor& ed, const Key& k);
    bool motion(Editor& ed, const std::string& m, int n, bool hasCount, bool structural, bool& linewise, bool& inclusive);
    void deleteLines(Editor& ed, int a, int b, bool yankOnly);
    void toInsert(Editor& ed, bool snapped = false);
    void visualSelect(Editor& ed);
    void leaderKey(Editor& ed, const std::string& c);
    std::pair<int, int> lines(const Editor& ed) const;
};

}  // namespace om
