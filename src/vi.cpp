#include "vi.hpp"
#include "math.hpp"
#include <algorithm>
#include <map>

namespace om {

static int lineIdx(const Editor& ed) { return ed.lineOf(ed.topOf(ed.cur.row)); }
static int len(const Row* r) { return int(r->size()); }

void Vi::refresh(Editor& ed) {
    ed.reindex();
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
        ed.cur = dragFrom = p;
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
        if (s[i] == '<' && close != std::string::npos && close > i + 1) {
            std::string t = s.substr(i + 1, close - i - 1);
            i = close + 1;
            if (t == "Esc") k.sp = Key::Esc;
            else if (t == "CR") k.sp = Key::Enter;
            else if (t == "S-CR") k.sp = Key::Enter, k.shift = true;
            else if (t == "S-Space") k.ch = " ", k.shift = true;
            else if (t == "Space") k.ch = " ";
            else if (t == "Tab") k.sp = Key::Tab;
            else if (t == "BS") k.sp = Key::Backspace;
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
    const Row& r = *ed.cur.row;  // keep typing in the style of the text before the cursor
    int i = ed.cur.i;
    ed.style = i > 0 && r[i - 1].t == T::Ch && !r[i - 1].m && !isSpace(r[i - 1]) ? r[i - 1].f : 0;
}

// ---------- formatting ----------
std::pair<int, int> Vi::lines(const Editor& ed) const {
    if (auto v = visualLines(ed)) return *v;
    int k = ed.range ? ed.lineOf(ed.topOf(ed.range->row)) : lineIdx(ed);
    return {k, k};
}

void Vi::format(Editor& ed, uint8_t flag) {
    auto done = [&] {
        if (mode == Mode::Visual || mode == Mode::VisualLine) mode = Mode::Normal, ed.range.reset();
    };
    if (mode == Mode::VisualLine) {
        auto [a, b] = lines(ed);
        ed.snap();
        for (int k = a; k <= b; k++) ed.toggleFmt(ed.doc[k], lineStyle(ed.doc[k]).len, int(ed.doc[k].size()), flag);
        return done();
    }
    if (auto s = ed.selRange()) {
        ed.snap();
        ed.toggleFmt(*ed.range->row, s->first, s->second, flag);
        return done();
    }
    if (mode == Mode::Insert) {
        ed.style = flag ? ed.style ^ flag : 0;
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

void Vi::leaderKey(Editor& ed, const std::string& c) {
    static const std::map<std::string, uint8_t> FMT = {{"b", Bold}, {"i", Italic}, {"u", Underline}, {"s", Strike},
                                                       {"c", Code}, {"h", Mark}, {"r", 0}};
    static const std::map<std::string, Line> LINE = {{"0", Line::Text}, {"1", Line::H1}, {"2", Line::H2}, {"3", Line::H3},
                                                     {"-", Line::Bullet}, {"n", Line::Number}, {">", Line::Quote}, {"t", Line::Todo}};
    if (auto f = FMT.find(c); f != FMT.end()) format(ed, f->second);
    else if (auto l = LINE.find(c); l != LINE.end()) setLine(ed, l->second);
    else if (c == "x") toggleDone(ed);
    else if (c == "m") ed.mathMode = !ed.mathMode;
    else if (c == "e") cmds.push_back("sidebar");
    else message = "Space " + c + " is not mapped";
}

// charwise Visual includes the item under the cursor, like vim
void Vi::visualSelect(Editor& ed) {
    ed.selectBetween(anchor, ed.cur);
    int n = len(ed.cur.row);
    if (!ed.range) {
        if (ed.cur.i < n) ed.range = Range{ed.cur.row, ed.cur.i, ed.cur.i + 1};
    } else if (ed.range->a < ed.range->b) ed.range->b = std::min(ed.range->b + 1, n);
    else ed.range->a = std::min(ed.range->a + 1, n);
}

void Vi::key(Editor& ed, const Key& k) {
    message.clear();
    if (k.ctrl && k.ch == "\\") cmds.push_back("sidebar");
    else if (k.ctrl && (k.ch == "b" || k.ch == "i" || k.ch == "u") && mode != Mode::Command)
        format(ed, k.ch == "b" ? Bold : k.ch == "i" ? Italic : Underline);
    else if (k.ctrl && k.ch == "s") cmds.push_back("w");
    else if (k.ctrl && k.ch == "m") ed.mathMode = !ed.mathMode;
    else if (mode == Mode::Insert) insertKey(ed, k);
    else if (mode == Mode::Command) commandKey(ed, k);
    else {
        // arrows and friends double as motions outside Insert mode
        std::string c = k.ch;
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
    auto sel = ed.selRange();
    int n = int(items.size());
    if (k.ctrl) {
        if (k.ch.size() == 1 && k.ch[0] >= '1' && k.ch[0] <= '9') pickItem(ed, k.ch[0] - '1');
        else if (n && (k.ch == "n" || k.ch == "p")) ed.pick = (ed.pick + (k.ch == "n" ? 1 : n - 1)) % n;
        else if (k.ch == "a") ed.selectAll();
        return;
    }
    switch (k.sp) {
    case Key::Esc:
        ed.range.reset();
        ed.dismissed = ed.popupKey();
        mode = Mode::Normal;
        return;
    case Key::Enter:
        if (k.shift) { if (!ed.addMatrixRow()) ed.enter(); }
        else if (n && !items[0].insert) pickItem(ed, ed.pick);
        else { ed.deleteSelection(); if (!ed.listEnter()) ed.enter(); }
        return;
    case Key::Tab:
        if (n && items[0].insert) pickItem(ed, ed.pick);
        else if (auto g = ghostHere()) ed.insertItems(g->insert);
        else ed.exitSlot();
        return;
    case Key::Up: case Key::Down:
        if (n) ed.pick = (ed.pick + (k.sp == Key::Down ? 1 : n - 1)) % n;
        else { ed.range.reset(); ed.vertical(k.sp == Key::Up ? -1 : 1); }
        return;
    case Key::Left: case Key::Right:
        if (k.shift) ed.extendSelection(k.sp == Key::Left ? -1 : 1);
        else if (sel) { ed.cur.i = k.sp == Key::Left ? sel->first : sel->second; ed.range.reset(); }
        else if (k.sp == Key::Left) ed.left();
        else ed.right();
        return;
    case Key::Home: case Key::End: ed.range.reset(); ed.home(k.sp == Key::End); return;
    case Key::Backspace: if (!ed.deleteSelection()) ed.backspace(); return;
    case Key::Delete:
        if (!ed.deleteSelection() && ed.cur.i < len(ed.cur.row) && (*ed.cur.row)[ed.cur.i].s.empty())
            ed.cur.row->erase(ed.cur.row->begin() + ed.cur.i);
        return;
    case Key::None:
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
            else if (m == "h" && !(ed.cur.i == 0 && !ed.inSlot())) ed.left();
            else if (m == "l" && !(ed.cur.i == len(ed.cur.row) && !ed.inSlot())) ed.right();
            ed.reindex();
        }
    } else if (m == "j" || m == "k") {
        for (int k = 0; k < n; k++) ed.vertical(m == "j" ? 1 : -1);
        linewise = true;
    } else if (m == "w") {
        for (int k = 0; k < n; k++) {
            int i = ed.cur.i;
            while (i < len(row) && !sp(i)) i++;
            while (i < len(row) && sp(i)) i++;
            ed.cur.i = i;
        }
    } else if (m == "e") {
        for (int k = 0; k < n; k++) {
            int i = ed.cur.i + 1;
            while (i < len(row) && sp(i)) i++;
            while (i + 1 < len(row) && !sp(i + 1)) i++;
            ed.cur.i = std::min(i, std::max(len(row) - 1, 0));
        }
        inclusive = true;
    } else if (m == "b") {
        for (int k = 0; k < n; k++) {
            int i = ed.cur.i;
            while (i > 0 && sp(i - 1)) i--;
            while (i > 0 && !sp(i - 1)) i--;
            ed.cur.i = i;
        }
    } else if (m == "0") ed.cur.i = 0;
    else if (m == "^") {
        int i = 0;
        while (i < len(row) && sp(i)) i++;
        ed.cur.i = i;
    } else if (m == "$") ed.cur.i = len(row);
    else if (m == "gg" || m == "G") {
        int k = hasCount ? n - 1 : m == "gg" ? 0 : int(ed.doc.size()) - 1;
        ed.cur = {&ed.doc[std::clamp(k, 0, int(ed.doc.size()) - 1)], 0};
        linewise = true;
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
    if (yankOnly) return;
    ed.doc.erase(ed.doc.begin() + a, ed.doc.begin() + b + 1);
    if (ed.doc.empty()) ed.doc.emplace_back();
    ed.cur = {&ed.doc[std::min(a, int(ed.doc.size()) - 1)], 0};
    ed.reindex();
}

// ---------- Normal ----------
void Vi::normalKey(Editor& ed, const std::string& c, const Key& k) {
    if (k.sp == Key::Esc) { pending.clear(); ed.range.reset(); return; }
    if (k.ctrl) {
        if (k.ch == "r") ed.redo();
        pending.clear();
        return;
    }
    if (c.empty()) return;
    if (leader()) { pending.clear(); return leaderKey(ed, c); }
    if (c == " " && pending.empty()) { pending = " "; return; }
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
        if (c != "g") { pending.clear(); return; }
        m = "gg";
    } else if (c == "g" || c == "r") { pending += c; return; }

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
        if (op == "c" && m == "w" && ed.cur.i < len(ed.cur.row) && !isSpace((*ed.cur.row)[ed.cur.i])) m = "e";  // cw is ce
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
        if (ed.cur.row != from.row) { ed.cur = from; return; }  // ponytail: operators work inside one row
        int a = std::min(from.i, ed.cur.i), b = std::max(from.i, ed.cur.i) + (inclusive ? 1 : 0);
        b = std::min(b, len(from.row));
        reg.lines = false;
        reg.items.assign(from.row->begin() + a, from.row->begin() + b);
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
    if (m == "I") { ed.cur.i = 0; return toInsert(ed); }
    if (m == "A") { ed.cur.i = len(row); return toInsert(ed); }
    if (m == "o" || m == "O") {
        ed.snap();
        int at = m == "o" ? k0 + 1 : k0;
        ed.doc.insert(ed.doc.begin() + at, Row{});
        ed.cur = {&ed.doc[at], 0};
        ed.reindex();
        LineStyle s = lineStyle(ed.doc[k0 + (m == "O")]);
        if (m == "o" && s.kind >= Line::Bullet) ed.setLine(ed.doc[at], s.kind == Line::Done ? Line::Todo : s.kind, s.num + 1);
        return toInsert(ed, true);
    }
    if (m == "x" || m == "s" || m == "X") {
        int a = m == "X" ? std::max(i - count, 0) : i, b = m == "X" ? i : std::min(i + count, len(row));
        if (a >= b && m != "s") return;
        ed.snap();
        reg.lines = false;
        reg.items.assign(row->begin() + a, row->begin() + b);
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
        for (int n = 0; n < count; n++) {
            if (reg.lines) {
                int at = m == "p" ? lineIdx(ed) + 1 : lineIdx(ed);
                ed.doc.insert(ed.doc.begin() + at, reg.doc.begin(), reg.doc.end());
                ed.cur = {&ed.doc[at], 0};
                ed.reindex();
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
    if (m == ":") { mode = Mode::Command; cmdline.clear(); return; }
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
    if (pending == "g" && c != "g") pending.clear();
    if (c == "g" && pending.empty()) { pending = "g"; return; }
    std::string m = c == "g" ? "gg" : c;
    pending.clear();
    bool linewise, inclusive;
    if (motion(ed, m, 1, false, true, linewise, inclusive)) {
        if (!lineMode) visualSelect(ed);
        return;
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
        if (del) { ed.snap(); ed.deleteSelection(); }
        else if (auto r = ed.selRange()) ed.cur = {ed.range->row, r->first};
        leave();
    }
    if (c == "c" || c == "s") toInsert(ed, del);
}

// ---------- Command line ----------
void Vi::commandKey(Editor& ed, const Key& k) {
    if (k.sp == Key::Esc) { mode = Mode::Normal; return; }
    if (k.sp == Key::Backspace) {
        if (cmdline.empty()) { mode = Mode::Normal; return; }
        while (!cmdline.empty() && (cmdline.back() & 0xC0) == 0x80) cmdline.pop_back();  // whole UTF-8 sequence
        if (!cmdline.empty()) cmdline.pop_back();
        return;
    }
    if (k.sp == Key::Enter) {
        mode = Mode::Normal;
        std::string c = cmdline;
        while (!c.empty() && c.back() == ' ') c.pop_back();
        if (c.empty()) return;
        if (std::all_of(c.begin(), c.end(), ::isdigit)) {
            int n = std::clamp(std::stoi(c), 1, int(ed.doc.size()));
            ed.cur = {&ed.doc[n - 1], 0};
            return;
        }
        std::string name = c.substr(0, c.find(' '));
        static const std::vector<std::string> known = {"w", "q", "q!", "wq", "x", "e", "new", "rename", "trash", "set"};
        if (std::find(known.begin(), known.end(), name) == known.end()) { message = "Not an editor command: " + c; return; }
        cmds.push_back(c);
        return;
    }
    if (k.sp == Key::None && !k.ctrl) cmdline += k.ch;
}

}  // namespace om
