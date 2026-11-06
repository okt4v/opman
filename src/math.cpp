#include "math.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <numbers>
#include <set>

namespace om {
MathOpts mathOpts;
namespace {

struct Fail {};
[[noreturn]] void fail() { throw Fail{}; }

// ---------- numbers: exact rationals {n, d} while they fit in safe integers, floats otherwise ----------
// n and d are doubles holding integers, so overflow behaves exactly like corca's JS (2^53, then float)
struct Value {
    enum K : uint8_t { Q, F, M } k = Q;
    double n = 0, d = 1, f = 0;
    std::vector<std::vector<Value>> m;
    bool operator==(const Value&) const = default;
};

const double SAFE = 9007199254740991.0;
bool safeInt(double x) { return std::isfinite(x) && std::floor(x) == x && std::fabs(x) <= SAFE; }
double jsRound(double x) { return std::floor(x + 0.5); }
double gcd(double a, double b) {
    a = std::fabs(a), b = std::fabs(b);
    while (b) {
        double t = std::fmod(a, b);
        a = b, b = t;
    }
    return a;
}
Value Fl(double f) {  // a raw float, like corca's {f}
    Value v;
    v.k = Value::F;
    v.f = f;
    return v;
}
Value F(double f);
Value Q(double n, double d = 1) {
    if (d == 0) fail();
    if (!safeInt(n) || !safeInt(d)) return F(n / d);
    if (d < 0) n = -n, d = -d;
    double g = gcd(n, d);
    if (!g) g = 1;
    Value v;
    v.n = n / g + 0.0;  // +0.0: no -0
    v.d = d / g;
    return v;
}
Value F(double f) {
    if (std::isnan(f)) fail();
    if (safeInt(f)) {
        Value v;
        v.n = f + 0.0;
        return v;
    }
    return Fl(f);
}
// builtin results that are an integer up to rounding noise become exact: sin(π) = 0, not 1.2e-16
Value F0(double f) {
    double r = jsRound(f);
    return std::isfinite(f) && std::fabs(f - r) < 1e-12 * std::max(1.0, std::fabs(f)) ? F(r) : F(f);
}
// floats from numeric methods snap to a nearby simple fraction (1/3 from an integral)
Value nearQ(double f, double tol = 1e-11) {
    if (!std::isfinite(f)) return F(f);
    for (int d = 1; d <= 1000; d++) {
        double n = jsRound(f * d);
        if (std::fabs(n / d - f) < tol * std::max(1.0, std::fabs(f))) return Q(n, d);
    }
    return Fl(f);
}
bool exact(const Value& x) { return x.k == Value::Q; }
bool isMat(const Value& x) { return x.k == Value::M; }
double val(const Value& x) { return exact(x) ? x.n / x.d : x.f; }
bool isZero(const Value& x) { return val(x) == 0; }
bool isInt(const Value& x) { return exact(x) && x.d == 1; }
Value add(const Value& a, const Value& b) { return exact(a) && exact(b) ? Q(a.n * b.d + b.n * a.d, a.d * b.d) : F(val(a) + val(b)); }
Value neg(const Value& a) { return exact(a) ? Q(-a.n, a.d) : F(-a.f); }
Value mul(const Value& a, const Value& b) { return exact(a) && exact(b) ? Q(a.n * b.n, a.d * b.d) : F(val(a) * val(b)); }
Value div(const Value& a, const Value& b) {
    if (isZero(b)) fail();
    return exact(a) && exact(b) ? Q(a.n * b.d, a.d * b.n) : F(val(a) / val(b));
}
Value pow(const Value& a, const Value& b) {
    if (exact(a) && isInt(b) && std::fabs(b.n) <= 64) {
        Value r = Q(1);
        for (int k = 0; k < std::fabs(b.n); k++) r = mul(r, a);
        return b.n < 0 ? div(Q(1), r) : r;
    }
    return F(std::pow(val(a), val(b)));
}
Value fact(const Value& a) {
    if (!isInt(a) || a.n < 0 || a.n > 170) fail();
    Value r = Q(1);
    for (int k = 2; k <= a.n; k++) r = mul(r, Q(k));
    return r;
}
Value matV(std::vector<std::vector<Value>> m) {
    Value v;
    v.k = Value::M;
    v.m = std::move(m);
    return v;
}
std::string intStr(double n) {
    char b[64];
    std::snprintf(b, sizeof b, "%.0f", n);
    return b;
}

}  // namespace

// String(x) of a JS number: shortest round-trip digits, fixed notation for 1e-7 < |x| < 1e21
std::string jsNum(double x) {
    if (std::isnan(x)) return "NaN";
    if (std::isinf(x)) return x < 0 ? "-Infinity" : "Infinity";
    if (x == 0) return "0";
    char buf[64];
    for (int p = 1; p <= 17; p++) {
        std::snprintf(buf, sizeof buf, "%.*e", p - 1, x);
        if (std::strtod(buf, nullptr) == x) break;
    }
    std::string s = buf, sign, digits;
    if (s[0] == '-') sign = "-", s = s.substr(1);
    size_t e = s.find('e');
    for (size_t i = 0; i < e; i++)
        if (s[i] != '.') digits += s[i];
    while (digits.size() > 1 && digits.back() == '0') digits.pop_back();
    int n = std::atoi(s.c_str() + e + 1) + 1, k = int(digits.size());  // value = 0.digits × 10^n
    if (k <= n && n <= 21) return sign + digits + std::string(n - k, '0');
    if (0 < n && n <= 21) return sign + digits.substr(0, n) + "." + digits.substr(n);
    if (-6 < n && n <= 0) return sign + "0." + std::string(-n, '0') + digits;
    std::string exp = (n - 1 < 0 ? "-" : "+") + std::to_string(std::abs(n - 1));
    return sign + digits.substr(0, 1) + (k > 1 ? "." + digits.substr(1) : "") + "e" + exp;
}

double toPrecision(double x, int p) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*e", p - 1, x);
    return std::strtod(buf, nullptr);
}

namespace {

std::string fmt(const Value& x, int digits = 0) {
    if (isMat(x)) {
        std::string s = "(";
        for (size_t r = 0; r < x.m.size(); r++) {
            if (r) s += "; ";
            for (size_t c = 0; c < x.m[r].size(); c++) s += (c ? " " : "") + fmt(x.m[r][c]);
        }
        return s + ")";
    }
    if (exact(x)) return x.d == 1 ? intStr(x.n) : intStr(x.n) + "/" + intStr(x.d);
    if (!std::isfinite(x.f)) fail();
    return jsNum(toPrecision(x.f, digits ? digits : mathOpts.digits));
}

const std::map<std::string, double (*)(double)> BUILTIN = {
    {"sin", [](double x) { return std::sin(x); }},   {"cos", [](double x) { return std::cos(x); }},
    {"tan", [](double x) { return std::tan(x); }},   {"cot", [](double x) { return 1 / std::tan(x); }},
    {"sec", [](double x) { return 1 / std::cos(x); }}, {"csc", [](double x) { return 1 / std::sin(x); }},
    {"arcsin", [](double x) { return std::asin(x); }}, {"arccos", [](double x) { return std::acos(x); }},
    {"arctan", [](double x) { return std::atan(x); }}, {"sinh", [](double x) { return std::sinh(x); }},
    {"cosh", [](double x) { return std::cosh(x); }}, {"tanh", [](double x) { return std::tanh(x); }},
    {"log", [](double x) { return std::log10(x); }}, {"ln", [](double x) { return std::log(x); }},
    {"exp", [](double x) { return std::exp(x); }},   {"sqrt", [](double x) { return std::sqrt(x); }},
    {"abs", [](double x) { return std::fabs(x); }},  {"floor", [](double x) { return std::floor(x); }},
    {"ceil", [](double x) { return std::ceil(x); }}, {"round", [](double x) { return std::round(x); }},
    {"sign", [](double x) { return double((x > 0) - (x < 0)); }}};

// functions of any number of arguments; a table column's name as the only argument stands for its numbers: mean(h)
const std::set<std::string> VARIADIC = {"max", "min", "mean", "median", "stdev", "var", "sum", "count", "gcd", "lcm"};
bool isTrig(const std::string& f) {
    return f == "sin" || f == "cos" || f == "tan" || f == "cot" || f == "sec" || f == "csc";
}
bool isArc(const std::string& f) { return f == "arcsin" || f == "arccos" || f == "arctan"; }

std::optional<double> isqrt(double n) {
    double r = jsRound(std::sqrt(n));
    return r * r == n ? std::optional(r) : std::nullopt;
}
std::optional<Value> exactFn(const std::string& f, const Value& x) {
    if (f == "abs") return Q(std::fabs(x.n), x.d);
    if (f == "floor") return Q(std::floor(x.n / x.d));
    if (f == "ceil") return Q(std::ceil(x.n / x.d));
    if (f == "sqrt") {
        auto a = isqrt(x.n), b = isqrt(x.d);
        if (x.n >= 0 && a && b) return Q(*a, *b);
    }
    return std::nullopt;
}
bool hasExact(const std::string& f) { return f == "abs" || f == "floor" || f == "ceil" || f == "sqrt"; }
std::map<std::string, std::vector<Value>> COLUMNS;
bool DECIMAL = false;  // numItems writes decimals (the "Decimal" suggestion)  // table columns of the section: header name -> numbers
bool isConst(const std::string& n) { return n == "π" || n == "e"; }
double constVal(const std::string& n) { return n == "π" ? std::numbers::pi : std::numbers::e; }

// ---------- expressions ----------
struct Ast {
    enum K : uint8_t { Num, Var, Group, Neg, Bin, Fact, Tr, Call, Mat, Deriv, Lim, Big } k = Num;
    Value v;             // Num
    std::string n;       // Var name; Call/Deriv function; Lim/Big bound variable
    std::string op;      // Bin: + - * / ^; Big: sum prod int
    int order = 0;       // Deriv
    int cols = 0;        // Mat
    std::vector<Ast> a;  // Group/Neg/Fact/Tr [x]; Bin [l, r]; Call args; Mat cells; Deriv [arg]; Lim [to, body]; Big [lo, hi, body]
    bool operator==(const Ast&) const = default;
};
Ast num(Value v) {
    Ast a;
    a.v = std::move(v);
    return a;
}
Ast mk(Ast::K k, std::vector<Ast> kids = {}, std::string n = "", std::string op = "") {
    Ast a;
    a.k = k;
    a.a = std::move(kids);
    a.n = std::move(n);
    a.op = std::move(op);
    return a;
}
Ast var(std::string n) { return mk(Ast::Var, {}, std::move(n)); }
Ast bin(std::string op, Ast l, Ast r) { return mk(Ast::Bin, {std::move(l), std::move(r)}, "", std::move(op)); }

struct Def {
    std::vector<std::string> params;
    Row body;
};
// ponytail: file-level state like corca's globals; the UI is single-threaded
Editor* ED = nullptr;
std::map<std::string, Def> DEFS;
bool UNIT_MODE = false;
const Def* def(const std::string& n) {
    auto f = DEFS.find(n);
    return f == DEFS.end() ? nullptr : &f->second;
}

// units: symbol → size in SI base units, dimensions (in insertion order, like JS objects)
using Dims = std::vector<std::pair<std::string, int>>;
struct Unit {
    Value size;
    Dims d;
};
const std::map<std::string, Unit>& UNITS() {
    static const std::map<std::string, Unit> u = {
        {"m", {Q(1), {{"m", 1}}}}, {"km", {Q(1000), {{"m", 1}}}}, {"cm", {Q(1, 100), {{"m", 1}}}}, {"mm", {Q(1, 1000), {{"m", 1}}}},
        {"g", {Q(1, 1000), {{"kg", 1}}}}, {"kg", {Q(1), {{"kg", 1}}}},
        {"s", {Q(1), {{"s", 1}}}}, {"ms", {Q(1, 1000), {{"s", 1}}}}, {"min", {Q(60), {{"s", 1}}}}, {"h", {Q(3600), {{"s", 1}}}},
        {"N", {Q(1), {{"kg", 1}, {"m", 1}, {"s", -2}}}}, {"J", {Q(1), {{"kg", 1}, {"m", 2}, {"s", -2}}}},
        {"W", {Q(1), {{"kg", 1}, {"m", 2}, {"s", -3}}}}, {"Hz", {Q(1), {{"s", -1}}}},
        {"Pa", {Q(1), {{"kg", 1}, {"m", -1}, {"s", -2}}}}, {"A", {Q(1), {{"A", 1}}}},
        {"V", {Q(1), {{"kg", 1}, {"m", 2}, {"s", -3}, {"A", -1}}}}, {"K", {Q(1), {{"K", 1}}}}, {"L", {Q(1, 1000), {{"m", 3}}}}};
    return u;
}
const Unit* unit(const std::string& n) {
    auto f = UNITS().find(n);
    return f == UNITS().end() ? nullptr : &f->second;
}
const std::vector<std::string> NAMED = {"N", "J", "W", "Pa", "V", "Hz"};

// ---------- tokens and parsing (the editor tree, not a string) ----------
struct Tok {
    enum K : uint8_t { Num, Var, Op, Bang, Prime, A, Pow, Paren, Fn, Big, Lim } k;
    Value v;
    std::string n;  // Var name, Fn name, Op
    Ast a;          // A, Pow
    const Item* node = nullptr;  // Paren, Big, Lim
};

Ast parse(const Row& row);
Ast parseTokens(const std::vector<Tok>& t);

std::vector<Tok> mergeUnits(const std::vector<Tok>& t) {
    std::vector<Tok> out;
    for (size_t j = 0; j < t.size(); j++) {
        bool pair = t[j].k == Tok::Var && j + 1 < t.size() && t[j + 1].k == Tok::Var;
        std::string two = pair ? t[j].n + t[j + 1].n : "";
        if (pair && unit(two) && !def(t[j].n) && !def(t[j + 1].n)) {
            out.push_back({Tok::Var, {}, two});
            j++;
        } else if (t[j].k == Tok::Fn && unit(t[j].n) && !(j + 1 < t.size() && t[j + 1].k == Tok::Paren))
            out.push_back({Tok::Var, {}, t[j].n});
        else out.push_back(t[j]);
    }
    return out;
}

bool numChar(const Item& it) { return it.t == T::Ch && (isDigit(it.c) || it.c == "."); }

std::vector<Tok> tokens(const Row& row) {
    std::vector<Tok> out;
    for (size_t j = 0; j < row.size(); j++) {
        const Item& it = row[j];
        bool hasC = it.t == T::Ch || it.t == T::Sym;
        const std::string& c = it.c;
        if (hasC && c == " ") continue;
        if (numChar(it)) {
            std::string s = c;
            while (j + 1 < row.size() && (numChar(row[j + 1]) || isDecimalComma(row, j + 1))) s += row[++j].c == "," ? "." : row[j].c;
            size_t dot = s.find('.');
            std::string whole = s.substr(0, dot), frac = dot == std::string::npos ? "" : s.substr(dot + 1);
            frac = frac.substr(0, frac.find('.'));
            std::string digits = whole + frac;
            out.push_back({Tok::Num, Q(digits.empty() ? 0 : std::strtod(digits.c_str(), nullptr), std::pow(10.0, double(frac.size())))});
        } else if (it.t == T::Ch && isLetter(c)) out.push_back({Tok::Var, {}, c});
        else if (hasC && (c == "+" || c == "-" || c == "−")) out.push_back({Tok::Op, {}, c == "+" ? "+" : "-"});
        else if (hasC && (c == "*" || c == "⋅" || c == "×")) out.push_back({Tok::Op, {}, "*"});
        else if (hasC && (c == "/" || c == "÷")) out.push_back({Tok::Op, {}, "/"});
        else if (hasC && c == "!") out.push_back({Tok::Bang});
        else if (hasC && c == "′") out.push_back({Tok::Prime});
        else if (hasC && c == "∞") out.push_back({Tok::Num, Fl(INFINITY)});
        else if (it.t == T::Var || it.t == T::Const || it.t == T::Unit) out.push_back({Tok::Var, {}, c});
        else if (it.t == T::Frac) out.push_back({Tok::A, {}, "", bin("/", parse(it.s[0]), parse(it.s[1]))});
        else if (it.t == T::Sup) out.push_back({Tok::Pow, {}, "", parse(it.s[0])});
        else if (it.t == T::Sub && !out.empty() && out.back().k == Tok::Var) out.back().n += "_" + tex(it.s[0]);
        else if (it.t == T::Sqrt) out.push_back({Tok::A, {}, "", mk(Ast::Call, {parse(it.s[0])}, "sqrt")});
        else if (it.t == T::Paren) out.push_back({Tok::Paren, {}, "", {}, &it});
        else if (it.t == T::Fn) out.push_back({Tok::Fn, {}, c});
        else if (it.t == T::Big || it.t == T::Lim) out.push_back({it.t == T::Big ? Tok::Big : Tok::Lim, {}, "", {}, &it});
        else if (it.t == T::Mat) {
            Ast m = mk(Ast::Mat);
            m.cols = it.cols;
            for (auto& cell : it.s) m.a.push_back(parse(cell));
            out.push_back({Tok::A, {}, "", it.o == "|" ? mk(Ast::Call, {std::move(m)}, "det") : std::move(m)});
        } else fail();
    }
    return UNIT_MODE ? mergeUnits(out) : out;
}

std::string parenFn(const std::string& o) {
    return o == "|" || o == "‖" ? "abs" : o == "⌊" ? "floor" : o == "⌈" ? "ceil" : "";
}

bool argSep(const Row& row, size_t j) { return row[j].t == T::Ch && (row[j].c == ";" || (row[j].c == "," && !isDecimalComma(row, j))); }

std::vector<Row> splitArgs(const Row& row) {
    std::vector<Row> parts(1);
    for (size_t j = 0; j < row.size(); j++)
        if (argSep(row, j)) parts.emplace_back();
        else parts.back().push_back(row[j]);
    return parts;
}

std::optional<std::string> varName(const Item& it) {
    if (it.t == T::Var || it.t == T::Const) return it.c;
    if (it.t == T::Ch && it.m && isLetter(it.c)) return it.c;
    return std::nullopt;
}

// "i = 1" in a sum's lower limit, "x → 0" under lim: bound variable and its start/target
std::pair<std::string, Ast> binding(const Row& row, const std::function<bool(const Item&)>& sep) {
    int k = -1;
    for (size_t j = 0; j < row.size(); j++)
        if (sep(row[j])) { k = int(j); break; }
    Row left;
    for (int j = 0; j < k; j++)
        if (!isSpace(row[j])) left.push_back(row[j]);
    if (k < 1 || left.size() != 1 || !varName(left[0])) fail();
    return {*varName(left[0]), parse(Row(row.begin() + k + 1, row.end()))};
}

Ast parse(const Row& row) { return parseTokens(tokens(row)); }

struct Parser {
    const std::vector<Tok>& t;
    size_t p = 0;
    const Tok* peek() const { return p < t.size() ? &t[p] : nullptr; }
    Ast parenAst(const Tok& n) {
        std::string f = parenFn(n.node->o);
        return f.empty() ? mk(Ast::Group, {parse(n.node->s[0])}) : mk(Ast::Call, {parse(n.node->s[0])}, f);
    }
    Ast expr() {
        Ast a = unary();
        while (peek() && peek()->k == Tok::Op && (peek()->n == "+" || peek()->n == "-")) {
            std::string op = t[p++].n;
            a = bin(op, std::move(a), unary());
        }
        return a;
    }
    Ast unary() {
        if (peek() && peek()->k == Tok::Op && peek()->n == "-") {
            p++;
            return mk(Ast::Neg, {term()});
        }
        return term();
    }
    Ast term() {
        Ast a = power();
        for (;;) {
            const Tok* n = peek();
            if (n && n->k == Tok::Op && (n->n == "*" || n->n == "/")) {
                p++;
                a = bin(n->n, std::move(a), power());
            } else if (n && n->k != Tok::Op) a = bin("*", std::move(a), power());
            else return a;
        }
    }
    Ast power() {
        Ast a = atom();
        for (;;) {
            if (peek() && peek()->k == Tok::Pow) {
                const Ast& e = t[p++].a;
                a = e.k == Ast::Var && e.n == "T" && !def("T") ? mk(Ast::Tr, {std::move(a)}) : bin("^", std::move(a), e);
            } else if (peek() && peek()->k == Tok::Bang) {
                p++;
                a = mk(Ast::Fact, {std::move(a)});
            } else return a;
        }
    }
    Ast atom() {
        if (p >= t.size()) fail();
        const Tok& n = t[p++];
        switch (n.k) {
        case Tok::Num: return num(n.v);
        case Tok::A: return n.a;
        case Tok::Paren: return parenAst(n);
        case Tok::Var: {
            int order = 0;
            while (peek() && peek()->k == Tok::Prime) p++, order++;
            const Def* d = def(n.n);
            bool call = peek() && peek()->k == Tok::Paren && peek()->node->o == "(" && d && !d->params.empty();
            if (order) {
                if (!call || d->params.size() != 1) fail();
                Ast a = mk(Ast::Deriv, {parse(t[p++].node->s[0])}, n.n);
                a.order = order;
                return a;
            }
            if (call) {
                Ast c = mk(Ast::Call, {}, n.n);
                for (auto& part : splitArgs(t[p++].node->s[0])) c.a.push_back(parse(part));
                return c;
            }
            return var(n.n);
        }
        case Tok::Fn: {
            if (peek() && peek()->k == Tok::Paren && peek()->node->o == "(") {  // max(2, 4): several arguments
                std::string inner = plain(peek()->node->s[0]);
                if (n.n == "fit") {  // fit(t, h[, 2]): column names stay names
                    p++;
                    Ast c = mk(Ast::Call, {}, n.n);
                    for (auto& part : splitArgs(t[p - 1].node->s[0])) {
                        std::string w = plain(part);
                        std::erase(w, ' ');
                        c.a.push_back(COLUMNS.count(w) || w == "exp" ? var(w) : parse(part));
                    }
                    return c;
                }
                if (VARIADIC.count(n.n) && COLUMNS.count(inner)) {  // mean(mass): a column name of several letters
                    p++;
                    return mk(Ast::Call, {var(inner)}, n.n);
                }
                auto parts = splitArgs(peek()->node->s[0]);
                if (parts.size() > 1) {
                    p++;
                    Ast c = mk(Ast::Call, {}, n.n);
                    for (auto& part : parts) c.a.push_back(parse(part));
                    return c;
                }
            }
            Ast arg = peek() && peek()->k == Tok::Paren ? parenAst(t[p++]) : power();
            return mk(Ast::Call, {arg.k == Ast::Group ? arg.a[0] : arg}, n.n);
        }
        case Tok::Lim: {
            auto [v, to] = binding(n.node->s[0], [](const Item& it) { return it.t == T::Sym && it.c == "→"; });
            return mk(Ast::Lim, {std::move(to), term()}, v);
        }
        case Tok::Big:
            if (n.node->c == "∑" || n.node->c == "∏") {
                auto [v, lo] = binding(n.node->s[0], [](const Item& it) { return isRel(&it); });
                Ast hi = parse(n.node->s[1]);
                return mk(Ast::Big, {std::move(lo), std::move(hi), term()}, v, n.node->c == "∑" ? "sum" : "prod");
            }
            if (n.node->c == "∫") {
                // the body runs up to "d<var>"
                size_t j = p;
                while (j + 1 < t.size() && !(t[j].k == Tok::Var && t[j].n == "d" && t[j + 1].k == Tok::Var)) j++;
                if (j + 1 >= t.size()) fail();
                Ast body = parseTokens(std::vector<Tok>(t.begin() + p, t.begin() + j));
                std::string v = t[j + 1].n;
                p = j + 2;
                return mk(Ast::Big, {parse(n.node->s[0]), parse(n.node->s[1]), std::move(body)}, v, "int");
            }
            fail();
        default: fail();
        }
    }
};

Ast parseTokens(const std::vector<Tok>& t) {
    Parser ps{t};
    Ast r = ps.expr();
    if (ps.p < t.size()) fail();
    return r;
}

// ---------- evaluation ----------
using Scope = std::map<std::string, Value>;
Value evaluate(const Ast& a, const Scope& scope = {}, int depth = 0);
Value det(const std::vector<std::vector<Value>>& A);
Value matOp(const std::string& op, const Value& x, const Value& y);
Value derivative(const Ast& a, const Value& x);

// ponytail: fixed-step Simpson, accurate for smooth integrands on finite bounds
double simpson(const std::function<double(double)>& f, double lo, double hi, int n = 2000) {
    if (!std::isfinite(lo) || !std::isfinite(hi)) fail();
    double h = (hi - lo) / n, s = f(lo) + f(hi);
    for (int k = 1; k < n; k++) s += f(lo + k * h) * (k % 2 ? 4 : 2);
    return s * h / 3;
}

// numeric limit: approach from both sides (or outwards for ±∞) and require the values to settle
Value limit(const std::function<double(double)>& f, double to) {
    auto tryf = [&](double x) {
        try { return f(x); } catch (...) { return double(NAN); }
    };
    double ys[3];
    if (std::isfinite(to)) {
        // averaging both sides cancels the first-order error: (e^h - 1)/h → 1 cleanly
        double hs[] = {1e-4, 1e-5, 1e-6};
        for (int k = 0; k < 3; k++) ys[k] = (tryf(to + hs[k]) + tryf(to - hs[k])) / 2;
    } else {
        double xs[] = {1e6, 1e7, 1e8};
        for (int k = 0; k < 3; k++) ys[k] = tryf((to < 0 ? -1 : 1) * xs[k]);
    }
    double a = ys[1], b = ys[2];
    if (!std::isfinite(a) || !std::isfinite(b) || std::fabs(a - b) > 1e-5 * std::max(1.0, std::fabs(b))) fail();
    return nearQ(toPrecision(b, 9), 1e-8);
}

Value sub(const Value& a, const Value& b) { return add(a, neg(b)); }
// max min mean median stdev var sum count gcd lcm over a list of numbers
Value variadic(const std::string& f, const std::vector<Value>& xs) {
    if (xs.empty()) fail();
    if (f == "count") return Q(double(xs.size()));
    if (f == "sum" || f == "mean") {
        Value s = Q(0);
        for (auto& x : xs) s = add(s, x);
        return f == "sum" ? s : div(s, Q(double(xs.size())));
    }
    if (f == "gcd" || f == "lcm") {
        double r = f == "gcd" ? 0 : 1;
        for (auto& x : xs) {
            if (!isInt(x)) fail();
            double v = std::fabs(x.n);
            r = f == "gcd" ? gcd(r, v) : (v == 0 ? 0 : r / gcd(r, v) * v);
        }
        return Q(r);
    }
    if (f == "var" || f == "stdev") {  // sample variance (n − 1), like a calculator's s
        if (xs.size() < 2) fail();
        Value m = variadic("mean", xs), s = Q(0);
        for (auto& x : xs) s = add(s, pow(sub(x, m), Q(2)));
        Value v = div(s, Q(double(xs.size() - 1)));
        if (f == "var") return v;
        if (exact(v)) if (auto r = exactFn("sqrt", v)) return *r;
        return F0(std::sqrt(val(v)));
    }
    std::vector<Value> v = xs;
    std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return val(a) < val(b); });
    if (f == "min") return v.front();
    if (f == "max") return v.back();
    size_t n = v.size();  // median
    return n % 2 ? v[n / 2] : div(add(v[n / 2 - 1], v[n / 2]), Q(2));
}

Value evaluate(const Ast& a, const Scope& scope, int depth) {
    if (depth > 50) fail();
    auto ev = [&](const Ast& x) { return evaluate(x, scope, depth); };
    switch (a.k) {
    case Ast::Num: return a.v;
    case Ast::Group: return ev(a.a[0]);
    case Ast::Neg: {
        Value x = ev(a.a[0]);
        return isMat(x) ? matOp("*", Q(-1), x) : neg(x);
    }
    case Ast::Mat: {
        std::vector<std::vector<Value>> m;
        for (size_t i = 0; i < a.a.size(); i++) {
            if (i % a.cols == 0) m.emplace_back();
            Value v = ev(a.a[i]);
            if (isMat(v)) fail();
            m.back().push_back(v);
        }
        if (m.empty()) fail();
        return matV(std::move(m));
    }
    case Ast::Tr: {
        Value x = ev(a.a[0]);
        if (!isMat(x)) fail();
        std::vector<std::vector<Value>> m(x.m[0].size());
        for (size_t j = 0; j < x.m[0].size(); j++)
            for (auto& r : x.m) m[j].push_back(r[j]);
        return matV(std::move(m));
    }
    case Ast::Fact: return fact(ev(a.a[0]));
    case Ast::Bin: {
        Value x = ev(a.a[0]), y = ev(a.a[1]);
        if (isMat(x) || isMat(y)) return matOp(a.op, x, y);
        return a.op == "+" ? add(x, y) : a.op == "-" ? add(x, neg(y)) : a.op == "*" ? mul(x, y) : a.op == "/" ? div(x, y) : pow(x, y);
    }
    case Ast::Var: {
        if (auto f = scope.find(a.n); f != scope.end()) return f->second;
        const Def* d = def(a.n);
        if (d && d->params.empty()) return evaluate(parse(d->body), {}, depth + 1);
        if (isConst(a.n)) return Fl(constVal(a.n));
        fail();
    }
    case Ast::Call: {
        if (VARIADIC.count(a.n) && a.a.size() == 1 && a.a[0].k == Ast::Var && !scope.count(a.a[0].n))
            if (auto c = COLUMNS.find(a.a[0].n); c != COLUMNS.end()) return variadic(a.n, c->second);  // mean(h): a table column
        std::vector<Value> args;
        for (auto& x : a.a) args.push_back(ev(x));
        const Def* d = def(a.n);
        if (d && d->params.size() == args.size()) {
            Scope s;
            for (size_t j = 0; j < args.size(); j++) s[d->params[j]] = args[j];
            return evaluate(parse(d->body), s, depth + 1);
        }
        if (args.size() == 1 && isMat(args[0])) {
            if (a.n == "det" || a.n == "abs") return det(args[0].m);
            fail();
        }
        if (hasExact(a.n) && args.size() == 1 && exact(args[0]))
            if (auto r = exactFn(a.n, args[0])) return *r;
        for (auto& x : args)
            if (isMat(x)) fail();
        if (VARIADIC.count(a.n)) return variadic(a.n, args);
        if (args.size() == 2 && (a.n == "mod" || a.n == "nCr" || a.n == "nPr")) {
            Value x = args[0], y = args[1];
            if (a.n == "mod") {
                if (exact(x) && exact(y)) return sub(x, mul(y, Q(std::floor(val(x) / val(y)))));
                return F0(val(x) - val(y) * std::floor(val(x) / val(y)));
            }
            if (!isInt(x) || !isInt(y) || y.n < 0 || y.n > x.n) fail();
            Value r = Q(1);  // n(n−1)…(n−k+1), over k! for nCr
            for (double j = 0; j < y.n; j++) r = mul(r, Q(x.n - j));
            return a.n == "nPr" ? r : div(r, fact(y));
        }
        if (auto f = BUILTIN.find(a.n); f != BUILTIN.end() && args.size() == 1) {
            double x = val(args[0]);
            if (mathOpts.degrees && isTrig(a.n)) x *= std::numbers::pi / 180;
            double r = f->second(x);
            if (mathOpts.degrees && isArc(a.n)) r *= 180 / std::numbers::pi;
            return mathOpts.degrees && (isTrig(a.n) || isArc(a.n)) ? nearQ(r) : F0(r);  // sin(30) = 1/2 exactly
        }
        fail();
    }
    case Ast::Big: {
        auto at = [&](const Value& x) {
            Scope s = scope;
            s[a.n] = x;
            return evaluate(a.a[2], s, depth + 1);
        };
        if (a.op == "int") return nearQ(simpson([&](double x) { return val(at(Fl(x))); }, val(ev(a.a[0])), val(ev(a.a[1]))));
        Value lo = ev(a.a[0]), hi = ev(a.a[1]);
        if (!isInt(lo) || !isInt(hi) || hi.n - lo.n > 1e5) fail();
        Value acc = Q(a.op == "sum" ? 0 : 1);
        for (double k = lo.n; k <= hi.n; k++) acc = a.op == "sum" ? add(acc, at(Q(k))) : mul(acc, at(Q(k)));
        return acc;
    }
    case Ast::Lim:
        return limit([&](double x) {
            Scope s = scope;
            s[a.n] = Fl(x);
            return val(evaluate(a.a[1], s, depth + 1));
        }, val(ev(a.a[0])));
    case Ast::Deriv: return derivative(a, ev(a.a[0]));
    }
    fail();
}

// replace calls to defined functions with their bodies and defined variables with their values
Ast subst(const Ast& a, const std::map<std::string, Ast>& bind = {}, int depth = 0, const std::set<std::string>& bound = {}) {
    if (depth > 20) fail();
    auto s = [&](const Ast& x) { return subst(x, bind, depth, bound); };
    auto inner = [&](const Ast& x, const std::string& v) {
        auto b = bound;
        b.insert(v);
        return subst(x, bind, depth, b);
    };
    Ast r = a;
    switch (a.k) {
    case Ast::Num: return a;
    case Ast::Var: {
        if (bound.count(a.n)) return a;
        if (auto f = bind.find(a.n); f != bind.end()) return f->second;
        const Def* d = def(a.n);
        return d && d->params.empty() ? num(evaluate(a)) : a;
    }
    case Ast::Call: {
        for (auto& x : r.a) x = s(x);
        const Def* d = def(a.n);
        if (d && d->params.size() == r.a.size()) {
            std::map<std::string, Ast> b;
            for (size_t j = 0; j < r.a.size(); j++) b[d->params[j]] = r.a[j];
            return subst(parse(d->body), b, depth + 1);
        }
        return r;
    }
    case Ast::Big: r.a = {s(a.a[0]), s(a.a[1]), inner(a.a[2], a.n)}; return r;
    case Ast::Lim: r.a = {s(a.a[0]), inner(a.a[1], a.n)}; return r;
    default:  // Bin, Mat, Deriv (arg), Group, Neg, Fact, Tr
        for (auto& x : r.a) x = s(x);
        return r;
    }
}

int prec(const std::string& op) { return op == "+" || op == "-" ? 1 : op == "*" || op == "/" ? 2 : op == "^" ? 3 : 0; }
void chainLeaves(const Ast& a, int p, std::vector<const Ast*>& out) {
    if (a.k == Ast::Bin && prec(a.op) == p) {
        chainLeaves(a.a[0], p, out);
        out.push_back(&a.a[1]);
    } else out.push_back(&a);
}

// one simplification step: evaluate every innermost operation whose inputs are all numbers
Ast reduce(const Ast& a) {
    auto allNum = [](auto& v) { return std::all_of(v.begin(), v.end(), [](auto& x) { return x.k == Ast::Num; }); };
    if (a.k == Ast::Num) return a;
    if (a.k == Ast::Var || a.k == Ast::Lim || a.k == Ast::Deriv || (a.k == Ast::Big && a.op == "int")) return num(evaluate(a));
    if (a.k == Ast::Big) {
        // short sums/products are written out term by term
        Value lo = evaluate(a.a[0]), hi = evaluate(a.a[1]);
        if (!isInt(lo) || !isInt(hi) || hi.n - lo.n > 7 || hi.n < lo.n) return num(evaluate(a));
        std::optional<Ast> out;
        for (double k = lo.n; k <= hi.n; k++) {
            Ast term = subst(a.a[2], {{a.n, num(Q(k))}});
            out = out ? bin(a.op == "sum" ? "+" : "*", std::move(*out), std::move(term)) : std::move(term);
        }
        return *out;
    }
    if (a.k == Ast::Bin && a.op != "^") {
        std::vector<const Ast*> leaves;
        chainLeaves(a, prec(a.op), leaves);
        if (std::all_of(leaves.begin(), leaves.end(), [](auto* x) { return x->k == Ast::Num; })) return num(evaluate(a));
    }
    Ast r = a;
    if (a.k == Ast::Mat) {
        if (allNum(a.a)) return num(evaluate(a));
        for (auto& x : r.a) x = reduce(x);
        return r;
    }
    if (allNum(a.a)) return num(evaluate(a));
    for (auto& x : r.a) x = reduce(x);
    return r;
}

std::vector<Ast> steps(const Ast& ast) {
    Ast cur = subst(ast);
    std::vector<Ast> out;
    if (!(cur == ast)) out.push_back(cur);
    while (cur.k != Ast::Num && out.size() < 30) out.push_back(cur = reduce(cur));
    return out;
}

// ---------- polynomials: monomial (sorted [var, exponent] pairs) → coefficient ----------
using Mono = std::vector<std::pair<std::string, int>>;
using Poly = std::map<Mono, Value>;
Poly pconst(const Value& c) { return isZero(c) ? Poly{} : Poly{{Mono{}, c}}; }
Poly pvar(const std::string& n) { return {{Mono{{n, 1}}, Q(1)}}; }
void pput(Poly& r, const Mono& k, const Value& c) {
    auto f = r.find(k);
    Value v = add(f == r.end() ? Q(0) : f->second, c);
    if (isZero(v)) r.erase(k);
    else r[k] = v;
}
Poly padd(const Poly& a, const Poly& b, int sign = 1) {
    Poly r = a;
    for (auto& [k, c] : b) pput(r, k, sign == 1 ? c : neg(c));
    return r;
}
Poly pmul(const Poly& a, const Poly& b) {
    Poly r;
    for (auto& [ka, ca] : a)
        for (auto& [kb, cb] : b) {
            std::map<std::string, int> m(ka.begin(), ka.end());
            for (auto& [n, e] : kb) m[n] += e;
            pput(r, Mono(m.begin(), m.end()), mul(ca, cb));
        }
    return r;
}
Value constOf(const Poly& p) {
    if (p.empty()) return Q(0);
    if (p.size() == 1 && p.count(Mono{})) return p.at(Mono{});
    fail();
}
Poly pderiv(const Poly& p, const std::string& v) {
    Poly r;
    for (auto& [k, c] : p) {
        std::map<std::string, int> m(k.begin(), k.end());
        auto f = m.find(v);
        if (f == m.end()) continue;
        int e = f->second;
        if (e == 1) m.erase(f);
        else f->second = e - 1;
        pput(r, Mono(m.begin(), m.end()), mul(c, Q(e)));
    }
    return r;
}
Ast polyToAst(const Poly& p);

// AST to polynomial; env substitutes variables with polynomials (used to plug the argument into f′)
Poly poly(const Ast& a, const std::map<std::string, Poly>& env = {}) {
    switch (a.k) {
    case Ast::Num:
        if (!exact(a.v)) fail();
        return pconst(a.v);
    case Ast::Var: {
        auto f = env.find(a.n);
        return f != env.end() ? f->second : pvar(a.n);
    }
    case Ast::Group: return poly(a.a[0], env);
    case Ast::Neg: return padd({}, poly(a.a[0], env), -1);
    case Ast::Bin: {
        Poly x = poly(a.a[0], env), y = poly(a.a[1], env);
        if (a.op == "+") return padd(x, y);
        if (a.op == "-") return padd(x, y, -1);
        if (a.op == "*") return pmul(x, y);
        if (a.op == "/") return pmul(x, pconst(div(Q(1), constOf(y))));
        Value e = constOf(y);
        if (!isInt(e) || e.n < 0 || e.n > 50) fail();
        Poly r = pconst(Q(1));
        for (int k = 0; k < e.n; k++) r = pmul(r, x);
        return r;
    }
    case Ast::Deriv: {
        const Def* d = def(a.n);
        if (!d) fail();
        const std::string& v = d->params[0];
        Poly pd = poly(subst(parse(d->body), {}, 0, {v}));
        for (int k = 0; k < a.order; k++) pd = pderiv(pd, v);
        auto e = env;
        e[v] = poly(a.a[0], env);
        return poly(polyToAst(pd), e);
    }
    default: fail();
    }
}

int degree(const Mono& m) {
    int s = 0;
    for (auto& [n, e] : m) s += e;
    return s;
}
std::string monoKey(const Mono& m) {
    std::string s = "[";
    for (size_t i = 0; i < m.size(); i++) s += (i ? ",[\"" : "[\"") + m[i].first + "\"," + std::to_string(m[i].second) + "]";
    return s + "]";
}

Ast polyToAst(const Poly& p) {
    std::vector<std::pair<Mono, Value>> terms(p.begin(), p.end());
    std::stable_sort(terms.begin(), terms.end(), [](auto& x, auto& y) {
        if (degree(x.first) != degree(y.first)) return degree(x.first) > degree(y.first);
        return monoKey(x.first) < monoKey(y.first);
    });
    if (terms.empty()) return num(Q(0));
    std::optional<Ast> out;
    for (auto& [mono, c] : terms) {
        bool negative = val(c) < 0;
        Value ac = negative ? neg(c) : c;
        std::optional<Ast> t;
        for (auto& [n, e] : mono) {
            Ast f = e == 1 ? var(n) : bin("^", var(n), num(Q(e)));
            t = t ? bin("*", std::move(*t), std::move(f)) : std::move(f);
        }
        if (!t) t = num(ac);
        else if (!(exact(ac) && ac.n == 1 && ac.d == 1)) t = bin("*", num(ac), std::move(*t));
        out = !out ? (negative ? mk(Ast::Neg, {std::move(*t)}) : std::move(*t)) : bin(negative ? "-" : "+", std::move(*out), std::move(*t));
    }
    return *out;
}

void freeVars(const Ast& a, const std::set<std::string>& bound, std::vector<std::string>& out) {
    if (a.k == Ast::Var && !bound.count(a.n) && !isConst(a.n) && std::find(out.begin(), out.end(), a.n) == out.end()) out.push_back(a.n);
    if (a.k == Ast::Mat) return;  // corca doesn't look inside matrices
    if (a.k == Ast::Lim || a.k == Ast::Big) {
        auto inner = bound;
        inner.insert(a.n);
        for (size_t i = 0; i < a.a.size(); i++) freeVars(a.a[i], i + 1 == a.a.size() ? inner : bound, out);
        return;
    }
    for (auto& x : a.a) freeVars(x, bound, out);
}
std::vector<std::string> freeVars(const Ast& a) {
    std::vector<std::string> out;
    freeVars(a, {}, out);
    return out;
}

// ---------- AST back into editor items; parentheses only where precedence needs them ----------
Row digitsOf(const std::string& s) {
    Row r;
    for (char c : s) r.push_back(M(std::string(1, c)));
    return r;
}
// a decimal that is really (p/q)·√n, (p/q)·π or (a + b√n)/c: the exact form, as text and items. Small numbers only, so
// a value that merely happens to be close isn't dressed up (ponytail: a heuristic, like a calculator's "exact" key)
struct ExactForm {
    std::string text;
    Row items;
};
std::optional<ExactForm> exactForm(double v) {
    if (!std::isfinite(v) || std::fabs(v) < 1e-9 || std::fabs(v) > 1e6) return std::nullopt;
    double tol = 1e-10 * std::max(1.0, std::fabs(v));
    // v = (n/d)·unit with d <= maxD, |n| <= maxN
    auto near = [&](double unit, double& n, int maxD, double maxN, int& d) {
        for (d = 1; d <= maxD; d++) {
            n = std::round(v / unit * d);
            if (n != 0 && std::fabs(n) <= maxN && std::fabs(n / d * unit - v) < tol && gcd(std::fabs(n), d) == 1) return true;
        }
        return false;
    };
    auto squarefree = [](int n) {
        for (int f = 2; f * f <= n; f++)
            if (n % (f * f) == 0) return false;
        return true;
    };
    auto frac = [](Row top, double d) {  // top / d
        if (d == 1) return top;
        Item f = node(T::Frac);
        f.s = {std::move(top), digitsOf(intStr(d))};
        return Row{f};
    };
    auto root = [](int n) {
        Item r = node(T::Sqrt);
        r.s[0] = digitsOf(std::to_string(n));
        return r;
    };
    auto coefRow = [&](double n, Item tail) {  // n·tail, without "1"
        Row r;
        if (std::fabs(n) != 1) r = digitsOf(intStr(std::fabs(n)));
        r.push_back(std::move(tail));
        return r;
    };
    auto sign = [&](double n, Row r) {
        if (n < 0) r.insert(r.begin(), M("−"));
        return r;
    };
    auto coefText = [](double n) { return std::fabs(n) == 1 ? std::string(n < 0 ? "−" : "") : intStr(n); };
    double n;
    int d;
    if (near(std::numbers::pi, n, 24, 500, d))  // multiples of π
        return ExactForm{coefText(n) + "π" + (d > 1 ? "/" + std::to_string(d) : ""),
                         sign(n, frac(coefRow(n, named(T::Var, "π")), d))};
    for (int k = 2; k <= 200; k++) {  // (p/q)·√k
        if (!squarefree(k)) continue;
        if (near(std::sqrt(double(k)), n, 60, 1000, d))
            return ExactForm{coefText(n) + "√" + std::to_string(k) + (d > 1 ? "/" + std::to_string(d) : ""), sign(n, frac(coefRow(n, root(k)), d))};
    }
    for (int k = 2; k <= 30; k++) {  // (a + b√k)/c: roots of quadratics
        if (!squarefree(k)) continue;
        for (int c = 1; c <= 12; c++)
            for (int b = -12; b <= 12; b++) {
                if (!b) continue;
                double a = std::round(v * c - b * std::sqrt(double(k)));
                if (a == 0 || std::fabs(a) > 1000 || std::fabs((a + b * std::sqrt(double(k))) / c - v) > tol) continue;
                if (gcd(gcd(std::fabs(a), std::fabs(double(b))), c) != 1) continue;
                std::string t = intStr(a) + (b < 0 ? " − " : " + ") + (std::abs(b) == 1 ? "" : std::to_string(std::abs(b))) + "√" + std::to_string(k);
                Row top = digitsOf(intStr(std::fabs(a)));
                if (a < 0) top.insert(top.begin(), M("−"));
                top.push_back(M(b < 0 ? "−" : "+"));
                for (auto& it : coefRow(std::abs(b), root(k))) top.push_back(it);
                if (c == 1) return ExactForm{t, top};
                return ExactForm{"(" + t + ")/" + std::to_string(c), frac(top, c)};
            }
    }
    return std::nullopt;
}

Row numItems(const Value& v) {
    if (isMat(v)) {
        std::vector<std::vector<Row>> rows;
        for (auto& r : v.m) {
            rows.emplace_back();
            for (auto& x : r) rows.back().push_back(numItems(x));
        }
        return {mat("(", ")", std::move(rows))};
    }
    double x = val(v);
    if (std::isinf(x)) {
        Row r;
        if (x < 0) r.push_back(M("−"));
        r.push_back(sym("∞", "\\infty"));
        return r;
    }
    if (exact(v) && v.d != 1) {
        Row r;
        if (v.n < 0) r.push_back(M("−"));
        Item f = node(T::Frac);
        f.s = {digitsOf(intStr(std::fabs(v.n))), digitsOf(intStr(v.d))};
        r.push_back(std::move(f));
        return r;
    }
    if (!exact(v) && !mathOpts.decimals && !DECIMAL)
        if (auto e = exactForm(v.f)) return e->items;
    Row r;
    for (char c : fmt(v)) r.push_back(M(c == '-' ? "−" : std::string(1, c)));
    return r;
}

Row cat(Row a, const Row& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

Row toItems(const Ast& a, double ctx = 0) {
    Row out;
    double p = 4;
    switch (a.k) {
    case Ast::Num:
        out = numItems(a.v);
        if (val(a.v) < 0) p = 1.5;
        break;
    case Ast::Var: out = {named(T::Var, a.n)}; break;
    case Ast::Group: out = {paren("(", ")", toItems(a.a[0]))}; break;
    case Ast::Neg: out = cat({M("−")}, toItems(a.a[0], 2)); p = 1.5; break;
    case Ast::Fact: out = cat(toItems(a.a[0], 3.5), {M("!")}); break;
    case Ast::Mat: {
        std::vector<std::vector<Row>> rows;
        for (size_t i = 0; i < a.a.size(); i++) {
            if (i % a.cols == 0) rows.emplace_back();
            rows.back().push_back(toItems(a.a[i]));
        }
        out = {mat("(", ")", std::move(rows))};
        break;
    }
    case Ast::Tr: {
        Item s = node(T::Sup);
        s.s[0] = {M("T")};
        out = cat(toItems(a.a[0], 3.5), {s});
        p = 3;
        break;
    }
    case Ast::Deriv:
        out = {named(T::Var, a.n)};
        for (int k = 0; k < a.order; k++) out.push_back(sym("′", "'"));
        out.push_back(paren("(", ")", toItems(a.a[0])));
        break;
    case Ast::Lim: {
        Item l = node(T::Lim);
        l.s[0] = cat({named(T::Var, a.n), sym("→", "\\to")}, toItems(a.a[0]));
        out = cat({l}, toItems(a.a[1], 2));
        p = 2;
        break;
    }
    case Ast::Big: {
        Item b = a.op == "sum" ? big("∑", "\\sum", true) : a.op == "prod" ? big("∏", "\\prod", true) : big("∫", "\\int", false);
        b.s[0] = a.op == "int" ? toItems(a.a[0]) : cat({named(T::Var, a.n), M("=")}, toItems(a.a[0]));
        b.s[1] = toItems(a.a[1]);
        out = cat({b}, toItems(a.a[2], 2));
        if (a.op == "int") out = cat(out, {M("d"), named(T::Var, a.n)});
        p = 2;
        break;
    }
    case Ast::Call: {
        Row args;
        for (size_t j = 0; j < a.a.size(); j++) {
            if (j) args.push_back(M(","));
            args = cat(args, toItems(a.a[j]));
        }
        if (a.n == "sqrt") {
            Item s = node(T::Sqrt);
            s.s[0] = args;
            out = {s};
        } else if (a.n == "abs") out = {paren("|", "|", args)};
        else if (a.n == "floor") out = {paren("⌊", "⌋", args)};
        else if (a.n == "ceil") out = {paren("⌈", "⌉", args)};
        else out = {named(BUILTIN.count(a.n) || VARIADIC.count(a.n) || a.n == "fit" || a.n == "mod" || a.n == "nCr" || a.n == "nPr" ? T::Fn : T::Var, a.n),
                    paren("(", ")", args)};
        break;
    }
    case Ast::Bin:
        if (a.op == "/") {
            Item f = node(T::Frac);
            f.s = {toItems(a.a[0]), toItems(a.a[1])};
            out = {f};
        } else if (a.op == "^") {
            Item s = node(T::Sup);
            s.s[0] = toItems(a.a[1]);
            out = cat(toItems(a.a[0], 3.5), {s});
            p = 3;
        } else if (a.op == "*") {
            Row l = toItems(a.a[0], 2), r = toItems(a.a[1], 2.6);
            bool star = !r.empty() && ((r[0].t == T::Ch && (isDigit(r[0].c) || r[0].c == "−")) || r[0].t == T::Frac);
            out = cat(star ? cat(l, {M("*")}) : l, r);
            p = 2;
        } else {
            out = cat(cat(toItems(a.a[0], 1), {M(a.op == "-" ? "−" : "+")}), toItems(a.a[1], 1.6));
            p = 1;
        }
        break;
    }
    return p < ctx ? Row{paren("(", ")", out)} : out;
}

// ---------- definitions ----------
void defsIn(const Row& run) {
    Row items;
    for (auto& x : run)
        if (!isSpace(x)) items.push_back(x);
    int eq = -1;
    for (size_t k = 0; k < items.size(); k++)
        if (isRel(&items[k])) { eq = int(k); break; }
    if (eq < 1) return;
    size_t end = items.size();
    for (size_t k = eq + 1; k < items.size(); k++)
        if (isRel(&items[k]) || (items[k].t == T::Sym && items[k].c == "⇒")) { end = k; break; }
    Row body(items.begin() + eq + 1, items.begin() + end), lhs(items.begin(), items.begin() + eq);
    auto name = varName(lhs[0]);
    if (body.empty() || !name) return;
    if (lhs.size() == 1) DEFS[*name] = {{}, body};
    else if (lhs.size() == 2 && lhs[1].t == T::Paren && lhs[1].o == "(") {
        std::vector<std::string> params;
        for (auto& part : splitArgs(lhs[1].s[0])) {
            Row q;
            for (auto& x : part)
                if (!isSpace(x)) q.push_back(x);
            if (q.size() != 1 || !varName(q[0])) return;
            params.push_back(*varName(q[0]));
        }
        DEFS[*name] = {params, body};
    }
}

// definitions from every line of exclude's section (=== lines separate exercises; null: the whole doc); on line
// `exclude` only the part before index `upto` counts ("v = 90 km/h and v * 2 h =" sees v, but a line never defines itself)
// a table cell's items as math (cells are typed as text)
Row cellMath(const Row& r, std::pair<int, int> c) {
    Row q;
    for (int j = c.first; j < c.second; j++)
        if (!isSpace(r[j])) {
            Item it = r[j];
            if (it.t == T::Ch) it.m = true;
            q.push_back(it);
        }
    return q;
}

// every table in lines [a, b): a header cell that is a name ("h", "mass") makes its column's numbers a list
void collectColumns(int a, int b) {
    COLUMNS.clear();
    for (int n = a; n < b; n++) {
        if (lineStyle(ED->doc[n]).kind != Line::Table || (n > a && lineStyle(ED->doc[n - 1]).kind == Line::Table)) continue;
        auto head = tableCells(ED->doc[n]);
        std::vector<std::string> names;
        for (auto& c : head) {
            std::string s = plain(cellMath(ED->doc[n], c));
            bool name = !s.empty() && std::all_of(s.begin(), s.end(), [](char ch) { return std::isalnum((unsigned char)ch) || ch == '_' || (ch & 0x80); });
            names.push_back(name && !std::isdigit((unsigned char)s[0]) ? s : "");
        }
        for (int r = n + 1; r < b && lineStyle(ED->doc[r]).kind == Line::Table; r++) {
            auto cells = tableCells(ED->doc[r]);
            for (size_t c = 0; c < cells.size() && c < names.size(); c++) {
                if (names[c].empty()) continue;
                try { COLUMNS[names[c]].push_back(evaluate(parse(cellMath(ED->doc[r], cells[c])))); } catch (...) {}
            }
        }
    }
}

void collectDefs(const Row* exclude, int upto = 0) {
    DEFS.clear();
    int k = exclude ? ED->lineOf(exclude) : -1;
    auto [a, b] = k >= 0 ? sectionOf(ED->doc, k) : std::pair{0, int(ED->doc.size())};
    collectColumns(a, b);
    for (int n = a; n < b; n++) {
        const Row& whole = ED->doc[n];
        if (lineStyle(whole).kind == Line::Plot) continue;  // "plot y = x^2" doesn't define y
        int cut = stepBar(whole) >= 0 ? stepBar(whole) : int(whole.size());  // a step "| :2" defines nothing
        Row line = Row(whole.begin(), whole.begin() + std::min(&whole == exclude ? upto : cut, cut));
        auto mask = mathMask(line, true);
        for (size_t j = 0; j < line.size();) {
            size_t k = j;
            while (k < line.size() && mask[k] == mask[j]) k++;
            if (mask[j]) defsIn(Row(line.begin() + j, line.begin() + k));
            j = k;
        }
    }
}

// how much of the cursor's line may define things: only on the line itself, up to where the current expression starts
int defsUpto(const Row* row, int start) { return ED->parents.count(row) ? 0 : start; }

// the math items left of index j, back to the previous relation (or the start of the math run)
Row segmentBefore(const Row& row, int j, const std::function<bool(const Item&)>& stop) {
    int s = j;
    while (s > 0 && !stop(row[s - 1]) &&
           ((isMath(row[s - 1]) && !isSpace(row[s - 1])) || (isSpace(row[s - 1]) && s > 1 && isMath(row[s - 2]))))
        s--;
    return Row(row.begin() + s, row.begin() + j);
}

Row join(const std::vector<Row>& parts, const Row& sep) {
    Row out;
    for (size_t k = 0; k < parts.size(); k++) out = cat(k ? cat(out, sep) : out, parts[k]);
    return out;
}
const Row EQ = {M(" "), M("="), M(" ")};
bool relItem(const Item& x) { return isRel(&x); }
bool impliesItem(const Item& x) { return x.t == T::Sym && x.c == "⇒"; }

Entry result(std::string name, std::string cat, Row insert) { return {std::move(name), {}, {}, std::move(cat), std::move(insert)}; }

// ---------- matrices: exact arithmetic through the rational helpers ----------
using Mat = std::vector<std::vector<Value>>;
Mat identity(size_t n) {
    Mat m(n, std::vector<Value>(n));
    for (size_t i = 0; i < n; i++)
        for (size_t j = 0; j < n; j++) m[i][j] = Q(i == j ? 1 : 0);
    return m;
}
Mat mmul(const Mat& A, const Mat& B) {
    if (A[0].size() != B.size()) fail();
    Mat r(A.size(), std::vector<Value>(B[0].size()));
    for (size_t i = 0; i < A.size(); i++)
        for (size_t j = 0; j < B[0].size(); j++) {
            Value s = Q(0);
            for (size_t k = 0; k < B.size(); k++) s = add(s, mul(A[i][k], B[k][j]));
            r[i][j] = s;
        }
    return r;
}
// Gaussian elimination; returns the reduced copy, the row-swap sign
struct Elim {
    bool singular = false;
    Mat A, B;
    int sign = 1;
};
Elim eliminate(Mat A, std::optional<Mat> B = std::nullopt) {
    size_t n = A.size();
    for (auto& r : A)
        if (r.size() != n) fail();
    Elim e;
    for (size_t c = 0; c < n; c++) {
        size_t p = c;
        while (p < n && isZero(A[p][c])) p++;
        if (p == n) { e.singular = true; return e; }
        if (p != c) {
            std::swap(A[p], A[c]);
            if (B) std::swap((*B)[p], (*B)[c]);
            e.sign = -e.sign;
        }
        for (size_t i = 0; i < n; i++) {
            if (i == c || isZero(A[i][c])) continue;
            Value f = div(A[i][c], A[c][c]);
            for (size_t j = 0; j < n; j++) A[i][j] = add(A[i][j], neg(mul(f, A[c][j])));
            if (B)
                for (size_t j = 0; j < (*B)[i].size(); j++) (*B)[i][j] = add((*B)[i][j], neg(mul(f, (*B)[c][j])));
        }
    }
    e.A = std::move(A);
    if (B) e.B = std::move(*B);
    return e;
}
Value det(const Mat& A) {
    Elim r = eliminate(A);
    if (r.singular) return Q(0);
    Value s = Q(r.sign);
    for (size_t i = 0; i < r.A.size(); i++) s = mul(s, r.A[i][i]);
    return s;
}
Mat inverse(const Mat& A) {
    Elim r = eliminate(A, identity(A.size()));
    if (r.singular) fail();
    for (size_t i = 0; i < r.B.size(); i++)
        for (auto& x : r.B[i]) x = div(x, r.A[i][i]);
    return r.B;
}
Value matOp(const std::string& op, const Value& x, const Value& y) {
    bool both = isMat(x) && isMat(y);
    if (op == "+" || op == "-") {
        if (!both || x.m.size() != y.m.size() || x.m[0].size() != y.m[0].size()) fail();
        Mat r = x.m;
        for (size_t i = 0; i < r.size(); i++)
            for (size_t j = 0; j < r[i].size(); j++) r[i][j] = add(r[i][j], op == "+" ? y.m[i][j] : neg(y.m[i][j]));
        return matV(r);
    }
    if (op == "*") {
        if (both) return matV(mmul(x.m, y.m));
        const Value& s = isMat(x) ? y : x;
        Mat r = isMat(x) ? x.m : y.m;
        for (auto& row : r)
            for (auto& v : row) v = mul(s, v);
        return matV(r);
    }
    if (op == "/") {
        if (isMat(y)) fail();
        Mat r = x.m;
        for (auto& row : r)
            for (auto& v : row) v = div(v, y);
        return matV(r);
    }
    // A^n, A^-1
    if (!isMat(x) || isMat(y) || !isInt(y) || std::fabs(y.n) > 64) fail();
    Mat base = y.n < 0 ? inverse(x.m) : x.m, r = identity(base.size());
    for (int k = 0; k < std::fabs(y.n); k++) r = mmul(r, base);
    return matV(r);
}

// f′(x): exact through the polynomial when the body is one, central differences otherwise
Value derivative(const Ast& a, const Value& x) {
    const Def* d = def(a.n);
    if (!d) fail();
    const std::string v = d->params[0];
    try {
        Poly pd = poly(subst(parse(d->body), {}, 0, {v}));
        for (int k = 0; k < a.order; k++) pd = pderiv(pd, v);
        return evaluate(polyToAst(pd), {{v, x}});
    } catch (...) {
        Row body = d->body;
        auto g = [&](double t) { return val(evaluate(parse(body), {{v, Fl(t)}})); };
        double x0 = val(x), h = 1e-3 * std::max(1.0, std::fabs(x0));
        if (a.order == 1) return nearQ(toPrecision((g(x0 + h) - g(x0 - h)) / (2 * h), 7), 1e-6);
        if (a.order == 2) return nearQ(toPrecision((g(x0 + h) - 2 * g(x0) + g(x0 - h)) / (h * h), 5), 1e-4);
        fail();
    }
}

// ---------- units: 5 m/s * 3 s = 15 m ----------
int dimOf(const Dims& d, const std::string& k) {
    for (auto& [n, e] : d)
        if (n == k) return e;
    return 0;
}
bool dimsEqual(const Dims& a, const Dims& b) {
    for (auto& [k, e] : a)
        if (dimOf(b, k) != e) return false;
    for (auto& [k, e] : b)
        if (dimOf(a, k) != e) return false;
    return true;
}
Dims dimsCombine(Dims r, const Dims& b, int s) {
    for (auto& [k, e] : b) {
        auto f = std::find_if(r.begin(), r.end(), [&](auto& x) { return x.first == k; });
        if (f == r.end()) f = r.insert(r.end(), {k, 0});
        f->second += s * e;
        if (!f->second) r.erase(f);
    }
    return r;
}
struct Quantity {
    Value v;
    Dims d;
};
Quantity uev(const Ast& a) {
    switch (a.k) {
    case Ast::Num: return {a.v, {}};
    case Ast::Group: return uev(a.a[0]);
    case Ast::Neg: {
        Quantity x = uev(a.a[0]);
        return {neg(x.v), x.d};
    }
    case Ast::Var: {
        const Def* d = def(a.n);
        if (d && d->params.empty()) return uev(parse(d->body));
        if (const Unit* u = unit(a.n)) return {u->size, u->d};
        return {evaluate(a), {}};
    }
    case Ast::Bin: {
        Quantity x = uev(a.a[0]), y = uev(a.a[1]);
        if (a.op == "+" || a.op == "-") {
            if (!dimsEqual(x.d, y.d)) fail();
            return {add(x.v, a.op == "+" ? y.v : neg(y.v)), x.d};
        }
        if (a.op == "*") return {mul(x.v, y.v), dimsCombine(x.d, y.d, 1)};
        if (a.op == "/") return {div(x.v, y.v), dimsCombine(x.d, y.d, -1)};
        if (!y.d.empty() || !isInt(y.v)) fail();
        Dims d = x.d;
        for (auto& [k, e] : d) e *= int(y.v.n);
        return {pow(x.v, y.v), d};
    }
    default: return {evaluate(a), {}};
    }
}
void unitsIn(const Ast& a, std::vector<std::string>& out) {
    if (a.k == Ast::Var && unit(a.n) && std::find(out.begin(), out.end(), a.n) == out.end()) out.push_back(a.n);
    for (auto& x : a.a) unitsIn(x, out);
}
// express a quantity in the units the expression used: a single matching unit, a named unit, or per-dimension units
std::pair<Value, Dims> expressIn(const Quantity& q, const std::vector<std::string>& used) {
    std::vector<std::string> cands = used;
    cands.insert(cands.end(), NAMED.begin(), NAMED.end());
    for (auto& u : cands)
        if (dimsEqual(unit(u)->d, q.d)) return {div(q.v, unit(u)->size), {{u, 1}}};
    Dims units;
    Value v = q.v;
    for (auto& [dim, e] : q.d) {
        auto f = std::find_if(used.begin(), used.end(), [&](auto& x) { return dimsEqual(unit(x)->d, {{dim, 1}}); });
        std::string u = f != used.end() ? *f : dim;
        units.push_back({u, e});
        v = div(v, pow(unit(u)->size, Q(e)));
    }
    return {v, units};
}
Row unitItems(const Dims& units) {
    auto part = [](const Dims& list) {
        std::vector<Row> parts;
        for (auto& [u, e] : list) {
            Row r = {named(T::Unit, u)};
            if (e != 1) {
                Item s = node(T::Sup);
                s.s[0] = digitsOf(std::to_string(e));
                r.push_back(s);
            }
            parts.push_back(r);
        }
        return join(parts, {M("*")});
    };
    Dims top, bottom;
    for (auto& [u, e] : units)
        if (e > 0) top.push_back({u, e});
        else if (e < 0) bottom.push_back({u, -e});
    if (bottom.empty()) return part(top);
    Item f = node(T::Frac);
    f.s = {top.empty() ? Row{M("1")} : part(top), part(bottom)};
    return {f};
}
std::string unitText(const Dims& units) {
    std::string s;
    for (size_t i = 0; i < units.size(); i++)
        s += (i ? "·" : "") + units[i].first + (units[i].second == 1 ? "" : "^" + std::to_string(units[i].second));
    return s;
}
std::vector<Entry> unitSuggestions(const Row& seg, const Row& lead) {
    UNIT_MODE = true;  // also while definitions are parsed inside uev
    struct Reset {
        ~Reset() { UNIT_MODE = false; }
    } reset;
    Ast ast = parse(seg);
    std::vector<std::string> used;
    unitsIn(ast, used);
    Quantity q = uev(ast);
    // also the units inside definitions the expression uses (v = 90 km/h makes km a preferred unit)
    for (auto& n : freeVars(ast))
        if (const Def* d = def(n); d && d->params.empty()) unitsIn(parse(d->body), used);
    if (used.empty()) fail();
    auto [v, units] = expressIn(q, used);
    return {result("= " + fmt(v) + " " + unitText(units), "Units", cat(cat(cat(lead, numItems(v)), {M(" ")}), unitItems(units)))};
}

std::vector<Entry> expandSuggestions(const Ast& ast, const Row& lead) {
    Ast sub = subst(ast);
    Ast res = polyToAst(poly(sub));
    if (res == ast) return {};
    std::vector<Entry> out = {result("Expand and simplify", "Algebra", cat(lead, toItems(res)))};
    if (!(sub == ast) && !(sub == res)) out.push_back(result("Fill in the steps", "Steps", cat(lead, join({toItems(sub), toItems(res)}, EQ))));
    return out;
}

std::vector<Entry> solveSuggestions(const Row& seg, const Row& lead) {
    int eq = -1;
    for (size_t k = 0; k < seg.size(); k++)
        if (isRel(&seg[k])) { eq = int(k); break; }
    if (eq < 0) return {};
    Ast written = bin("-", parse(Row(seg.begin(), seg.begin() + eq)), parse(Row(seg.begin() + eq + 1, seg.end())));
    Ast diff = written;
    try { diff = subst(written); } catch (...) {}  // a name above that has no value (x = 1 ± 2)
    auto vars = freeVars(diff);
    if (vars.empty()) diff = written, vars = freeVars(diff);  // "x = 2" above: ⇒ still means x is the unknown here
    if (vars.size() != 1) return {};
    std::string v = vars[0];
    auto answer = [&](const std::vector<Value>& roots) {
        if (roots.empty()) return result("No real solution", "Solve", cat(lead, {M("∅")}));
        std::string name;
        std::vector<Row> parts;
        for (size_t k = 0; k < roots.size(); k++) {
            name += (k ? ", " : "") + v + " = " + fmt(roots[k]);
            parts.push_back(cat({named(T::Var, v), M("=")}, numItems(roots[k])));
        }
        return result(name, "Solve", cat(lead, join(parts, {M(","), M(" ")})));
    };
    std::optional<Poly> p;
    try { p = poly(diff); } catch (...) {}
    auto coef = [&](int e) {
        auto f = p->find(e ? Mono{{v, e}} : Mono{});
        return f == p->end() ? Q(0) : f->second;
    };
    int deg = 0;
    if (p)
        for (auto& [k, c] : *p) deg = std::max(deg, degree(k));
    if (p && deg == 1) return {answer({div(neg(coef(0)), coef(1))})};
    if (p && deg == 2) {
        // x = c ± √s with c = −b/2a, s = (b² − 4ac)/4a²
        Value a2 = coef(2), b = coef(1), c0 = coef(0);
        Value center = div(neg(b), mul(Q(2), a2));
        Value s = div(add(mul(b, b), neg(mul(Q(4), mul(a2, c0)))), mul(Q(4), mul(a2, a2)));
        if (val(s) < 0) return {answer({})};
        std::optional<Value> root = exact(s) ? exactFn("sqrt", s) : std::nullopt;
        if (root) return {answer(isZero(*root) ? std::vector{center} : std::vector{add(center, neg(*root)), add(center, *root)})};
        Item sq = node(T::Sqrt);
        sq.s[0] = numItems(s);
        Row ins = cat(lead, {named(T::Var, v), M("=")});
        if (!isZero(center)) ins = cat(ins, numItems(center));
        ins = cat(ins, {sym("±", "\\pm"), sq});
        return {result(v + " = " + (isZero(center) ? "" : fmt(center) + " ") + "± √" + fmt(s), "Solve", ins),
                answer({Fl(val(center) - std::sqrt(val(s))), Fl(val(center) + std::sqrt(val(s)))})};
    }
    // anything else: scan for sign changes and bisect
    auto f = [&](double x) {
        try { return val(evaluate(diff, {{v, Fl(x)}})); } catch (...) { return double(NAN); }
    };
    std::vector<Value> roots;
    const double step = 0.01;
    double x0 = -100, y0 = f(x0);
    for (double x1 = x0 + step; x1 <= 100 && roots.size() < 6; x1 += step) {
        double y1 = f(x1);
        if (std::isfinite(y0) && std::isfinite(y1) && (y0 == 0 || y0 * y1 < 0)) {
            double lo = x0, hi = x1;
            for (int k = 0; k < 60; k++) {
                double mid = (lo + hi) / 2;
                if (f(lo) * f(mid) <= 0) hi = mid;
                else lo = mid;
            }
            Value r = nearQ((lo + hi) / 2, 1e-12);
            bool dup = std::any_of(roots.begin(), roots.end(), [&](auto& q) { return std::fabs(val(q) - val(r)) < 1e-6; });
            if (std::fabs(f(val(r))) < 1e-6 * std::max(1.0, std::fabs(y1 - y0) / step) && !dup) roots.push_back(r);
        }
        x0 = x1, y0 = y1;
    }
    return {answer(roots)};
}

}  // namespace

bool unitStartsWith(const std::string& c) {
    for (auto& [u, _] : UNITS())
        if (u.starts_with(c)) return true;
    return false;
}

// ---------- equation steps: "2x = 4 |:2", what it gives, and writing out how an equation is solved ----------
namespace {
using EqPolys = std::pair<Poly, Poly>;

// a line's equation (before any step or #tag) as two polynomials; nothing if it isn't one. Names stay names, whatever
// they're defined as: a step works on what's written
std::optional<EqPolys> eqPolys(const Row& row) {
    int end = stepBar(row) >= 0 ? stepBar(row) : eqTag(row).first >= 0 ? eqTag(row).first : int(row.size()), eq = -1;
    for (int j = 0; j < end; j++)
        if (isRel(&row[j])) {
            if (eq >= 0 || row[j].c != "=") return std::nullopt;
            eq = j;
        }
    if (eq < 0) return std::nullopt;
    try {
        return EqPolys{poly(parse(Row(row.begin(), row.begin() + eq))), poly(parse(Row(row.begin() + eq + 1, row.begin() + end)))};
    } catch (...) {
        return std::nullopt;
    }
}

Row eqItems(const Poly& l, const Poly& r) {
    return cat(cat(toItems(polyToAst(l)), {M(" "), M("="), M(" ")}), toItems(polyToAst(r)));
}

// p / d, when d is one term that divides every term of p
std::optional<Poly> pdivTerm(const Poly& p, const Poly& d) {
    if (d.size() != 1 || isZero(d.begin()->second)) return std::nullopt;
    auto& [dk, dc] = *d.begin();
    Poly out;
    for (auto& [k, c] : p) {
        std::map<std::string, int> m(k.begin(), k.end());
        for (auto& [n, e] : dk) {
            auto f = m.find(n);
            if (f == m.end() || f->second < e) return std::nullopt;
            if ((f->second -= e) == 0) m.erase(f);
        }
        pput(out, Mono(m.begin(), m.end()), div(c, dc));
    }
    return out;
}

// x² = 9 under |√: x = ±3
std::optional<Row> rootLine(const EqPolys& e) {
    auto& [l, r] = e;
    if (l.size() != 1 || l.begin()->first.size() != 1 || l.begin()->first[0].second % 2 || !(exact(l.begin()->second) && l.begin()->second.n == 1 && l.begin()->second.d == 1)) return std::nullopt;
    Value c = Q(0);
    try { c = constOf(r); } catch (...) { return std::nullopt; }
    if (val(c) < 0) return std::nullopt;
    auto& [name, power] = l.begin()->first[0];
    Row out = cat(toItems(polyToAst({{Mono{{name, power / 2}}, Q(1)}})), {M(" "), M("="), M(" ")});
    if (isZero(c)) return cat(out, {M("0")});
    out.push_back(sym("±", "\\pm"));
    if (auto root = exact(c) ? exactFn("sqrt", c) : std::nullopt) return cat(out, numItems(*root));
    Item sq = node(T::Sqrt);
    sq.s[0] = numItems(c);
    out.push_back(sq);
    return out;
}

// what a step does to an equation; a |√ gives a line rather than polynomials
struct StepResult {
    std::optional<EqPolys> eq;
    std::optional<Row> line;
};
std::optional<StepResult> applyStep(const EqPolys& e, const Row& line) {
    Row step;
    int end = eqTag(line).first >= 0 ? eqTag(line).first : int(line.size());
    for (int j = stepBar(line) + 1; j < end; j++)
        if (!isSpace(line[j])) step.push_back(line[j]);
    if (step.empty()) return std::nullopt;
    const Item& first = step[0];
    if (first.t == T::Sqrt && first.s[0].empty() && step.size() == 1) {
        if (auto r = rootLine(e)) return StepResult{std::nullopt, r};
        return std::nullopt;
    }
    auto& [l, r] = e;
    if (first.t == T::Sup && step.size() == 1 && plain(first.s[0]) == "2") return StepResult{EqPolys{pmul(l, l), pmul(r, r)}, std::nullopt};
    try {
        Row rest(step.begin() + 1, step.end());
        auto over = [&](const Poly& d) -> std::optional<StepResult> {
            auto a = pdivTerm(l, d), b = pdivTerm(r, d);
            if (!a || !b) return std::nullopt;
            return StepResult{EqPolys{*a, *b}, std::nullopt};
        };
        if (first.t == T::Frac && first.s[0].empty() && rest.empty()) return over(poly(parse(first.s[1])));  // "/" typed alone is an empty fraction
        const std::string& c = first.c;
        if (first.t != T::Ch && first.t != T::Sym) return std::nullopt;
        if (rest.empty()) return std::nullopt;
        Poly by = poly(parse(rest));
        if (c == ":" || c == "/" || c == "÷") return over(by);
        if (c == "*" || c == "·" || c == "⋅" || c == "×") return StepResult{EqPolys{pmul(l, by), pmul(r, by)}, std::nullopt};
        if (c == "+") return StepResult{EqPolys{padd(l, by), padd(r, by)}, std::nullopt};
        if (c == "-" || c == "−") return StepResult{EqPolys{padd(l, by, -1), padd(r, by, -1)}, std::nullopt};
    } catch (...) {}
    return std::nullopt;
}

// the same equation up to a factor?
bool proportional(const EqPolys& a, const EqPolys& b) {
    Poly x = padd(a.first, a.second, -1), y = padd(b.first, b.second, -1);
    if (x.size() != y.size()) return false;
    if (x.empty()) return true;
    double ratio = 0;
    for (auto& [k, c] : x) {
        auto f = y.find(k);
        if (f == y.end() || val(c) == 0) return false;
        double t = val(f->second) / val(c);
        if (ratio != 0 && std::fabs(t - ratio) > 1e-9 * std::fabs(ratio)) return false;
        ratio = t;
    }
    return true;
}

// "x = 3" under an equation: does that value solve it? (squaring or a |√ can lose or gain solutions, so a line needn't
// be equivalent to be right)
bool solvesIt(const EqPolys& line, const EqPolys& eq) {
    Poly d = padd(line.first, line.second, -1), e = padd(eq.first, eq.second, -1);
    std::string name;
    Value a = Q(0), b = Q(0);
    for (auto& [k, c] : d) {
        if (k.empty()) b = c;
        else if (k.size() == 1 && k[0].second == 1 && name.empty()) name = k[0].first, a = c;
        else return false;
    }
    if (name.empty()) return false;
    double x = -val(b) / val(a), sum = 0, size = 1;
    for (auto& [k, c] : e) {
        double t = val(c);
        for (auto& [n, p] : k) {
            if (n != name) return false;
            t *= std::pow(x, p);
        }
        sum += t;
        size = std::max(size, std::fabs(t));
    }
    return std::fabs(sum) < 1e-9 * size;
}

// Tab on the empty line under a step writes what it gives
std::optional<Ghost> stepGhost(const Editor& ed, const Row& row) {
    int k = ed.lineOf(&row);
    if (k < 1 || stepBar(ed.doc[k - 1]) < 0) return std::nullopt;
    const Row& above = ed.doc[k - 1];
    auto eq = eqPolys(above);
    if (!eq) return std::nullopt;
    auto done = applyStep(*eq, above);
    if (row.empty()) {
        if (!done) return std::nullopt;
        Row ins = done->line ? *done->line : eqItems(done->eq->first, done->eq->second);
        return Ghost{plain(ins), ins};
    }
    auto now = eqPolys(row);
    if (!now || proportional(*now, *eq) || solvesIt(*now, *eq) || (done && done->eq && proportional(*now, *done->eq))) return std::nullopt;
    if (done && done->line) return std::nullopt;  // ±: "x = 3" was solvesIt's job, anything else can't be judged here
    auto names = [](const EqPolys& p) {
        std::set<std::string> out;
        for (auto* side : {&p.first, &p.second})
            for (auto& [k, c] : *side)
                for (auto& [n, e] : k) out.insert(n);
        return out;
    };
    auto was = names(*eq), is = names(*now);
    if (std::none_of(is.begin(), is.end(), [&](auto& n) { return was.count(n) > 0; })) return std::nullopt;  // about something else
    return Ghost{"≠ doesn't follow from the line above", {}, true};
}

Row barThen(Row op) { return cat({M(" "), sym("|", "\\qquad|\\;")}, op); }

// "− 2x" or "+ 3": the step that takes this off both sides
Row minusOp(const Poly& t) {
    if (t.size() != 1) return {M("−"), paren("(", ")", toItems(polyToAst(t)))};
    auto [k, c] = *t.begin();
    bool negative = val(c) < 0;
    return cat({M(negative ? "+" : "−")}, toItems(polyToAst({{k, negative ? neg(c) : c}})));
}
Row divOp(const Value& a) {
    if (val(a) < 0) return {M(":"), paren("(", ")", numItems(a))};
    return cat({M(":")}, numItems(a));
}

// how "3x + 1 = 7 ⇒" comes out, line by line; eraseBack takes the ⇒ away
std::optional<Entry> workingEntry(const Row& seg, int eraseBack) {
    int eq = -1;
    for (size_t j = 0; j < seg.size(); j++)
        if (isRel(&seg[j])) {
            if (eq >= 0 || seg[j].c != "=") return std::nullopt;
            eq = int(j);
        }
    if (eq < 0) return std::nullopt;
    Ast ls = parse(Row(seg.begin(), seg.begin() + eq)), rs = parse(Row(seg.begin() + eq + 1, seg.end()));
    Poly l, r;
    try {
        l = poly(subst(ls)), r = poly(subst(rs));
    } catch (...) {  // a name above that has no value (x = 1 ± 2): as written
        l = poly(ls), r = poly(rs);
    }
    Poly both = padd(l, r, -1);
    std::set<std::string> names;
    int deg = 0;
    auto scan = [&] {
        names.clear();
        deg = 0;
        for (auto& [k, c] : both) {
            deg = std::max(deg, degree(k));
            for (auto& [n, e] : k) names.insert(n);
        }
    };
    scan();
    if (names.empty()) {  // "x = 2" above: x is still the unknown here
        l = poly(ls), r = poly(rs);
        both = padd(l, r, -1);
        scan();
    }
    if (names.size() != 1 || deg < 1 || deg > 2) return std::nullopt;
    std::string v = *names.begin();
    auto term = [&](const Poly& p, int e) {
        auto f = p.find(e ? Mono{{v, e}} : Mono{});
        return f == p.end() ? Q(0) : f->second;
    };
    auto only = [&](int e, const Value& c) { return Poly{{e ? Mono{{v, e}} : Mono{}, c}}; };
    std::vector<Row> lines;  // the first is what follows the original equation: its step
    Row pendingStep;
    auto step = [&](Row op, Poly nl, Poly nr) {
        if (lines.empty()) pendingStep = barThen(op);
        else lines.back() = cat(lines.back(), barThen(op));
        l = std::move(nl), r = std::move(nr);
        lines.push_back(eqItems(l, r));
    };
    auto off = [&](const Poly& t) {
        if (!t.empty()) step(minusOp(t), padd(l, t, -1), padd(r, t, -1));
    };
    auto divide = [&](const Value& a) {
        if (exact(a) && a.n == 1 && a.d == 1) return;
        Poly by = pconst(a);
        step(divOp(a), *pdivTerm(l, by), *pdivTerm(r, by));
    };
    Value a2 = term(both, 2), b1 = term(both, 1);
    if (deg == 1) {
        if (Value x = term(r, 1); !isZero(x)) off(only(1, x));  // the unknown to the left
        if (Value c = term(l, 0); !isZero(c)) off(only(0, c));  // the rest to the right
        divide(term(l, 1));
    } else if (isZero(b1)) {  // x² = 9
        if (Value x = term(r, 2); !isZero(x)) off(only(2, x));
        if (Value c = term(l, 0); !isZero(c)) off(only(0, c));
        divide(term(l, 2));
        Value s = term(r, 0);
        if (val(s) < 0) lines.push_back({M("∅")});
        else if (auto root = rootLine({l, r})) {
            Item sq = node(T::Sqrt);
            if (lines.empty()) pendingStep = barThen({sq});
            else lines.back() = cat(lines.back(), barThen({sq}));
            lines.push_back(*root);
        }
    } else {  // pq: x² + px + q = 0
        off(r);
        divide(term(l, 2));
        Value p2 = div(term(l, 1), Q(2)), q = term(l, 0), center = neg(p2);
        Value s = add(mul(p2, p2), neg(q));
        Row x = {named(T::Var, v)}, pre = cat(x, {M(" "), M("="), M(" ")});
        // x = −p/2 ± √((p/2)² − q)
        Item sup = node(T::Sup);
        sup.s[0] = {M("2")};
        Row inside = val(p2) < 0 ? Row{paren("(", ")", numItems(p2)), sup} : cat(numItems(p2), {sup});
        inside = cat(inside, val(q) < 0 ? Row{M("+")} : Row{M("−")});
        inside = cat(inside, numItems(val(q) < 0 ? neg(q) : q));
        Item sq = node(T::Sqrt);
        sq.s[0] = inside;
        Row formula = pre;
        if (!isZero(center)) formula = cat(formula, numItems(center));
        formula.push_back(sym("±", "\\pm"));
        formula.push_back(sq);
        lines.push_back(formula);
        if (val(s) < 0) lines.push_back({M("∅")});
        else {
            std::optional<Value> root = exact(s) ? exactFn("sqrt", s) : std::nullopt;
            Row line = pre;
            if (!isZero(center)) line = cat(line, numItems(center));
            line.push_back(sym("±", "\\pm"));
            if (root) line = cat(line, numItems(*root));
            else {
                Item q2 = node(T::Sqrt);
                q2.s[0] = numItems(s);
                line.push_back(q2);
            }
            lines.push_back(line);
            if (root) {
                auto named12 = [&](const char* n, const Value& w) {
                    Item sb = node(T::Sub);
                    sb.s[0] = {M(n)};
                    return cat(cat(x, {sb, M(" "), M("="), M(" ")}), numItems(w));
                };
                Row last = named12("1", add(center, neg(*root)));
                if (!isZero(*root)) last = cat(cat(last, {M(";"), M(" ")}), named12("2", add(center, *root)));
                lines.push_back(last);
            }
        }
    }
    if (lines.empty()) return std::nullopt;
    Entry out = result("Show the working", "Steps", pendingStep);
    out.eraseBack = eraseBack;
    out.below = std::move(lines);
    return out;
}

// f′(x) of a polynomial, term by term with the power rule
std::optional<Entry> derivWorking(const Ast& ast, const Row& seg, const Row& lead) {
    if (ast.k != Ast::Deriv || ast.order != 1 || ast.a[0].k != Ast::Var) return std::nullopt;
    const std::string& x = ast.a[0].n;
    Ast call = mk(Ast::Call, {var(x)}, ast.n);
    Poly p = poly(subst(call));
    std::vector<std::pair<Mono, Value>> terms(p.begin(), p.end());
    std::stable_sort(terms.begin(), terms.end(), [](auto& a, auto& b) { return degree(a.first) > degree(b.first); });
    std::optional<Ast> sum;
    for (auto& [k, c] : terms) {
        if (k.empty()) continue;
        if (k.size() != 1 || k[0].first != x) return std::nullopt;
        int e = k[0].second;
        bool negative = val(c) < 0;
        Value ac = negative ? neg(c) : c;
        Ast t = num(Q(e));
        if (!(exact(ac) && ac.n == 1 && ac.d == 1)) t = bin("*", num(ac), std::move(t));
        if (e > 1) t = bin("*", std::move(t), e == 2 ? var(x) : bin("^", var(x), num(Q(e - 1))));
        sum = !sum ? (negative ? mk(Ast::Neg, {std::move(t)}) : std::move(t)) : bin(negative ? "-" : "+", std::move(*sum), std::move(t));
    }
    if (!sum) return std::nullopt;
    Entry out = result("Show the working", "Steps", cat(lead, toItems(*sum)));
    out.below = {cat(cat(seg, {M(" "), M("="), M(" ")}), toItems(polyToAst(pderiv(p, x))))};
    return out;
}
}  // namespace

static std::vector<Entry> evalSuggestions0(Editor& ed) {
    ED = &ed;
    const Row& row = *ed.cur.row;
    if (int b = stepBar(*ed.topOf(ed.cur.row)); b >= 0) {  // typing the step after "|": no results
        Cur at = ed.cur;
        for (auto f = ed.parents.find(at.row); f != ed.parents.end(); f = ed.parents.find(at.row))
            at = {f->second.row, int(f->second.node - f->second.row->data())};
        if (at.i > b) return {};
    }
    int i = ed.cur.i, j = i;
    while (j > 0 && isSpace(row[j - 1])) j--;
    const Item* last = j > 0 ? &row[j - 1] : nullptr;
    Row lead = j == i ? Row{M(" ")} : Row{};
    try {
        if (last && impliesItem(*last)) {
            Row eqn = segmentBefore(row, j - 1, impliesItem);
            collectDefs(ed.topOf(ed.cur.row), defsUpto(&row, j - 1 - int(eqn.size())));
            auto out = solveSuggestions(eqn, lead);
            try {
                if (auto w = workingEntry(eqn, i - (j - 1)); w && !ed.inSlot()) out.push_back(std::move(*w));
            } catch (...) {}
            return out;
        }
        if (!(last && last->t == T::Ch && last->m && last->c == "=")) return {};
        Row seg = segmentBefore(row, j - 1, relItem);
        collectDefs(ed.topOf(ed.cur.row), defsUpto(&row, j - 1 - int(seg.size())));
        Ast ast = parse(seg);
        Value v;
        try {
            v = evaluate(ast);
        } catch (...) {
            try { return unitSuggestions(seg, lead); } catch (...) {}
            auto out = expandSuggestions(ast, lead);
            try {
                if (auto w = derivWorking(ast, seg, lead); w && !ed.inSlot()) out.push_back(std::move(*w));
            } catch (...) {}
            return out;
        }
        std::vector<Entry> out = {result("= " + fmt(v), "Result", cat(lead, numItems(v)))};
        if (!exact(v) && !isMat(v))
            if (auto e = exactForm(v.f)) {
                out[0].name = "= " + e->text;
                DECIMAL = true;
                out.push_back(result("= " + fmt(v), "Decimal", cat(lead, numItems(v))));
                DECIMAL = false;
            }
        if (exact(v) && v.d != 1) {
            double d = toPrecision(v.n / v.d, mathOpts.digits);
            out.push_back(result("= " + jsNum(d), "Decimal", cat(lead, numItems(Fl(v.n / v.d)))));
        }
        auto st = steps(ast);
        if (st.size() > 1) {
            std::vector<Row> parts;
            for (auto& x : st) parts.push_back(toItems(x));
            out.push_back(result("Fill in the steps", "Steps", cat(lead, join(parts, EQ))));
        }
        return out;
    } catch (...) {
        return {};
    }
}

namespace {
std::string fitText(const std::vector<double>& c, bool expo, const std::string& x);
Ast fitAst(const std::vector<double>& c, bool expo, const std::string& x);
}  // namespace

// results in the number format of the settings
static void localize(Row& row) {
    for (size_t j = 0; j < row.size(); j++) {
        if (row[j].t == T::Ch && row[j].c == "." && j > 0 && j + 1 < row.size() && isDigit(row[j - 1].c) && isDigit(row[j + 1].c)) row[j].c = ",";
        for (auto& slot : row[j].s) localize(slot);
    }
}
std::vector<Entry> evalSuggestions(Editor& ed) {
    auto out = evalSuggestions0(ed);
    if (decimalComma)
        for (auto& e : out) {
            e.name = localNum(e.name);
            if (e.insert) localize(*e.insert);
        }
    return out;
}

static std::optional<Ghost> ghost0(Editor& ed, const Row& row) {
    ED = &ed;
    if (auto g = stepGhost(ed, row)) return g;
    auto mask = mathMask(row, true);
    int end = eqTag(row).first >= 0 ? eqTag(row).first : int(row.size());  // "x + 1 #eq": the answer goes before the tag
    if (stepBar(row) >= 0) return std::nullopt;  // "2x = 4 | :2": a transformation step, nothing to answer
    while (end > 0 && isSpace(row[end - 1])) end--;
    if (!end || !mask[end - 1] || isRel(&row[end - 1]) || impliesItem(row[end - 1])) return std::nullopt;
    Row seg = segmentBefore(row, end, relItem);
    bool any = std::any_of(seg.begin(), seg.end(), [](auto& x) { return !isSpace(x); });
    if (!any) return std::nullopt;
    try {
        collectDefs(&row, end - int(seg.size()));
        Ast ast = parse(seg);
        // "h(2) = 1019/50", "= 2√2", "= π/4": a number (or exact form) written after "=" is already the answer
        std::function<bool(const Ast&)> named = [&](const Ast& x) {
            if (x.k == Ast::Var) return x.n != "π" && x.n != "e";
            if (x.k == Ast::Call && x.n != "sqrt") return true;
            return x.k == Ast::Deriv || std::any_of(x.a.begin(), x.a.end(), named);
        };
        int s0 = end - int(seg.size());
        while (s0 > 0 && isSpace(row[s0 - 1])) s0--;
        if (s0 > 0 && row[s0 - 1].c == "=" && !named(ast)) return std::nullopt;
        // already a plain number or a matrix of numbers: nothing to compute
        if (ast.k == Ast::Num || (ast.k == Ast::Neg && ast.a[0].k == Ast::Num) ||
            (ast.k == Ast::Mat && std::all_of(ast.a.begin(), ast.a.end(), [](auto& x) { return x.k == Ast::Num; })))
            return std::nullopt;
        if (ast.k == Ast::Call && ast.n == "fit") {  // fit(t, h): the least-squares line (or curve) through two columns
            if (ast.a.size() < 2 || ast.a[0].k != Ast::Var || ast.a[1].k != Ast::Var) fail();
            auto cx = COLUMNS.find(ast.a[0].n), cy = COLUMNS.find(ast.a[1].n);
            if (cx == COLUMNS.end() || cy == COLUMNS.end()) fail();
            int deg = 1;
            if (ast.a.size() > 2) {
                if (ast.a[2].k == Ast::Var && ast.a[2].n == "exp") deg = -1;  // ponytail: "exp" parses as a name here
                else deg = int(val(evaluate(ast.a[2])));
            }
            std::vector<double> xs, ys;
            for (size_t k = 0; k < std::min(cx->second.size(), cy->second.size()); k++)
                xs.push_back(val(cx->second[k])), ys.push_back(val(cy->second[k]));
            double r2;
            auto c = fitCurve(xs, ys, deg, r2);
            if (c.empty()) fail();
            std::string x = ast.a[0].n;
            return Ghost{"= " + fitText(c, deg < 0, x) + "   (R² = " + jsNum(toPrecision(r2, 4)) + ")",
                         cat({M(" "), M("="), M(" ")}, toItems(fitAst(c, deg < 0, x)))};
        }
        Value v = evaluate(ast);
        if (mathOpts.decimals && exact(v) && v.d != 1) v = Fl(v.n / v.d);
        if (!exact(v) && !isMat(v) && !mathOpts.decimals)
            if (auto e = exactForm(v.f)) return Ghost{"= " + e->text + " ≈ " + fmt(v), cat({M(" "), M("="), M(" ")}, e->items)};
        return Ghost{"= " + fmt(v), cat({M(" "), M("="), M(" ")}, numItems(v))};
    } catch (...) {
        try {
            bool calc = std::any_of(seg.begin(), seg.end(), [](auto& x) { return !isSpace(x) && (isOp(&x) || x.t == T::Frac || x.t == T::Sup); });
            if (!calc) return std::nullopt;  // "2 m" alone is not a calculation
            auto u = unitSuggestions(seg, {M(" ")});
            return Ghost{u[0].name, cat({M(" "), M("=")}, *u[0].insert)};
        } catch (...) {
            return std::nullopt;
        }
    }
}

std::optional<Ghost> ghost(Editor& ed, const Row& row) {
    auto g = ghost0(ed, row);
    if (g && decimalComma) g->text = localNum(g->text), localize(g->insert);
    return g;
}

// "plot table": the nearest table above in the section; column 1 is x, every other column a series of points
static void tableSeries(Editor& ed, const Row& row, Plot& out) {
    int k = ed.lineOf(&row), top = sectionOf(ed.doc, k).first, end = k;
    while (end > top && lineStyle(ed.doc[end - 1]).kind != Line::Table) end--;
    int start = end;
    while (start > top && lineStyle(ed.doc[start - 1]).kind == Line::Table) start--;
    if (start == end) { out.error = "No table above to plot"; return; }
    auto cellRow = cellMath;
    auto head = tableCells(ed.doc[start]);
    for (size_t c = 1; c < head.size(); c++) {
        Row h = cellRow(ed.doc[start], head[c]);
        out.series.push_back({tex(h), plain(h), {}, true});
    }
    for (int r = start + 1; r < end; r++) {
        auto cells = tableCells(ed.doc[r]);
        double x = NAN;
        for (size_t c = 0; c < cells.size() && c <= out.series.size(); c++) {
            double v = NAN;
            try { v = val(evaluate(parse(cellRow(ed.doc[r], cells[c])))); } catch (...) {}
            if (c == 0) x = v;
            else if (std::isfinite(x)) out.series[out.series.size() - head.size() + c].pts.push_back({x, v});
        }
    }
}

std::vector<double> fitCurve(const std::vector<double>& xs, const std::vector<double>& ys, int degree, double& r2) {
    size_t n = xs.size();
    bool expo = degree < 0;
    int m = expo ? 2 : degree + 1;
    if (n < size_t(m)) return {};
    std::vector<double> ty = ys;
    if (expo)
        for (auto& y : ty) {
            if (y <= 0) return {};  // e^x is positive
            y = std::log(y);
        }
    // normal equations A c = r, A[i][j] = Σ x^(i+j), solved with partial pivoting
    std::vector<std::vector<double>> A(m, std::vector<double>(m + 1, 0));
    for (size_t k = 0; k < n; k++)
        for (int i = 0; i < m; i++) {
            for (int j = 0; j < m; j++) A[i][j] += std::pow(xs[k], i + j);
            A[i][m] += std::pow(xs[k], i) * ty[k];
        }
    for (int c = 0; c < m; c++) {
        int piv = c;
        for (int r = c + 1; r < m; r++)
            if (std::fabs(A[r][c]) > std::fabs(A[piv][c])) piv = r;
        std::swap(A[c], A[piv]);
        if (std::fabs(A[c][c]) < 1e-300) return {};
        for (int r = 0; r < m; r++)
            if (r != c) {
                double f = A[r][c] / A[c][c];
                for (int j = c; j <= m; j++) A[r][j] -= f * A[c][j];
            }
    }
    std::vector<double> c(m);
    for (int i = 0; i < m; i++) c[i] = A[i][m] / A[i][i];
    if (expo) c = {std::exp(c[0]), c[1]};
    auto f = [&](double x) {
        if (expo) return c[0] * std::exp(c[1] * x);
        double y = 0;
        for (int i = m - 1; i >= 0; i--) y = y * x + c[i];
        return y;
    };
    double mean = 0, res = 0, tot = 0;
    for (auto y : ys) mean += y / n;
    for (size_t k = 0; k < n; k++) res += (ys[k] - f(xs[k])) * (ys[k] - f(xs[k])), tot += (ys[k] - mean) * (ys[k] - mean);
    r2 = tot > 0 ? 1 - res / tot : 1;
    return c;
}

namespace {
// a fitted curve as text: "2.5t + 7.5", "1.2·e^(0.3t)"
std::string fitText(const std::vector<double>& c, bool expo, const std::string& x) {
    auto n = [](double v) { return jsNum(toPrecision(v, 4)); };
    if (expo) return n(c[0]) + "·e^(" + n(c[1]) + x + ")";
    std::string t;
    for (int i = int(c.size()) - 1; i >= 0; i--) {
        double v = toPrecision(c[i], 4);
        if (v == 0 && c.size() > 1) continue;
        std::string mag = jsNum(std::fabs(v)), pw = i == 0 ? "" : i == 1 ? x : x + (i == 2 ? "²" : i == 3 ? "³" : "^" + std::to_string(i));
        if (i > 0 && mag == "1") mag = "";
        t += (t.empty() ? (v < 0 ? "−" : "") : (v < 0 ? " − " : " + ")) + mag + pw;
    }
    return t.empty() ? "0" : t;
}
// the same as an expression, to write into the note
Ast fitAst(const std::vector<double>& c, bool expo, const std::string& x) {
    auto n = [](double v) { return num(nearQ(toPrecision(v, 4), 1e-12)); };
    if (expo) return bin("*", n(c[0]), mk(Ast::Bin, {var("e"), bin("*", n(c[1]), var(x))}, "", "^"));
    Ast out;
    bool first = true;
    for (int i = int(c.size()) - 1; i >= 0; i--) {
        double v = toPrecision(c[i], 4);
        if (v == 0 && c.size() > 1) continue;
        Ast term = i == 0 ? n(std::fabs(v)) : i == 1 ? var(x) : bin("^", var(x), num(Q(i)));
        if (i > 0 && std::fabs(v) != 1) term = bin("*", n(std::fabs(v)), term);
        if (first) out = v < 0 ? mk(Ast::Neg, {term}) : term, first = false;
        else out = bin(v < 0 ? "-" : "+", out, term);
    }
    return first ? num(Q(0)) : out;
}
}  // namespace

Plot plot(Editor& ed, const Row& row, const double* view) {
    ED = &ed;
    Plot out;
    out.x0 = mathOpts.plotX0, out.x1 = mathOpts.plotX1;
    bool domain = false, yFixed = false, zFixed = false;
    double t0 = 0, t1 = 2 * std::numbers::pi;
    collectDefs(&row, 0);
    std::vector<Row> parts(1);  // split at top-level commas, without spaces; text letters count as math here
    for (size_t j = 1; j < row.size(); j++) {  // after the plot function
        Item it = row[j];
        if (argSep(row, j)) parts.emplace_back();
        else if (!isSpace(it)) {
            if (it.t == T::Ch) it.m = true;
            parts.back().push_back(it);
        }
    }
    enum Kind { Fx, Param, Polar, Implicit, Surface, Space };
    struct Fn {
        Kind k;
        std::vector<Ast> a;  // Fx/Polar/Implicit/Surface: one; Param: x, y; Space: x, y, z
        size_t at;           // index in series / surfaces / curves
    };
    std::vector<Fn> fns;
    std::vector<std::string> fits;
    auto unknown = [](const Ast& a) {  // variables nothing in the section defines
        std::set<std::string> v;
        for (auto& n : freeVars(a))
            if (!def(n)) v.insert(n);
        return v;
    };
    for (auto& q : parts) {
        if (q.empty()) continue;
        std::string pq = plain(q);
        if (pq == "table") {
            tableSeries(ed, row, out);
            continue;
        }
        if (pq.starts_with("fit")) {
            fits.push_back(pq.substr(3));
            continue;
        }
        std::vector<int> rel, eq;
        for (int j = 0; j < int(q.size()); j++) {
            if (q[j].c == "<" || q[j].c == "≤") rel.push_back(j);
            if (q[j].c == "=") eq.push_back(j);
        }
        try {
            if (rel.size() == 2) {  // a < x < b; y: the y range (a 3D plot's y domain); t, θ: the parameter; z: 3D height
                double lo = val(evaluate(parse(Row(q.begin(), q.begin() + rel[0]))));
                std::string v = plain(Row(q.begin() + rel[0] + 1, q.begin() + rel[1]));
                double hi = val(evaluate(parse(Row(q.begin() + rel[1] + 1, q.end()))));
                if (v == "y") out.y0 = lo, out.y1 = hi, yFixed = true;
                else if (v == "t" || v == "θ") t0 = lo, t1 = hi;
                else if (v == "z") out.z0 = lo, out.z1 = hi, zFixed = true;
                else out.x0 = lo, out.x1 = hi, domain = true;
                continue;
            }
            if (q.size() == 1 && q[0].t == T::Paren && q[0].o == "(") {  // (x(t), y(t)) or (x(t), y(t), z(t))
                auto xs = splitArgs(q[0].s[0]);
                if (xs.size() == 2 || xs.size() == 3) {
                    Fn f{xs.size() == 2 ? Param : Space, {}, 0};
                    for (auto& x : xs) f.a.push_back(parse(x));
                    if (f.k == Param) f.at = out.series.size(), out.series.push_back({tex(q), plain(q), {}});
                    else f.at = out.curves.size(), out.curves.push_back({tex(q), plain(q), {}});
                    fns.push_back(std::move(f));
                    continue;
                }
            }
            if (!eq.empty()) {
                Row lhs(q.begin(), q.begin() + eq.back()), rhs(q.begin() + eq.back() + 1, q.end());
                std::string l = plain(lhs);
                if (l == "y" || l == "r" || l == "z" || l == "f(x)") {
                    Kind k = l == "r" ? Polar : l == "z" ? Surface : Fx;
                    Fn f{k, {parse(rhs)}, 0};
                    if (k == Fx && unknown(f.a[0]).count("y")) f.k = k = Implicit;  // y = x + y²…
                    if (k == Surface) f.at = out.surfaces.size(), out.surfaces.push_back({tex(rhs), plain(rhs), 0, {}});
                    else f.at = out.series.size(), out.series.push_back({k == Fx ? tex(rhs) : tex(q), k == Fx ? plain(rhs) : plain(q), {}});
                    if (k == Implicit) f.a[0] = bin("-", parse(lhs), f.a[0]);
                    fns.push_back(std::move(f));
                    continue;
                }
                Fn f{Implicit, {bin("-", parse(lhs), parse(rhs))}, out.series.size()};  // x² + y² = 4
                out.series.push_back({tex(q), plain(q), {}});
                fns.push_back(std::move(f));
                continue;
            }
            Ast a = parse(q);
            if (unknown(a).count("y")) {  // sin(x) cos(y): a surface
                fns.push_back({Surface, {a}, out.surfaces.size()});
                out.surfaces.push_back({tex(q), plain(q), 0, {}});
            } else {
                fns.push_back({Fx, {a}, out.series.size()});
                out.series.push_back({tex(q), plain(q), {}});
            }
        } catch (...) {
            out.error = "Can't plot " + pq;
        }
    }
    auto at = [](const Ast& a, Scope s) {
        try {
            Value v = evaluate(a, s);
            if (!isMat(v) && std::isfinite(val(v))) return val(v);
        } catch (...) {}
        return double(NAN);
    };
    // curves that don't depend on the x range first, so the range can fit them
    for (auto& f : fns) {
        if (f.k != Param && f.k != Polar) continue;
        auto& pts = out.series[f.at].pts;
        for (int i = 0; i < 720; i++) {
            double t = t0 + (t1 - t0) * i / 719, x, y;
            if (f.k == Param) x = at(f.a[0], {{"t", Fl(t)}}), y = at(f.a[1], {{"t", Fl(t)}});
            else {
                double r = at(f.a[0], {{"θ", Fl(t)}, {"t", Fl(t)}});
                x = r * std::cos(t), y = r * std::sin(t);
            }
            pts.push_back({std::isfinite(x) ? x : NAN, std::isfinite(x) ? y : NAN});
        }
    }
    // fits through the first table series
    std::vector<std::pair<size_t, std::function<double(double)>>> fitFns;
    for (auto& how : fits) {
        const Plot::Series* data = nullptr;
        for (auto& se : out.series)
            if (se.points) { data = &se; break; }
        if (!data) { out.error = "fit needs a table: plot table, fit"; continue; }
        std::vector<double> xs, ys;
        for (auto& [x, y] : data->pts)
            if (std::isfinite(x) && std::isfinite(y)) xs.push_back(x), ys.push_back(y);
        std::string h = how;
        h.erase(std::remove(h.begin(), h.end(), ' '), h.end());
        int deg = h.empty() || h == "linear" ? 1 : h == "quadratic" ? 2 : h == "cubic" ? 3 : h == "exp" ? -1 : std::atoi(h.c_str());
        if (deg == 0 || deg > 6) { out.error = "fit, fit 2, fit 3 or fit exp"; continue; }
        double r2 = 0;
        auto c = fitCurve(xs, ys, deg, r2);
        if (c.empty()) { out.error = "Not enough data to fit"; continue; }
        std::string label = "fit: y = " + fitText(c, deg < 0, "x") + "   R² = " + jsNum(toPrecision(r2, 4));
        fitFns.push_back({out.series.size(), [c, deg](double x) {
                              if (deg < 0) return c[0] * std::exp(c[1] * x);
                              double y = 0;
                              for (int i = int(c.size()) - 1; i >= 0; i--) y = y * x + c[i];
                              return y;
                          }});
        out.series.push_back({"\\text{" + label + "}", label, {}});
    }
    if (!domain && !out.is3d()) {  // no domain given: fit the table data and parametric curves, if any
        double lo = INFINITY, hi = -INFINITY;
        for (auto& s : out.series)
            for (auto& [x, y] : s.pts)
                if (std::isfinite(x) && std::isfinite(y)) lo = std::min(lo, x), hi = std::max(hi, x);
        if (lo < hi) out.x0 = lo - 0.05 * (hi - lo), out.x1 = hi + 0.05 * (hi - lo);
    }
    if (view && !out.is3d()) out.x0 = view[0], out.x1 = view[1], out.y0 = view[2], out.y1 = view[3], yFixed = true;
    if (!(out.x1 > out.x0)) std::swap(out.x0, out.x1);
    if (out.x0 == out.x1) out.x1 = out.x0 + 1;
    if (out.is3d()) {  // 3D: y is a domain like x; z is the height
        if (!yFixed) out.y0 = out.x0, out.y1 = out.x1;
        std::vector<double> zs;
        for (auto& f : fns) {
            if (f.k == Surface) {
                auto& sf = out.surfaces[f.at];
                sf.n = 48;
                for (int j = 0; j < sf.n; j++)
                    for (int i = 0; i < sf.n; i++) {
                        double x = out.x0 + (out.x1 - out.x0) * i / (sf.n - 1), y = out.y0 + (out.y1 - out.y0) * j / (sf.n - 1);
                        double z = at(f.a[0], {{"x", Fl(x)}, {"y", Fl(y)}});
                        sf.z.push_back(z);
                        if (std::isfinite(z)) zs.push_back(z);
                    }
            } else if (f.k == Space) {
                auto& cv = out.curves[f.at];
                for (int i = 0; i < 600; i++) {
                    double t = t0 + (t1 - t0) * i / 599;
                    std::array<double, 3> p{at(f.a[0], {{"t", Fl(t)}}), at(f.a[1], {{"t", Fl(t)}}), at(f.a[2], {{"t", Fl(t)}})};
                    cv.pts.push_back(p);
                    if (std::isfinite(p[2])) zs.push_back(p[2]);
                }
            } else if (out.error.empty()) out.error = "A 3D plot can't also have 2D curves";
        }
        if (!out.curves.empty() && !domain && !yFixed) {  // a space curve sets its own box
            double b[6] = {INFINITY, -INFINITY, INFINITY, -INFINITY};
            for (auto& cv : out.curves)
                for (auto& p : cv.pts)
                    if (std::isfinite(p[0]) && std::isfinite(p[1]))
                        b[0] = std::min(b[0], p[0]), b[1] = std::max(b[1], p[0]), b[2] = std::min(b[2], p[1]), b[3] = std::max(b[3], p[1]);
            if (b[0] < b[1]) out.x0 = b[0], out.x1 = b[1];
            if (b[2] < b[3]) out.y0 = b[2], out.y1 = b[3];
        }
        if (!zFixed && !zs.empty()) {
            std::sort(zs.begin(), zs.end());
            double lo = zs[size_t(0.01 * (zs.size() - 1))], hi = zs[size_t(0.99 * (zs.size() - 1))];
            if (hi - lo < 1e-9) lo -= 1, hi += 1;
            out.z0 = lo, out.z1 = hi;
        }
        if (zs.empty() && out.error.empty()) out.error = "Nothing to draw (are x and y the only unknowns?)";
        return out;
    }
    std::vector<double> ys;
    for (auto& s : out.series)
        for (auto& [x, y] : s.pts)
            if (std::isfinite(y)) ys.push_back(y);
    const int N = 400;
    bool implicit = false;
    for (auto& f : fns) {
        auto& se = out.series[f.at];
        if (f.k == Fx) {
            bool any = false;
            for (int i = 0; i < N; i++) {
                double x = out.x0 + (out.x1 - out.x0) * i / (N - 1), y = at(f.a[0], {{"x", Fl(x)}});
                if (std::isfinite(y)) ys.push_back(y), any = true;
                se.pts.push_back({x, y});
            }
            if (!any && out.error.empty()) out.error = "Can't plot " + se.label + " (is x the only unknown?)";
        }
        implicit = implicit || f.k == Implicit;
    }
    for (auto& [k, f] : fitFns)
        for (int i = 0; i < N; i++) {
            double x = out.x0 + (out.x1 - out.x0) * i / (N - 1), y = f(x);
            out.series[k].pts.push_back({x, std::isfinite(y) ? y : NAN});
            if (std::isfinite(y)) ys.push_back(y);
        }
    // ponytail: y range from the 2nd to 98th percentile plus a margin, so asymptotes don't flatten everything else
    if (!ys.empty() && !yFixed) {
        std::sort(ys.begin(), ys.end());
        double lo = ys[size_t(0.02 * (ys.size() - 1))], hi = ys[size_t(0.98 * (ys.size() - 1))];
        if (ys.front() > lo - 0.1 * (hi - lo)) lo = ys.front();
        if (ys.back() < hi + 0.1 * (hi - lo)) hi = ys.back();
        if (hi - lo < 1e-9) lo -= 1, hi += 1;
        double pad = 0.08 * (hi - lo);
        out.y0 = lo - pad, out.y1 = hi + pad;
    }
    // only shapes (parametric, polar, implicit; no y = f(x) or data): equal scales, so a circle is round
    bool shapes = !fns.empty() && std::all_of(fns.begin(), fns.end(), [](auto& f) { return f.k == Param || f.k == Polar || f.k == Implicit; }) &&
                  fitFns.empty() && std::none_of(out.series.begin(), out.series.end(), [](auto& s) { return s.points; });
    if (shapes && !view) {
        out.equal = true;
        if (implicit && ys.empty() && !yFixed) out.y0 = out.x0, out.y1 = out.x1;  // only implicit: x's range to start from
        if (implicit && !domain) {  // implicit curves can reach past the other curves: leave them room
            double cx = (out.x0 + out.x1) / 2, cyy = (out.y0 + out.y1) / 2, r = std::max({out.x1 - out.x0, out.y1 - out.y0, 4.0}) / 2;
            if (ys.empty()) r = (out.x1 - out.x0) / 2;
            out.x0 = cx - r, out.x1 = cx + r, out.y0 = cyy - r, out.y1 = cyy + r;
        }
        double xs = out.x1 - out.x0, yspan = out.y1 - out.y0, aspect = mathOpts.plotAspect;
        if (!domain && !yFixed) {
            if (yspan * aspect > xs) { double m = (out.x0 + out.x1) / 2; out.x0 = m - yspan * aspect / 2, out.x1 = m + yspan * aspect / 2; }
            else { double m = (out.y0 + out.y1) / 2; out.y0 = m - xs / aspect / 2, out.y1 = m + xs / aspect / 2; }
        }
    }
    // implicit curves: marching squares over the view; each piece is two points and a NaN (a break)
    for (auto& f : fns) {
        if (f.k != Implicit) continue;
        auto& se = out.series[f.at];
        const int G = 160;
        std::vector<double> F((G + 1) * (G + 1));
        auto X = [&](int i) { return out.x0 + (out.x1 - out.x0) * i / G; };
        auto Y = [&](int j) { return out.y0 + (out.y1 - out.y0) * j / G; };
        for (int j = 0; j <= G; j++)
            for (int i = 0; i <= G; i++) F[j * (G + 1) + i] = at(f.a[0], {{"x", Fl(X(i))}, {"y", Fl(Y(j))}});
        for (int j = 0; j < G; j++)
            for (int i = 0; i < G; i++) {
                double c[4] = {F[j * (G + 1) + i], F[j * (G + 1) + i + 1], F[(j + 1) * (G + 1) + i + 1], F[(j + 1) * (G + 1) + i]};
                double px[4] = {X(i), X(i + 1), X(i + 1), X(i)}, py[4] = {Y(j), Y(j), Y(j + 1), Y(j + 1)};
                if (std::any_of(c, c + 4, [](double v) { return !std::isfinite(v); })) continue;
                std::vector<std::pair<double, double>> cross;
                for (int e = 0; e < 4; e++) {
                    double a = c[e], b = c[(e + 1) % 4];
                    if ((a < 0) != (b < 0)) {
                        double t = a / (a - b);
                        // a sign change across a pole (tan, 1/x) isn't a crossing: the values are huge, not near 0
                        if (std::fabs(a) + std::fabs(b) > 1e6) continue;
                        cross.push_back({px[e] + t * (px[(e + 1) % 4] - px[e]), py[e] + t * (py[(e + 1) % 4] - py[e])});
                    }
                }
                for (size_t m = 0; m + 1 < cross.size(); m += 2) {
                    se.pts.push_back(cross[m]);
                    se.pts.push_back(cross[m + 1]);
                    se.pts.push_back({cross[m].first, NAN});
                }
            }
        if (se.pts.empty() && out.error.empty()) out.error = "No points of " + se.label + " in view";
    }
    return out;
}

std::vector<std::string> definedNames(Editor& ed) {
    ED = &ed;
    collectDefs(nullptr);
    std::vector<std::string> out;
    for (auto& [n, _] : DEFS) out.push_back(n);
    return out;
}

}  // namespace om
