#include "doc.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>

namespace om {

Item ch(std::string c, bool m) {
    Item it;
    it.c = std::move(c);
    it.m = m;
    return it;
}
Item named(T t, std::string v) {
    Item it;
    it.t = t;
    it.c = std::move(v);
    return it;
}
Item sym(std::string s, std::string l) {
    Item it;
    it.t = T::Sym;
    it.c = std::move(s);
    it.l = std::move(l);
    return it;
}
Item node(T t) {
    Item it;
    it.t = t;
    it.s.resize(t == T::Frac ? 2 : 1);
    return it;
}
Item paren(std::string o, std::string c, Row a) {
    Item it;
    it.t = T::Paren;
    it.o = std::move(o);
    it.cl = std::move(c);
    it.s = {std::move(a)};
    return it;
}
Item big(std::string s, std::string l, bool limits) {
    Item it;
    it.t = T::Big;
    it.c = std::move(s);
    it.l = std::move(l);
    it.m = limits;
    it.s.resize(2);
    return it;
}
Item mat(std::string o, std::string c, std::vector<std::vector<Row>> rows) {
    Item it;
    it.t = T::Mat;
    it.o = std::move(o);
    it.cl = std::move(c);
    it.cols = rows.empty() ? 0 : int(rows[0].size());
    for (auto& r : rows)
        for (auto& cell : r) it.s.push_back(std::move(cell));
    return it;
}
int matRows(const Item& n) { return n.cols ? int(n.s.size()) / n.cols : 0; }

bool isLetter(const std::string& c) { return c.size() == 1 && ((c[0] >= 'A' && c[0] <= 'Z') || (c[0] >= 'a' && c[0] <= 'z')); }
bool isDigit(const std::string& c) { return c.size() == 1 && c[0] >= '0' && c[0] <= '9'; }

static const std::string OPS = "+-−*/⋅×÷±∓=≠≈≡≔∝<>≤≥≪≫∈∉⊂⊆⊃∪∩∖∧∨⇒⇔→←↔↦,";
bool isOp(const Item* it) {
    return it && !it->c.empty() &&
           ((it->t == T::Ch && it->m && OPS.find(it->c) != std::string::npos) || (it->t == T::Sym && OPS.find(it->c) != std::string::npos));
}
bool isRel(const Item* it) {
    return it && ((it->t == T::Ch && it->m && it->c == "=") || (it->t == T::Sym && (it->c == "=" || it->c == "≔")));
}
const Item* prevNonSpace(const Row& row, int i) {
    for (int j = i - 1; j >= 0; j--)
        if (!isSpace(row[j])) return &row[j];
    return nullptr;
}

// which top-level items render as math: math items, and spaces between two math items
std::vector<bool> mathMask(const Row& row, bool top) {
    std::vector<bool> out(row.size());
    for (size_t j = 0; j < row.size(); j++) {
        if (!top || isMath(row[j])) { out[j] = true; continue; }
        if (!isSpace(row[j])) continue;
        const Item* p = prevNonSpace(row, int(j));
        if (!p || !isMath(*p)) continue;
        for (size_t k = j + 1; k < row.size(); k++)
            if (!isSpace(row[k])) { out[j] = isMath(row[k]); break; }
    }
    if (auto t = top ? eqTag(row) : std::pair{-1, -1}; t.first >= 0)  // an equation tag is a label, not math
        for (int j = t.first - 1; j < int(row.size()); j++) out[j] = false;
    return out;
}

std::map<int, int> followLines(const std::vector<std::pair<int, std::string>>& before, const std::vector<std::pair<int, std::string>>& after) {
    std::map<int, int> out;
    std::vector<bool> used(after.size());
    std::vector<int> oldLeft, newLeft;
    for (auto& [k, text] : before) {
        size_t j = 0;
        while (j < after.size() && (used[j] || after[j].second != text)) j++;
        if (j == after.size()) { oldLeft.push_back(k); continue; }
        used[j] = true;
        out[k] = after[j].first;
    }
    for (size_t j = 0; j < after.size(); j++)
        if (!used[j]) newLeft.push_back(after[j].first);
    if (oldLeft.size() == newLeft.size())
        for (size_t j = 0; j < oldLeft.size(); j++) out[oldLeft[j]] = newLeft[j];
    return out;
}

// ---------- text forms ----------
const std::vector<std::pair<std::string, std::string>> GREEK = {
    {"alpha", "α"}, {"beta", "β"}, {"gamma", "γ"}, {"delta", "δ"}, {"epsilon", "ε"}, {"zeta", "ζ"}, {"eta", "η"},
    {"theta", "θ"}, {"iota", "ι"}, {"kappa", "κ"}, {"lambda", "λ"}, {"mu", "μ"}, {"nu", "ν"}, {"xi", "ξ"},
    {"pi", "π"}, {"rho", "ρ"}, {"sigma", "σ"}, {"tau", "τ"}, {"upsilon", "υ"}, {"phi", "φ"}, {"chi", "χ"},
    {"psi", "ψ"}, {"omega", "ω"}, {"Gamma", "Γ"}, {"Delta", "Δ"}, {"Theta", "Θ"}, {"Lambda", "Λ"}, {"Xi", "Ξ"},
    {"Pi", "Π"}, {"Sigma", "Σ"}, {"Phi", "Φ"}, {"Psi", "Ψ"}, {"Omega", "Ω"}};

static std::vector<std::string> codepoints(const std::string& s) {
    std::vector<std::string> out;
    for (size_t i = 0; i < s.size();) {
        unsigned char b = s[i];
        size_t n = b < 0x80 ? 1 : b < 0xE0 ? 2 : b < 0xF0 ? 3 : 4;
        out.push_back(s.substr(i, n));
        i += n;
    }
    return out;
}

bool decimalComma = false;
bool isDecimalComma(const Row& row, size_t j) {
    return decimalComma && j > 0 && j + 1 < row.size() && row[j].t == T::Ch && row[j].c == "," && row[j - 1].t == T::Ch && row[j + 1].t == T::Ch &&
           isDigit(row[j - 1].c) && isDigit(row[j + 1].c);
}
std::string localNum(std::string s) {
    if (decimalComma)
        for (size_t j = 1; j + 1 < s.size(); j++)
            if (s[j] == '.' && std::isdigit((unsigned char)s[j - 1]) && std::isdigit((unsigned char)s[j + 1])) s[j] = ',';
    return s;
}

static std::string trimSpaces(const std::string& s) {
    size_t a = s.find_first_not_of(' '), b = s.find_last_not_of(' ');
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

static std::string texName(const std::string& v, const char* wrap) {
    std::string s;
    for (auto& c : codepoints(v)) {
        auto g = std::find_if(GREEK.begin(), GREEK.end(), [&](auto& e) { return e.second == c; });
        s += g != GREEK.end() ? "\\" + g->first + " " : c;
    }
    s = trimSpaces(s);
    bool letters = s.size() >= 2 && std::all_of(s.begin(), s.end(), [](char c) { return std::isalpha((unsigned char)c); });
    return letters ? std::string(wrap) + "{" + s + "}" : s;
}

static std::string texParen(const std::string& d) {
    static const std::map<std::string, std::string> m = {{"{", "\\{"}, {"}", "\\}"}, {"‖", "\\|"}, {"⌊", "\\lfloor"},
                                                         {"⌋", "\\rfloor"}, {"⌈", "\\lceil"}, {"⌉", "\\rceil"}};
    auto f = m.find(d);
    return f != m.end() ? f->second : d;
}

static std::string texItem(const Item& it) {
    switch (it.t) {
    case T::Ch:
        if (!it.m) return it.c;
        if (it.c == "*") return "\\cdot ";
        if (it.c == " ") return "";
        if (it.c == "−") return "-";
        if (it.c == "%" || it.c == "#" || it.c == "&" || it.c == "$") return "\\" + it.c;  // TeX specials ("50%")
        return it.c;
    case T::Var: return texName(it.c, "\\mathit") + " ";
    case T::Const: return (isLetter(it.c) ? "\\mathrm{" + it.c + "}" : texName(it.c, "\\mathrm")) + " ";
    case T::Unit: return "\\,\\mathrm{" + it.c + "}";
    case T::Sym: return it.l + " ";
    case T::Fn: {  // LaTeX's own operators; the rest (mean, nCr, plot) as \\operatorname
        static const std::set<std::string> known = {"sin", "cos", "tan", "cot", "sec", "csc", "arcsin", "arccos", "arctan", "sinh",
                                                    "cosh", "tanh", "log", "ln", "exp", "det", "dim", "ker", "max", "min", "sup",
                                                    "inf", "gcd", "arg", "lim"};
        return known.count(it.c) ? "\\" + it.c + " " : "\\operatorname{" + it.c + "} ";
    }
    case T::Frac: return "\\frac{" + tex(it.s[0]) + "}{" + tex(it.s[1]) + "}";
    case T::Sup: return "^{" + tex(it.s[0]) + "}";
    case T::Sub: return "_{" + tex(it.s[0]) + "}";
    case T::Sqrt: return "\\sqrt{" + tex(it.s[0]) + "}";
    case T::Lim: return "\\lim_{" + tex(it.s[0]) + "} ";
    case T::Paren: return "\\left" + texParen(it.o) + " " + tex(it.s[0]) + " \\right" + texParen(it.cl) + " ";
    case T::Mat: {
        static const std::map<std::string, std::string> env = {{"(", "pmatrix"}, {"[", "bmatrix"}, {"{", "Bmatrix"},
                                                               {"|", "vmatrix"}, {"‖", "Vmatrix"}, {"", "matrix"}};
        auto e = env.find(it.o);
        std::string name = e != env.end() ? e->second : "pmatrix", out = "\\begin{" + name + "} ";
        for (int r = 0; r < matRows(it); r++) {
            if (r) out += " \\\\ ";
            for (int c = 0; c < it.cols; c++) out += (c ? " & " : "") + tex(it.s[r * it.cols + c]);
        }
        return out + " \\end{" + name + "} ";
    }
    case T::Big:
        return it.l + (it.s[0].empty() ? "" : "_{" + tex(it.s[0]) + "}") + (it.s[1].empty() ? "" : "^{" + tex(it.s[1]) + "}") + " ";
    }
    return "";
}

std::string tex(const Row& row) {
    std::string s, out;
    for (size_t j = 0; j < row.size(); j++) s += isDecimalComma(row, j) ? "{,}" : texItem(row[j]);
    bool ws = false;  // collapse whitespace runs, then trim
    for (char c : s) {
        bool w = c == ' ' || c == '\n' || c == '\t';
        if (w && ws) continue;
        out += w ? ' ' : c;
        ws = w;
    }
    return trimSpaces(out);
}

std::string lineTex(const Row& row, bool top) {
    if (!top) return tex(row);
    auto mask = mathMask(row, true);
    std::string out;
    for (size_t j = 0; j < row.size();) {
        size_t k = j;
        while (k < row.size() && mask[k] == mask[j]) k++;
        Row seg(row.begin() + j, row.begin() + k);
        if (mask[j]) out += "$" + tex(seg) + "$";
        else
            for (auto& it : seg) out += it.c;
        j = k;
    }
    return out;
}

namespace {
struct Mk {
    uint16_t f;
    const char *open, *close;
};
// text formatting as markers around runs, math as $…$; spaces keep only the markers the next word continues
// ("**a b**", not "**a **b"). ponytail: closes and reopens every marker on a change, so "**a*****b***" can happen
template <size_t N>
std::string styled(const Row& row, int a, int b, const Mk (&mk)[N], std::string (*esc)(const std::string&)) {
    auto mask = mathMask(row, true);
    std::string out;
    uint16_t on = 0;
    auto set = [&](uint16_t f) {
        if (f == on) return;
        for (int m = int(N) - 1; m >= 0; m--) if (on & mk[m].f) out += mk[m].close;
        for (auto& m : mk) if (f & m.f) out += m.open;
        on = f;
    };
    for (int j = a; j < b;) {
        int k = j;
        while (k < b && mask[k] == mask[j]) k++;
        if (mask[j]) {
            set(0);
            std::string t = tex(Row(row.begin() + j, row.begin() + k));
            if (!t.empty()) out += "$" + t + "$";
        } else
            for (int i = j; i < k; i++) {
                uint16_t f = row[i].f;
                if (isSpace(row[i])) {
                    int n = i;
                    while (n < k && isSpace(row[n])) n++;
                    f = on & (n < k ? row[n].f : 0);
                }
                set(f);
                out += esc(row[i].c);
            }
        j = k;
    }
    set(0);
    return out;
}
std::string same(const std::string& c) { return c; }
std::string texEsc(const std::string& c) {
    static const std::map<std::string, std::string> E = {{"\\", "\\textbackslash{}"}, {"{", "\\{"}, {"}", "\\}"}, {"$", "\\$"},
        {"&", "\\&"}, {"#", "\\#"}, {"%", "\\%"}, {"_", "\\_"}, {"~", "\\textasciitilde{}"}, {"^", "\\textasciicircum{}"},
        {"<", "\\textless{}"}, {">", "\\textgreater{}"}, {"|", "\\textbar{}"}};
    auto f = E.find(c);
    return f == E.end() ? c : f->second;
}
}  // namespace

std::string markdown(const Row& row) {
    static const Mk MK[] = {{Bold, "**", "**"}, {Italic, "*", "*"}, {Strike, "~~", "~~"}, {Mark, "==", "=="},
                            {Underline, "<u>", "</u>"}, {Red, "<span style=\"color:red\">", "</span>"},
                            {Green, "<span style=\"color:green\">", "</span>"}, {Blue, "<span style=\"color:blue\">", "</span>"},
                            {Sup, "<sup>", "</sup>"}, {Sub, "<sub>", "</sub>"}, {Code, "`", "`"}};
    return styled(row, 0, int(row.size()), MK, same);
}

std::string latexText(const Row& row, int a, int b) {
    static const Mk MK[] = {{Mark, "\\hl{", "}"}, {Underline, "\\uline{", "}"}, {Strike, "\\sout{", "}"}, {Bold, "\\textbf{", "}"},
                            {Italic, "\\textit{", "}"}, {Red, "\\textcolor{red}{", "}"}, {Green, "\\textcolor{green!50!black}{", "}"},
                            {Blue, "\\textcolor{blue}{", "}"}, {Sup, "\\textsuperscript{", "}"}, {Sub, "\\textsubscript{", "}"},
                            {Code, "\\texttt{", "}"}};
    return styled(row, a, b, MK, texEsc);
}

std::string markdown(const Doc& doc, const std::filesystem::path& base) {
    auto group = [](const Row& r) {  // consecutive list items or quote lines stay together; others are paragraphs
        Line l = lineStyle(r).kind;
        return l == Line::Quote ? 2 : isList(l) ? 1 : l == Line::Table ? 3 : 0;
    };
    std::string out;
    for (size_t k = 0; k < doc.size(); k++) {
        if (k) out += group(doc[k]) && group(doc[k]) == group(doc[k - 1]) ? "\n" : "\n\n";
        std::string alt, path;
        if (lineStyle(doc[k]).kind == Line::Section) out += "---\n\n**" + sectionTitle(doc[k]) + "**";
        else if (!base.empty() && imageRef(doc[k], alt, path) && std::filesystem::path(path).is_relative())
            out += "![" + alt + "](" + (base / path).generic_string() + ")";
        else out += markdown(doc[k]);
        if (group(doc[k]) == 3 && (!k || group(doc[k - 1]) != 3)) {  // a table's first row is its header
            out += "\n|";
            for (size_t c = 0; c < std::max<size_t>(1, tableCells(doc[k]).size()); c++) out += "---|";
        }
    }
    return out + "\n";
}

std::string plain(const Row& row) {
    static const std::map<std::string, std::string> SUPS = {{"0", "⁰"}, {"1", "¹"}, {"2", "²"}, {"3", "³"}, {"4", "⁴"},
                                                            {"5", "⁵"}, {"6", "⁶"}, {"7", "⁷"}, {"8", "⁸"}, {"9", "⁹"},
                                                            {"−", "⁻"}, {"-", "⁻"}, {"n", "ⁿ"}};
    std::string out;
    for (auto& it : row) {
        switch (it.t) {
        case T::Ch: out += it.c == "*" ? "·" : it.c; break;
        case T::Var: case T::Const: case T::Unit: case T::Fn: case T::Sym: out += it.c; break;
        case T::Frac: out += plain(it.s[0]) + "/" + plain(it.s[1]); break;
        case T::Sup: {
            std::string s = plain(it.s[0]), sup;
            bool all = true;
            for (auto& c : codepoints(s)) {
                auto f = SUPS.find(c);
                if (f == SUPS.end()) { all = false; break; }
                sup += f->second;
            }
            out += all ? sup : "^" + s;
            break;
        }
        case T::Sub: out += "_" + plain(it.s[0]); break;
        case T::Sqrt: out += "√" + plain(it.s[0]); break;
        case T::Paren: out += it.o + plain(it.s[0]) + it.cl; break;
        case T::Mat: {
            out += it.o;
            for (int r = 0; r < matRows(it); r++) {
                if (r) out += "; ";
                for (int c = 0; c < it.cols; c++) out += (c ? " " : "") + plain(it.s[r * it.cols + c]);
            }
            out += it.cl;
            break;
        }
        case T::Big: out += it.c; break;
        case T::Lim: out += "lim "; break;
        }
    }
    return out;
}

uint16_t fmtGroup(uint16_t flag) { return flag & Script ? Script : flag & Colors ? Colors : flag; }

bool imageRef(const Row& row, std::string& alt, std::string& path) {
    std::string s;
    for (auto& it : row) {
        if (it.t != T::Ch) return false;
        s += it.c;
    }
    size_t mid = s.find("](");
    if (!s.starts_with("![") || !s.ends_with(")") || mid == std::string::npos) return false;
    alt = s.substr(2, mid - 2);
    path = s.substr(mid + 2, s.size() - mid - 3);
    return !path.empty();
}

// ponytail: a pipe inside math stays an absolute value, so only text pipes split cells
std::vector<std::pair<int, int>> tableCells(const Row& row) {
    std::vector<int> pipes;
    for (int j = 0; j < int(row.size()); j++)
        if (row[j].t == T::Ch && !row[j].m && row[j].c == "|") pipes.push_back(j);
    std::vector<std::pair<int, int>> out;
    for (size_t k = 0; k + 1 < pipes.size(); k++) out.push_back({pipes[k] + 1, pipes[k + 1]});
    if (!pipes.empty() && pipes.back() + 1 < int(row.size())) {  // text after the last pipe is a cell too
        int a = pipes.back() + 1;
        bool blank = std::all_of(row.begin() + a, row.end(), [](auto& x) { return isSpace(x); });
        if (!blank) out.push_back({a, int(row.size())});
    }
    return out;
}

std::string sectionTitle(const Row& row) {
    std::string s;
    for (size_t j = 3; j < row.size(); j++) s += row[j].t == T::Ch ? row[j].c : plain(Row{row[j]});
    size_t a = s.find_first_not_of(" =");
    return a == std::string::npos ? "" : s.substr(a, s.find_last_not_of(' ') - a + 1);
}

std::pair<int, int> sectionOf(const Doc& doc, int k) {
    int a = k, b = k + 1, n = int(doc.size());
    while (a > 0 && lineStyle(doc[a]).kind != Line::Section) a--;
    while (b < n && lineStyle(doc[b]).kind != Line::Section) b++;
    return {a, b};
}

std::vector<std::pair<int, int>> linkSpans(const Row& row) {
    std::vector<std::pair<int, int>> out;
    auto at = [&](int j, const char* c) { return j < int(row.size()) && row[j].t == T::Ch && !row[j].m && row[j].c == c; };
    for (int j = 0; j + 1 < int(row.size()); j++) {
        if (!(at(j, "[") && at(j + 1, "["))) continue;
        for (int e = j + 2; e + 1 < int(row.size()); e++) {
            if (row[e].t != T::Ch || row[e].m) break;  // ponytail: a link's title is plain text
            if (at(e, "]") && at(e + 1, "]")) {
                if (e > j + 2) out.push_back({j, e + 2});
                j = e + 1;
                break;
            }
        }
    }
    return out;
}

std::string linkTitle(const Row& row, std::pair<int, int> span) {
    std::string s;
    for (int j = span.first + 2; j < span.second - 2; j++) s += row[j].c;
    return s;
}

static bool nameChar(const Item& it) {  // a letter, digit, _ or -, typed as text or math
    return (it.t == T::Ch || it.t == T::Var) && it.c.size() == 1 && (std::isalnum((unsigned char)it.c[0]) || it.c == "_" || it.c == "-");
}

std::pair<int, int> eqTag(const Row& row) {
    int n = int(row.size()), e = n;
    while (e > 0 && isSpace(row[e - 1])) e--;
    int j = e;
    while (j > 0 && nameChar(row[j - 1])) j--;
    if (j == e || j < 2 || row[j - 1].t != T::Ch || row[j - 1].c != "#" || !isSpace(row[j - 2])) return {-1, -1};
    return {j - 1, n};
}

int stepBar(const Row& row) {
    for (int j = 0; j < int(row.size()); j++)
        if (isStepBar(row[j])) return j;
    return -1;
}

std::vector<std::pair<int, int>> eqRefs(const Row& row) {
    std::vector<std::pair<int, int>> out;
    auto at = [&](int j, const char* c) { return j < int(row.size()) && row[j].t == T::Ch && row[j].c == c; };
    for (int j = 0; j + 3 < int(row.size()); j++) {
        if (!at(j, "[") || !at(j + 1, "#")) continue;
        int e = j + 2;
        while (e < int(row.size()) && nameChar(row[e])) e++;
        if (e > j + 2 && at(e, "]")) out.push_back({j, e + 1}), j = e;
    }
    return out;
}

std::string eqName(const Row& row, std::pair<int, int> span) {
    std::string s;
    for (int j = span.first; j < span.second; j++)
        if (nameChar(row[j])) s += row[j].c;
    return s;
}

std::map<std::string, int> eqNumbers(const std::vector<Row>& doc) {
    std::map<std::string, int> out;
    for (auto& r : doc)
        if (auto t = eqTag(r); t.first >= 0) out.try_emplace(eqName(r, t), int(out.size()) + 1);
    return out;
}

LineStyle lineStyle(const Row& row) {
    std::string s;  // the leading plain characters, enough for the longest prefix
    for (size_t i = 0; i < row.size() && i < 12 && row[i].t == T::Ch; i++) s += row[i].c;
    auto is = [&](const char* p) { return s.starts_with(p); };
    auto n = [](const char* p) { return int(std::string(p).size()); };
    if (row.size() == 3 && s == "---") return {Line::Rule};
    if (is("===")) return {Line::Section};
    if (is("|")) return {Line::Table};
    if (!row.empty() && row[0].t == T::Fn && row[0].c == "plot") return {Line::Plot};  // the program function, not the word
    if (std::string alt, path; is("![") && imageRef(row, alt, path)) return {Line::Image};
    if (is("- [ ] ")) return {Line::Todo, 6};
    if (is("- [x] ") || is("- [X] ")) return {Line::Done, 6};
    if (is("### ")) return {Line::H3, 4};
    if (is("## ")) return {Line::H2, 3};
    if (is("# ")) return {Line::H1, 2};
    if (is("- ")) return {Line::Bullet, 2};
    if (is("> ")) return {Line::Quote, 2};
    size_t d = 0;
    while (d < s.size() && d < 4 && std::isdigit((unsigned char)s[d])) d++;
    if (d && s.compare(d, 2, ". ") == 0) return {Line::Number, n("1. ") + int(d) - 1, std::stoi(s.substr(0, d))};
    return {};
}

std::string linePrefix(Line kind, int num) {
    switch (kind) {
    case Line::Text: return "";
    case Line::H1: return "# ";
    case Line::H2: return "## ";
    case Line::H3: return "### ";
    case Line::Bullet: return "- ";
    case Line::Number: return std::to_string(num) + ". ";
    case Line::Quote: return "> ";
    case Line::Todo: return "- [ ] ";
    case Line::Done: return "- [x] ";
    default: break;
    }
    return "";
}

std::string docTitle(const Doc& doc) {
    for (auto& r : doc) {
        auto cps = codepoints(trimSpaces(plain(Row(r.begin() + lineStyle(r).len, r.end()))));
        if (cps.empty()) continue;
        std::string s;
        for (size_t i = 0; i < cps.size() && i < 80; i++) s += cps[i];
        return s;
    }
    return "Untitled";
}

// ---------- tables as text ----------
static std::string fixNumber(std::string c) {
    size_t i = !c.empty() && (c[0] == '-' || c[0] == '+');
    if (i >= c.size()) return c;
    int seps = 0;
    for (size_t j = i; j < c.size(); j++) {
        if (c[j] == '.' || c[j] == ',') seps++;
        else if (!std::isdigit((unsigned char)c[j])) return c;
    }
    if (seps != 1 || c[i] == '.' || c[i] == ',' || c.back() == '.' || c.back() == ',') return c;
    for (auto& x : c)
        if (x == '.' || x == ',') x = decimalComma ? ',' : '.';
    return c;
}

std::vector<Row> tableFromText(std::string s) {
    if (s.starts_with("\xEF\xBB\xBF")) s.erase(0, 3);  // Excel's byte order mark
    size_t lineEnd = s.find('\n');
    std::string head = s.substr(0, lineEnd);
    char sep = ',';
    size_t most = 0;
    for (char c : {',', ';', '\t'})  // ties go to the later: "1,5;2,5" is split at ;
        if (size_t n = std::count(head.begin(), head.end(), c); n && n >= most) most = n, sep = c;
    std::vector<std::vector<std::string>> cells(1, std::vector<std::string>(1));
    bool quoted = false;
    for (size_t j = 0; j < s.size(); j++) {
        char c = s[j];
        if (quoted) {
            if (c == '"' && j + 1 < s.size() && s[j + 1] == '"') cells.back().back() += '"', j++;
            else if (c == '"') quoted = false;
            else cells.back().back() += c == '\n' || c == '\r' ? ' ' : c;  // a table cell is one line
        } else if (c == '"' && cells.back().back().empty()) quoted = true;
        else if (c == sep) cells.back().emplace_back();
        else if (c == '\n') cells.emplace_back(1);
        else if (c != '\r') cells.back().back() += c;
    }
    while (!cells.empty() && cells.back().size() == 1 && cells.back()[0].empty()) cells.pop_back();
    size_t cols = 0;
    for (auto& r : cells) cols = std::max(cols, r.size());
    std::vector<Row> out;
    for (auto& r : cells) {
        std::string line = "|";
        for (size_t c = 0; c < cols; c++) {
            std::string v = c < r.size() ? fixNumber(trimSpaces(r[c])) : "";
            std::replace(v.begin(), v.end(), '|', '/');  // a | would end the cell
            line += " " + v + " |";
        }
        Row row;
        for (auto& c : codepoints(line)) row.push_back(ch(c, false));
        out.push_back(std::move(row));
    }
    return out;
}

std::string tableText(const Doc& doc, int k, char sep) {
    auto isTable = [&](int j) { return j >= 0 && j < int(doc.size()) && lineStyle(doc[j]).kind == Line::Table; };
    if (!isTable(k))
        for (k = 0; k < int(doc.size()) && !isTable(k); k++) {}
    if (!isTable(k)) return "";
    while (isTable(k - 1)) k--;
    std::string out;
    for (; isTable(k); k++) {
        bool first = true;
        for (auto [a, b] : tableCells(doc[k])) {
            std::string c = trimSpaces(plain(Row(doc[k].begin() + a, doc[k].begin() + b)));
            if (c.find_first_of(std::string("\"\n") + sep) != std::string::npos) {  // quoted, as CSV wants
                std::string q = "\"";
                for (char x : c) q += x == '"' ? "\"\"" : std::string(1, x);
                c = q + "\"";
            }
            out += (first ? "" : std::string(1, sep)) + c;
            first = false;
        }
        out += "\n";
    }
    return out;
}

// ---------- JSON ----------
using nlohmann::json;
static const char* TNAME[] = {"ch", "var", "const", "unit", "sym", "fn", "frac", "sup", "sub", "sqrt", "paren", "mat", "big", "lim"};

static json itemJson(const Item& it) {
    json j = {{"t", TNAME[int(it.t)]}};
    switch (it.t) {
    case T::Ch: j["c"] = it.c; j["m"] = it.m; if (it.f) j["f"] = it.f; break;
    case T::Var: case T::Const: case T::Unit: case T::Fn: j["v"] = it.c; break;
    case T::Sym: j["s"] = it.c; j["l"] = it.l; break;
    case T::Frac: j["a"] = toJson(it.s[0]); j["b"] = toJson(it.s[1]); break;
    case T::Sup: case T::Sub: case T::Sqrt: case T::Lim: j["a"] = toJson(it.s[0]); break;
    case T::Paren: j["o"] = it.o; j["c"] = it.cl; j["a"] = toJson(it.s[0]); break;
    case T::Mat: {
        json rows = json::array();
        for (int r = 0; r < matRows(it); r++) {
            json cells = json::array();
            for (int c = 0; c < it.cols; c++) cells.push_back(toJson(it.s[r * it.cols + c]));
            rows.push_back(cells);
        }
        j["o"] = it.o; j["c"] = it.cl; j["rows"] = rows;
        break;
    }
    case T::Big: j["s"] = it.c; j["l"] = it.l; j["limits"] = it.m; j["lo"] = toJson(it.s[0]); j["hi"] = toJson(it.s[1]); break;
    }
    return j;
}

json toJson(const Row& row) {
    json a = json::array();
    for (auto& it : row) a.push_back(itemJson(it));
    return a;
}

static std::string str(const json& j, const char* k) { return j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : ""; }

static Item itemFromJson(const json& j) {
    std::string t = str(j, "t");
    auto slot = [&](const char* k) { return j.contains(k) ? rowFromJson(j[k]) : Row{}; };
    if (t == "var" || t == "const" || t == "unit" || t == "fn")
        return named(t == "var" ? T::Var : t == "const" ? T::Const : t == "unit" ? T::Unit : T::Fn, str(j, "v"));
    if (t == "sym") return sym(str(j, "s"), str(j, "l"));
    if (t == "frac") { Item n = node(T::Frac); n.s = {slot("a"), slot("b")}; return n; }
    if (t == "sup" || t == "sub" || t == "sqrt" || t == "lim") {
        Item n = node(t == "sup" ? T::Sup : t == "sub" ? T::Sub : t == "sqrt" ? T::Sqrt : T::Lim);
        n.s = {slot("a")};
        return n;
    }
    if (t == "paren") return paren(str(j, "o"), str(j, "c"), slot("a"));
    if (t == "mat") {
        std::vector<std::vector<Row>> rows;
        if (j.contains("rows"))
            for (auto& r : j["rows"]) {
                rows.emplace_back();
                for (auto& cell : r) rows.back().push_back(rowFromJson(cell));
            }
        return mat(str(j, "o"), str(j, "c"), std::move(rows));
    }
    if (t == "big") {
        Item n = big(str(j, "s"), str(j, "l"), j.value("limits", false));
        n.s = {slot("lo"), slot("hi")};
        return n;
    }
    Item c = ch(str(j, "c"), j.value("m", false));
    if (j.contains("f") && j["f"].is_number_unsigned()) c.f = uint16_t(j["f"].get<unsigned>() & AllFmt);
    return c;
}

Row rowFromJson(const json& j) {
    Row r;
    if (j.is_array())
        for (auto& x : j) r.push_back(itemFromJson(x));
    return r;
}
json docJson(const Doc& doc) {
    json a = json::array();
    for (auto& r : doc) a.push_back(toJson(r));
    return a;
}
Doc docFromJson(const json& j) {
    Doc d;
    if (j.is_array())
        for (auto& r : j) d.push_back(rowFromJson(r));
    if (d.empty()) d.emplace_back();
    return d;
}

json pathJson(const Path& p) {
    json steps = json::array();
    for (auto [a, b] : p.p) steps.push_back({a, b});
    return {{"k", p.k}, {"p", steps}, {"i", p.i}};
}
Path pathFromJson(const json& j) {
    Path p;
    if (!j.is_object()) return p;
    p.k = j.value("k", 0);
    p.i = j.value("i", 0);
    if (j.contains("p") && j["p"].is_array())
        for (auto& s : j["p"])
            if (s.is_array() && s.size() == 2) p.p.push_back({s[0].get<int>(), s[1].get<int>()});
    return p;
}

// ---------- files ----------
std::string base64(const std::string& in) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        unsigned v = (unsigned char)in[i] << 16 | (unsigned char)in[i + 1] << 8 | (unsigned char)in[i + 2];
        out += {T[v >> 18], T[v >> 12 & 63], T[v >> 6 & 63], T[v & 63]};
    }
    if (i + 1 == in.size()) {
        unsigned v = (unsigned char)in[i] << 16;
        out += {T[v >> 18], T[v >> 12 & 63], '=', '='};
    } else if (i + 2 == in.size()) {
        unsigned v = (unsigned char)in[i] << 16 | (unsigned char)in[i + 1] << 8;
        out += {T[v >> 18], T[v >> 12 & 63], T[v >> 6 & 63], '='};
    }
    return out;
}

std::string unbase64(const std::string& in) {
    std::string out;
    unsigned v = 0;
    int bits = 0;
    for (char c : in) {
        int d = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52
              : c == '+' ? 62 : c == '/' ? 63 : -1;
        if (d < 0) continue;  // '=' padding, line breaks
        v = v << 6 | d;
        if ((bits += 6) >= 8) out += char(v >> (bits -= 8) & 0xFF);
    }
    return out;
}

// notes: name.opman, or corca's name.json (read, and converted to .opman by the app)
bool validName(const std::string& name) {
    static const std::regex re(R"(^[^.].*\.(opman|json)$)");
    return std::regex_match(name, re);
}

bool readFile(const fs::path& file, json& out) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    out = json::parse(in, nullptr, false);
    return !out.is_discarded();
}

bool writeFile(const fs::path& file, const json& j) {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    fs::path tmp = file;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << j.dump();
        if (!out.flush()) return false;
    }
    fs::rename(tmp, file, ec);  // replaces the target atomically on POSIX and Windows
    return !ec;
}

std::vector<FileInfo> listDocs(const fs::path& dir) {
    std::vector<FileInfo> out;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(dir, ec)) {
        std::string name = e.path().filename().string();
        if (!e.is_regular_file(ec) || !validName(name)) continue;
        json j;
        if (!readFile(e.path(), j) || !j.is_object() || !j.contains("doc")) continue;  // some other JSON
        out.push_back({e.path().string(), j.contains("meta") && j["meta"].is_object() ? j["meta"] : json::object()});
    }
    return out;
}

}  // namespace om
