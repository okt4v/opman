// Self-checks: corca's test.js scenarios (typing model, evaluation) plus the vi layer. Run: ./build/opman_check
#include "edit.hpp"
#include "math.hpp"
#include "vi.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace om;

#define CHECK(c) \
    do { \
        if (!(c)) { std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #c); std::exit(1); } \
    } while (0)
#define CHECK_EQ(a, b) \
    do { \
        auto _a = (a); \
        auto _b = (b); \
        if (!(_a == _b)) { \
            std::fprintf(stderr, "%s:%d: %s\n  got:  %s\n  want: %s\n", __FILE__, __LINE__, #a, str(_a).c_str(), str(_b).c_str()); \
            std::exit(1); \
        } \
    } while (0)

static std::string str(const std::string& s) { return s; }
static std::string str(int x) { return std::to_string(x); }
static std::string str(const std::vector<std::string>& v) {
    std::string s = "[";
    for (auto& x : v) s += (s.size() > 1 ? ", " : "") + ("'" + x + "'");
    return s + "]";
}
static std::string noSpaces(std::string s) {
    std::erase(s, ' ');
    return s;
}

static Editor E;
static void type(const std::string& s) {
    for (size_t i = 0; i < s.size();) {
        unsigned char b = s[i];
        size_t n = b < 0x80 ? 1 : b < 0xE0 ? 2 : b < 0xF0 ? 3 : 4;
        E.insertChar(s.substr(i, n));
        E.reindex();
        i += n;
    }
}
static std::vector<std::string> names() {
    std::vector<std::string> out;
    for (auto& e : E.suggestions()) out.push_back(e.name);
    return out;
}
static void pick(const std::string& name) {
    for (auto& e : E.suggestions())
        if (e.name == name) {
            E.applySuggestion(e);
            E.reindex();
            return;
        }
    std::fprintf(stderr, "no suggestion '%s' in %s\n", name.c_str(), str(names()).c_str());
    std::exit(1);
}
static void newLine() {
    E.doc.emplace_back();
    E.reindex();
    E.cur = {&E.doc.back(), 0};
}
static std::string line(int k = -1) { return lineTex(E.doc[k < 0 ? E.doc.size() - 1 : k]); }

static void checkCorca() {
    // "E = mc^2": variables via Enter, operators/digits become math after a math node
    type("E"); pick("Variable E");
    type(" = m"); pick("Variable m");
    type("c"); pick("Constant c");
    type("^2"); E.exitSlot();
    CHECK_EQ(line(0), "$E = m \\mathrm{c} ^{2}$");

    // Space keeps a word as text; search finds structures by keyword; ^ in a sum's lower limit jumps to the upper
    type(" where "); type("sum"); pick("Summation");
    type("i=1^n"); E.exitSlot();
    CHECK_EQ(line(0), "$E = m \\mathrm{c} ^{2}$ where $\\sum_{i=1}^{n}$");

    // "/" pulls the preceding math run into a numerator; backspace at slot start unwraps
    newLine(); type("x"); pick("Variable x"); type("/2");
    CHECK_EQ(line(), "$\\frac{x}{2}$");
    E.left(); E.reindex(); E.backspace(); E.reindex();
    CHECK_EQ(line(), "$x 2$");

    // search ranking, ASCII shortcut keywords
    CHECK_EQ(E.search("alpha")[0].name, "α alpha");
    CHECK_EQ(E.search("int")[0].name, "Integral");
    CHECK_EQ(E.search("x")[0].name, "Variable x");
    {
        auto s = E.search("E");
        bool found = false;
        for (size_t k = 0; k < 4 && k < s.size(); k++) found = found || s[k].name == "Element of";
        CHECK(found);
    }

    // undo restores the previous document
    E.snap(); type("+y"); E.undo();
    CHECK_EQ(line(), "$x 2$");

    // typed combos become symbols, exponent is left by an operator
    newLine(); type("a"); pick("Variable a"); type("->b<=c!=d");
    CHECK_EQ(line(), "$a \\to b\\leq c\\neq d$");

    // define a function, evaluate it with and without steps
    newLine(); type("f"); pick("Variable f"); type("(x"); type(") = x^2+1");
    newLine(); type("f"); pick("Variable f"); type("(4"); type(")=");
    CHECK_EQ(names(), (std::vector<std::string>{"= 17", "Fill in the steps"}));
    pick("Fill in the steps");
    CHECK_EQ(line(), "$f \\left( 4 \\right) =4^{2}+1=16+1=17$");

    // plain arithmetic, precedence, defined variables, auto-converted functions
    newLine(); type("2+3*4=");
    CHECK_EQ(names()[0], "= 14");
    newLine(); type("w"); pick("Variable w"); type(" = 3");
    newLine(); type("w"); pick("Variable w"); type("^2-f(2)/5 =");
    CHECK_EQ(names()[0], "= 8");
    newLine(); type("y"); pick("Variable y"); type(" = sin(0)+sqrt9=");
    CHECK_EQ(names()[0], "= 3");
    CHECK(line().find("\\sin") != std::string::npos);

    // text never triggers evaluation
    newLine(); type("I think so =");
    CHECK(names().empty());

    // a plain text word without catalog matches shows no popup; one with matches does
    newLine(); type("notes");
    CHECK(names().empty());
    type(" sum");
    CHECK_EQ(names()[0], "Summation");
    newLine(); type("det(");
    CHECK(line().find("\\det") != std::string::npos);
    // exact fractions with a decimal alternative
    newLine(); type("1/3"); E.exitSlot(); type("+1/6 =");
    CHECK_EQ(names()[0], "= 1/2");
    CHECK_EQ(names()[1], "= 0.5");
    newLine(); type("5! =");
    CHECK_EQ(names()[0], "= 120");

    // sum with written-out steps
    newLine(); type("S"); pick("Summation"); type("i=1^4 i^2 =");
    CHECK_EQ(names()[0], "= 30");
    pick("Fill in the steps");
    CHECK(line().ends_with("=1^{2}+2^{2}+3^{2}+4^{2}=1+4+9+16=30$"));

    // integral, limit, derivative (f(x) = x^2 + 1 from above)
    newLine(); type("I"); pick("Integral"); type("0^1 x^2 dx =");
    CHECK_EQ(names()[0], "= 1/3");
    newLine(); type("L"); pick("Limit"); type("x->0 sin(x)/x =");
    CHECK_EQ(names()[0], "= 1");
    newLine(); type("f"); pick("Variable f"); type("'(3) =");
    CHECK_EQ(names()[0], "= 6");
    newLine(); type("f"); pick("Variable f"); type("'(t) =");
    CHECK_EQ(names(), (std::vector<std::string>{"Expand and simplify"}));
    pick("Expand and simplify");
    CHECK(noSpaces(line()).ends_with("=2t$"));

    // polynomial expansion with free variables
    newLine(); type("2(a+1)^2 =");
    pick("Expand and simplify");
    CHECK(noSpaces(line()).ends_with("=2a^{2}+4a+2$"));

    // solving after =>
    newLine(); type("x"); pick("Variable x"); type("^2-4 = 0 =>");
    CHECK_EQ(names()[0], "x = -2, x = 2");
    newLine(); type("x"); pick("Variable x"); type("^2 = 2 =>");
    CHECK_EQ(names(), (std::vector<std::string>{"x = ± √2", "x = -1.414213562, x = 1.414213562"}));
    newLine(); type("3x + 1 = 7 =>");
    CHECK_EQ(names()[0], "x = 2");
    newLine(); type("cos"); pick("cos"); type("(x) = x =>");
    CHECK_EQ(names()[0], "x = 0.7390851332");

    // matrices: Shift+Space / Shift+Enter turn (…) into a matrix; det, products, inverse, transpose
    newLine(); type("A"); pick("Variable A"); type(" = (1"); E.addMatrixCol(); E.reindex(); type("2"); E.addMatrixRow(); E.reindex(); type("3");
    E.exitSlot(); E.reindex(); type("4"); E.exitSlot(); E.exitSlot(); E.reindex();
    CHECK(line().find("\\begin{pmatrix} 1 & 2 \\\\ 3 & 4 \\end{pmatrix}") != std::string::npos);
    newLine(); type("det"); pick("det"); type("(A) =");
    CHECK_EQ(names()[0], "= -2");
    newLine(); type("A"); pick("Variable A"); type("^-1 =");
    CHECK_EQ(names()[0], "= (-2 1; 3/2 -1/2)");
    newLine(); type("A"); pick("Variable A"); type("^T*A =");
    CHECK_EQ(names()[0], "= (10 14; 14 20)");

    // units, converted into the units used
    newLine(); type("5 m/s"); E.exitSlot(); type(" * 3 s =");
    CHECK_EQ(names()[0], "= 15 m");
    newLine(); type("2 km + 300 m =");
    CHECK_EQ(names()[0], "= 23/10 km");
    newLine(); type("v"); pick("Variable v"); type(" = 90 km/h");
    newLine(); type("v"); pick("Variable v"); type(" * 2 h =");
    CHECK_EQ(names()[0], "= 180 km");
    newLine(); type("u = 3 m and u * 4 =");
    CHECK_EQ(names()[0], "= 12 m");

    // live results: shown for evaluable lines, not for plain numbers or open "="
    newLine(); type("f(5) + 1");
    {
        auto g = ghost(E, E.doc.back());
        CHECK(g && g->text == "= 27");
    }
    newLine(); type("w"); pick("Variable w"); type(" = 3");
    CHECK(!ghost(E, E.doc.back()));

    // selection between two points picks the deepest common row; deleting removes it
    newLine(); type("x"); pick("Variable x"); type("+1/2"); E.exitSlot(); type("+y");
    {
        Row& row = E.doc.back();
        E.selectBetween({&row, 1}, {&row[2].s[0], 1});
        CHECK_EQ(lineTex(E.selectedItems(), false), "+\\frac{1}{2}");
        E.deleteSelection();
        CHECK_EQ(line(), "$x +y$");
    }

    // fractions keep what you type (spaces, + - *); a relation at the end leaves all nested levels
    newLine(); type("1/x + 1/2 =");
    CHECK_EQ(noSpaces(line()), "$\\frac{1}{x+\\frac{1}{2}}=$");
    // typed superscripts are exponents
    newLine(); type("x"); pick("Variable x"); type("²+1");
    CHECK_EQ(noSpaces(line()), "$x^{2}+1$");
    // ↑/↓ inside a fraction move between numerator and denominator
    newLine(); type("a/b"); E.vertical(-1); E.reindex(); type("c");
    CHECK_EQ(noSpaces(line()), "$\\frac{ac}{b}$");

    // JSON round trip keeps the document
    {
        Doc back = docFromJson(nlohmann::json::parse(docJson(E.doc).dump()));
        CHECK(back == E.doc);
    }
    // number formatting matches JS
    CHECK_EQ(jsNum(0.5), "0.5");
    CHECK_EQ(jsNum(1e21), "1e+21");
    CHECK_EQ(jsNum(1e-7), "1e-7");
    CHECK_EQ(jsNum(0.000001), "0.000001");
    CHECK_EQ(jsNum(toPrecision(std::sqrt(2.0), 10)), "1.414213562");
}

static void keys(Editor& ed, Vi& vi, const std::string& s) {
    for (auto& k : Vi::parse(s)) vi.key(ed, k);
}

static void checkVi() {
    Editor ed;
    Vi vi;
    auto ln = [&](int k) { return noSpaces(lineTex(ed.doc[k])); };
    CHECK(vi.mode == Mode::Normal);

    // math mode: a paren at the start of a line is math, not text
    {
        Editor e2;
        Vi v2;
        keys(e2, v2, "i(a+b)/2<Esc>");
        CHECK(e2.doc[0][0].t == T::Ch && !e2.doc[0][0].m);
        keys(e2, v2, "o<C-m>(a+b)/2 = c<Esc><C-m>");
        CHECK_EQ(noSpaces(lineTex(e2.doc[1])), "$\\frac{\\left(a+b\\right)}{2}=c$");
        CHECK(!e2.mathMode);
    }

    // formatting: Space leader, Ctrl-b, line prefixes, list continuation, JSON round trip
    {
        Editor e;
        Vi v;
        auto flags = [&](int k) { std::string s; for (auto& it : e.doc[k]) s += char('0' + it.f); return s; };
        keys(e, v, "ihello big world<Esc>0w<Space>b");
        CHECK_EQ(flags(0), "000000111000000");
        keys(e, v, "<Space>b");
        CHECK_EQ(flags(0), "000000000000000");
        keys(e, v, "0vll<Space>i");
        CHECK_EQ(flags(0), "222000000000000");
        CHECK(v.mode == Mode::Normal);
        keys(e, v, "A <C-b>bold<C-b> x<Esc>");
        CHECK_EQ(flags(0).substr(15), "0111100");
        keys(e, v, "<Space>1");
        CHECK_EQ(plain(e.doc[0]).substr(0, 8), "# hello ");
        CHECK(lineStyle(e.doc[0]).kind == Line::H1);
        keys(e, v, "<Space>2");
        CHECK(lineStyle(e.doc[0]).kind == Line::H2 && plain(e.doc[0]).starts_with("## hello"));
        keys(e, v, "<Space>2");
        CHECK(plain(e.doc[0]).starts_with("hello"));
        keys(e, v, "u");
        CHECK(lineStyle(e.doc[0]).kind == Line::H2);
        CHECK_EQ(docTitle(e.doc), "hello big world bold x");
        keys(e, v, "o<Esc><Space>tAmilk<CR>eggs<CR><CR>after<Esc>");
        CHECK_EQ(plain(e.doc[1]), "- [ ] milk");
        CHECK_EQ(plain(e.doc[2]), "- [ ] eggs");
        CHECK_EQ(plain(e.doc[3]), "after");
        keys(e, v, "kk<Space>x");
        CHECK(lineStyle(e.doc[1]).kind == Line::Done);
        keys(e, v, "Go<Esc><Space>nAone<Esc>otwo<Esc>");
        CHECK_EQ(plain(e.doc[5]), "2. two");
        keys(e, v, "Vk<Space>-");
        CHECK_EQ(plain(e.doc[4]), "- one");
        CHECK_EQ(plain(e.doc[5]), "- two");
        Doc back = docFromJson(docJson(e.doc));
        CHECK(back == e.doc);
    }

    // insert, define, evaluate with Tab from the popup
    keys(ed, vi, "if(x) = x^2+1<Esc>");
    CHECK(vi.mode == Mode::Normal);
    keys(ed, vi, "of(4)=");
    CHECK(!vi.items.empty() && vi.items[0].name == "= 17");
    keys(ed, vi, "<Tab><Esc>");
    CHECK_EQ(ln(1), "$f\\left(4\\right)=17$");

    // dd and u: one undo step per command, the whole insert session is one step
    keys(ed, vi, "dd");
    CHECK_EQ(int(ed.doc.size()), 1);
    keys(ed, vi, "u");
    CHECK_EQ(int(ed.doc.size()), 2);
    CHECK_EQ(ln(1), "$f\\left(4\\right)=17$");
    keys(ed, vi, "u");
    CHECK_EQ(int(ed.doc.size()), 1);
    keys(ed, vi, "<C-r>");
    CHECK_EQ(int(ed.doc.size()), 2);

    // j/k move between numerator and denominator
    keys(ed, vi, "Goa/b<Esc>kic<Esc>");
    CHECK_EQ(ln(2), "$\\frac{ac}{b}$");

    // word motions and operators on text
    keys(ed, vi, "ohello big world<Esc>0dw");
    CHECK_EQ(lineTex(ed.doc[3]), "big world");
    keys(ed, vi, "x");
    CHECK_EQ(lineTex(ed.doc[3]), "ig world");
    keys(ed, vi, "u0wD");
    CHECK_EQ(lineTex(ed.doc[3]), "big ");
    keys(ed, vi, "u03x");
    CHECK_EQ(lineTex(ed.doc[3]), " world");
    keys(ed, vi, "P");
    CHECK_EQ(lineTex(ed.doc[3]), "big world");
    keys(ed, vi, "0cwsmall<Esc>");
    CHECK_EQ(lineTex(ed.doc[3]), "small world");
    keys(ed, vi, "0de");
    CHECK_EQ(lineTex(ed.doc[3]), " world");

    // yank/put lines, gg/G, counts on G, J joins
    keys(ed, vi, "yyp");
    CHECK_EQ(int(ed.doc.size()), 5);
    CHECK_EQ(lineTex(ed.doc[4]), " world");
    keys(ed, vi, "4GJ");
    CHECK_EQ(int(ed.doc.size()), 4);
    CHECK_EQ(lineTex(ed.doc[3]), " world  world");
    keys(ed, vi, "gg");
    CHECK(ed.cur.row == &ed.doc[0]);
    keys(ed, vi, "2dd");
    CHECK_EQ(int(ed.doc.size()), 2);
    CHECK_EQ(ln(0), "$\\frac{ac}{b}$");

    // visual: select inside the row and delete; visual line
    keys(ed, vi, "Goabcdef<Esc>0vlld");
    CHECK_EQ(lineTex(ed.doc.back()), "def");
    keys(ed, vi, "Vkd");
    CHECK_EQ(int(ed.doc.size()), 1);

    // cc clears the line into insert mode; r replaces one item
    keys(ed, vi, "ccxyz<Esc>0rq");
    CHECK_EQ(lineTex(ed.doc[0]), "qyz");

    // ex commands go to the app; unknown ones show a message
    keys(ed, vi, ":w<CR>");
    CHECK(vi.cmds.size() == 1 && vi.cmds[0] == "w");
    keys(ed, vi, ":nope<CR>");
    CHECK(vi.message.starts_with("Not an editor command"));
    CHECK(vi.mode == Mode::Normal);

    // matrices in insert mode: Shift+Space grows a column, Shift+Enter a row
    keys(ed, vi, "oA = (1<S-Space>2<S-CR>3<Tab>4<Esc>");
    CHECK(lineTex(ed.doc.back()).find("\\begin{pmatrix} 1 & 2 \\\\ 3 & 4 \\end{pmatrix}") != std::string::npos);
}

int main() {
    checkCorca();
    checkVi();
    std::puts("all checks passed");
}
