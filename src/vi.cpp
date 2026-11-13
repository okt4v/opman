#include "vi.hpp"
#include "math.hpp"
#include <algorithm>
#include <cstring>
#include <map>

namespace om {

static int lineIdx(const Editor& ed) { return ed.lineOf(ed.topOf(ed.cur.row)); }
static int len(const Row* r) { return int(r->size()); }

void Vi::refresh(Editor& ed) {
    ed.reindex();
    ed.clampCur();
    items = mode == Mode::Insert && !ed.range ? ed.suggestions() : std::vector<Entry>{};
    if (ed.pick >= int(items.size())) ed.pick = 0;
}

std::optional<std::pair<int, int>> Vi::visualLines(const Editor& ed) const {
    if (mode != Mode::VisualLine) return std::nullopt;
    int k = lineIdx(ed);
    return std::pair{std::min(k, anchorLine), std::max(k, anchorLine)};
}

// one undo step per insert session (commands that already changed the doc snapped before that change)
void Vi::mouse(Editor& ed, Cur p, bool drag) {
    if (!drag) {
        ed.cur = p;
        ed.snapToCell();  // a click on a table border lands in a cell
        ed.clampCur();
        ed.syncMode();
        dragFrom = ed.cur;
        ed.range.reset();
        if (mode != Mode::Insert) mode = Mode::Normal;
    } else if (mode == Mode::Insert) ed.selectBetween(dragFrom, p);
    else {
        mode = Mode::Visual;
        anchor = dragFrom;
        ed.cur = p;
        visualSelect(ed);
    }
    pending.clear();
    refresh(ed);
}

std::vector<Key> Vi::parse(const std::string& s) {
    std::vector<Key> out;
    for (size_t i = 0; i < s.size();) {
        Key k;
        size_t close = s.find('>', i);
        if (s[i] == '<' && close != std::string::npos && close > i + 1 &&
            s.find_first_of(" <", i + 1) > close) {  // "<Esc>" is a key; the "<" in "0 < x <Esc>" is typed
            std::string t = s.substr(i + 1, close - i - 1);
            i = close + 1;
            if (t == "Esc") k.sp = Key::Esc;
            else if (t == "CR") k.sp = Key::Enter;
            else if (t == "S-CR") k.sp = Key::Enter, k.shift = true;
            else if (t == "S-Space") k.ch = " ", k.shift = true;
            else if (t == "Space") k.ch = " ";
            else if (t == "Tab") k.sp = Key::Tab;
            else if (t == "S-Tab") k.sp = Key::Tab, k.shift = true;
            else if (t == "Del") k.sp = Key::Delete;
            else if (t == "Home") k.sp = Key::Home;
            else if (t == "End") k.sp = Key::End;
            else if (t == "BS") k.sp = Key::Backspace;
            else if (t == "F1") k.sp = Key::F1;
            else if (t == "S-Down") k.sp = Key::Down, k.shift = true;
            else if (t == "S-Up") k.sp = Key::Up, k.shift = true;
            else if (t == "Down") k.sp = Key::Down;
            else if (t == "Up") k.sp = Key::Up;
            else if (t == "Left") k.sp = Key::Left;
            else if (t == "Right") k.sp = Key::Right;
            else if (t.starts_with("C-S-")) k.ctrl = k.shift = true, k.ch = t.substr(4);
            else if (t == "C-Space") k.ctrl = true, k.ch = " ";
            else if (t.starts_with("C-")) k.ctrl = true, k.ch = t.substr(2);
            else k.ch = "<" + t + ">";  // ponytail: not a key name, typed literally as one string
        } else {
            unsigned char b = s[i];
            size_t n = b < 0x80 ? 1 : b < 0xE0 ? 2 : b < 0xF0 ? 3 : 4;
            k.ch = s.substr(i, n);
            i += n;
        }
        out.push_back(k);
    }
    return out;
}

void Vi::toInsert(Editor& ed, bool snapped) {
    if (!snapped) ed.snap();
    mode = Mode::Insert;
    ed.range.reset();
    ed.clampCur();
    ed.syncMode();
    const Row& r = *ed.cur.row;  // keep typing in the style of the text before the cursor
    int i = ed.cur.i;
    ed.style = i > 0 && r[i - 1].t == T::Ch && !r[i - 1].m && !isSpace(r[i - 1]) ? r[i - 1].f : 0;
}

// ---------- formatting ----------
std::pair<int, int> Vi::lines(const Editor& ed) const {
    if (auto v = visualLines(ed)) return *v;
    if (ed.range && ed.range->row2) return std::minmax(ed.lineOf(ed.range->row), ed.lineOf(ed.range->row2));
    int k = ed.range ? ed.lineOf(ed.topOf(ed.range->row)) : lineIdx(ed);
    return {k, k};
}

void Vi::format(Editor& ed, uint16_t flag) {
    auto done = [&] {
        if (mode == Mode::Visual || mode == Mode::VisualLine) mode = Mode::Normal, ed.range.reset();
    };
    if (mode == Mode::VisualLine) {
        auto [a, b] = lines(ed);
        ed.snap();
        for (int k = a; k <= b; k++) ed.toggleFmt(ed.doc[k], lineStyle(ed.doc[k]).len, int(ed.doc[k].size()), flag);
        return done();
    }
    if (ed.range && ed.range->row2) {
        auto [a, b] = lines(ed);
        ed.snap();
        for (int k = a; k <= b; k++) {
            auto s = ed.selOn(ed.doc[k]);
            ed.toggleFmt(ed.doc[k], std::max(s->first, lineStyle(ed.doc[k]).len), s->second, flag);
        }
        return done();
    }
    if (auto s = ed.selRange()) {
        ed.snap();
        ed.toggleFmt(*ed.range->row, s->first, s->second, flag);
        return done();
    }
    if (mode == Mode::Insert) {
        ed.style = !flag ? 0 : fmtHas(ed.style, flag) ? ed.style & ~fmtGroup(flag) : (ed.style & ~fmtGroup(flag)) | flag;
        return;
    }
    Row& r = *ed.cur.row;  // Normal: the word under the cursor
    int i = ed.cur.i, n = len(&r), a = i, b = i;
    if (i >= n || isSpace(r[i])) { message = "No word under the cursor"; return; }
    while (a > 0 && !isSpace(r[a - 1])) a--;
    while (b < n && !isSpace(r[b])) b++;
    ed.snap();
    ed.toggleFmt(r, a, b, flag);
}

void Vi::setLine(Editor& ed, Line kind) {
    auto [a, b] = lines(ed);
    auto same = [&](Line l) { return l == kind || (kind == Line::Todo && l == Line::Done); };
    bool all = true;
    for (int k = a; k <= b; k++) all = all && same(lineStyle(ed.doc[k]).kind);
    ed.snap();
    for (int k = a, num = 1; k <= b; k++) ed.setLine(ed.doc[k], all ? Line::Text : kind, num++);
    if (mode == Mode::Visual || mode == Mode::VisualLine) mode = Mode::Normal, ed.range.reset();
}

void Vi::toggleDone(Editor& ed) {
    auto [a, b] = lines(ed);
    ed.snap();
    for (int k = a; k <= b; k++) ed.setLine(ed.doc[k], lineStyle(ed.doc[k]).kind == Line::Todo ? Line::Done : Line::Todo);
    if (mode == Mode::Visual || mode == Mode::VisualLine) mode = Mode::Normal, ed.range.reset();
}

void Vi::align(Editor& ed, int al) {
    auto [a, b] = lines(ed);
    ed.snap();
    ed.setAlign(a, b, al);
}
void Vi::indent(Editor& ed, int d) {
    auto [a, b] = lines(ed);
    ed.snap();
    ed.indent(a, b, d);
}

void Vi::leaderKey(Editor& ed, const std::string& c) {
    static const std::map<std::string, uint16_t> FMT = {{"b", Bold}, {"i", Italic}, {"u", Underline}, {"s", Strike}, {"c", Code},
                                                        {"h", Mark}, {"^", Sup}, {"_", Sub}, {"R", Red}, {"G", Green},
                                                        {"B", Blue}, {"O", Orange}, {"P", Purple}, {"T", Teal}, {"A", Gray},
                                                        {"+", Large}, {"S", Small}, {"r", 0}};
    static const std::map<std::string, Line> LINE = {{"0", Line::Text}, {"1", Line::H1}, {"2", Line::H2}, {"3", Line::H3},
                                                     {"-", Line::Bullet}, {"n", Line::Number}, {">", Line::Quote}, {"t", Line::Todo}};
    if (c == "|") return cmds.push_back("table 3 2");  // a 3×2 table below
    if (c == "/") return cmds.push_back("rule");
    if (c == "=") return cmds.push_back("section");
    if (auto f = FMT.find(c); f != FMT.end()) format(ed, f->second);
    else if (auto l = LINE.find(c); l != LINE.end()) setLine(ed, l->second);
    else if (c == "x") toggleDone(ed);
    else if (c == "m") ed.toggleMath();
    else if (c == "[" || c == "]" || c == "\\") align(ed, c == "[" ? 0 : c == "]" ? 2 : 1);  // left, right, centre
    else if (c == "e") cmds.push_back("sidebar");
    else message = "Space " + c + " is not mapped";
}

// charwise Visual includes the item under the cursor, like vim
void Vi::visualSelect(Editor& ed) {
    ed.selectBetween(anchor, ed.cur);
    int n = len(ed.cur.row);
    if (ed.range && ed.range->row2) {
        if (ed.lineOf(ed.range->row2) > ed.lineOf(ed.range->row)) ed.range->b = std::min(ed.range->b + 1, n);
        else ed.range->a = std::min(ed.range->a + 1, len(ed.range->row));
    } else if (!ed.range) {
        if (ed.cur.i < n) ed.range = Range{ed.cur.row, ed.cur.i, ed.cur.i + 1};
    } else if (ed.range->a < ed.range->b) ed.range->b = std::min(ed.range->b + 1, n);
    else ed.range->a = std::min(ed.range->a + 1, n);
}

void Vi::key(Editor& ed, const Key& k) {
    bool idle = mode == Mode::Normal && pending.empty();
    if (!replaying) {
        if (idle && k.ch == "." && !k.ctrl) {  // ponytail: no count on ., and an insert replays its keys as typed
            replaying = true;
            for (auto& x : std::vector<Key>(last)) key(ed, x);
            replaying = false;
            return;
        }
        if (idle) rec.clear(), recFrom = ed.changes;
        rec.push_back(k);
    }
    key1(ed, k);
    if (standard && mode != Mode::Insert && mode != Mode::Command) mode = Mode::Insert, pending.clear(), ed.range.reset();
    if (!replaying && mode == Mode::Normal && pending.empty() && ed.changes != recFrom) last = rec, recFrom = ed.changes;
}

void Vi::key1(Editor& ed, const Key& k) {
    message.clear();
    if (k.ctrl && k.ch == "\\") cmds.push_back("sidebar");
    else if (k.ctrl && (k.ch == "b" || k.ch == "i" || k.ch == "u") && mode != Mode::Command)
        format(ed, k.ch == "b" ? Bold : k.ch == "i" ? Italic : Underline);
    else if (k.ctrl && k.ch == "s") cmds.push_back("w");
    else if (k.ctrl && k.ch == "m") ed.toggleMath();
    else if (mode == Mode::Insert) insertKey(ed, k);
    else if (mode == Mode::Command) commandKey(ed, k);
    else {
        // arrows and friends double as motions outside Insert mode
        std::string c = k.ch;
        if (mode == Mode::Normal && !k.shift && (k.sp == Key::Left || k.sp == Key::Right) && ed.cellMove(k.sp == Key::Left ? -1 : 1, 0))
            return refresh(ed);  // tables: arrows go cell to cell
        if (k.sp == Key::Left || k.sp == Key::Backspace) c = "h";
        else if (k.sp == Key::Right) c = "l";
        else if (k.sp == Key::Up) c = "k";
        else if (k.sp == Key::Down) c = "j";
        else if (k.sp == Key::Home) c = "0";
        else if (k.sp == Key::End) c = "$";
        if (mode == Mode::Normal) normalKey(ed, c, k);
        else visualKey(ed, c, k);
    }
    refresh(ed);
}

// ---------- Insert: corca's keyboard handler ----------
void Vi::pickItem(Editor& ed, size_t j) {
    if (j >= items.size()) return;
    ed.applySuggestion(items[j]);
    ed.pick = 0;
}

void Vi::insertKey(Editor& ed, const Key& k) {
    auto ghostHere = [&]() -> std::optional<Ghost> {
        if (ed.inSlot() || ed.cur.i != len(ed.cur.row)) return std::nullopt;
        return ghost(ed, *ed.cur.row);
    };
    // standard keys have no insert sessions to undo as one step: typing merges (Editor::snap), anything else is its own step
    if (standard && (k.ctrl ? k.ch == "v" : k.sp == Key::Enter || k.sp == Key::Tab || k.sp == Key::Backspace || k.sp == Key::Delete || (k.sp == Key::None && !k.ch.empty())))
        ed.snap(!ed.range && !k.ctrl && k.sp != Key::Enter && k.sp != Key::Tab ? "char" : "");
    auto sel = ed.selRange();
    int n = int(items.size());
    // a Ctrl-Space popup lasts while you type letters of its word
    if (ed.forced && (k.ctrl || k.sp != Key::None || !isLetter(k.ch)) && !(k.ctrl && k.ch == " ") && k.sp != Key::Up && k.sp != Key::Down &&
        !(k.ctrl && (k.ch == "n" || k.ch == "p")))
        ed.forced = false;
    if (k.ctrl) {
        if (k.ch == " ") { ed.forced = true, ed.dismissed.clear(); return; }  // open the popup (also when it's off in settings)
        if (k.ch.size() == 1 && k.ch[0] >= '1' && k.ch[0] <= '9') pickItem(ed, k.ch[0] - '1');
        else if (n && (k.ch == "n" || k.ch == "p")) ed.pick = (ed.pick + (k.ch == "n" ? 1 : n - 1)) % n, navigated = true;
        else if (k.ch == "a") ed.selectAll();
        else if (k.ch == "v" && getClip) {  // what was copied here goes back as it was (math stays math); other text is typed in
            ed.deleteSelection();
            if (std::string t = getClip(); !copied.empty() && t == copied && !reg.lines) {
                if (!reg.parts.empty()) ed.insertLines(reg.parts);
                else ed.insertItems(reg.items);
            } else pasteText(ed, t);
        }
        else if (k.ch == "z") k.shift ? ed.redo() : ed.undo();
        else if (k.ch == "y") ed.redo();
        else if ((k.ch == "c" || k.ch == "x") && ed.range) {
            Doc parts = ed.selectedLines();
            copied = parts.size() > 1 ? markdown(parts) : markdown(parts[0]);
            if (parts.size() > 1 && copied.ends_with("\n")) copied.pop_back();
            reg.lines = false;
            reg.items = parts.size() == 1 ? parts[0] : Row{};
            reg.parts = parts.size() > 1 ? parts : Doc{};
            if (setClip) setClip(copied);
            if (k.ch == "x") ed.snap(), ed.deleteSelection();
        } else if (k.ch == "f") mode = Mode::Command, prompt = '/', cmdline.clear();
        return;
    }
    switch (k.sp) {
    case Key::Esc:
        ed.range.reset();
        ed.dismissed = ed.popupKey();
        if (!standard) mode = Mode::Normal;
        return;
    case Key::Enter:
        if (k.shift) { if (!ed.addMatrixRow() && !ed.gridOp('j')) ed.softBreak(); }  // a matrix or table row, else the line goes on below
        else if (n && !items[0].insert && (navigated || (completeOpts.accept != 1 && enterPicks(ed)))) pickItem(ed, ed.pick);
        else {
            ed.deleteSelection();
            // after a line with text in it the next one starts as text; a run of math lines stays in math
            const Row& r = *ed.cur.row;
            bool top = !ed.inSlot(), allMath = top && !r.empty() && std::all_of(r.begin(), r.end(), [](auto& x) { return isMath(x) || isSpace(x); });
            bool decide = top && !r.empty() && lineStyle(r).kind != Line::Plot;  // (r moves when the line is added)
            if (!ed.listEnter()) ed.enter();
            if (decide && !allMath) ed.mathMode = false;
        }
        return;
    case Key::Tab:
        if (n && (items[0].insert || navigated || items[ed.pick].cat == "Program" || (completeOpts.accept != 0 && enterPicks(ed))))
            pickItem(ed, ed.pick);
        else if (auto g = ghostHere(); g && !g->warn && !k.shift) {
            if (auto t = eqTag(*ed.cur.row); t.first >= 0 && !ed.inSlot()) ed.cur.i = t.first - 1;  // before the "#tag"
            ed.insertItems(g->insert);
        }
        else if (!ed.inSlot() && lineStyle(*ed.cur.row).kind == Line::Table) {  // next cell (Shift-Tab: previous)
            Row& r = *ed.cur.row;
            auto cells = tableCells(r);
            int c = 0;
            while (c < int(cells.size()) && cells[c].second < ed.cur.i) c++;
            c += k.shift ? -1 : 1;
            if (c < 0) return;
            if (c >= int(cells.size())) {  // past the last cell: a new row
                ed.cur.i = len(&r);
                ed.listEnter();
                return;
            }
            ed.cur.i = cells[c].second;
            ed.snapToCell();  // after the cell's text, like the arrows
        } else if (LineStyle ls = ed.inSlot() ? LineStyle{} : lineStyle(*ed.cur.row); !ed.inSlot() && (isList(ls.kind) || ed.cur.i <= ls.len)) {
            int at = ed.lineOf(ed.cur.row);  // a list item, or the start of a line: nest it (Shift-Tab: back out)
            ed.indent(at, at, k.shift ? -1 : 1);
        } else ed.exitSlot();
        return;
    case Key::Up: case Key::Down:
        if (k.shift) {  // select over lines
            Cur from = ed.range ? Cur{ed.range->row, ed.range->a} : ed.cur;
            ed.range.reset();
            ed.vertical(k.sp == Key::Up ? -1 : 1);
            ed.reindex();
            ed.selectBetween(from, ed.cur);
        } else if (n) ed.pick = (ed.pick + (k.sp == Key::Down ? 1 : n - 1)) % n, navigated = true;
        else { ed.collapseSelection(k.sp == Key::Down); ed.range.reset(); ed.vertical(k.sp == Key::Up ? -1 : 1); ed.reindex(); ed.clampCur(); ed.syncMode(); }
        return;
    case Key::Left: case Key::Right:
        if (k.shift) ed.extendSelection(k.sp == Key::Left ? -1 : 1);
        else if (ed.range && ed.range->row2) ed.collapseSelection(k.sp == Key::Right);
        else if (sel) { ed.cur.i = k.sp == Key::Left ? sel->first : sel->second; ed.range.reset(); }
        else if (ed.cellMove(k.sp == Key::Left ? -1 : 1, 0)) {}  // tables: cell to cell
        else if (k.sp == Key::Left) ed.left();
        else ed.right();
        if (!k.shift) ed.reindex(), ed.clampCur(), ed.syncMode();
        return;
    case Key::Home: case Key::End: ed.range.reset(); ed.home(k.sp == Key::End); ed.clampCur(); ed.syncMode(); return;
    case Key::Backspace: if (!ed.deleteSelection()) ed.backspace(); return;
    case Key::Delete:
        if (!ed.deleteSelection()) ed.del();
        return;
    case Key::None:
        navigated = false;
        if (k.ch.empty()) return;
        if (k.ch == " " && k.shift) {
            ed.deleteSelection();
            if (!ed.addMatrixCol()) ed.insertChar(" ");
            return;
        }
        ed.deleteSelection();
        ed.insertChar(k.ch);
        ed.pick = 0;
        return;
    }
}

// ---------- motions ----------
// moves ed.cur; linewise: j k gg G; inclusive: e (the item under the target belongs to the range)
bool Vi::motion(Editor& ed, const std::string& m, int n, bool hasCount, bool structural, bool& linewise, bool& inclusive) {
    Row* row = ed.cur.row;
    auto sp = [&](int i) { return isSpace((*row)[i]); };
    linewise = inclusive = false;
    if (m == "h" || m == "l") {
        for (int k = 0; k < n; k++) {
            if (!structural) ed.cur.i = std::clamp(ed.cur.i + (m == "h" ? -1 : 1), 0, len(row));
            else if (m == "h" && !(ed.cur.i <= ed.lineStart() && !ed.inSlot())) ed.left();
            else if (m == "l" && !(ed.cur.i == len(ed.cur.row) && !ed.inSlot())) ed.right();
            ed.reindex();
        }
    } else if (m == "j" || m == "k") {
        for (int k = 0; k < n; k++) ed.vertical(m == "j" ? 1 : -1, structural);  // an operator's j/k count whole lines
        linewise = true;
    } else if (m == "w" || m == "e" || m == "b") {  // words run on over line ends, not out of a slot
        int line = ed.inSlot() ? -1 : ed.lineOf(row), last = int(ed.doc.size()) - 1, i = ed.cur.i;
        auto step = [&](int d) {
            if (line < 0 || line + d < 0 || line + d > last) return false;
            line += d;
            row = &ed.doc[line];
            i = d > 0 ? 0 : len(row);
            return true;
        };
        for (int k = 0; k < n; k++) {
            if (m == "w") {
                while (i < len(row) && !sp(i)) i++;
                while (i < len(row) && sp(i)) i++;
                if (i >= len(row) && step(1))
                    while (i < len(row) && sp(i)) i++;
            } else if (m == "e") {
                i++;
                for (;;) {
                    while (i < len(row) && sp(i)) i++;
                    if (i < len(row) || !step(1)) break;
                }
                while (i + 1 < len(row) && !sp(i + 1)) i++;
                i = std::min(i, std::max(len(row) - 1, 0));
            } else {
                for (;;) {
                    while (i > 0 && sp(i - 1)) i--;
                    if (i > 0 || !step(-1)) break;
                }
                while (i > 0 && !sp(i - 1)) i--;
            }
        }
        ed.cur = {row, i};
        inclusive = m == "e";
    } else if (m == "0") ed.cur.i = ed.lineStart();
    else if (m == "^") {
        int i = 0;
        while (i < len(row) && sp(i)) i++;
        ed.cur.i = i;
    } else if (m == "$") ed.cur.i = len(row);
    else if (m == "gg" || m == "G") {
        int k = hasCount ? n - 1 : m == "gg" ? 0 : int(ed.doc.size()) - 1;
        ed.cur = {&ed.doc[std::clamp(k, 0, int(ed.doc.size()) - 1)], 0};
        linewise = true;
    } else if (m == ";" || m == "," || (m.size() > 1 && std::strchr("fFtT", m[0]))) {
        std::string f = m.size() > 1 ? m : lastFind;
        if (f.empty()) return true;
        bool repeat = m.size() == 1;
        if (!repeat) lastFind = m;
        else if (m == ",") f[0] = f[0] == 'f' ? 'F' : f[0] == 'F' ? 'f' : f[0] == 't' ? 'T' : 't';
        bool fwd = f[0] == 'f' || f[0] == 't', till = f[0] == 't' || f[0] == 'T';
        std::string want = f.substr(1);
        int i = ed.cur.i;
        for (int k = 0; k < n; k++) {
            int j = i + (fwd ? 1 : -1) * (till && (repeat || k) ? 2 : 1);  // a repeated t jumps past the target it stops at
            while (j >= 0 && j < len(row) && (*row)[j].c != want) j += fwd ? 1 : -1;
            if (j < 0 || j >= len(row)) return true;  // not found: stay, and an operator gets an empty range
            i = till ? j + (fwd ? -1 : 1) : j;
        }
        ed.cur.i = i;
        inclusive = fwd;
    } else if (m == "%") {
        auto f = ed.parents.find(ed.cur.row);
        if (f != ed.parents.end()) ed.cur = {f->second.row, int(f->second.node - f->second.row->data())};
    } else return false;
    return true;
}

void Vi::deleteLines(Editor& ed, int a, int b, bool yankOnly) {
    b = std::min(b, int(ed.doc.size()) - 1);
    reg.lines = true;
    reg.doc.assign(ed.doc.begin() + a, ed.doc.begin() + b + 1);
    yanked();
    if (yankOnly) return;
    ed.doc.erase(ed.doc.begin() + a, ed.doc.begin() + b + 1);
    if (ed.doc.empty()) ed.doc.emplace_back();
    ed.cur = {&ed.doc[std::min(a, int(ed.doc.size()) - 1)], 0};
    ed.reindex();
}

// ---------- Normal ----------
void Vi::normalKey(Editor& ed, const std::string& c0, const Key& k) {
    std::string c = c0;
    if (k.sp == Key::Esc) { pending.clear(); ed.range.reset(); hlsearch = false; return; }
    auto link = [&] {  // the [[link]] under the cursor
        if (ed.inSlot()) return false;
        for (auto sp : linkSpans(*ed.cur.row))
            if (ed.cur.i >= sp.first && ed.cur.i < sp.second) return cmds.push_back("link " + linkTitle(*ed.cur.row, sp)), true;
        return false;
    };
    if (k.sp == Key::Enter && pending.empty()) {  // follows a [[link]], else moves down like vim
        bool lw, inc;
        if (!link()) motion(ed, "j", 1, false, true, lw, inc);
        return;
    }
    if (k.ctrl) {
        if (k.ch == "o") cmds.push_back("back");
        if (k.ch == "r") ed.redo();
        pending.clear();
        return;
    }
    if (c.empty()) return;
    if (leader()) { pending.clear(); return leaderKey(ed, c); }
    if (c == " " && pending.empty()) { pending = " "; return; }
    if (c == ">" || c == "<") {  // >> and <<: nest the line, or bring it back out
        if (pending != c) { pending = c; return; }
        pending.clear();
        ed.snap();
        ed.indent(lineIdx(ed), lineIdx(ed), c == ">" ? 1 : -1);
        return;
    }
    // r<c>: replace the item under the cursor by typing c there
    if (pending.ends_with("r")) {
        pending.clear();
        Row* row = ed.cur.row;
        int i = ed.cur.i;
        if (i >= len(row)) return;
        ed.snap();
        row->erase(row->begin() + i);
        ed.reindex();
        ed.insertChar(c);
        if (ed.cur.row == row) ed.cur.i = i;
        return;
    }
    // f/t/F/T<c>: the motion is the prefix and its target
    if (!pending.empty() && std::strchr("fFtT", pending.back())) {
        c = pending.back() + c;
        pending.pop_back();
    }
    // count
    if ((c >= "1" && c <= "9" && c.size() == 1) || (c == "0" && !pending.empty() && std::isdigit((unsigned char)pending.back()))) {
        pending += c;
        return;
    }
    size_t digits = 0;
    while (digits < pending.size() && std::isdigit((unsigned char)pending[digits])) digits++;
    bool hasCount = digits > 0;
    int count = hasCount ? std::max(1, std::stoi(pending.substr(0, digits))) : 1;
    std::string rest = pending.substr(digits);  // operator and/or g prefix
    std::string op = !rest.empty() && (rest[0] == 'd' || rest[0] == 'c' || rest[0] == 'y') ? rest.substr(0, 1) : "";
    std::string m = c;
    if (rest.ends_with("g")) {
        if (c == "f" && op.empty()) { pending.clear(); if (!link()) message = "No [[link]] under the cursor"; return; }
        if (c != "g") { pending.clear(); return; }
        m = "gg";
    } else if (c == "g" || c == "r" || c == "f" || c == "F" || c == "t" || c == "T") { pending += c; return; }

    if (!op.empty()) {
        pending.clear();
        int k0 = lineIdx(ed);
        if (m == op) {  // dd cc yy
            int b = k0 + count - 1;
            if (op == "y") return deleteLines(ed, k0, b, true);
            ed.snap();
            if (op == "c") {
                deleteLines(ed, k0, b, true);
                b = std::min(b, int(ed.doc.size()) - 1);
                ed.doc.erase(ed.doc.begin() + k0 + 1, ed.doc.begin() + b + 1);
                ed.doc[k0].clear();
                ed.cur = {&ed.doc[k0], 0};
                return toInsert(ed, true);
            }
            return deleteLines(ed, k0, b, false);
        }
        Cur from = ed.cur;
        bool linewise, inclusive;
        bool cw = op == "c" && m == "w" && ed.cur.i < len(ed.cur.row) && !isSpace((*ed.cur.row)[ed.cur.i]);
        if (cw) m = "e";  // cw is ce
        Editor::Snap before{ed.doc, ed.curPath()};
        if (!motion(ed, m, count, hasCount, false, linewise, inclusive)) return;
        if (linewise) {
            int k1 = lineIdx(ed);
            ed.cur = from;
            if (op == "y") return deleteLines(ed, std::min(k0, k1), std::max(k0, k1), true);
            ed.undoStack.push_back(std::move(before));
            ed.redoStack.clear();
            deleteLines(ed, std::min(k0, k1), std::max(k0, k1), false);
            if (op == "c") {
                ed.doc.insert(ed.doc.begin() + std::min(k0, k1), Row{});
                ed.cur = {&ed.doc[std::min(k0, k1)], 0};
                toInsert(ed, true);
            }
            return;
        }
        if (ed.cur.row != from.row && m == "w" && !hasCount) ed.cur = {from.row, len(from.row)}, inclusive = false;  // dw on a line's last word stops there, as in vim
        if (ed.cur.row != from.row && cw) ed.cur = from;  // so does cw
        if (ed.cur.row != from.row) {  // db at a line start, de past its end, d3w: charwise over the lines
            Cur to = ed.cur;
            ed.selectBetween(from, to);
            if (!ed.range || !ed.range->row2) { ed.range.reset(); ed.cur = from; return; }
            if (inclusive) ed.range->b = std::min(ed.range->b + (ed.lineOf(ed.range->row2) > ed.lineOf(ed.range->row)), len(ed.range->row2));
            reg.lines = false;
            reg.items.clear();
            reg.parts = ed.selectedLines();
            yanked();
            if (op == "y") { ed.collapseSelection(false); return; }
            ed.changes++;
            ed.undoStack.push_back(std::move(before));
            ed.redoStack.clear();
            ed.deleteSelection();
            if (op == "c") toInsert(ed, true);
            return;
        }
        int a = std::min(from.i, ed.cur.i), b = std::max(from.i, ed.cur.i) + (inclusive ? 1 : 0);
        b = std::min(b, len(from.row));
        reg.lines = false;
        reg.parts.clear();
        reg.items.assign(from.row->begin() + a, from.row->begin() + b);
        yanked();
        ed.cur = {from.row, a};
        if (op == "y") return;
        ed.undoStack.push_back(std::move(before));
        ed.redoStack.clear();
        from.row->erase(from.row->begin() + a, from.row->begin() + b);
        ed.reindex();
        if (op == "c") toInsert(ed, true);
        return;
    }
    pending.clear();

    bool linewise, inclusive;
    if (motion(ed, m, count, hasCount, true, linewise, inclusive)) return;
    Row* row = ed.cur.row;
    int i = ed.cur.i, k0 = lineIdx(ed);
    if (m == "d" || m == "c" || m == "y") { pending = (hasCount ? std::to_string(count) : "") + m; return; }
    if (m == "i") return toInsert(ed);
    if (m == "a") { ed.cur.i = std::min(i + 1, len(row)); return toInsert(ed); }
    if (m == "I") {  // in a table: the first cell, not before its pipe
        auto cells = ed.inSlot() ? std::vector<std::pair<int, int>>{} : tableCells(*row);
        ed.cur.i = lineStyle(*row).kind == Line::Table && !cells.empty() ? std::min(cells[0].first + 1, cells[0].second) : 0;
        return toInsert(ed);
    }
    if (m == "A") { ed.cur.i = len(row); return toInsert(ed); }
    if (m == "o" || m == "O") {
        ed.snap();
        int at = m == "o" ? k0 + 1 : k0;
        ed.doc.insert(ed.doc.begin() + at, Row{});
        ed.cur = {&ed.doc[at], 0};
        ed.reindex();
        LineStyle s = lineStyle(ed.doc[k0 + (m == "O")]);
        if (m == "o" && s.kind == Line::Table) ed.tableRow(ed.doc[k0], ed.doc[at]);
        else if (m == "o" && isList(s.kind)) ed.setLine(ed.doc[at], s.kind == Line::Done ? Line::Todo : s.kind, s.num + 1);
        return toInsert(ed, true);
    }
    if (m == "x" || m == "s" || m == "X") {
        int a = m == "X" ? std::max(i - count, 0) : i, b = m == "X" ? i : std::min(i + count, len(row));
        if (a >= b && m != "s") return;
        ed.snap();
        reg.lines = false;
        reg.items.assign(row->begin() + a, row->begin() + b);
        yanked();
        row->erase(row->begin() + a, row->begin() + b);
        ed.cur.i = a;
        ed.reindex();
        if (m == "s") toInsert(ed, true);
        return;
    }
    if (m == "D" || m == "C") {
        ed.snap();
        reg.lines = false;
        reg.items.assign(row->begin() + i, row->end());
        yanked();
        row->erase(row->begin() + i, row->end());
        ed.reindex();
        if (m == "C") toInsert(ed, true);
        return;
    }
    if (m == "S") {
        ed.snap();
        deleteLines(ed, k0, k0, true);
        ed.doc[k0].clear();
        ed.cur = {&ed.doc[k0], 0};
        return toInsert(ed, true);
    }
    if (m == "p" || m == "P") {
        ed.snap();
        if (std::string t = getClip ? getClip() : ""; !t.empty() && t != copied) {  // copied in another app
            if (m == "p" && ed.cur.i < len(ed.cur.row)) ed.cur.i++;
            return pasteText(ed, t);
        }
        for (int n = 0; n < count; n++) {
            if (reg.lines) {
                int at = m == "p" ? lineIdx(ed) + 1 : lineIdx(ed);
                ed.doc.insert(ed.doc.begin() + at, reg.doc.begin(), reg.doc.end());
                ed.cur = {&ed.doc[at], 0};
                ed.reindex();
            } else if (!reg.parts.empty()) {
                if (m == "p" && ed.cur.i < len(ed.cur.row)) ed.cur.i++;
                ed.insertLines(reg.parts);
            } else if (!reg.items.empty()) {
                if (m == "p" && ed.cur.i < len(ed.cur.row)) ed.cur.i++;
                ed.insertItems(reg.items);
                ed.cur.i--;
            }
        }
        return;
    }
    if (m == "J") {
        if (k0 + 1 >= int(ed.doc.size())) return;
        ed.snap();
        Row& a = ed.doc[k0];
        Row next = std::move(ed.doc[k0 + 1]);
        ed.doc.erase(ed.doc.begin() + k0 + 1);
        int at = len(&a);
        if (!a.empty() && !next.empty()) a.push_back(ch(" ", false));
        a.insert(a.end(), next.begin(), next.end());
        ed.cur = {&a, at};
        ed.reindex();
        return;
    }
    if (m == "u") { for (int n = 0; n < count; n++) ed.undo(); return; }
    if (m == "v") { mode = Mode::Visual; anchor = ed.cur; visualSelect(ed); return; }
    if (m == "V") { mode = Mode::VisualLine; anchorLine = k0; return; }
    if (m == ":" || m == "/" || m == "?") { mode = Mode::Command; prompt = m[0]; cmdline.clear(); return; }
    if (m == "n" || m == "N") { for (int n = 0; n < count; n++) searchNext(ed, m == "N"); return; }
}

// ---------- Visual / Visual line ----------
void Vi::visualKey(Editor& ed, const std::string& c, const Key& k) {
    bool lineMode = mode == Mode::VisualLine;
    auto leave = [&] {
        mode = Mode::Normal;
        ed.range.reset();
        pending.clear();
    };
    if (k.sp == Key::Esc || (c == "v" && !lineMode) || (c == "V" && lineMode)) return leave();
    if (c == "v" || c == "V") {  // switch between the two
        mode = c == "v" ? Mode::Visual : Mode::VisualLine;
        anchor = ed.cur;
        anchorLine = lineIdx(ed);
        ed.range.reset();
        if (mode == Mode::Visual) visualSelect(ed);
        return;
    }
    if (c.empty()) return;
    if (leader()) { pending.clear(); return leaderKey(ed, c); }
    if (c == " " && pending.empty()) { pending = " "; return; }
    std::string m = c == "g" ? "gg" : c;
    if (!pending.empty() && std::strchr("fFtT", pending[0])) m = pending + c;
    else if (c.size() == 1 && std::strchr("fFtT", c[0]) && pending.empty()) { pending = c; return; }
    else if (pending == "g" && c != "g") pending.clear();
    else if (c == "g" && pending.empty()) { pending = "g"; return; }
    pending.clear();
    bool linewise, inclusive;
    if (motion(ed, m, 1, false, true, linewise, inclusive)) {
        if (!lineMode) visualSelect(ed);
        return;
    }
    if (c == ">" || c == "<") {
        auto [a, b] = lines(ed);
        ed.snap();
        ed.indent(a, b, c == ">" ? 1 : -1);
        return leave();
    }
    if (c == "o" && !lineMode) {
        std::swap(anchor, ed.cur);
        visualSelect(ed);
        return;
    }
    bool del = c == "d" || c == "x" || c == "c" || c == "s", yank = c == "y";
    if (!del && !yank) return;
    if (lineMode) {
        auto [a, b] = *visualLines(ed);
        if (del) ed.snap();
        deleteLines(ed, a, b, yank);
        if (c == "c" || c == "s") {
            ed.doc.insert(ed.doc.begin() + a, Row{});
            ed.cur = {&ed.doc[a], 0};
            ed.reindex();
        }
        leave();
    } else {
        reg.lines = false;
        reg.items = ed.selectedItems();
        reg.parts.clear();
        if (ed.range && ed.range->row2) reg.parts = ed.selectedLines();
        yanked();
        if (del) { ed.snap(); ed.deleteSelection(); }
        else if (auto r = ed.selRange()) ed.cur = {ed.range->row, r->first};
        else ed.collapseSelection(false);
        leave();
    }
    if (c == "c" || c == "s") toInsert(ed, del);
}

// ---------- Command line ----------
void Vi::commandKey(Editor& ed, const Key& k) {
    if (k.sp == Key::Esc) { mode = Mode::Normal; prompt = ':'; return; }
    if (k.sp == Key::Backspace) {
        if (cmdline.empty()) { mode = Mode::Normal; prompt = ':'; return; }
        while (!cmdline.empty() && (cmdline.back() & 0xC0) == 0x80) cmdline.pop_back();  // whole UTF-8 sequence
        if (!cmdline.empty()) cmdline.pop_back();
        return;
    }
    if (k.sp == Key::Enter) {
        mode = Mode::Normal;
        if (prompt != ':') {
            if (!cmdline.empty()) lastSearch = cmdline;
            lastBack = prompt == '?';
            prompt = ':';
            hlsearch = true;
            return searchNext(ed, false);
        }
        std::string c = cmdline;
        while (!c.empty() && c.back() == ' ') c.pop_back();
        if (c.empty()) return;
        if (c == "noh" || c == "nohlsearch") { hlsearch = false; return; }
        if (std::all_of(c.begin(), c.end(), ::isdigit)) {
            int n = std::clamp(std::stoi(c), 1, int(ed.doc.size()));
            ed.cur = {&ed.doc[n - 1], 0};
            return;
        }
        std::string name = c.substr(0, c.find(' '));
        static const std::vector<std::string> known = {"w", "q", "q!", "wq", "x", "e", "new", "rename", "trash", "set", "export",
                                                       "image", "table", "rule", "section", "row", "col", "print", "tour", "import", "copytable", "saveas", "open", "help", "settings",
                                                       "palette", "outline", "zoom", "folder", "align", "indent", "outdent", "pasteimage", "close"};
        bool subst = name.starts_with("s/") || name.starts_with("%s/");
        if (!subst && std::find(known.begin(), known.end(), name) == known.end()) { message = "Not an editor command: " + c; return; }
        cmds.push_back(c);
        return;
    }
    if (k.sp == Key::None && !k.ctrl) cmdline += k.ch;
}

// ---------- clipboard and search ----------
void Vi::yanked() {
    bool many = reg.lines || !reg.parts.empty();
    copied = reg.lines ? markdown(reg.doc) : many ? markdown(reg.parts) : markdown(reg.items);
    if (many && copied.ends_with("\n")) copied.pop_back();
    if (setClip) setClip(copied);
}

// ponytail: pasted text is typed in, so it gets the same text/math reading as typing it ("a/b" becomes a fraction)
void Vi::pasteText(Editor& ed, const std::string& s) {
    if (s.find('\t') != std::string::npos && !ed.inSlot()) {  // cells copied from a spreadsheet: a table
        auto rows = tableFromText(s);
        int at = lineIdx(ed) + !ed.cur.row->empty();
        if (ed.cur.row->empty()) ed.doc.erase(ed.doc.begin() + at);
        ed.doc.insert(ed.doc.begin() + at, rows.begin(), rows.end());
        ed.cur = {&ed.doc[at + rows.size() - 1], 0};
        ed.cur.i = len(ed.cur.row);
        ed.reindex();
        return;
    }
    for (size_t i = 0; i < s.size();) {
        unsigned char b = s[i];
        size_t n = b < 0x80 ? 1 : b < 0xE0 ? 2 : b < 0xF0 ? 3 : 4;
        std::string c = s.substr(i, n);
        i += n;
        if (c == "\n") ed.enter();
        else if (c == "\t") ed.insertChar(" ");
        else if (c != "\r") ed.insertChar(c);
        ed.reindex();
    }
}

// matches in the text of top-level lines; math inside slots is one placeholder item and isn't searched
std::vector<Vi::Match> Vi::matches(const Editor& ed, const std::string& pattern) {
    std::vector<Match> out;
    if (pattern.empty()) return out;
    bool icase = std::none_of(pattern.begin(), pattern.end(), [](char c) { return std::isupper((unsigned char)c); });
    auto fold = [&](std::string t) {
        if (icase) for (auto& c : t) c = char(std::tolower((unsigned char)c));
        return t;
    };
    std::string want = fold(pattern);
    for (int k = 0; k < int(ed.doc.size()); k++) {  // ponytail: scans the whole doc, fine for notes
        std::string text;
        std::vector<int> item;  // item index of every byte
        for (int j = 0; j < len(&ed.doc[k]); j++) {
            const Item& it = ed.doc[k][j];
            std::string t = it.s.empty() && !it.c.empty() ? fold(it.c) : "\x01";
            text += t;
            item.insert(item.end(), t.size(), j);
        }
        for (size_t p = text.find(want); p != std::string::npos; p = text.find(want, p + 1))
            out.push_back({k, item[p], item[p + want.size() - 1] + 1});
    }
    return out;
}

std::string Vi::highlight() const {
    if (mode == Mode::Command && prompt != ':') return cmdline;  // as you type
    return hlsearch ? lastSearch : "";
}

bool Vi::enterPicks(const Editor& ed) const {
    if (navigated) return true;
    std::string w = ed.pendingWord();
    int i = ed.cur.i - int(w.size());
    return w.empty() || ed.inSlot() || (i >= 0 && i < len(ed.cur.row) && (*ed.cur.row)[i].m);
}

void Vi::searchNext(Editor& ed, bool reverse) {
    if (lastSearch.empty()) { message = "No previous search"; return; }
    bool back = lastBack != reverse;
    Cur at = ed.cur;  // where the cursor is in its top-level line
    for (auto f = ed.parents.find(at.row); f != ed.parents.end(); f = ed.parents.find(at.row))
        at = {f->second.row, int(f->second.node - f->second.row->data())};
    std::pair<int, int> here{ed.lineOf(at.row), at.i};
    std::vector<std::pair<int, int>> hits;
    for (auto& m : matches(ed, lastSearch)) hits.push_back({m.line, m.a});
    if (hits.empty()) { message = "Pattern not found: " + lastSearch; return; }
    std::pair<int, int> to;
    if (!back) {
        auto h = std::upper_bound(hits.begin(), hits.end(), here);
        if (h == hits.end()) message = "search hit BOTTOM, continuing at TOP", h = hits.begin();
        to = *h;
    } else {
        auto h = std::lower_bound(hits.begin(), hits.end(), here);
        if (h == hits.begin()) message = "search hit TOP, continuing at BOTTOM", h = hits.end();
        to = *--h;
    }
    ed.range.reset();
    ed.cur = {&ed.doc[to.first], to.second};
}

}  // namespace om
