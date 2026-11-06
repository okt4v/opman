// Self-checks: corca's test.js scenarios (typing model, evaluation) plus the vi layer. Run: ./build/opman_check
#include "edit.hpp"
#include "math.hpp"
#include "help.hpp"
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

    // . repeats the last change; f/t/;/, find in the line; / ? n N search
    {
        Editor e;
        Vi v;
        keys(e, v, "ia,b,c,d<Esc>0x");
        CHECK_EQ(lineTex(e.doc[0]), ",b,c,d");
        keys(e, v, ".");
        CHECK_EQ(lineTex(e.doc[0]), "b,c,d");
        keys(e, v, "0df,");
        CHECK_EQ(lineTex(e.doc[0]), "c,d");
        keys(e, v, "u0f,;");  // ; with nothing after: stays
        CHECK(e.cur.i == 3);
        keys(e, v, "0dt,");
        CHECK_EQ(lineTex(e.doc[0]), ",c,d");
        keys(e, v, "$F,D");
        CHECK_EQ(lineTex(e.doc[0]), ",c");
        keys(e, v, "Aend<Esc>o<Esc>.");  // o<Esc> was the last change
        CHECK_EQ(int(e.doc.size()), 3);
        keys(e, v, "ggcwone<Esc>j.");
        CHECK_EQ(lineTex(e.doc[0]), "one");
        keys(e, v, "ggccalpha beta<Esc>oBeta gamma<Esc>gg");
        keys(e, v, "/beta<CR>");
        CHECK(e.cur.row == &e.doc[0] && e.cur.i == 6);
        keys(e, v, "n");  // smartcase: lowercase matches Beta too
        CHECK(e.cur.row == &e.doc[1] && e.cur.i == 0);
        keys(e, v, "n");
        CHECK(e.cur.row == &e.doc[0] && v.message.starts_with("search hit BOTTOM"));
        keys(e, v, "N");
        CHECK(e.cur.row == &e.doc[1]);
        keys(e, v, "/Beta<CR>");
        CHECK(e.cur.row == &e.doc[1] && e.cur.i == 0);
        keys(e, v, "?zzz<CR>");
        CHECK(v.message.starts_with("Pattern not found"));
        CHECK(v.prompt == ':');
    }

    // clipboard: yanks go out as Markdown, p pastes what another app copied
    {
        Editor e;
        Vi v;
        std::string clip;
        v.setClip = [&](const std::string& s) { clip = s; };
        v.getClip = [&] { return clip; };
        keys(e, v, "i<C-b>big<C-b> x = a/b<Esc>yy");
        CHECK_EQ(noSpaces(clip), "**big**$x=\\frac{a}{b}$");
        keys(e, v, "p");  // our own yank: the line register
        CHECK_EQ(int(e.doc.size()), 2);
        clip = "from\nelsewhere";
        keys(e, v, "Go<Esc>p");
        CHECK_EQ(int(e.doc.size()), 4);
        CHECK_EQ(plain(e.doc[2]), "from");
        CHECK_EQ(plain(e.doc[3]), "elsewhere");
        keys(e, v, "o<C-v><Esc>");
        CHECK_EQ(int(e.doc.size()), 6);
        CHECK_EQ(plain(e.doc[5]), "elsewhere");
    }

    // Markdown export: prefixes, lists together, paragraphs apart
    {
        Editor e;
        Vi v;
        keys(e, v, "iTitle<Esc><Space>1o<Esc><Space>-Aone<Esc>otwo<Esc>o<Esc><Space>-Atext <C-b>bold<C-b> end<Esc>");
        CHECK_EQ(markdown(e.doc), "# Title\n\n- one\n- two\n\ntext **bold** end\n");
    }

    // blocks: divider, image, plot, table; | in a table line is a cell border, Enter adds a row
    {
        Editor e;
        Vi v;
        keys(e, v, "i---<Esc>o![a cat](media/cat.png)<Esc>o| x^2 | b |<Esc>A<CR>7<Esc>");
        CHECK(lineStyle(e.doc[0]).kind == Line::Rule);
        std::string alt, path;
        CHECK(imageRef(e.doc[1], alt, path) && alt == "a cat" && path == "media/cat.png");
        CHECK(lineStyle(e.doc[2]).kind == Line::Table);
        CHECK_EQ(int(tableCells(e.doc[2]).size()), 2);
        CHECK_EQ(lineTex(e.doc[2]), "| $x^{2}$ | b |");  // the pipe after x^2 is a border, not |…| (abs)
        CHECK_EQ(plain(e.doc[3]), "| 7 |  |");
        keys(e, v, "A<CR><CR>after<Esc>");  // Enter on an empty row ends the table
        CHECK_EQ(plain(e.doc[4]), "after");
        CHECK_EQ(markdown(Doc(e.doc.begin() + 2, e.doc.begin() + 4)), "| $x^{2}$ | b |\n|---|---|\n| $7$ |  |\n");
        keys(e, v, "oplot<Tab>x^2, -1 < x < 2<Esc>");
        Plot pl = plot(e, e.doc.back());
        CHECK(pl.x0 == -1 && pl.x1 == 2 && pl.series.size() == 1 && pl.error.empty());
        CHECK(std::fabs(pl.series[0].pts.back().second - 4) < 1e-9);
        CHECK(pl.y0 < 0 && pl.y1 > 4);
        std::string t = latex(e, "/notes");
        CHECK(t.find("\\rule{\\linewidth}") != std::string::npos);
        CHECK(t.find("{/notes/media/cat.png}") != std::string::npos);
        CHECK(t.find("\\begin{tabular}{|l|l|}") != std::string::npos);
        CHECK(t.find("\\addplot") != std::string::npos);
    }

    // superscript/subscript and the colours exclude each other; LaTeX escapes text
    {
        Editor e;
        Vi v;
        keys(e, v, "iab 50%<Esc>0<Space>^<Space>_");
        CHECK(e.doc[0][0].f == Sub);
        keys(e, v, "<Space>R<Space>B");
        CHECK(e.doc[0][0].f == (Sub | Blue));
        keys(e, v, "<Space>B");
        CHECK(e.doc[0][0].f == Sub);
        CHECK_EQ(latexText(e.doc[0], 0, int(e.doc[0].size())), "\\textsubscript{ab} $50\\%$");
    }

    // standard keys: always typing; Esc stays in Insert, Ctrl-z undoes, Ctrl-x cuts the selection
    {
        Editor e;
        Vi v;
        v.standard = true;
        v.mode = Mode::Insert;
        std::string clip;
        v.setClip = [&](const std::string& c) { clip = c; };
        keys(e, v, "hello world<Esc>");
        CHECK(v.mode == Mode::Insert);
        CHECK_EQ(plain(e.doc[0]), "hello world");
        keys(e, v, "<C-a><C-x>");
        CHECK_EQ(plain(e.doc[0]), "");
        CHECK_EQ(clip, "hello world");
        keys(e, v, "<C-z>");
        CHECK_EQ(plain(e.doc[0]), "hello world");
    }

    // statistics, table columns as lists, and the result settings (digits, degrees, decimals)
    {
        Editor e;
        Vi v;
        keys(e, v, "imean(2, 4, 9)<Esc>omedian(5, 1, 3, 8)<Esc>ostdev(2, 4, 4, 4, 5, 5, 7, 9)<Esc>onCr(5, 2)<Esc>omod(17, 5)<Esc>"
                   "olcm(4, 6)<Esc>");
        CHECK_EQ(ghost(e, e.doc[0])->text, "= 5");
        CHECK_EQ(ghost(e, e.doc[1])->text, "= 4");
        CHECK_EQ(ghost(e, e.doc[2])->text, "= 4√14/7 ≈ 2.138089935");  // √(32/7), found exactly
        CHECK_EQ(ghost(e, e.doc[3])->text, "= 10");
        CHECK_EQ(ghost(e, e.doc[4])->text, "= 2");
        CHECK_EQ(ghost(e, e.doc[5])->text, "= 12");
        keys(e, v, "o<Esc>cc| t | h |<Esc>o<Esc>cc| 1 | 10 |<Esc>o<Esc>cc| 2 | 20 |<Esc>o<Esc>cc| 3 | 45 |<Esc>o<Esc>ccmean(h)<Esc>osum(t)<Esc>");
        CHECK_EQ(ghost(e, e.doc[10])->text, "= 25");
        CHECK_EQ(ghost(e, e.doc[11])->text, "= 6");
        keys(e, v, "o<Esc>cc| mass |<Esc>o<Esc>cc| 2 |<Esc>o<Esc>cc| 5 |<Esc>o<Esc>ccmax(mass)<Esc>");
        CHECK_EQ(ghost(e, e.doc.back())->text, "= 5");  // a column name of several letters
        mathOpts.digits = 4;
        CHECK_EQ(ghost(e, e.doc[2])->text, "= 4√14/7 ≈ 2.138");
        mathOpts.digits = 10;
        mathOpts.degrees = true;
        keys(e, v, "osin(30)<Esc>");
        CHECK_EQ(ghost(e, e.doc.back())->text, "= 1/2");
        mathOpts.decimals = true;
        CHECK_EQ(ghost(e, e.doc.back())->text, "= 0.5");
        mathOpts = {};
        // exact forms of decimals: roots, π, quadratic roots; sin(1) stays a decimal
        keys(e, v, "o<C-m>sqrt8<Esc><C-m>oarctan(1)<Esc>osin(1)<Esc>o<C-m>(1+sqrt(5<Tab>)/2<Esc><C-m>");
        int n = int(e.doc.size());
        CHECK_EQ(ghost(e, e.doc[n - 4])->text, "= 2√2 ≈ 2.828427125");
        CHECK_EQ(ghost(e, e.doc[n - 3])->text, "= π/4 ≈ 0.7853981634");
        CHECK_EQ(ghost(e, e.doc[n - 2])->text, "= 0.8414709848");
        CHECK_EQ(ghost(e, e.doc[n - 1])->text, "= (1 + √5)/2 ≈ 1.618033989");
        keys(e, v, "o<C-m>sqrt(2)*sqrt(3)<Tab><Esc><C-m>");  // Tab writes "= √6"; no hint repeats it
        CHECK_EQ(plain(e.doc.back()), "√2·√3 = √6");
        CHECK(!ghost(e, e.doc.back()));
    }

    // sections: each === starts a fresh set of definitions, so two exercises can both use x
    {
        Editor e;
        Vi v;
        keys(e, v, "ix = 3<Esc>ox + 1<Esc>o=== Exercise 2<Esc>ox = 10<Esc>ox + 1<Esc>o=== Exercise 3<Esc>ox + 1<Esc>");
        CHECK(lineStyle(e.doc[2]).kind == Line::Section);
        CHECK_EQ(sectionTitle(e.doc[2]), "Exercise 2");
        CHECK_EQ(ghost(e, e.doc[1])->text, "= 4");
        CHECK_EQ(ghost(e, e.doc[4])->text, "= 11");
        CHECK(!ghost(e, e.doc[6]));  // x is undefined in the third section
        CHECK(sectionOf(e.doc, 4) == std::pair(2, 5));
        // a number written after = is already the answer: no faint "= 4" after "x + 1 = 4"
        keys(e, v, "ggjA = 4<Esc>");
        CHECK(!ghost(e, e.doc[1]));
    }

    // Enter after a text word starts a new line even with the popup open; a math word or ↓ makes Enter pick
    {
        Editor e;
        Vi v;
        keys(e, v, "i- one");
        bool popup = !v.items.empty();
        keys(e, v, "<CR>two<Esc>");
        CHECK_EQ(plain(e.doc[0]), "- one");
        CHECK_EQ(plain(e.doc[1]), "- two");
        (void)popup;
    }

    // Tab walks table cells; past the last one it adds a row
    {
        Editor e;
        Vi v;
        keys(e, v, "i| a | b |<Esc>0");
        keys(e, v, "a<Tab>X<Tab>Y<Tab>Z<Esc>");
        CHECK_EQ(plain(e.doc[0]), "| a | bX |");  // after the cell's text, like the arrows
        CHECK_EQ(plain(e.doc[1]), "| Y | Z |");
    }

    // search: all matches for highlighting; :noh and Esc turn it off
    {
        Editor e;
        Vi v;
        keys(e, v, "ione two one<Esc>oone<Esc>gg/one<CR>");
        auto m = Vi::matches(e, v.highlight());
        CHECK_EQ(int(m.size()), 3);
        CHECK(m[1].line == 0 && m[1].a == 8 && m[1].b == 11);
        keys(e, v, ":noh<CR>");
        CHECK(v.highlight().empty());
        keys(e, v, "/tw");
        CHECK_EQ(v.highlight(), "tw");  // as you type
        keys(e, v, "<Esc>");
    }

    // [[links]]: Enter or gf on one asks the app to open it; Ctrl-o goes back
    {
        Editor e;
        Vi v;
        keys(e, v, "isee [[Other note]] here<Esc>");
        CHECK((linkSpans(e.doc[0]) == std::vector<std::pair<int, int>>{{4, 18}}));
        keys(e, v, "0fO<CR>");
        CHECK(!v.cmds.empty() && v.cmds.back() == "link Other note");
        v.cmds.clear();
        keys(e, v, "0gf");
        CHECK(v.cmds.empty() && v.message.starts_with("No [[link]]"));
        keys(e, v, "fOgf<C-o>");
        CHECK(v.cmds.size() == 2 && v.cmds[0] == "link Other note" && v.cmds[1] == "back");
    }

    // plots: a table's columns as points, a y range, a view override; HTML and Markdown export
    {
        Editor e;
        Vi v;
        keys(e, v, "i| t | h |<Esc>o<Esc>cc| 1 | 5 |<Esc>o<Esc>cc| 2 | 9 |<Esc>o<Esc>ccplot<Tab>table, x^2, -1 < y < 1<Esc>");
        Plot pl = plot(e, e.doc[3]);
        CHECK(pl.series.size() == 2 && pl.series[0].points && pl.series[0].label == "h" && pl.series[0].pts.size() == 2);
        CHECK(pl.series[0].pts[1] == std::pair(2.0, 9.0));
        CHECK(pl.y0 == -1 && pl.y1 == 1);
        CHECK(pl.x0 < 1 && pl.x1 > 2 && pl.x1 < 3);  // fitted to the data
        double view[4] = {0, 10, -5, 5};
        Plot zoomed = plot(e, e.doc[3], view);
        CHECK(zoomed.x0 == 0 && zoomed.x1 == 10 && zoomed.series[1].pts.back().first == 10);
        keys(e, v, "o![pic](media/p.png)<Esc>o=== Part 2<Esc>");
        std::string h = html(e, "/n", "T");
        CHECK(h.find("<th>t</th>") != std::string::npos && h.find("<svg") != std::string::npos);
        CHECK(h.find("Image not found: media/p.png") != std::string::npos && h.find("class=section>Part 2") != std::string::npos);
        std::string md = markdown(e.doc, "/n");
        CHECK(md.find("![pic](/n/media/p.png)") != std::string::npos && md.find("**Part 2**") != std::string::npos);
    }

    // the tour note types cleanly: its calculations have answers, its plots draw
    {
        Editor e;
        Vi v;
        keys(e, v, tourKeys());
        int ghosts = 0, plots = 0;
        for (auto& r : e.doc) {
            if (ghost(e, r)) ghosts++;
            if (lineStyle(r).kind == Line::Plot) {
                Plot pl = plot(e, r);
                CHECK(pl.error.empty() && (pl.is3d() || !pl.series.empty()));
                plots++;
            }
        }
        CHECK(ghosts >= 5 && plots == 4 && v.mode == Mode::Normal);
    }

    // braces in math: a set like L = {2; 3; 5}, set-builder {x | x > 0}; in text { stays text
    {
        Editor e;
        Vi v;
        keys(e, v, "i<C-m>L={2; 3; 5}<Esc>oL = {x | x > 0}<Esc><C-m>oa {b}<Esc>");
        CHECK(e.doc[0].size() == 3 && e.doc[0][2].t == T::Paren && e.doc[0][2].o == "{");
        CHECK_EQ(plain(e.doc[0]), "L={2; 3; 5}");
        CHECK(noSpaces(lineTex(e.doc[1])).find("\\left\\{x\\midx>0\\right\\}") != std::string::npos);
        CHECK_EQ(plain(e.doc[2]), "a {b}");
        CHECK(e.doc[2][2].t == T::Ch);
    }

    // equation transformation steps: "2x = 4 | :2" — the step is shown, not calculated; |x| after = is still abs
    {
        Editor e;
        Vi v;
        keys(e, v, "i<C-m>3x+1=7 |−1<Esc>ob = 2 | *3<Esc>ob+1<Esc>o2 = |x|<Esc>");
        CHECK(stepBar(e.doc[0]) == 7 && !ghost(e, e.doc[0]));
        CHECK(noSpaces(lineTex(e.doc[0])).find("=7\\qquad|\\;-1") != std::string::npos);
        CHECK(stepBar(e.doc[1]) >= 0 && !ghost(e, e.doc[1]));
        CHECK_EQ(ghost(e, e.doc[2])->text, "= 3");  // b = 2 still defines b
        CHECK(stepBar(e.doc[3]) < 0 && e.doc[3].back().t == T::Paren);
    }

    // autocomplete settings: popup off (Ctrl-Space opens it), letters before it opens, accept key, conversions
    {
        Editor e;
        Vi v;
        completeOpts.popup = false;
        keys(e, v, "i<C-m>si");
        CHECK(v.items.empty());
        keys(e, v, "<C-Space>");
        CHECK(!v.items.empty());
        keys(e, v, "n");  // typing on keeps it open for the word
        CHECK(!v.items.empty() && v.items[0].name.find("sin") != std::string::npos);
        keys(e, v, "<Esc>");
        completeOpts = {};
        completeOpts.minLetters = 3;
        keys(e, v, "o<C-m>co");
        CHECK(v.items.empty());
        keys(e, v, "s");
        CHECK(!v.items.empty());
        completeOpts.maxItems = 3;
        keys(e, v, "<BS>s");
        CHECK(v.items.size() <= 3);
        keys(e, v, "<Esc>");
        completeOpts = {};
        completeOpts.accept = 1;  // Tab: Enter makes a new line
        keys(e, v, "o<C-m>alph<CR>");
        CHECK_EQ(plain(e.doc[e.doc.size() - 2]), "alph");
        keys(e, v, "<Esc>oalph<Tab><Esc><C-m>");
        CHECK_EQ(plain(e.doc.back()), "α");
        completeOpts = {};
        completeOpts.convert = false, completeOpts.combos = false;
        keys(e, v, "o<C-m>sin -> 2<Esc>");
        CHECK_EQ(plain(e.doc.back()), "sin -> 2");
        completeOpts = {};
        keys(e, v, "oa -> 2<Esc><C-m>");  // math is still on from the line before
        CHECK_EQ(plain(e.doc.back()), "a → 2");
    }

    // equation numbers: "#tag" at the end of a line, "[#tag]" refers to it; the tag isn't math, Tab answers before it
    {
        Editor e;
        Vi v;
        keys(e, v, "i<C-m>A = 2 #area<Esc>o<C-m>2+3 #sum<Tab><Esc>oBy [#area], [#sum] and [#x].<Esc>");
        CHECK_EQ(plain(e.doc[1]), "2+3 = 5 #sum");
        auto nums = eqNumbers(e.doc);
        CHECK(nums.size() == 2 && nums["area"] == 1 && nums["sum"] == 2);
        CHECK(eqRefs(e.doc[2]).size() == 3 && eqName(e.doc[2], eqRefs(e.doc[2])[1]) == "sum");
        CHECK(eqTag(e.doc[2]).first < 0);
        std::string h = html(e, "/n", "T"), t = latex(e, "/n");
        CHECK(h.find("<span class=eqn>(2)</span><math display=\"block\">") != std::string::npos && h.find("By (1), (2) and (?).") != std::string::npos);
        CHECK(h.find("http") == std::string::npos && h.find("<mn>2</mn>") != std::string::npos);  // nothing from the network
        CHECK(t.find("A=2 \\tag{1}") != std::string::npos && t.find("#area") == std::string::npos);
    }

    // tables move like a spreadsheet: arrows go cell to cell (after the text), up/down keep the column, clicks on a
    // border land in a cell; vim's h/l still move by character
    {
        Editor e;
        Vi v;
        keys(e, v, "i| name | value |<Esc>o<Esc>cc| a | 12 |<Esc>o<Esc>cc|  |  |<Esc>");
        auto at = [&](int line, const char* text) {  // the cursor is on `line`, right after `text`
            std::string before;
            for (int j = 0; j < e.cur.i; j++) before += (*e.cur.row)[j].c;
            return e.cur.row == &e.doc[line] && before.ends_with(text);
        };
        e.cur = {&e.doc[0], 3};  // inside "name"
        keys(e, v, "<Right>");
        CHECK(at(0, "| name | value"));
        keys(e, v, "<Right>");  // the last column: stays
        CHECK(at(0, "| name | value"));
        keys(e, v, "<Down>");
        CHECK(at(1, "| a | 12"));
        keys(e, v, "<Left>");
        CHECK(at(1, "| a"));
        keys(e, v, "<Down>");  // an empty cell: in its middle
        CHECK(at(2, "| "));
        keys(e, v, "ix<Esc>");
        CHECK_EQ(plain(e.doc[2]), "| x |  |");
        keys(e, v, "<Up><Up>a<Right>!<Esc>");  // Insert mode too
        CHECK_EQ(plain(e.doc[0]), "| name | value! |");
        e.cur = {&e.doc[1], int(e.doc[1].size())};  // past the last |
        v.mouse(e, e.cur, false);
        CHECK(at(1, "| a | 12"));
        e.cur = {&e.doc[1], 0};  // before the first |
        v.mouse(e, e.cur, false);
        CHECK(at(1, "| a"));
        keys(e, v, "<Esc>0l");  // h/l: one character
        CHECK(e.cur.i == 1);
    }

    // tables and matrices grow and shrink: rows above/below, columns left/right, deletes; I lands in the first cell
    {
        Editor e;
        Vi v;
        keys(e, v, "i| a | b |<Esc>o<Esc>cc| 1 | 2 |<Esc>");
        CHECK(e.gridOp('l'));  // cursor in the last cell of row 2: a column after it
        CHECK_EQ(plain(e.doc[0]), "| a | b |  |");
        CHECK_EQ(plain(e.doc[1]), "| 1 | 2 |  |");
        keys(e, v, "ix<Esc>");
        CHECK_EQ(plain(e.doc[1]), "| 1 | 2 | x |");
        CHECK(e.gridOp('k'));  // a row above row 2
        CHECK(e.doc.size() == 3 && plain(e.doc[1]) == "|  |  |  |");
        CHECK(e.gridOp('r'));
        CHECK(e.doc.size() == 2);
        e.cur = {&e.doc[0], 3};  // in "a"
        CHECK(e.gridOp('c'));
        CHECK_EQ(plain(e.doc[0]), "| b |  |");
        CHECK_EQ(plain(e.doc[1]), "| 2 | x |");
        keys(e, v, "<Esc>IZ<Esc>");
        CHECK_EQ(plain(e.doc[0]), "| Zb |  |");
        keys(e, v, "o<Esc>cc<C-m>matrix<Down><CR>1<Esc>");
        Item* m = &e.doc[1][0];
        CHECK(m->t == T::Mat && m->cols == 3 && matRows(*m) == 3);
        e.cur = {&m->s[0], 1};
        CHECK(e.inGrid() && e.gridOp('j') && matRows(*m) == 4 && e.cur.row == &m->s[3]);
        CHECK(e.gridOp('c') && m->cols == 2 && e.gridOp('r') && matRows(*m) == 3);
        CHECK(!Editor().gridOp('j'));
    }

    // plot kinds: fit through a table, parametric, polar, implicit; 3D surfaces and space curves; fit() as a calculation
    {
        Editor e;
        Vi v;
        keys(e, v, "i| t | h |<Esc>o<Esc>cc| 1 | 3 |<Esc>o<Esc>cc| 2 | 5 |<Esc>o<Esc>cc| 3 | 7 |<Esc>o<Esc>ccplot<Tab>table, fit<Esc>");
        Plot pl = plot(e, e.doc[4]);
        CHECK(pl.series.size() == 2 && pl.series[1].label.starts_with("fit: y = 2x + 1") && !pl.series[1].pts.empty());
        keys(e, v, "ofit(t, h)<Esc>ofit(t, h, exp)<Esc>");
        CHECK_EQ(ghost(e, e.doc[5])->text, "= 2t + 1   (R² = 1)");
        CHECK(ghost(e, e.doc[6]) && ghost(e, e.doc[6])->text.find("e^(") != std::string::npos);
        keys(e, v, "oplot<Tab>(cos(t), sin(t)), r = 2, x^2 + y^2 = 9<Esc>");
        pl = plot(e, e.doc[7]);
        CHECK(pl.error.empty() && pl.series.size() == 3 && !pl.is3d());
        auto onCircle = [](const Plot::Series& s, double r) {
            int n = 0;
            for (auto& [x, y] : s.pts)
                if (std::isfinite(x) && std::isfinite(y)) {
                    if (std::fabs(std::hypot(x, y) - r) > 0.05) return false;
                    n++;
                }
            return n > 50;
        };
        CHECK(onCircle(pl.series[0], 1) && onCircle(pl.series[1], 2) && onCircle(pl.series[2], 3));
        keys(e, v, "oplot<Tab>x y, −2 < x < 2<Esc>oplot<Tab>(cos(t), sin(t), t)<Esc>");
        pl = plot(e, e.doc[8]);
        CHECK(pl.is3d() && pl.surfaces.size() == 1 && pl.surfaces[0].n * pl.surfaces[0].n == int(pl.surfaces[0].z.size()));
        CHECK(pl.y0 == -2 && pl.y1 == 2 && pl.z0 < -2.5 && pl.z1 > 2.5);
        pl = plot(e, e.doc[9]);
        CHECK(pl.is3d() && pl.curves.size() == 1 && pl.curves[0].pts.size() == 600);
        std::string h = html(e, "/n", "T"), t = latex(e, "/n");
        CHECK(h.find("<polygon") != std::string::npos && t.find("\\addplot3[surf") != std::string::npos);
    }

    // program functions: plot/table/section/divider come from the popup for a word alone on its line; the word "plot"
    // in a sentence is just text
    {
        Editor e;
        Vi v;
        keys(e, v, "iplot twist<Esc>oplot");
        CHECK(lineStyle(e.doc[0]).kind == Line::Text);
        CHECK(!v.items.empty() && v.items[0].name == "plot" && v.items[0].cat == "Program");
        keys(e, v, "<Tab>sin(x)<Esc>");
        CHECK(lineStyle(e.doc[1]).kind == Line::Plot && plot(e, e.doc[1]).series.size() == 1);
        keys(e, v, "osection<Tab>x = 1<Esc>odivider<Tab><Esc>otable<Tab>a<Tab>b<Esc>");
        CHECK_EQ(sectionTitle(e.doc[2]), "Exercise 1");
        CHECK_EQ(plain(e.doc[3]), "x = 1");
        CHECK(lineStyle(e.doc[4]).kind == Line::Rule);
        CHECK_EQ(plain(e.doc[5]), "");  // the divider leaves you on a new line
        CHECK_EQ(plain(e.doc[6]), "| a | b |");
        keys(e, v, "o<Esc>cca plot<Esc>");  // not at the line start: no program functions offered
        CHECK(lineStyle(e.doc.back()).kind == Line::Text);
    }

    // selections over several lines
    {
        Editor e;
        Vi v;
        keys(e, v, "ione<CR>two<CR>three<Esc>ggwl");  // w stops on nothing here: to line 2
        e.cur = {&e.doc[0], 1};
        e.selectBetween({&e.doc[0], 1}, {&e.doc[2], 2});
        CHECK(e.range && e.range->row2 && !e.selRange());
        CHECK(e.selOn(e.doc[1]) == std::pair(0, 3));
        CHECK(e.selOn(e.doc[0]) == std::pair(1, 3));
        CHECK(e.selOn(e.doc[2]) == std::pair(0, 2));
        CHECK_EQ(int(e.selectedLines().size()), 3);
        CHECK_EQ(plain(e.selectedLines()[0]), "ne");
        e.deleteSelection();
        CHECK_EQ(int(e.doc.size()), 1);
        CHECK_EQ(plain(e.doc[0]), "oree");
        CHECK_EQ(e.cur.i, 1);
        // backwards, and paste puts the pieces back
        e.load({Row{ch("a", false), ch("b", false)}, Row{ch("c", false), ch("d", false)}}, Path{});
        e.reindex();
        e.selectBetween({&e.doc[1], 1}, {&e.doc[0], 1});
        Doc parts = e.selectedLines();
        CHECK_EQ(plain(parts[0]), "b");
        CHECK_EQ(plain(parts[1]), "c");
        e.deleteSelection();
        CHECK_EQ(plain(e.doc[0]), "ad");
        e.insertLines(parts);
        CHECK_EQ(int(e.doc.size()), 2);
        CHECK_EQ(plain(e.doc[0]), "ab");
        CHECK_EQ(plain(e.doc[1]), "cd");
        CHECK_EQ(e.cur.i, 1);
        // vim: charwise Visual over lines, d and p, one undo; dw stops at the line end; db joins
        Editor f;
        Vi w;
        keys(f, w, "ione<CR>two<CR>three<Esc>ggvjd");
        CHECK_EQ(plain(f.doc[0]), "wo");
        CHECK_EQ(plain(f.doc[1]), "three");
        keys(f, w, "$p");
        CHECK_EQ(plain(f.doc[0]), "woone");
        CHECK_EQ(plain(f.doc[1]), "t");
        keys(f, w, "uu");
        CHECK_EQ(plain(f.doc[0]), "one");
        CHECK_EQ(int(f.doc.size()), 3);
        keys(f, w, "ggdw");
        CHECK_EQ(plain(f.doc[0]), "");
        CHECK_EQ(int(f.doc.size()), 3);
        keys(f, w, "u");
        keys(f, w, "jdb");
        CHECK_EQ(plain(f.doc[0]), "two");
        CHECK_EQ(int(f.doc.size()), 2);
        // standard keys: Shift-Down selects, typing replaces, Ctrl-z brings it back; Ctrl-a twice is the whole note
        Editor g;
        Vi x;
        x.standard = true;
        x.mode = Mode::Insert;
        keys(g, x, "ab<CR>cd<Esc><Up><S-Down>");
        CHECK(g.range && g.range->row2);
        keys(g, x, "Z");
        CHECK_EQ(int(g.doc.size()), 1);
        keys(g, x, "<C-z>");
        CHECK_EQ(int(g.doc.size()), 2);
        g.range.reset();
        keys(g, x, "<C-a><C-a>");
        CHECK(g.range && g.range->row2);
        CHECK_EQ(int(g.selectedLines().size()), 2);
        // typing in standard mode can be undone at all
        Editor h;
        Vi y;
        y.standard = true;
        y.mode = Mode::Insert;
        keys(h, y, "hello");
        keys(h, y, "<C-z>");
        CHECK_EQ(plain(h.doc[0]), "");
        // plot state follows its line
        using PL = std::vector<std::pair<int, std::string>>;
        auto m = followLines(PL{{2, "a"}, {5, "b"}}, PL{{3, "a"}, {6, "b"}});  // a line went in above
        CHECK(m[2] == 3 && m[5] == 6);
        m = followLines(PL{{2, "a"}, {5, "b"}}, PL{{2, "a"}, {5, "c"}});  // one was edited
        CHECK(m[5] == 5);
        m = followLines(PL{{2, "a"}, {5, "b"}}, PL{{4, "b"}});  // one was deleted
        CHECK(m.size() == 1 && m[5] == 4);
    }

    // German numbers: 2,5 is a decimal, ; separates arguments, a comma with a space too; results use the comma
    {
        decimalComma = true;
        Editor e;
        Vi v;
        v.mode = Mode::Insert;
        keys(e, v, "<C-m>a = 2,5<CR>a+1");
        CHECK_EQ(ghost(e, e.doc[1])->text, "= 7/2");
        keys(e, v, "<CR>max(1;2,5)");
        CHECK_EQ(ghost(e, e.doc[2])->text, "= 5/2");
        keys(e, v, "<CR>max(1, 2,5)");
        CHECK_EQ(ghost(e, e.doc[3])->text, "= 5/2");
        mathOpts.decimals = true;
        keys(e, v, "<CR>a/2");
        auto g = ghost(e, e.doc[4]);
        CHECK_EQ(g->text, "= 1,25");
        CHECK_EQ(plain(g->insert).find("1,25") != std::string::npos, true);
        CHECK_EQ(lineTex(e.doc[0]).find("2{,}5") != std::string::npos, true);
        mathOpts.decimals = false;
        decimalComma = false;
        CHECK_EQ(localNum("2.5"), "2.5");
    }

    // the built-in PDF's container: two pages with a good xref
    {
        std::string pdf = imagePdf({std::string(12, char(255)), std::string(12, char(0))}, 2, 2);
        CHECK(pdf.starts_with("%PDF-1.4") && pdf.ends_with("%%EOF\n"));
        CHECK(pdf.find("/Count 2") != std::string::npos);
        size_t x = pdf.rfind("startxref\n") + 10;
        size_t table = std::stoul(pdf.substr(x));
        CHECK(pdf.compare(table, 4, "xref") == 0);
        CHECK(pdf.compare(std::stoul(pdf.substr(table + pdf.substr(table).find("\n", 8) + 21, 10)), 7, "1 0 obj") == 0);
    }

    // matrices in insert mode: Shift+Space grows a column, Shift+Enter a row
    keys(ed, vi, "oA = (1<S-Space>2<S-CR>3<Tab>4<Esc>");
    CHECK(lineTex(ed.doc.back()).find("\\begin{pmatrix} 1 & 2 \\\\ 3 & 4 \\end{pmatrix}") != std::string::npos);
}

int main() {
    checkCorca();
    checkVi();
    std::puts("all checks passed");
}
