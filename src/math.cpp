#include "math.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <numbers>
#include <set>

namespace om {
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

std::string fmt(const Value& x) {
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
    return jsNum(toPrecision(x.f, 10));
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
    {"ceil", [](double x) { return std::ceil(x); }}, {"max", nullptr}, {"min", nullptr}};  // max/min: variadic, see call

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
            while (j + 1 < row.size() && numChar(row[j + 1])) s += row[++j].c;
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

std::vector<Row> splitArgs(const Row& row) {
    std::vector<Row> parts(1);
    for (auto& it : row)
        if (it.t == T::Ch && it.c == ",") parts.emplace_back();
        else parts.back().push_back(it);
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
        if (auto f = BUILTIN.find(a.n); f != BUILTIN.end()) {
            for (auto& x : args)
                if (isMat(x)) fail();
            if (!f->second) {  // max, min
                double r = a.n == "max" ? -INFINITY : INFINITY;
                for (auto& x : args) {
                    double v = val(x);
                    if (std::isnan(v)) return F0(NAN);
                    r = a.n == "max" ? std::max(r, v) : std::min(r, v);
                }
                return F0(r);
            }
            return F0(f->second(val(args[0])));
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
        else out = {named(BUILTIN.count(a.n) ? T::Fn : T::Var, a.n), paren("(", ")", args)};
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
void collectDefs(const Row* exclude, int upto = 0) {
    DEFS.clear();
    int k = exclude ? ED->lineOf(exclude) : -1;
    auto [a, b] = k >= 0 ? sectionOf(ED->doc, k) : std::pair{0, int(ED->doc.size())};
    for (int n = a; n < b; n++) {
        const Row& whole = ED->doc[n];
        if (lineStyle(whole).kind == Line::Plot) continue;  // "plot y = x^2" doesn't define y
        Row line = &whole == exclude ? Row(whole.begin(), whole.begin() + std::min(upto, int(whole.size()))) : whole;
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
    Ast diff = subst(bin("-", parse(Row(seg.begin(), seg.begin() + eq)), parse(Row(seg.begin() + eq + 1, seg.end()))));
    auto vars = freeVars(diff);
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

std::vector<Entry> evalSuggestions(Editor& ed) {
    ED = &ed;
    const Row& row = *ed.cur.row;
    int i = ed.cur.i, j = i;
    while (j > 0 && isSpace(row[j - 1])) j--;
    const Item* last = j > 0 ? &row[j - 1] : nullptr;
    Row lead = j == i ? Row{M(" ")} : Row{};
    try {
        if (last && impliesItem(*last)) {
            Row eqn = segmentBefore(row, j - 1, impliesItem);
            collectDefs(ed.topOf(ed.cur.row), defsUpto(&row, j - 1 - int(eqn.size())));
            return solveSuggestions(eqn, lead);
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
            return expandSuggestions(ast, lead);
        }
        std::vector<Entry> out = {result("= " + fmt(v), "Result", cat(lead, numItems(v)))};
        if (exact(v) && v.d != 1) {
            double d = toPrecision(v.n / v.d, 10);
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

std::optional<Ghost> ghost(Editor& ed, const Row& row) {
    ED = &ed;
    auto mask = mathMask(row, true);
    int end = int(row.size());
    while (end > 0 && isSpace(row[end - 1])) end--;
    if (!end || !mask[end - 1] || isRel(&row[end - 1]) || impliesItem(row[end - 1])) return std::nullopt;
    Row seg = segmentBefore(row, end, relItem);
    bool any = std::any_of(seg.begin(), seg.end(), [](auto& x) { return !isSpace(x); });
    if (!any) return std::nullopt;
    try {
        collectDefs(&row, end - int(seg.size()));
        Ast ast = parse(seg);
        // "h(2) = 1019/50": a number written after "=" is already the answer
        std::function<bool(const Ast&)> named = [&](const Ast& x) {
            return x.k == Ast::Var || x.k == Ast::Call || x.k == Ast::Deriv || std::any_of(x.a.begin(), x.a.end(), named);
        };
        int s0 = end - int(seg.size());
        while (s0 > 0 && isSpace(row[s0 - 1])) s0--;
        if (s0 > 0 && row[s0 - 1].c == "=" && !named(ast)) return std::nullopt;
        // already a plain number or a matrix of numbers: nothing to compute
        if (ast.k == Ast::Num || (ast.k == Ast::Neg && ast.a[0].k == Ast::Num) ||
            (ast.k == Ast::Mat && std::all_of(ast.a.begin(), ast.a.end(), [](auto& x) { return x.k == Ast::Num; })))
            return std::nullopt;
        Value v = evaluate(ast);
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

// "plot table": the nearest table above in the section; column 1 is x, every other column a series of points
static void tableSeries(Editor& ed, const Row& row, Plot& out) {
    int k = ed.lineOf(&row), top = sectionOf(ed.doc, k).first, end = k;
    while (end > top && lineStyle(ed.doc[end - 1]).kind != Line::Table) end--;
    int start = end;
    while (start > top && lineStyle(ed.doc[start - 1]).kind == Line::Table) start--;
    if (start == end) { out.error = "No table above to plot"; return; }
    auto cellRow = [](const Row& r, std::pair<int, int> c) {
        Row q;
        for (int j = c.first; j < c.second; j++)
            if (!isSpace(r[j])) {
                Item it = r[j];
                if (it.t == T::Ch) it.m = true;
                q.push_back(it);
            }
        return q;
    };
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

Plot plot(Editor& ed, const Row& row, const double* view) {
    ED = &ed;
    Plot out;
    bool domain = false, yFixed = false;
    collectDefs(&row, 0);
    std::vector<Row> parts(1);  // split at top-level commas, without spaces; text letters count as math here
    for (size_t j = 1; j < row.size(); j++) {  // after the plot function
        Item it = row[j];
        if (it.t == T::Ch && it.c == ",") parts.emplace_back();
        else if (!isSpace(it)) {
            if (it.t == T::Ch) it.m = true;
            parts.back().push_back(it);
        }
    }
    std::vector<Ast> fns;
    for (auto& q : parts) {
        if (q.empty()) continue;
        if (plain(q) == "table") {
            tableSeries(ed, row, out);
            continue;
        }
        std::vector<int> rel, eq;
        for (int j = 0; j < int(q.size()); j++) {
            if (q[j].c == "<" || q[j].c == "≤") rel.push_back(j);
            if (q[j].c == "=") eq.push_back(j);
        }
        try {
            if (rel.size() == 2) {  // a < x < b, or a < y < b for the visible y range
                double lo = val(evaluate(parse(Row(q.begin(), q.begin() + rel[0]))));
                double hi = val(evaluate(parse(Row(q.begin() + rel[1] + 1, q.end()))));
                bool y = rel[1] - rel[0] == 2 && q[rel[0] + 1].c == "y";
                (y ? out.y0 : out.x0) = lo, (y ? out.y1 : out.x1) = hi;
                (y ? yFixed : domain) = true;
                continue;
            }
            Row e = eq.empty() ? q : Row(q.begin() + eq.back() + 1, q.end());  // y = …: the right side
            fns.push_back(parse(e));
            out.series.push_back({tex(e), plain(e), {}});
        } catch (...) {
            out.error = "Can't plot " + plain(q);
        }
    }
    std::vector<double> ys;
    if (!domain) {  // no domain given: fit the table data, if any
        double lo = INFINITY, hi = -INFINITY;
        for (auto& s : out.series)
            for (auto& [x, y] : s.pts) lo = std::min(lo, x), hi = std::max(hi, x);
        if (lo < hi) out.x0 = lo - 0.05 * (hi - lo), out.x1 = hi + 0.05 * (hi - lo);
    }
    for (auto& s : out.series)
        for (auto& [x, y] : s.pts)
            if (std::isfinite(y)) ys.push_back(y);
    if (view) out.x0 = view[0], out.x1 = view[1], out.y0 = view[2], out.y1 = view[3], yFixed = true;
    if (!(out.x1 > out.x0)) std::swap(out.x0, out.x1);
    if (out.x0 == out.x1) out.x1 = out.x0 + 1;
    const int N = 400;
    size_t first = out.series.size() - fns.size();  // table series come first
    for (size_t f = 0; f < fns.size(); f++) {
        size_t s = first + f;
        bool any = false;
        for (int i = 0; i < N; i++) {
            double x = out.x0 + (out.x1 - out.x0) * i / (N - 1), y = NAN;
            try {
                Value v = evaluate(fns[f], {{"x", Fl(x)}});
                if (!isMat(v)) y = val(v);
            } catch (...) {}
            if (!std::isfinite(y)) y = NAN;
            else ys.push_back(y), any = true;
            out.series[s].pts.push_back({x, y});
        }
        if (!any && out.error.empty()) out.error = "Can't plot " + out.series[s].label + " (is x the only unknown?)";
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
