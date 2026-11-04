#include "doc.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <regex>
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
        return it.c;
    case T::Var: return texName(it.c, "\\mathit") + " ";
    case T::Const: return (isLetter(it.c) ? "\\mathrm{" + it.c + "}" : texName(it.c, "\\mathrm")) + " ";
    case T::Unit: return "\\,\\mathrm{" + it.c + "}";
    case T::Sym: return it.l + " ";
    case T::Fn: return "\\" + it.c + " ";
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
    for (auto& it : row) s += texItem(it);
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

std::string docTitle(const Doc& doc) {
    for (auto& r : doc) {
        auto cps = codepoints(trimSpaces(plain(r)));
        if (cps.empty()) continue;
        std::string s;
        for (size_t i = 0; i < cps.size() && i < 80; i++) s += cps[i];
        return s;
    }
    return "Untitled";
}

// ---------- JSON ----------
using nlohmann::json;
static const char* TNAME[] = {"ch", "var", "const", "unit", "sym", "fn", "frac", "sup", "sub", "sqrt", "paren", "mat", "big", "lim"};

static json itemJson(const Item& it) {
    json j = {{"t", TNAME[int(it.t)]}};
    switch (it.t) {
    case T::Ch: j["c"] = it.c; j["m"] = it.m; break;
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
    return ch(str(j, "c"), j.value("m", false));
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
bool validName(const std::string& name) {
    static const std::regex re(R"(^[\w.-]+\.json$)");
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
        out.push_back({name, readFile(e.path(), j) && j.contains("meta") && j["meta"].is_object() ? j["meta"] : json::object()});
    }
    return out;
}

}  // namespace om
