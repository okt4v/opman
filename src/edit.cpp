#include "edit.hpp"
#include "math.hpp"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <sstream>

namespace om {

// ---------- catalog ----------
const std::map<std::string, unsigned> CATS = {
    {"Structure", 0x7c5cff}, {"Calculus", 0xe0539a}, {"Operator", 0x3b82f6}, {"Relation", 0x0d9488}, {"Set", 0x16a34a},
    {"Logic", 0xd97706}, {"Arrow", 0x64748b}, {"Greek", 0x9333ea}, {"Function", 0x2563eb}, {"Variable", 0x059669},
    {"Constant", 0xc2410c}, {"Result", 0x16a34a}, {"Steps", 0x0891b2}, {"Decimal", 0x65a30d}, {"Algebra", 0xdb2777},
    {"Solve", 0xea580c}, {"Units", 0x0d9488}, {"Program", 0x0f766e}};

// name|keywords|symbol|latex|category  (single-character keywords like E, A are Corca-style ASCII shortcuts)
static const char* SYMS = R"(Plus|plus add|+|+|Operator
Minus|minus subtract|−|-|Operator
Times|times multiply cross|×|\times|Operator
Dot|dot multiply cdot|⋅|\cdot|Operator
Division|divide division obelus|÷|\div|Operator
Plus-minus|plusminus pm +-|±|\pm|Operator
Minus-plus|minusplus mp -+|∓|\mp|Operator
Equals|equals equal|=|=|Relation
Not equal|notequal neq ne !=|≠|\neq|Relation
Approximately|approx approximately ~=|≈|\approx|Relation
Equivalent|equiv equivalent identical congruent ===|≡|\equiv|Relation
Defined as|define definition coloneq :=|≔|\coloneqq|Relation
Proportional to|proportional propto oc|∝|\propto|Relation
Less than|less lt|<|<|Relation
Greater than|greater gt|>|>|Relation
Less or equal|leq le lessequal <=|≤|\leq|Relation
Greater or equal|geq ge greaterequal >=|≥|\geq|Relation
Much less|muchless ll <<|≪|\ll|Relation
Much greater|muchgreater gg >>|≫|\gg|Relation
Infinity|infinity infty oo|∞|\infty|Calculus
Partial|partial derivative d|∂|\partial|Calculus
Nabla|nabla gradient del|∇|\nabla|Calculus
Element of|in element member E|∈|\in|Set
Not element of|notin notelement E/|∉|\notin|Set
Subset|subset|⊂|\subset|Set
Subset or equal|subseteq subset|⊆|\subseteq|Set
Superset|superset supset|⊃|\supset|Set
Union|union cup U|∪|\cup|Set
Intersection|intersection cap|∩|\cap|Set
Set minus|setminus difference|∖|\setminus|Set
Empty set|emptyset empty O/|∅|\emptyset|Set
Real numbers|reals real R|ℝ|\mathbb{R}|Set
Natural numbers|naturals natural N|ℕ|\mathbb{N}|Set
Integers|integers integer Z|ℤ|\mathbb{Z}|Set
Rationals|rationals rational Q|ℚ|\mathbb{Q}|Set
Complex numbers|complex C|ℂ|\mathbb{C}|Set
Aleph|aleph cardinal|ℵ|\aleph|Set
For all|forall all A|∀|\forall|Logic
Exists|exists E|∃|\exists|Logic
Not|not neg|¬|\neg|Logic
And|and wedge|∧|\land|Logic
Or|or vee|∨|\lor|Logic
Implies|implies then =>|⇒|\Rightarrow|Logic
If and only if|iff equivalent <=>|⇔|\Leftrightarrow|Logic
Therefore|therefore|∴|\therefore|Logic
Because|because since|∵|\because|Logic
Right arrow|to arrow right tends ->|→|\to|Arrow
Left arrow|leftarrow gets arrow <-|←|\leftarrow|Arrow
Left right arrow|leftrightarrow arrow <->|↔|\leftrightarrow|Arrow
Maps to|mapsto maps|↦|\mapsto|Arrow
Dots|dots ldots ellipsis ...|…|\ldots|Operator
Centered dots|cdots dots|⋯|\cdots|Operator
Perpendicular|perp perpendicular orthogonal|⊥|\perp|Relation
Parallel|parallel|∥|\parallel|Relation
Angle|angle|∠|\angle|Operator
Degree|degree|°|^\circ|Operator
Direct sum|oplus directsum|⊕|\oplus|Operator
Tensor product|otimes tensor|⊗|\otimes|Operator
Composition|compose composition circ ring|∘|\circ|Operator
Dagger|dagger adjoint|†|^\dagger|Operator
Prime|prime|′|'|Operator
Reduced Planck|hbar planck|ℏ|\hbar|Constant)";

static const char* FNS = "sin sine,cos cosine,tan tangent,cot cotangent,sec secant,csc cosecant,arcsin,arccos,arctan,sinh,cosh,tanh,"
                         "log logarithm,ln natural logarithm,exp exponential,det determinant,dim dimension,ker kernel,max maximum,"
                         "min minimum,sup supremum,inf infimum,gcd,lcm,arg argument,round,sign,mod modulo remainder,mean average,"
                         "median,stdev standard deviation,var variance,sum total,count,nCr choose binomial combinations,"
                         "nPr permutations,fit regression trend";

static std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    for (std::string x; std::getline(ss, x, sep);) out.push_back(x);
    return out;
}

static std::vector<Entry> buildCatalog() {
    std::vector<Entry> c;
    auto add = [&](std::string name, std::string kw, Item make, std::string cat) {
        c.push_back({std::move(name), split(kw, ' '), std::move(make), std::move(cat), std::nullopt});
    };
    auto two = [] { return std::vector<std::vector<Row>>{{{}, {}}, {{}, {}}}; };
    add("Fraction", "fraction frac division over", node(T::Frac), "Structure");
    add("Power", "power exponent superscript squared", node(T::Sup), "Structure");
    add("Subscript", "subscript index", node(T::Sub), "Structure");
    add("Square root", "sqrt root square", node(T::Sqrt), "Structure");
    add("Parentheses", "parentheses paren brackets", paren("(", ")"), "Structure");
    add("Square brackets", "brackets square", paren("[", "]"), "Structure");
    add("Braces", "braces set curly", paren("{", "}"), "Structure");
    add("Absolute value", "abs absolute modulus", paren("|", "|"), "Structure");
    add("Norm", "norm", paren("‖", "‖"), "Structure");
    add("Floor", "floor", paren("⌊", "⌋"), "Structure");
    add("Ceiling", "ceiling ceil", paren("⌈", "⌉"), "Structure");
    add("Summation", "sum summation sigma series S", big("∑", "\\sum", true), "Calculus");
    add("Product", "product prod Pr", big("∏", "\\prod", true), "Calculus");
    add("Integral", "integral int", big("∫", "\\int", false), "Calculus");
    add("Double integral", "iint double integral", big("∬", "\\iint", false), "Calculus");
    add("Triple integral", "iiint triple integral", big("∭", "\\iiint", false), "Calculus");
    add("Contour integral", "oint contour integral", big("∮", "\\oint", false), "Calculus");
    add("Big union", "bigcup union", big("⋃", "\\bigcup", true), "Set");
    add("Big intersection", "bigcap intersection", big("⋂", "\\bigcap", true), "Set");
    add("Limit", "limit lim", node(T::Lim), "Calculus");
    auto grid = [](int r, int c) { return std::vector<std::vector<Row>>(r, std::vector<Row>(c)); };
    add("Matrix", "matrix pmatrix", mat("(", ")", two()), "Structure");
    add("Matrix 3×3", "matrix 3x3 pmatrix", mat("(", ")", grid(3, 3)), "Structure");
    add("Column vector", "vector column matrix", mat("(", ")", grid(3, 1)), "Structure");
    add("Row vector", "vector row matrix", mat("(", ")", grid(1, 3)), "Structure");
    add("Determinant", "determinant vmatrix", mat("|", "|", two()), "Structure");
    add("Determinant 3×3", "determinant 3x3 vmatrix", mat("|", "|", grid(3, 3)), "Structure");
    for (auto& line : split(SYMS, '\n')) {
        auto f = split(line, '|');
        add(f[0], f[1], sym(f[2], f[3]), f[4]);
    }
    for (auto& f : split(FNS, ',')) {
        auto v = split(f, ' ')[0];
        add(v, f, named(T::Fn, v), "Function");
    }
    for (auto& [n, g] : GREEK) add(g + " " + n, n, named(T::Var, g), "Greek");
    // program functions: blocks for a whole line, offered only for a word alone at the start of a line (see suggestions)
    add("plot", "plot graph chart", named(T::Fn, "plot"), "Program");
    add("table", "table grid", Item{}, "Program");
    add("section", "section exercise", Item{}, "Program");
    add("divider", "divider rule line hr", Item{}, "Program");
    return c;
}
const std::vector<Entry> CATALOG = buildCatalog();

static const Entry& entryNamed(const std::string& name) {
    return *std::find_if(CATALOG.begin(), CATALOG.end(), [&](const Entry& e) { return e.name == name; });
}

static const std::map<std::string, std::string> SYM_TEX = [] {
    std::map<std::string, std::string> m;
    for (auto& e : CATALOG)
        if (e.make.t == T::Sym) m[e.make.c] = e.make.l;
    return m;
}();

// math words that convert on their own once you type past them (in math only): functions, greek names, sqrt, infty
static const std::map<std::string, const Entry*> AUTO = [] {
    std::map<std::string, const Entry*> m;
    for (auto& e : CATALOG)
        if (e.cat == "Function" || (e.cat == "Greek" && (e.kw[0].size() > 2 || e.kw[0] == "pi"))) m[e.kw[0]] = &e;
    m["sqrt"] = &CATALOG[3];
    m["infty"] = &entryNamed("Infinity");
    return m;
}();
static const Entry* autoEntry(const std::string& w) {
    auto f = AUTO.find(w);
    return f == AUTO.end() ? nullptr : f->second;
}

// typed ASCII sequences that turn into one symbol in math
static const std::map<std::string, std::string> COMBOS = {
    {"->", "→"}, {"<-", "←"}, {"←>", "↔"}, {"<=", "≤"}, {">=", "≥"}, {"≤>", "⇔"}, {"=>", "⇒"}, {"!=", "≠"},
    {"~=", "≈"}, {"+-", "±"}, {"-+", "∓"}, {"===", "≡"}, {"...", "…"}, {"<<", "≪"}, {">>", "≫"}, {":=", "≔"}};

static std::string lower(std::string s) {
    for (auto& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}
static bool isSubseq(const std::string& q, const std::string& s) {
    size_t j = 0;
    for (char c : s)
        if (j < q.size() && c == q[j]) j++;
    return j == q.size();
}

CompleteOpts completeOpts;

// ponytail: linear scan with prefix scoring; fine for a few hundred entries
std::vector<Entry> Editor::search(const std::string& q) const {
    std::string ql = lower(q);
    std::vector<std::pair<int, const Entry*>> scored;
    for (auto& e : CATALOG) {
        int best = 0;
        for (auto& k : e.kw) {
            std::string kl = lower(k);
            int s = k == q ? 100 : kl == ql ? 90 : kl.starts_with(ql) ? 60 - int(kl.size() - ql.size())
                  : ql.size() > 1 && kl.find(ql) != std::string::npos ? 20 : ql.size() > 2 && isSubseq(ql, kl) ? 8 : 0;
            best = std::max(best, s);
        }
        auto u = usage.find(e.name);
        if (best) scored.push_back({best + (completeOpts.learn ? std::min(u == usage.end() ? 0 : u->second, 10) * 2 : 0), &e});
    }
    std::stable_sort(scored.begin(), scored.end(), [](auto& a, auto& b) { return a.first > b.first; });
    std::vector<Entry> hits;
    for (size_t k = 0; k < scored.size() && k < 7; k++) hits.push_back(*scored[k].second);
    std::vector<Entry> vars = {{"Variable " + q, {}, named(T::Var, q), "Variable", std::nullopt},
                               {"Constant " + q, {}, named(T::Const, q), "Constant", std::nullopt}};
    if (q.size() == 1) { vars.insert(vars.end(), hits.begin(), hits.end()); return vars; }
    hits.insert(hits.end(), vars.begin(), vars.end());
    return hits;
}

// ---------- model ----------
void Editor::reindex() {
    gen++;
    parents.clear();
    auto walk = [&](auto& self, Row& row) -> void {
        for (auto& n : row)
            for (int s = 0; s < int(n.s.size()); s++) {
                parents[&n.s[s]] = {&n, &row, s};
                self(self, n.s[s]);
            }
    };
    for (auto& r : doc) walk(walk, r);
}

static int idxIn(const Row* row, const Item* n) { return int(n - row->data()); }

Row* Editor::topOf(Row* row) const {
    for (auto f = parents.find(row); f != parents.end(); f = parents.find(row)) row = f->second.row;
    return row;
}
int Editor::lineOf(const Row* row) const {
    return row >= doc.data() && row < doc.data() + doc.size() ? int(row - doc.data()) : -1;
}

Path Editor::curPath() const {
    Path p;
    const Row* r = cur.row;
    for (auto f = parents.find(r); f != parents.end(); f = parents.find(r)) {
        p.p.insert(p.p.begin(), {idxIn(f->second.row, f->second.node), f->second.slot});
        r = f->second.row;
    }
    p.k = lineOf(r);
    p.i = cur.i;
    return p;
}

void Editor::setPath(const Path& p) {
    cur = {&doc[0], 0};
    if (p.k < 0 || p.k >= int(doc.size())) return;
    Row* r = &doc[p.k];
    for (auto [j, s] : p.p) {
        if (j < 0 || j >= int(r->size()) || s < 0 || s >= int((*r)[j].s.size())) return;
        r = &(*r)[j].s[s];
    }
    cur = {r, std::clamp(p.i, 0, int(r->size()))};
}

void Editor::load(Doc d, const Path& p) {
    doc = d.empty() ? Doc{Row{}} : std::move(d);
    range.reset();
    reindex();
    setPath(p);
}

static double nowMs() {
    using namespace std::chrono;
    return double(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

// kind "char": consecutive letters/digits typed within a second share one undo step (undo works per word)
void Editor::snap(const std::string& kind) {
    changes++;
    double now = nowMs();
    bool merge = kind == "char" && lastSnapKind == "char" && now - lastSnapAt < 1000;
    lastSnapKind = kind;
    lastSnapAt = now;
    redoStack.clear();
    if (merge) return;
    undoStack.push_back({doc, curPath()});
    if (undoStack.size() > 500) undoStack.erase(undoStack.begin());
}

void Editor::undo() {
    if (undoStack.empty()) return;
    Snap s = std::move(undoStack.back());
    undoStack.pop_back();
    redoStack.push_back({doc, curPath()});
    load(std::move(s.doc), s.path);
    lastSnapKind = "";
    lastSnapAt = -1e9;
}

void Editor::redo() {
    if (redoStack.empty()) return;
    Snap s = std::move(redoStack.back());
    redoStack.pop_back();
    undoStack.push_back({doc, curPath()});
    load(std::move(s.doc), s.path);
    lastSnapKind = "";
    lastSnapAt = -1e9;
}

// ---------- completion ----------
std::string Editor::pendingWord() const {
    const Row& row = *cur.row;
    int j = cur.i;
    while (j > 0 && row[j - 1].t == T::Ch && isLetter(row[j - 1].c)) j--;
    std::string w;
    for (int k = j; k < cur.i; k++) w += row[k].c;
    return w;
}

std::string Editor::popupKey() const { return nlohmann::json::array({pathJson(curPath()), pendingWord()}).dump(); }

std::vector<Entry> Editor::suggestions() {
    if (popupKey() == dismissed) return {};
    std::string w = pendingWord();
    if (!forced && (!completeOpts.popup || (!w.empty() && int(w.size()) < completeOpts.minLetters))) return {};
    auto cap = [](std::vector<Entry> v) {
        if (int(v.size()) > completeOpts.maxItems) v.resize(completeOpts.maxItems);
        return v;
    };
    if (w.empty()) return cap(evalSuggestions(*this));
    auto hits = cap(search(w));
    // program functions only for a word alone at the start of a top-level line, so "plot" in a sentence stays text
    bool alone = !inSlot() && cur.i == int(w.size()) && cur.i == int(cur.row->size());
    if (!alone) std::erase_if(hits, [](auto& e) { return e.cat == "Program"; });
    // plain text offers no symbols: only the program functions (Ctrl-Space still opens everything)
    if (!inSlot() && !mathMode && !autoMath && !forced && !(*cur.row)[cur.i - 1].m) std::erase_if(hits, [](auto& e) { return e.cat != "Program"; });
    // a plain text word with nothing to offer but "Variable notes" gets no popup, so Enter still makes a new line
    bool textWord = w.size() > 1 && !(*cur.row)[cur.i - 1].m;
    bool onlyVars = std::all_of(hits.begin(), hits.end(), [](auto& e) { return e.cat == "Variable" || e.cat == "Constant"; });
    return textWord && onlyVars && !forced ? std::vector<Entry>{} : hits;
}

void Editor::insertNode(Item n) {
    Row* row = cur.row;
    int i = cur.i;
    row->insert(row->begin() + i, std::move(n));
    reindex();
    Item& placed = (*row)[i];
    cur = placed.s.empty() ? Cur{row, i + 1} : Cur{&placed.s[0], 0};
}

void Editor::applySuggestion(const Entry& e) {
    int n = int(pendingWord().size());  // the word is ASCII letters: bytes = items
    cur.row->erase(cur.row->begin() + (cur.i - n), cur.row->begin() + cur.i);
    cur.i -= n;
    usage[e.name]++;
    if (e.cat == "Program" && e.name != "plot") {  // a whole-line block in place of the word
        Row& line = *cur.row;
        int sections = 1;
        for (auto& r : doc) sections += lineStyle(r).kind == Line::Section;
        line.clear();
        if (e.name == "table") return tableRow(Row{ch("|", false), ch("|", false), ch("|", false)}, line);
        for (char c : e.name == "divider" ? std::string("---") : "=== Exercise " + std::to_string(sections)) line.push_back(ch(std::string(1, c), false));
        cur.i = int(line.size());
        enter();  // keep writing below it
        return;
    }
    if (e.name == "plot") {
        insertNode(e.make);
        return insertChar(" ");
    }
    if (!e.insert) return insertNode(e.make);
    if (e.eraseBack) {
        cur.row->erase(cur.row->begin() + (cur.i - e.eraseBack), cur.row->begin() + cur.i);
        cur.i -= e.eraseBack;
    }
    cur.row->insert(cur.row->begin() + cur.i, e.insert->begin(), e.insert->end());
    cur.i += int(e.insert->size());
    if (!e.below.empty() && !inSlot()) {
        int k = lineOf(cur.row);
        doc.insert(doc.begin() + k + 1, e.below.begin(), e.below.end());
        Row& last = doc[k + e.below.size()];
        cur = {&last, int(last.size())};
    }
    reindex();
}

// a math word you typed past (e.g. "sin" then "(") becomes its node; returns the created node if it has slots
Item* Editor::autoConvert() {
    std::string w = pendingWord();
    const Entry* e = completeOpts.convert ? autoEntry(w) : nullptr;
    if (!e || !(*cur.row)[cur.i - 1].m) return nullptr;
    applySuggestion(*e);
    if (e->make.s.empty()) return nullptr;
    return parents.at(cur.row).node;
}

static const std::map<std::string, std::string> FROM_SUP = {{"⁰", "0"}, {"¹", "1"}, {"²", "2"}, {"³", "3"}, {"⁴", "4"},
                                                            {"⁵", "5"}, {"⁶", "6"}, {"⁷", "7"}, {"⁸", "8"}, {"⁹", "9"},
                                                            {"⁻", "-"}, {"ⁿ", "n"}};

static bool in(const char* set, const std::string& c) { return !c.empty() && std::string(set).find(c) != std::string::npos; }
static bool alnum(const std::string& c) { return isLetter(c) || isDigit(c); }
// corca reads `.c` off any item: the char of a ch, the closing delimiter of a paren/matrix
static std::string jsC(const Item& it) { return it.t == T::Ch ? it.c : (it.t == T::Paren || it.t == T::Mat) ? it.cl : ""; }

void Editor::insertChar(std::string c) {
    if (!inSlot() && lineStyle(*cur.row).kind == Line::Rule) {  // a divider holds no text: typing starts the line below it
        int k = lineOf(cur.row);
        doc.insert(doc.begin() + k + 1, Row{});
        cur = {&doc[k + 1], 0};
        reindex();
    }
    if (!inSlot()) {
        if (!mathMode && !autoMath && lineStyle(*cur.row).kind != Line::Plot) {  // text mode: just the character
            Item it = ch(c, false);
            it.f = style;
            cur.row->insert(cur.row->begin() + cur.i, it);
            cur.i++;
            // "1. " or "# " typed at the start of a list item or heading changes what the line is
            auto styled = [](Line l) { return l >= Line::H1 && l <= Line::Done; };
            if (LineStyle ls = lineStyle(*cur.row); c == " " && styled(ls.kind) && cur.i > ls.len) {
                Row typed(cur.row->begin() + ls.len, cur.row->begin() + cur.i);
                if (LineStyle in = lineStyle(typed); styled(in.kind) && in.len == int(typed.size())) {
                    cur.row->erase(cur.row->begin(), cur.row->begin() + ls.len);
                    cur.i -= ls.len;
                }
            }
            return;
        }
    }
    if (c == "(" && !inSlot()) {
        const Entry* e = completeOpts.convert ? autoEntry(pendingWord()) : nullptr;
        if (e && e->cat == "Function")  // "det(" or "sin(" typed as text turns into the function
            for (int k = cur.i - int(pendingWord().size()); k < cur.i; k++) (*cur.row)[k].m = true;
    }
    if (!isLetter(c)) {
        Item* n = autoConvert();
        if (n && c == "(") return;  // sqrt( : already inside the root
    }
    // typed superscripts (², ³ from compose or AltGr) are exponents
    if (auto f = FROM_SUP.find(c); f != FROM_SUP.end()) {
        auto q = parents.find(cur.row);
        if (q == parents.end() || q->second.node->t != T::Sup) {
            insertChar("^");
            insertChar(f->second);
            exitSlot();
            return;
        }
        c = f->second;
    }
    // a few characters leave a filled slot at its end, where they can't belong inside it (repeats outwards)
    for (;;) {
        auto q = parents.find(cur.row);
        if (q == parents.end()) break;
        const Parent& p = q->second;
        bool limDone = false;
        if (p.node->t == T::Lim) {
            const Row& a = p.node->s[0];
            for (size_t k = 0; k + 1 < a.size(); k++)
                if (a[k].t == T::Sym && a[k].c == "→") limDone = true;
        }
        const char* leaves = p.node->t == T::Sup || p.node->t == T::Sub ? "+-*/=<>, "
                           : (p.node->t == T::Frac && p.slot == 1) || p.node->t == T::Sqrt ? "=<>,"
                           : (p.node->t == T::Big && p.slot == 1) || limDone ? " " : "";
        if (!(*leaves && !cur.row->empty() && cur.i == int(cur.row->size()) && in(leaves, c))) break;
        exitSlot();
    }
    Row& row = *cur.row;
    int i = cur.i;
    bool top = !inSlot() && !mathMode;
    if (top && in("+-*/=^_(<>", c)) {
        // a one-letter word followed by an operator is math: "f(2) =", "n = 5", "x^2"
        int j = i - 1;
        while (j >= 0 && isSpace(row[j])) j--;
        if (j >= 0 && row[j].t == T::Ch && !row[j].m && isLetter(row[j].c) && (j == 0 || isSpace(row[j - 1]))) row[j].m = true;
    }
    const Item* prev = i > 0 ? &row[i - 1] : nullptr;
    const Item* prevNS = prevNonSpace(row, i);
    bool mathCtx = !top || (prevNS && isMath(*prevNS));
    auto pf = parents.find(&row);
    const Parent* p = pf == parents.end() ? nullptr : &pf->second;
    if (c == "^" && p && p->node->t == T::Big && p->slot == 0) { cur = {&p->node->s[1], 0}; return; }
    if (mathCtx && (c == "^" || c == "_")) return insertNode(node(c == "^" ? T::Sup : T::Sub));
    if (mathCtx && c == "(") return insertNode(paren("(", ")"));
    if (mathCtx && c == "{") return insertNode(paren("{", "}"));  // a set: L = {2; 3; 5}
    if (c == ")" && p && (p->node->o == "(" || p->node->t == T::Sqrt) && i == int(row.size())) return exitSlot();
    if (c == "|" && p && p->node->o == "|" && i == int(row.size())) return exitSlot();
    if (c == "}" && p && p->node->o == "{" && i == int(row.size())) return exitSlot();
    if (mathCtx && c == "'") return insertNode(sym("′", "'"));
    if (c == "|" && !inSlot() && lineStyle(row).kind == Line::Table) {
        row.insert(row.begin() + i, ch("|", false));
        cur.i++;
        return;
    }
    if (mathCtx && c == "|" && p && p->node->o == "{") return insertNode(sym("|", "\\mid"));  // {x | x > 0}: "such that"
    if (mathCtx && c == "|" && !p && prev && isSpace(*prev) && stepBar(row) < 0) {  // 2x = 4 |:2  a transformation step
        bool rel = std::any_of(row.begin(), row.begin() + i, [](const Item& x) { return isRel(&x); });
        const Item* q = prevNonSpace(row, i);
        if (rel && q && !isRel(q) && !isOp(q)) {
            row.insert(row.begin() + i, sym("|", "\\qquad|\\;"));
            cur.i++;
            return;
        }
    }
    if (mathCtx && c == "|" && !(prev && prev->t == T::Ch && prev->c == "|")) return insertNode(paren("|", "|"));
    if (mathCtx && c == "/") {
        // numerator = the run of math items right before the cursor, stopping at operators and spaces
        int j = i;
        while (j > 0 && isMath(row[j - 1]) && !isSpace(row[j - 1]) && !isOp(&row[j - 1]) && row[j - 1].t != T::Big && row[j - 1].t != T::Lim) j--;
        Item n = node(T::Frac);
        n.s[0].assign(std::make_move_iterator(row.begin() + j), std::make_move_iterator(row.begin() + i));
        row.erase(row.begin() + j, row.begin() + i);
        bool filled = !n.s[0].empty();
        cur.i = j;
        insertNode(std::move(n));
        if (filled) cur = {&row[j].s[1], 0};
        return;
    }
    // at the top level a char joins the math when it continues an expression, otherwise it is text
    bool glued = prev && !isSpace(*prev) && isMath(*prev);
    bool m;
    if (!top) m = true;
    else if (isLetter(c)) {
        bool unitStart = prev && isSpace(*prev) && prevNS && prevNS->t == T::Ch && prevNS->m && isDigit(prevNS->c) && unitStartsWith(c);
        m = (glued && !isOp(prev) && (prev->t != T::Ch || alnum(prev->c))) || isOp(prevNS) ||
            (prevNS && (prevNS->t == T::Big || prevNS->t == T::Lim || prevNS->t == T::Fn)) || (c == "d" && openIntegral(row, i)) || unitStart;
    } else if (isDigit(c)) m = !prev || isSpace(*prev) || isMath(*prev);
    else if (c == " ") m = false;
    else m = mathCtx;
    // everything after "plot " at the start of a line is math
    bool plotLine = false;
    if (top && i >= 5) {
        std::string s;
        for (int k = 0; k < 5; k++) s += jsC(row[k]);
        plotLine = lower(s) == "plot ";
    }
    row.insert(row.begin() + i, ch(c, m || (plotLine && c != " ")));
    if (!row[i].m) row[i].f = style;
    cur.i++;
    if (row[i].m) combine();
}

// is there an integral to the left in this math run still waiting for its "dx"?
bool Editor::openIntegral(const Row& row, int i) const {
    for (int j = i - 1; j >= 0 && (isMath(row[j]) || isSpace(row[j])); j--) {
        if (row[j].t == T::Ch && row[j].c == "d") return false;
        if (row[j].t == T::Big && row[j].c == "∫") return true;
    }
    return false;
}

void Editor::combine() {
    if (!completeOpts.combos) return;
    Row& row = *cur.row;
    int i = cur.i;
    for (int n : {3, 2}) {
        if (i < n) continue;
        std::string key;
        bool ok = true;
        for (int k = i - n; k < i; k++) {
            if (!((row[k].t == T::Ch && row[k].m) || row[k].t == T::Sym)) ok = false;
            key += row[k].c;
        }
        auto f = COMBOS.find(key);
        if (!ok || f == COMBOS.end()) continue;
        auto t = SYM_TEX.find(f->second);
        row.erase(row.begin() + (i - n), row.begin() + i);
        row.insert(row.begin() + (i - n), sym(f->second, t == SYM_TEX.end() ? "" : t->second));
        cur.i -= n - 1;
        return;
    }
}

// leave the current slot: go to the next slot of the node, or after the node
void Editor::exitSlot() {
    auto f = parents.find(cur.row);
    if (f == parents.end()) return;
    Parent p = f->second;
    cur = p.slot < int(p.node->s.size()) - 1 ? Cur{&p.node->s[p.slot + 1], 0} : Cur{p.row, idxIn(p.row, p.node) + 1};
}

void Editor::right() {
    Row* row = cur.row;
    int i = cur.i;
    if (i < int(row->size())) {
        auto& s = (*row)[i].s;
        cur = s.empty() ? Cur{row, i + 1} : Cur{&s[0], 0};
    } else if (inSlot()) exitSlot();
    else if (int k = lineOf(row); k < int(doc.size()) - 1) cur = {&doc[k + 1], 0};
}

int Editor::lineStart() const { return inSlot() ? 0 : lineStyle(*cur.row).len; }

void Editor::clampCur() {
    if (inSlot()) return;
    LineStyle ls = lineStyle(*cur.row);
    cur.i = std::clamp(cur.i, std::min(ls.len, int(cur.row->size())), std::max(ls.len, int(cur.row->size()) - ls.tail));
}

void Editor::syncMode() {
    if (inSlot() || cur.row->empty() || lineStyle(*cur.row).kind == Line::Plot) return;
    int i = cur.i, start = lineStart();
    const Row& r = *cur.row;
    mathMode = i > start ? isMath(r[i - 1]) : i < int(r.size()) && isMath(r[i]);
}

void Editor::softBreak() {
    if (inSlot() || lineStyle(*cur.row).kind == Line::Table) return enter();
    cur.row->insert(cur.row->begin() + cur.i++, ch("\n", false));
    reindex();
}

void Editor::toggleMath() {
    mathMode = !mathMode;
    if (mathMode || !inSlot()) return;
    Row* r = cur.row;
    Parent p{};
    for (auto f = parents.find(r); f != parents.end(); f = parents.find(r)) p = f->second, r = p.row;
    cur = {p.row, idxIn(p.row, p.node) + 1};
}

void Editor::indent(int a, int b, int d) {
    for (int k = a; k <= b && k < int(doc.size()); k++) doc[k].indent = uint8_t(std::clamp(int(doc[k].indent) + d, 0, MAX_INDENT));
}
void Editor::setAlign(int a, int b, int align) {
    for (int k = a; k <= b && k < int(doc.size()); k++) doc[k].align = uint8_t(align);
}

void Editor::left() {
    Row* row = cur.row;
    int i = cur.i;
    if (i > lineStart()) {
        auto& s = (*row)[i - 1].s;
        cur = s.empty() ? Cur{row, i - 1} : Cur{&s.back(), int(s.back().size())};
        return;
    }
    if (auto f = parents.find(row); f != parents.end()) {
        Parent p = f->second;
        cur = p.slot > 0 ? Cur{&p.node->s[p.slot - 1], int(p.node->s[p.slot - 1].size())} : Cur{p.row, idxIn(p.row, p.node)};
        return;
    }
    if (int k = lineOf(row); k > 0) cur = {&doc[k - 1], int(doc[k - 1].size())};
}

// ↑/↓ move between stacked slots (numerator/denominator, upper/lower limit, matrix rows, exponent/base),
// otherwise between lines
void Editor::vertical(int d, bool visual) {
    for (Row* r = cur.row; parents.count(r); r = parents.at(r).row) {
        Parent p = parents.at(r);
        auto& s = p.node->s;
        Row* target = nullptr;
        if (p.node->t == T::Frac) target = d < 0 ? (p.slot == 1 ? &s[0] : nullptr) : (p.slot == 0 ? &s[1] : nullptr);
        else if (p.node->t == T::Big) target = d < 0 ? (p.slot == 0 ? &s[1] : nullptr) : (p.slot == 1 ? &s[0] : nullptr);
        else if (p.node->t == T::Mat) {
            int t = p.slot + d * p.node->cols;
            target = t >= 0 && t < int(s.size()) ? &s[t] : nullptr;
        } else if (p.node->t == T::Sup && d > 0) { cur = {p.row, idxIn(p.row, p.node)}; return; }
        if (target) {
            int len = int(target->size());
            cur = {target, std::min(cur.row == r ? cur.i : len, len)};
            return;
        }
    }
    if (d < 0 && cur.i < int(cur.row->size()) && (*cur.row)[cur.i].t == T::Sup) {
        Row* a = &(*cur.row)[cur.i].s[0];
        cur = {a, int(a->size())};
        return;
    }
    if (cellMove(0, d)) return;  // table to table: the same column
    if (visual && !inSlot() && visualLine && visualLine(d)) return;
    Row* top = topOf(cur.row);
    int k = lineOf(top) + d;
    if (k >= 0 && k < int(doc.size())) cur = {&doc[k], std::min(top == cur.row ? cur.i : 0, int(doc[k].size()))};
}

void Editor::backspace() {
    Row* row = cur.row;
    int i = cur.i;
    if (!inSlot()) {
        // at the start of a line Backspace takes its formatting away step by step: nesting, then the list/heading/
        // section style; a divider or a picture goes entirely
        LineStyle ls = lineStyle(*row);
        if (i <= ls.len && (ls.len || row->indent)) {
            if (ls.kind == Line::Rule || ls.kind == Line::Image) row->clear(), row->w = 0, cur.i = 0;
            else if (row->indent) row->indent--;
            else row->erase(row->begin(), row->begin() + ls.len), cur.i = 0;
            reindex();
            return;
        }
        int k = lineOf(row);
        if (i == 0 && k > 0) {  // a divider or picture above goes as a whole, rather than its source joining this line
            Line above = lineStyle(doc[k - 1]).kind;
            if (above == Line::Rule || above == Line::Image) {
                doc.erase(doc.begin() + k - 1);
                reindex();
                cur = {&doc[k - 1], 0};
                return;
            }
        }
    }
    if (i > 0) {
        auto& s = (*row)[i - 1].s;
        if (std::any_of(s.begin(), s.end(), [](const Row& r) { return !r.empty(); })) cur = {&s.back(), int(s.back().size())};
        else { row->erase(row->begin() + i - 1); cur.i--; reindex(); }
        return;
    }
    if (auto f = parents.find(row); f != parents.end()) {
        Parent p = f->second;
        if (p.node->t == T::Mat) return matrixBackspace(p);
        // backspace at the start of a slot unwraps the node, keeping its contents
        int idx = idxIn(p.row, p.node), before = 0;
        for (int k = 0; k < p.slot; k++) before += int(p.node->s[k].size());
        Row flat;
        for (auto& r : p.node->s)
            for (auto& x : r) flat.push_back(std::move(x));
        p.row->erase(p.row->begin() + idx);
        p.row->insert(p.row->begin() + idx, std::make_move_iterator(flat.begin()), std::make_move_iterator(flat.end()));
        cur = {p.row, idx + before};
        reindex();
        return;
    }
    if (int k = lineOf(row); k > 0) {
        Row* prev = &doc[k - 1];
        int len = int(prev->size());
        prev->insert(prev->end(), std::make_move_iterator(row->begin()), std::make_move_iterator(row->end()));
        doc.erase(doc.begin() + k);
        cur = {prev, len};
        reindex();
    }
}

void Editor::del() {
    Row& row = *cur.row;
    int i = cur.i, n = int(row.size());
    if (!inSlot() && i >= n - lineStyle(row).tail) {  // at the end of a line: the next one joins it, without its prefix
        int k = lineOf(&row);
        if (k + 1 >= int(doc.size())) return;
        LineStyle here = lineStyle(row), next = lineStyle(doc[k + 1]);
        auto block = [](Line l) { return l == Line::Rule || l == Line::Image || l == Line::Plot || l == Line::Table || l == Line::Section; };
        if (next.kind == Line::Rule || next.kind == Line::Image || doc[k + 1].empty()) doc.erase(doc.begin() + k + 1);
        else if (row.empty()) { doc.erase(doc.begin() + k); reindex(); cur = {&doc[k], lineStyle(doc[k]).len}; return; }
        else if (block(here.kind) || block(next.kind)) return;
        else {
            row.insert(row.end(), doc[k + 1].begin() + next.len, doc[k + 1].end());
            doc.erase(doc.begin() + k + 1);
        }
        reindex();
        cur = {&doc[k], i};
        return;
    }
    if (i < n && row[i].s.empty()) row.erase(row.begin() + i);
}

void Editor::enter() {
    if (inSlot()) return exitSlot();
    int k = lineOf(cur.row);
    Line kind = lineStyle(*cur.row).kind;
    if (kind == Line::Rule || kind == Line::Image || kind == Line::Section) {  // these stay whole: a new line below
        doc.insert(doc.begin() + k + 1, Row{});
        cur = {&doc[k + 1], 0};
        reindex();
        return;
    }
    Row rest(std::make_move_iterator(cur.row->begin() + cur.i), std::make_move_iterator(cur.row->end()));
    rest.align = cur.row->align, rest.indent = cur.row->indent;
    cur.row->erase(cur.row->begin() + cur.i, cur.row->end());
    doc.insert(doc.begin() + k + 1, std::move(rest));
    cur = {&doc[k + 1], 0};
    reindex();
}

void Editor::home(bool end) { cur.i = end ? int(cur.row->size()) : 0; }

// ---------- matrices: Shift+Enter adds a row, Shift+Space a column; inside (…) they turn it into a matrix ----------
std::optional<std::tuple<Item*, int, int>> Editor::matrixCell() {
    auto f = parents.find(cur.row);
    if (f == parents.end()) return std::nullopt;
    Parent p = f->second;
    if (p.node->t == T::Mat) return std::tuple{p.node, p.slot / p.node->cols, p.slot % p.node->cols};
    if (p.node->t != T::Paren) return std::nullopt;
    Item m = mat(p.node->o, p.node->cl, {{std::move(p.node->s[0])}});
    *p.node = std::move(m);
    reindex();
    return std::tuple{p.node, 0, 0};
}

bool Editor::addMatrixRow() {
    auto m = matrixCell();
    if (!m) return false;
    auto [n, r, c] = *m;
    n->s.insert(n->s.begin() + (r + 1) * n->cols, n->cols, Row{});
    reindex();
    cur = {&n->s[(r + 1) * n->cols], 0};
    return true;
}

bool Editor::addMatrixCol() {
    auto m = matrixCell();
    if (!m) return false;
    auto [n, r, c] = *m;
    for (int k = matRows(*n) - 1; k >= 0; k--) n->s.insert(n->s.begin() + k * n->cols + c + 1, Row{});
    n->cols++;
    reindex();
    cur = {&n->s[r * n->cols + c + 1], 0};
    return true;
}

// the cell holding position i (a border belongs to the cell on its left), and where the cursor goes in a cell:
// after its text, or in the middle of an empty one
static int cellAt(const std::vector<std::pair<int, int>>& cells, int i) {
    for (int c = 0; c < int(cells.size()); c++)
        if (i <= cells[c].second) return c;
    return int(cells.size()) - 1;
}
static int cellCaret(const Row& r, std::pair<int, int> cell) {
    auto [a, b] = cell;
    int e = b;
    while (e > a && isSpace(r[e - 1])) e--;
    if (e > a) return e;
    return a < b ? a + 1 : a;
}
static bool tableTop(const Editor& ed) { return !ed.inSlot() && lineStyle(*ed.cur.row).kind == Line::Table; }

bool Editor::cellMove(int dx, int dy) {
    if (!tableTop(*this)) return false;
    auto cells = tableCells(*cur.row);
    if (cells.empty()) return false;
    int c = cellAt(cells, cur.i);
    if (dx) {
        int to = std::clamp(c + dx, 0, int(cells.size()) - 1);
        cur.i = cellCaret(*cur.row, cells[to]);
        return true;
    }
    int k = lineOf(cur.row) + dy;
    if (k < 0 || k >= int(doc.size()) || lineStyle(doc[k]).kind != Line::Table) return false;  // leaving the table
    auto there = tableCells(doc[k]);
    if (there.empty()) return false;
    cur = {&doc[k], cellCaret(doc[k], there[std::min(c, int(there.size()) - 1)])};
    return true;
}

void Editor::snapToCell() {
    if (!tableTop(*this)) return;
    auto cells = tableCells(*cur.row);
    if (cells.empty()) return;
    auto [a, b] = cells[cellAt(cells, cur.i)];
    int s = a, e = b;
    while (s < b && isSpace((*cur.row)[s])) s++;
    while (e > s && isSpace((*cur.row)[e - 1])) e--;
    if (s == e || cur.i < s || cur.i > e) cur.i = cellCaret(*cur.row, {a, b});
}

bool Editor::inGrid() const {
    if (auto f = parents.find(cur.row); f != parents.end()) return f->second.node->t == T::Mat;
    return lineStyle(*cur.row).kind == Line::Table;
}

bool Editor::gridOp(char op) {
    if (auto f = parents.find(cur.row); f != parents.end()) {  // a matrix cell
        Parent p = f->second;
        if (p.node->t != T::Mat) return false;
        Item& m = *p.node;
        int cols = m.cols, rows = matRows(m), r = p.slot / cols, c = p.slot % cols, to = p.slot;
        if (op == 'k' || op == 'j') {
            int at = op == 'k' ? r : r + 1;
            m.s.insert(m.s.begin() + at * cols, cols, Row{});
            to = at * cols + c;
        } else if (op == 'h' || op == 'l') {
            int at = op == 'h' ? c : c + 1;
            for (int k = rows - 1; k >= 0; k--) m.s.insert(m.s.begin() + k * cols + at, Row{});
            m.cols++;
            to = r * m.cols + at;
        } else if (op == 'r' && rows > 1) {
            m.s.erase(m.s.begin() + r * cols, m.s.begin() + (r + 1) * cols);
            to = std::min(r, rows - 2) * cols + c;
        } else if (op == 'c' && cols > 1) {
            for (int k = rows - 1; k >= 0; k--) m.s.erase(m.s.begin() + k * cols + c);
            m.cols--;
            to = r * m.cols + std::min(c, m.cols - 1);
        } else return true;  // the last row or column stays
        reindex();
        cur = {&m.s[to], int(m.s[to].size())};
        return true;
    }
    Row& line = *cur.row;
    if (lineStyle(line).kind != Line::Table) return false;
    int k = lineOf(&line), t0 = k, t1 = k;
    while (t0 > 0 && lineStyle(doc[t0 - 1]).kind == Line::Table) t0--;
    while (t1 + 1 < int(doc.size()) && lineStyle(doc[t1 + 1]).kind == Line::Table) t1++;
    auto cells = tableCells(line);
    int c = 0;
    while (c + 1 < int(cells.size()) && cells[c].second < cur.i) c++;
    auto pipes = [](const Row& r) {
        std::vector<int> out;
        for (int j = 0; j < int(r.size()); j++)
            if (r[j].t == T::Ch && !r[j].m && r[j].c == "|") out.push_back(j);
        return out;
    };
    auto cellStart = [&](int line, int col) {  // the cursor spot in a cell: after its leading space
        auto cs = tableCells(doc[line]);
        if (cs.empty()) return int(doc[line].size());
        int a = cs[std::min(col, int(cs.size()) - 1)].first;
        return a < int(doc[line].size()) && isSpace(doc[line][a]) ? a + 1 : a;
    };
    if (op == 'k' || op == 'j') {
        int at = op == 'k' ? k : k + 1;
        doc.insert(doc.begin() + at, Row{});
        tableRow(doc[op == 'k' ? k + 1 : k], doc[at]);
        cur = {&doc[at], cellStart(at, c)};
    } else if (op == 'r') {
        if (t0 == t1) doc[k].clear();  // the only row: an empty line
        else doc.erase(doc.begin() + k);
        int to = std::min(k, int(doc.size()) - 1);
        reindex();
        cur = {&doc[to], lineStyle(doc[to]).kind == Line::Table ? cellStart(to, c) : 0};
        return true;
    } else if (op == 'h' || op == 'l') {
        for (int j = t0; j <= t1; j++) {
            auto ps = pipes(doc[j]);
            if (ps.empty()) continue;
            int at = op == 'h' ? std::min(c, int(ps.size()) - 1) : std::min(c + 1, int(ps.size()) - 1);  // the pipe to insert after
            Row add = {ch(" ", false), ch(" ", false), ch("|", false)};
            doc[j].insert(doc[j].begin() + ps[at] + 1, add.begin(), add.end());
        }
        cur = {&doc[k], 0};
        reindex();
        cur.i = cellStart(k, op == 'h' ? c : c + 1);
        return true;
    } else if (op == 'c') {
        if (cells.size() < 2) return true;
        for (int j = t0; j <= t1; j++) {
            auto ps = pipes(doc[j]);
            if (c + 1 < int(ps.size())) doc[j].erase(doc[j].begin() + ps[c], doc[j].begin() + ps[c + 1]);
            else if (c < int(ps.size()) && c > 0) doc[j].erase(doc[j].begin() + ps[c - 1] + 1, doc[j].end()), doc[j].push_back(ch("|", false));
        }
        cur = {&doc[k], 0};
        reindex();
        cur.i = cellStart(k, std::max(0, c - (c + 1 >= int(cells.size()) ? 1 : 0)));
        return true;
    }
    reindex();
    return true;
}

// at the start of a cell: drop an empty row/column, otherwise step back a cell; a 1×1 matrix turns back into (…)
void Editor::matrixBackspace(const Parent& p) {
    Item& m = *p.node;
    int cols = m.cols, rows = matRows(m), r = p.slot / cols, c = p.slot % cols;
    auto cell = [&](int i, int j) -> Row& { return m.s[i * cols + j]; };
    bool rowEmpty = true, colEmpty = true;
    for (int j = 0; j < cols; j++) rowEmpty = rowEmpty && cell(r, j).empty();
    for (int i = 0; i < rows; i++) colEmpty = colEmpty && cell(i, c).empty();
    if (rows > 1 && rowEmpty) m.s.erase(m.s.begin() + r * cols, m.s.begin() + (r + 1) * cols);
    else if (cols > 1 && colEmpty) {
        for (int i = rows - 1; i >= 0; i--) m.s.erase(m.s.begin() + i * cols + c);
        m.cols--;
    } else if (p.slot > 0) return left();
    else return;
    if (m.s.size() == 1) {
        m = paren(m.o.empty() ? "(" : m.o, m.cl.empty() ? ")" : m.cl, std::move(m.s[0]));
        reindex();
        cur = {&m.s[0], int(m.s[0].size())};
        return;
    }
    reindex();
    Row& to = m.s[std::min(std::max(p.slot - 1, 0), int(m.s.size()) - 1)];
    cur = {&to, int(to.size())};
}

// ---------- selection: always a range of one row (the deepest row containing both ends) ----------
std::optional<std::pair<int, int>> Editor::selRange() const {
    if (!range || range->row2) return std::nullopt;
    return std::pair{std::min(range->a, range->b), std::max(range->a, range->b)};
}

namespace {
struct Link {
    Row* row;
    int i;
    bool inside;
};
}  // namespace

void Editor::selectBetween(Cur A, Cur B) {
    auto chain = [&](Cur pos) {
        std::vector<Link> out = {{pos.row, pos.i, false}};
        for (Row* r = pos.row; parents.count(r);) {
            Parent q = parents.at(r);
            out.push_back({q.row, idxIn(q.row, q.node), true});
            r = q.row;
        }
        return out;
    };
    auto ca = chain(A), cb = chain(B);
    std::optional<Link> ea, eb;
    for (auto& x : ca) {
        auto y = std::find_if(cb.begin(), cb.end(), [&](auto& z) { return z.row == x.row; });
        if (y != cb.end()) { ea = x; eb = *y; break; }
    }
    if (!ea) {  // different lines: whole top-level items, the node either end is inside included
        Link ta = ca.back(), tb = cb.back();
        bool fwd = lineOf(tb.row) > lineOf(ta.row);
        int a = ta.i + (!fwd && ta.inside), b = tb.i + (fwd && tb.inside);
        range = Range{ta.row, a, b, tb.row};
        cur = {tb.row, b};
        return;
    }
    bool forward = ea->i < eb->i || (ea->i == eb->i && !ea->inside && eb->inside);
    int a = ea->i, b = eb->i;
    if (forward && eb->inside) b++;
    if (!forward && ea->inside) a++;
    if (a == b) range.reset();
    else range = Range{ea->row, a, b};
    cur = {ea->row, b};
}

std::optional<std::pair<int, int>> Editor::selOn(const Row& row) const {
    if (!range) return std::nullopt;
    if (!range->row2) return range->row == &row ? selRange() : std::nullopt;
    int k = lineOf(&row), ka = lineOf(range->row), kb = lineOf(range->row2);
    if (k < std::min(ka, kb) || k > std::max(ka, kb)) return std::nullopt;
    int n = int(row.size());
    int from = k == std::min(ka, kb) ? (ka < kb ? range->a : range->b) : 0;
    int to = k == std::max(ka, kb) ? (ka < kb ? range->b : range->a) : n;
    return std::pair{std::min(from, n), std::min(to, n)};
}

Doc Editor::selectedLines() const {
    if (!range) return {};
    if (!range->row2) return {selectedItems()};
    Doc out;
    int ka = lineOf(range->row), kb = lineOf(range->row2);
    for (int k = std::min(ka, kb); k <= std::max(ka, kb); k++) {
        auto s = selOn(doc[k]);
        out.emplace_back(doc[k].begin() + s->first, doc[k].begin() + s->second);
    }
    return out;
}

void Editor::collapseSelection(bool toEnd) {
    if (!range || !range->row2) return;
    bool fwd = lineOf(range->row2) > lineOf(range->row);
    cur = fwd == toEnd ? Cur{range->row2, range->b} : Cur{range->row, range->a};
    range.reset();
}

void Editor::insertLines(const Doc& parts) {
    if (parts.size() < 2 || inSlot()) {  // a slot holds one line: the parts run together
        for (auto& p : parts) insertItems(p);
        return;
    }
    int k = lineOf(cur.row);
    Row tail(cur.row->begin() + cur.i, cur.row->end());
    cur.row->erase(cur.row->begin() + cur.i, cur.row->end());
    cur.row->insert(cur.row->end(), parts[0].begin(), parts[0].end());
    doc.insert(doc.begin() + k + 1, parts.begin() + 1, parts.end());
    Row& last = doc[k + parts.size() - 1];
    cur = {&last, int(last.size())};
    last.insert(last.end(), tail.begin(), tail.end());
    reindex();
}

void Editor::extendSelection(int d) {
    if (range && range->row2) {
        range->b = std::clamp(range->b + d, 0, int(range->row2->size()));
        cur.i = range->b;
        return;
    }
    if (!range || range->row != cur.row) range = Range{cur.row, cur.i, cur.i};
    range->b = std::clamp(range->b + d, 0, int(cur.row->size()));
    cur.i = range->b;
    if (range->a == range->b) range.reset();
}

// the line, or a second time (or on an empty line) the whole note
void Editor::selectAll() {
    auto whole = selRange();
    if (!inSlot() && doc.size() > 1 && (cur.row->empty() || (whole && whole->first == 0 && whole->second == int(cur.row->size())))) {
        range = Range{&doc[0], 0, int(doc.back().size()), &doc.back()};
        cur = {&doc.back(), range->b};
    } else if (cur.row->empty()) range.reset();
    else range = Range{cur.row, 0, int(cur.row->size())};
    cur.i = int(cur.row->size());
}

Row Editor::selectedItems() const {
    auto r = selRange();
    return r ? Row(range->row->begin() + r->first, range->row->begin() + r->second) : Row{};
}

bool Editor::deleteSelection() {
    if (range && range->row2) {
        int ka = lineOf(range->row), kb = lineOf(range->row2), k0 = std::min(ka, kb), k1 = std::max(ka, kb);
        int i0 = selOn(doc[k0])->first, i1 = selOn(doc[k1])->second;
        Row tail(doc[k1].begin() + i1, doc[k1].end());
        doc[k0].erase(doc[k0].begin() + i0, doc[k0].end());
        doc[k0].insert(doc[k0].end(), tail.begin(), tail.end());
        doc.erase(doc.begin() + k0 + 1, doc.begin() + k1 + 1);
        cur = {&doc[k0], i0};
        range.reset();
        reindex();
        return true;
    }
    auto r = selRange();
    if (!r) return false;
    range->row->erase(range->row->begin() + r->first, range->row->begin() + r->second);
    cur = {range->row, r->first};
    range.reset();
    reindex();
    return true;
}

// insert items at the cursor; inside a slot everything typed counts as math
void Editor::insertItems(const Row& items) {
    Row copy = items;
    if (inSlot())
        for (auto& it : copy)
            if (it.t == T::Ch) it.m = true;
    cur.row->insert(cur.row->begin() + cur.i, copy.begin(), copy.end());
    cur.i += int(copy.size());
    reindex();
}

// ---------- formatting ----------
void Editor::setLine(Row& line, Line kind, int num) {
    int old = lineStyle(line).len;
    std::string p = linePrefix(kind, num);
    Row pre;
    for (char c : p) pre.push_back(ch(std::string(1, c), false));
    line.erase(line.begin(), line.begin() + old);
    line.insert(line.begin(), pre.begin(), pre.end());
    int n = int(pre.size());
    if (cur.row == &line) cur.i = std::max(n, cur.i - old + n);
    if (range && range->row == &line) range.reset();
    reindex();
}

void Editor::toggleFmt(Row& row, int a, int b, uint16_t flag) {
    bool all = true, any = false;
    for (int j = a; j < b; j++)
        if (row[j].t == T::Ch && !row[j].m && !isSpace(row[j])) any = true, all = all && fmtHas(row[j].f, flag);
    if (!any) return;
    for (int j = a; j < b; j++)
        if (row[j].t == T::Ch && !row[j].m) row[j].f = !flag ? 0 : all ? row[j].f & ~fmtGroup(flag) : (row[j].f & ~fmtGroup(flag)) | flag;
}

void Editor::tableRow(const Row& from, Row& to) {
    int cells = std::max(1, int(tableCells(from).size()));
    to = {ch("|", false)};
    for (int k = 0; k < cells; k++) to.insert(to.end(), {ch(" ", false), ch(" ", false), ch("|", false)});  // "|  |  |"
    cur = {&to, 2};
    reindex();
}


bool Editor::listEnter() {
    if (inSlot()) return false;
    Row& line = *cur.row;
    LineStyle s = lineStyle(line);
    if (s.kind == Line::Table) {  // Enter at the end of a table row starts the next one; on an empty row, ends the table
        if (std::all_of(line.begin(), line.end(), [](auto& x) { return x.t == T::Ch && (x.c == "|" || x.c == " "); })) {
            line.clear();
            cur.i = 0;
            return true;
        }
        if (cur.i < int(line.size())) return false;
        enter();
        tableRow(doc[lineOf(cur.row) - 1], *cur.row);
        return true;
    }
    if (!isList(s.kind) || cur.i < s.len) return false;
    if (int(line.size()) == s.len) {  // an empty item steps out a level, then ends the list
        if (line.indent) line.indent--;
        else setLine(line, Line::Text);
        return true;
    }
    enter();
    setLine(*cur.row, s.kind == Line::Done ? Line::Todo : s.kind, s.num + 1);
    return true;
}

}  // namespace om
