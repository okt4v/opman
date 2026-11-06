// LaTeX export: an article with amsmath; lists, quotes and tables become environments, plots become pgfplots with the
// same sampled points the app draws, images \includegraphics.
#include "math.hpp"
#include <array>
#include <bit>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>

namespace om {

std::string imagePdf(const std::vector<std::string>& pages, int w, int h) {
    std::string out = "%PDF-1.4\n";
    std::vector<size_t> at;
    auto obj = [&](const std::string& head, const std::string& stream = {}, bool hasStream = false) {
        at.push_back(out.size());
        out += std::to_string(at.size()) + " 0 obj\n<<" + head;
        if (hasStream) out += " /Length " + std::to_string(stream.size()) + " >>\nstream\n" + stream + "\nendstream";
        else out += " >>";
        out += "\nendobj\n";
    };
    std::string kids;
    for (size_t p = 0; p < pages.size(); p++) kids += std::to_string(3 + 3 * p) + " 0 R ";
    obj(" /Type /Catalog /Pages 2 0 R");
    obj(" /Type /Pages /Count " + std::to_string(pages.size()) + " /Kids [" + kids + "]");
    const std::string draw = "q 595.28 0 0 841.89 0 0 cm /Im Do Q";
    for (size_t p = 0; p < pages.size(); p++) {
        int n = 3 + 3 * int(p), size = 0;
        obj(" /Type /Page /Parent 2 0 R /MediaBox [0 0 595.28 841.89] /Contents " + std::to_string(n + 1) + " 0 R /Resources << /XObject << /Im " + std::to_string(n + 2) + " 0 R >> >>");
        obj("", draw, true);
        unsigned char* z = stbi_zlib_compress((unsigned char*)pages[p].data(), int(pages[p].size()), &size, 8);
        obj(" /Type /XObject /Subtype /Image /Width " + std::to_string(w) + " /Height " + std::to_string(h) + " /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /FlateDecode", std::string((char*)z, size), true);
        STBIW_FREE(z);
    }
    size_t xref = out.size();
    char line[32];
    out += "xref\n0 " + std::to_string(at.size() + 1) + "\n0000000000 65535 f \n";
    for (size_t a : at) std::snprintf(line, sizeof line, "%010zu 00000 n \n", a), out += line;
    return out + "trailer\n<< /Size " + std::to_string(at.size() + 1) + " /Root 1 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
}

// equation tags and references for export: the tag dropped, each [#ref] as its number "(n)"
static Row eqPlain(const Row& row, const std::map<std::string, int>& nums) {
    auto refs = eqRefs(row);
    auto tag = eqTag(row);
    if (refs.empty() && tag.first < 0) return row;
    Row out;
    int end = tag.first >= 0 ? tag.first - 1 : int(row.size());
    for (int j = 0; j < end;) {
        auto r = std::find_if(refs.begin(), refs.end(), [&](auto sp) { return sp.first == j; });
        if (r == refs.end()) { out.push_back(row[j++]); continue; }
        auto f = nums.find(eqName(row, *r));
        for (char c : "(" + (f == nums.end() ? std::string("?") : std::to_string(f->second)) + ")") out.push_back(ch(std::string(1, c), false));
        j = r->second;
    }
    return out;
}
static std::string eqNum(const Row& row, const std::map<std::string, int>& nums) {
    auto t = eqTag(row);
    if (t.first < 0) return "";
    auto f = nums.find(eqName(row, t));
    return f == nums.end() ? "" : "(" + std::to_string(f->second) + ")";
}

// plots in exports have their own shape, whatever the app's window is (so a circle is round there too)
namespace {
struct PlotShape {
    double keep = mathOpts.plotAspect;
    explicit PlotShape(double aspect) { mathOpts.plotAspect = aspect; }
    ~PlotShape() { mathOpts.plotAspect = keep; }
};
}  // namespace

static std::string num(double x) {
    if (!std::isfinite(x)) return "nan";
    char b[32];
    std::snprintf(b, sizeof b, "%.6g", x);
    return b;
}

static std::string esc(const std::string& s) {  // a plain string as LaTeX text
    Row r;
    for (size_t i = 0; i < s.size();) {
        unsigned char b = s[i];
        size_t n = b < 0x80 ? 1 : b < 0xE0 ? 2 : b < 0xF0 ? 3 : 4;
        r.push_back(ch(s.substr(i, n), false));
        i += n;
    }
    return latexText(r, 0, int(r.size()));
}

static const char* PREAMBLE = R"(\documentclass[11pt]{article}
\usepackage[utf8]{inputenc}
\usepackage[T1]{fontenc}
\usepackage{lmodern}
\usepackage[margin=2.5cm]{geometry}
\usepackage{amsmath,amssymb}
\usepackage{graphicx}
\usepackage{xcolor}
\usepackage{soul}
\usepackage[normalem]{ulem}
\usepackage{pgfplots}
\pgfplotsset{compat=1.18}
\setlength{\parindent}{0pt}
\setlength{\parskip}{6pt}
\begin{document}
)";

std::string latex(Editor& ed, const std::filesystem::path& base) {
    PlotShape shape(1.6);  // about the shape of the pgfplots axis
    std::string out = PREAMBLE, env;
    auto open = [&](const std::string& e) {  // the list/quote environment the next line needs ("" = none)
        if (env == e) return;
        if (!env.empty()) out += "\\end{" + env + "}\n";
        if (!e.empty()) out += "\\begin{" + e + "}\n";
        env = e;
    };
    const Doc& doc = ed.doc;
    auto nums = eqNumbers(doc);
    for (size_t k = 0; k < doc.size(); k++) {
        bool eqs = eqTag(doc[k]).first >= 0 || !eqRefs(doc[k]).empty();  // copy only then: plots find their line by address
        Row plainRow = eqs ? eqPlain(doc[k], nums) : Row{};
        const Row& row = eqs ? plainRow : doc[k];
        LineStyle ls = lineStyle(row);
        std::string body = latexText(row, ls.len, int(row.size()));
        if (std::string n = eqNum(doc[k], nums); !n.empty()) body += "\\hfill" + n;  // text with math; display math gets \tag
        switch (ls.kind) {
        case Line::H1: open(""); out += "\\section*{" + body + "}\n"; break;
        case Line::H2: open(""); out += "\\subsection*{" + body + "}\n"; break;
        case Line::H3: open(""); out += "\\subsubsection*{" + body + "}\n"; break;
        case Line::Bullet: open("itemize"); out += "\\item " + body + "\n"; break;
        case Line::Todo: open("itemize"); out += "\\item[$\\square$] " + body + "\n"; break;
        case Line::Done: open("itemize"); out += "\\item[$\\boxtimes$] " + body + "\n"; break;
        case Line::Number: open("enumerate"); out += "\\item " + body + "\n"; break;
        case Line::Quote: open("quote"); out += body + "\\par\n"; break;
        case Line::Rule: open(""); out += "\\noindent\\rule{\\linewidth}{0.4pt}\n\n"; break;
        case Line::Section:
            open("");
            out += "\\bigskip\\noindent{\\large\\bfseries " + esc(sectionTitle(row)) + "}\\par\\nobreak\\noindent\\rule{\\linewidth}{1pt}\n\n";
            break;
        case Line::Image: {
            open("");
            std::string alt, path;
            imageRef(row, alt, path);
            std::filesystem::path p = path;
            if (p.is_relative()) p = base / p;
            std::string ext = p.extension().string();
            for (auto& c : ext) c = char(std::tolower((unsigned char)c));
            out += "\\begin{center}\n";
            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".pdf")
                out += "\\includegraphics[width=0.8\\linewidth,height=0.4\\textheight,keepaspectratio]{" + p.generic_string() + "}\n";
            else out += "\\texttt{" + esc(path) + "}\n";  // pdflatex reads png, jpg and pdf only
            if (!alt.empty()) out += "\\\\[2pt]{\\small " + esc(alt) + "}\n";
            out += "\\end{center}\n\n";
            break;
        }
        case Line::Plot: {
            open("");
            Plot pl = plot(ed, row);
            static const char* COLS[] = {"blue", "red", "green!60!black", "orange", "violet", "teal"};
            if (pl.is3d()) {  // pgfplots 3D: a faceted surface (every other grid point) and space curves
                out += "\\begin{center}\n\\begin{tikzpicture}\n\\begin{axis}[width=0.8\\linewidth, height=7cm, view={-37}{30}, "
                       "colormap/viridis, unbounded coords=jump, xlabel=$x$, ylabel=$y$, zlabel=$z$, xmin=" + num(pl.x0) + ", xmax=" +
                       num(pl.x1) + ", ymin=" + num(pl.y0) + ", ymax=" + num(pl.y1) + ", zmin=" + num(pl.z0) + ", zmax=" + num(pl.z1) + "]\n";
                for (auto& sf : pl.surfaces) {
                    int rows = 0;
                    std::string pts;
                    for (int j = 0; j < sf.n; j += 2, rows++)
                        for (int i = 0; i < sf.n; i += 2) {
                            double z = sf.z[j * sf.n + i];
                            pts += "(" + num(pl.x0 + (pl.x1 - pl.x0) * i / (sf.n - 1)) + "," + num(pl.y0 + (pl.y1 - pl.y0) * j / (sf.n - 1)) + "," +
                                   num(std::isfinite(z) ? std::clamp(z, pl.z0, pl.z1) : z) + ")";
                        }
                    out += "\\addplot3[surf, shader=faceted, mesh/rows=" + std::to_string(rows) + "] coordinates {" + pts + "};\n";
                }
                for (size_t c = 0; c < pl.curves.size(); c++) {
                    out += "\\addplot3[thick, no markers, color=" + std::string(COLS[c % 6]) + "] coordinates {";
                    for (size_t i = 0; i < pl.curves[c].pts.size(); i += 2) {
                        auto& q = pl.curves[c].pts[i];
                        out += "(" + num(q[0]) + "," + num(q[1]) + "," + num(q[2]) + ")";
                    }
                    out += "};\n";
                }
                out += "\\end{axis}\n\\end{tikzpicture}\n\\end{center}\n\n";
                break;
            }
            if (pl.series.empty()) { out += "\\texttt{" + esc(plain(row)) + "}\n\n"; break; }
            double span = pl.y1 - pl.y0;
            out += "\\begin{center}\n\\begin{tikzpicture}\n\\begin{axis}[width=0.8\\linewidth, height=6.5cm, grid=major, "
                   + std::string(decimalComma ? "/pgf/number format/use comma, " : "") +
                   "grid style={gray!25}, unbounded coords=jump, no markers, xmin=" + num(pl.x0) + ", xmax=" + num(pl.x1) +
                   ", ymin=" + num(pl.y0) + ", ymax=" + num(pl.y1) + ", restrict y to domain=" + num(pl.y0 - 4 * span) + ":" +
                   num(pl.y1 + 4 * span) + ", legend style={at={(0.5,-0.18)}, anchor=north, draw=none, legend columns=2, column sep=1em}]\n";
            for (size_t s = 0; s < pl.series.size(); s++) {
                out += "\\addplot[" + std::string(pl.series[s].points ? "only marks, mark=*" : "thick") + ", color=" + COLS[s % 6] +
                       "] coordinates {";
                for (auto& [x, y] : pl.series[s].pts) out += "(" + num(x) + "," + num(y) + ")";
                out += "};\n\\addlegendentry{$" + pl.series[s].tex + "$}\n";
            }
            out += "\\end{axis}\n\\end{tikzpicture}\n\\end{center}\n\n";
            break;
        }
        case Line::Table: {
            open("");
            size_t end = k;
            size_t cols = 1;
            while (end < doc.size() && lineStyle(doc[end]).kind == Line::Table) cols = std::max(cols, tableCells(doc[end++]).size());
            std::string spec = "|";
            for (size_t c = 0; c < cols; c++) spec += "l|";
            out += "\\begin{center}\n\\begin{tabular}{" + spec + "}\n\\hline\n";
            for (size_t r = k; r < end; r++) {
                auto cells = tableCells(doc[r]);
                for (size_t c = 0; c < cols; c++) {
                    if (c) out += " & ";
                    if (c < cells.size()) {
                        std::string t = latexText(doc[r], cells[c].first, cells[c].second);
                        out += r == k ? "\\textbf{" + t + "}" : t;  // the first row is the header
                    }
                }
                out += r == k ? " \\\\ \\hline\\hline\n" : " \\\\ \\hline\n";
            }
            out += "\\end{tabular}\n\\end{center}\n\n";
            k = end - 1;
            break;
        }
        default: {
            open("");
            int a = ls.len;
            while (a < int(row.size()) && isSpace(row[a])) a++;
            if (a == int(row.size())) { out += "\n"; break; }
            auto mask = mathMask(row, true);
            bool allMath = std::all_of(mask.begin() + a, mask.end(), [](bool m) { return m; });
            std::string n = eqNum(doc[k], nums);
            if (allMath) out += "\\[ " + tex(Row(row.begin() + a, row.end())) + (n.empty() ? "" : " \\tag{" + n.substr(1, n.size() - 2) + "}") + " \\]\n\n";  // a line of only math is displayed
            else out += body + "\n\n";
        }
        }
    }
    open("");
    return out + "\\end{document}\n";
}

// ---------- HTML ----------

static std::string htmlEsc(const std::string& s) {
    std::string out;
    for (char c : s) out += c == '&' ? "&amp;" : c == '<' ? "&lt;" : c == '>' ? "&gt;" : c == '"' ? "&quot;" : std::string(1, c);
    return out;
}

// math as MathML, which browsers draw themselves: the file needs nothing from the network
static std::string mathml(const Row& row);
static std::string mrow(const Row& row) { return "<mrow>" + mathml(row) + "</mrow>"; }
static std::string mo(const std::string& c) { return c.empty() ? "" : "<mo>" + htmlEsc(c) + "</mo>"; }
static std::string mi(const std::string& c, bool upright = false) {
    return std::string(upright ? "<mi mathvariant=\"normal\">" : "<mi>") + htmlEsc(c) + "</mi>";
}
static std::string mathml(const Row& row) {
    std::vector<std::string> out;
    auto base = [&] {  // what a script attaches to
        std::string b = out.empty() ? "<mrow></mrow>" : out.back();
        if (!out.empty()) out.pop_back();
        return b;
    };
    for (size_t j = 0; j < row.size(); j++) {
        const Item& it = row[j];
        switch (it.t) {
        case T::Ch: {
            if (it.c == " ") break;
            if (isDigit(it.c) || (it.c == "." && j + 1 < row.size() && isDigit(row[j + 1].c))) {
                std::string n;
                for (; j < row.size() && row[j].t == T::Ch && (isDigit(row[j].c) || row[j].c == "." || (row[j].c == "," && j + 1 < row.size() && decimalComma && isDigit(row[j + 1].c))); j++) n += row[j].c;
                j--;
                out.push_back("<mn>" + n + "</mn>");
            } else if (isLetter(it.c)) out.push_back(mi(it.c));
            else out.push_back(mo(it.c == "*" ? "⋅" : it.c == "-" ? "−" : it.c));
            break;
        }
        case T::Var: out.push_back(mi(it.c)); break;
        case T::Const: out.push_back(mi(it.c, true)); break;
        case T::Unit: out.push_back("<mspace width=\"0.17em\"></mspace>" + mi(it.c, true)); break;
        case T::Sym:
            if (isStepBar(it)) out.push_back("<mspace width=\"2em\"></mspace><mo>|</mo><mspace width=\"0.28em\"></mspace>");
            else out.push_back(mo(it.c));
            break;
        case T::Fn: out.push_back(mi(it.c, true) + "<mo>&#x2061;</mo>"); break;
        case T::Frac: out.push_back("<mfrac>" + mrow(it.s[0]) + mrow(it.s[1]) + "</mfrac>"); break;
        case T::Sup: out.push_back("<msup>" + base() + mrow(it.s[0]) + "</msup>"); break;
        case T::Sub: out.push_back("<msub>" + base() + mrow(it.s[0]) + "</msub>"); break;
        case T::Sqrt: out.push_back("<msqrt>" + mathml(it.s[0]) + "</msqrt>"); break;
        case T::Lim: out.push_back("<munder><mi mathvariant=\"normal\">lim</mi>" + mrow(it.s[0]) + "</munder>"); break;
        case T::Paren: out.push_back("<mrow>" + mo(it.o) + mathml(it.s[0]) + mo(it.cl) + "</mrow>"); break;
        case T::Mat: {
            std::string t = "<mtable>";
            for (int r = 0; r < matRows(it); r++) {
                t += "<mtr>";
                for (int c = 0; c < it.cols; c++) t += "<mtd>" + mathml(it.s[r * it.cols + c]) + "</mtd>";
                t += "</mtr>";
            }
            out.push_back("<mrow>" + mo(it.o) + t + "</mtable>" + mo(it.cl) + "</mrow>");
            break;
        }
        case T::Big: {
            const char* tag = it.m ? "munderover" : "msubsup";
            out.push_back(std::string("<") + tag + "><mo>" + htmlEsc(it.c) + "</mo>" + mrow(it.s[0]) + mrow(it.s[1]) + "</" + tag + ">");
            break;
        }
        }
    }
    std::string s;
    for (auto& e : out) s += e;
    return s;
}

// a top-level line's items [a, b): text with formatting tags, math as MathML
static std::string htmlText(const Row& row, int a, int b) {
    static const struct { uint16_t f; const char *open, *close; } MK[] = {
        {Mark, "<mark>", "</mark>"}, {Underline, "<u>", "</u>"}, {Strike, "<s>", "</s>"}, {Bold, "<b>", "</b>"},
        {Italic, "<i>", "</i>"}, {Red, "<span class=r>", "</span>"}, {Green, "<span class=g>", "</span>"},
        {Blue, "<span class=b>", "</span>"}, {Sup, "<sup>", "</sup>"}, {Sub, "<sub>", "</sub>"}, {Code, "<code>", "</code>"}};
    auto mask = mathMask(row, true);
    std::string out;
    uint16_t on = 0;
    auto set = [&](uint16_t f) {
        if (f == on) return;
        for (int m = 10; m >= 0; m--) if (on & MK[m].f) out += MK[m].close;
        for (auto& m : MK) if (f & m.f) out += m.open;
        on = f;
    };
    auto links = linkSpans(row);
    for (int j = a; j < b;) {
        int k = j;
        while (k < b && mask[k] == mask[j]) k++;
        if (mask[j]) {
            set(0);
            std::string t = mathml(Row(row.begin() + j, row.begin() + k));
            if (!t.empty()) out += "<math>" + t + "</math>";
        } else
            for (int i = j; i < k; i++) {
                set(isSpace(row[i]) ? on & row[i].f : row[i].f);
                bool open = false, close = false;
                for (auto& [s, e] : links) open = open || i == s, close = close || i == e - 1;
                if (open) out += "<span class=link>";
                out += htmlEsc(row[i].c);
                if (close) out += "</span>";
            }
        j = k;
    }
    set(0);
    return out;
}

// a 3D plot as SVG: the app's default view (yaw −0.65, pitch 0.45), quads far to near
static std::string svgPlot3D(const Plot& pl) {
    const double W = 640, H = 420, cy0 = std::cos(-0.65), sy0 = std::sin(-0.65), cp = std::cos(0.45), sp = std::sin(0.45), sc = 150;
    auto view = [&](double X, double Y, double Z) {  // box coordinates in [-1, 1] -> screen x, y, depth
        double nx = (X - pl.x0) / (pl.x1 - pl.x0) * 2 - 1, ny = (Y - pl.y0) / (pl.y1 - pl.y0) * 2 - 1, nz = ((Z - pl.z0) / (pl.z1 - pl.z0) * 2 - 1) * 0.75;
        double rx = nx * cy0 - ny * sy0, ry = nx * sy0 + ny * cy0;
        return std::array<double, 3>{W / 2 + rx * sc, H / 2 + 10 - (nz * cp + ry * sp) * sc, -ry * cp + nz * sp};
    };
    std::string s = "<svg viewBox=\"0 0 640 420\" class=plot>";
    for (int a = 0; a < 8; a++)  // the box
        for (int b = a + 1; b < 8; b++)
            if (std::popcount(unsigned(a ^ b)) == 1) {
                auto P = [&](int i) { return view(i & 1 ? pl.x1 : pl.x0, i & 2 ? pl.y1 : pl.y0, i & 4 ? pl.z1 : pl.z0); };
                auto p = P(a), q = P(b);
                s += "<line x1=\"" + num(p[0]) + "\" y1=\"" + num(p[1]) + "\" x2=\"" + num(q[0]) + "\" y2=\"" + num(q[1]) + "\" class=grid />";
            }
    struct Quad {
        double depth;
        std::string svg;
    };
    std::vector<Quad> quads;
    static const double RAMP[5][3] = {{68, 1, 84}, {59, 82, 139}, {33, 145, 140}, {94, 201, 98}, {253, 231, 37}};
    for (auto& sf : pl.surfaces) {
        int n = sf.n, st = 2;
        for (int j = 0; j + st < n; j += st)
            for (int i = 0; i + st < n; i += st) {
                int I[4] = {i, i + st, i + st, i}, J[4] = {j, j, j + st, j + st};
                double zsum = 0, d = 0;
                std::string pts;
                bool ok = true;
                for (int m = 0; m < 4; m++) {
                    double z = sf.z[J[m] * n + I[m]];
                    if (!std::isfinite(z)) { ok = false; break; }
                    z = std::clamp(z, pl.z0, pl.z1);
                    zsum += z;
                    auto v = view(pl.x0 + (pl.x1 - pl.x0) * I[m] / (n - 1), pl.y0 + (pl.y1 - pl.y0) * J[m] / (n - 1), z);
                    pts += num(v[0]) + "," + num(v[1]) + " ";
                    d += v[2];
                }
                if (!ok) continue;
                double t = std::clamp((zsum / 4 - pl.z0) / (pl.z1 - pl.z0), 0.0, 0.9999) * 4, f = t - int(t);
                const double *a = RAMP[int(t)], *b = RAMP[int(t) + 1];
                char col[16];
                std::snprintf(col, sizeof col, "#%02x%02x%02x", int(a[0] + (b[0] - a[0]) * f), int(a[1] + (b[1] - a[1]) * f), int(a[2] + (b[2] - a[2]) * f));
                quads.push_back({d, "<polygon points=\"" + pts + "\" fill=\"" + col + "\" stroke=\"rgba(0,0,0,.15)\" stroke-width=\"0.5\" />"});
            }
    }
    std::sort(quads.begin(), quads.end(), [](auto& a, auto& b) { return a.depth < b.depth; });
    for (auto& q : quads) s += q.svg;
    static const char* COLS[] = {"#3b82f6", "#e5484d", "#2f9e5b", "#f59e0b", "#8b5cf6", "#14b8a6"};
    for (size_t c = 0; c < pl.curves.size(); c++) {
        std::string d;
        bool pen = false;
        for (auto& q : pl.curves[c].pts) {
            if (!std::isfinite(q[0]) || !std::isfinite(q[1]) || !std::isfinite(q[2])) { pen = false; continue; }
            auto v = view(q[0], q[1], std::clamp(q[2], pl.z0, pl.z1));
            d += (pen ? "L" : "M") + num(v[0]) + " " + num(v[1]) + " ";
            pen = true;
        }
        s += "<path d=\"" + d + "\" stroke=\"" + COLS[c % 6] + "\" fill=\"none\" stroke-width=\"2.2\" />";
    }
    int k = 0;
    for (auto& sf : pl.surfaces) s += "<text x=\"14\" y=\"" + num(24 + 18 * k++) + "\">z = " + htmlEsc(sf.label) + "</text>";
    for (size_t c = 0; c < pl.curves.size(); c++)
        s += "<text x=\"14\" y=\"" + num(24 + 18 * k++) + "\" style=\"fill:" + COLS[c % 6] + "\">— " + htmlEsc(pl.curves[c].label) + "</text>";
    return s + "</svg>";
}

static std::string svgPlot(const Plot& pl) {
    if (pl.is3d()) return svgPlot3D(pl);
    static const char* COLS[] = {"#3b82f6", "#e5484d", "#2f9e5b", "#f59e0b", "#8b5cf6", "#14b8a6"};
    const double W = 640, H = 340, L = 44, R = 10, T = 10, B = 26;
    auto X = [&](double v) { return L + (v - pl.x0) / (pl.x1 - pl.x0) * (W - L - R); };
    auto Y = [&](double v) { return H - B - (v - pl.y0) / (pl.y1 - pl.y0) * (H - T - B); };
    auto step = [](double span) {
        double raw = span / 6, mag = std::pow(10, std::floor(std::log10(raw))), f = raw / mag;
        return (f < 1.5 ? 1 : f < 3.5 ? 2 : f < 7.5 ? 5 : 10) * mag;
    };
    std::string s = "<svg viewBox=\"0 0 640 340\" class=plot><clipPath id=c><rect x=44 y=10 width=586 height=304 /></clipPath>";
    double sx = step(pl.x1 - pl.x0), sy = step(pl.y1 - pl.y0);
    for (double t = std::ceil(pl.x0 / sx) * sx; t <= pl.x1 + 1e-9; t += sx)
        s += "<line x1=" + num(X(t)) + " x2=" + num(X(t)) + " y1=10 y2=314 class=grid /><text x=" + num(X(t)) + " y=330 text-anchor=middle>" +
             localNum(num(std::fabs(t) < 1e-12 ? 0 : t)) + "</text>";
    for (double t = std::ceil(pl.y0 / sy) * sy; t <= pl.y1 + 1e-9; t += sy)
        s += "<line x1=44 x2=630 y1=" + num(Y(t)) + " y2=" + num(Y(t)) + " class=grid /><text x=38 y=" + num(Y(t) + 4) +
             " text-anchor=end>" + localNum(num(std::fabs(t) < 1e-12 ? 0 : t)) + "</text>";
    s += "<g clip-path=url(#c)>";
    double span = pl.y1 - pl.y0;
    for (size_t k = 0; k < pl.series.size(); k++) {
        auto& se = pl.series[k];
        std::string c = COLS[k % 6];
        if (se.points) {
            for (auto& [x, y] : se.pts)
                if (std::isfinite(y)) s += "<circle cx=" + num(X(x)) + " cy=" + num(Y(y)) + " r=4 fill=\"" + c + "\" />";
            continue;
        }
        std::string d;
        double prev = NAN;
        for (auto& [x, y] : se.pts) {
            bool gap = std::isnan(y) || (!std::isnan(prev) && std::fabs(y - prev) > 3 * span);
            if (!std::isnan(y)) d += (d.empty() || gap ? "M" : "L") + num(X(x)) + " " + num(Y(std::clamp(y, pl.y0 - 2 * span, pl.y1 + 2 * span))) + " ";
            prev = y;
        }
        s += "<path d=\"" + d + "\" stroke=\"" + c + "\" fill=none stroke-width=2.2 />";
    }
    s += "</g>";
    for (size_t k = 0; k < pl.series.size(); k++)
        s += "<text x=60 y=" + num(28 + 18 * k) + " style=\"fill:" + COLS[k % 6] + "\">— " + htmlEsc(pl.series[k].label) + "</text>";
    return s + "</svg>";
}

static const char* HTML_HEAD = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>%TITLE%</title>
<style>
:root { --fg: #1d1d1f; --muted: #8a8a8e; --line: #e5e5e5; --bg: #fdfdfc; --side: #f6f5f3; }
@media (prefers-color-scheme: dark) { :root { --fg: #e8e8ea; --muted: #8a8a8e; --line: #3a3a3d; --bg: #1b1b1d; --side: #222225; } }
body { font: 17px/1.6 Inter, system-ui, sans-serif; color: var(--fg); background: var(--bg); max-width: 760px; margin: 40px auto; padding: 0 16px; }
math { font-family: "STIX Two Math", "Cambria Math", "Latin Modern Math", math; font-size: 1.1em; } math[display=block] { margin: .5em 0; }
p { margin: .4em 0; } hr { border: 0; border-top: 1px solid var(--line); margin: 1.2em 0; }
.section { margin: 2em 0 .6em; padding-bottom: .2em; border-bottom: 2px solid var(--fg); font-weight: 700; font-size: 1.15em; }
blockquote { border-left: 3px solid var(--line); margin: .4em 0; padding-left: 1em; color: var(--muted); }
table { border-collapse: collapse; margin: .6em 0; } td, th { border: 1px solid var(--line); padding: 4px 12px; text-align: left; }
th { background: var(--side); } img { max-width: 100%; border-radius: 6px; } figure { margin: 1em 0; } figcaption { color: var(--muted); font-size: .9em; }
code { font-family: "JetBrains Mono", monospace; background: var(--side); padding: 0 4px; border-radius: 4px; }
mark { background: #facc1566; color: inherit; } .r { color: #e5484d; } .g { color: #2f9e5b; } .b { color: #3b82f6; }
.link { color: #3b82f6; } .eqn { float: right; } li.done { text-decoration: line-through; color: var(--muted); }
.plot { width: 100%; background: var(--side); border-radius: 8px; } .plot text { font-size: 12px; fill: var(--muted); }
.plot .grid { stroke: var(--line); } ul.todo { list-style: none; padding-left: 1.2em; }
</style></head><body>
)HTML";

std::string html(Editor& ed, const std::filesystem::path& base, const std::string& title) {
    PlotShape shape(586.0 / 304);  // svgPlot's area
    std::string head = HTML_HEAD;
    head.replace(head.find("%TITLE%"), 7, htmlEsc(title));
    std::string out = head, env;
    auto open = [&](const std::string& e) {
        if (env == e) return;
        if (!env.empty()) out += "</" + env.substr(0, env.find(' ')) + ">\n";
        if (!e.empty()) out += "<" + e + ">\n";
        env = e;
    };
    const Doc& doc = ed.doc;
    auto nums = eqNumbers(doc);
    for (size_t k = 0; k < doc.size(); k++) {
        bool eqs = eqTag(doc[k]).first >= 0 || !eqRefs(doc[k]).empty();  // copy only then: plots find their line by address
        Row plainRow = eqs ? eqPlain(doc[k], nums) : Row{};
        const Row& row = eqs ? plainRow : doc[k];
        LineStyle ls = lineStyle(row);
        std::string body = htmlText(row, ls.len, int(row.size()));
        if (std::string n = eqNum(doc[k], nums); !n.empty()) body = "<span class=eqn>" + n + "</span>" + body;
        switch (ls.kind) {
        case Line::H1: open(""); out += "<h1>" + body + "</h1>\n"; break;
        case Line::H2: open(""); out += "<h2>" + body + "</h2>\n"; break;
        case Line::H3: open(""); out += "<h3>" + body + "</h3>\n"; break;
        case Line::Bullet: open("ul"); out += "<li>" + body + "</li>\n"; break;
        case Line::Todo: open("ul class=todo"); out += "<li>☐ " + body + "</li>\n"; break;
        case Line::Done: open("ul class=todo"); out += "<li class=done>☑ " + body + "</li>\n"; break;
        case Line::Number: open("ol"); out += "<li>" + body + "</li>\n"; break;
        case Line::Quote: open("blockquote"); out += "<p>" + body + "</p>\n"; break;
        case Line::Rule: open(""); out += "<hr>\n"; break;
        case Line::Section: open(""); out += "<div class=section>" + htmlEsc(sectionTitle(row)) + "</div>\n"; break;
        case Line::Image: {
            open("");
            std::string alt, path;
            imageRef(row, alt, path);
            std::filesystem::path p = path;
            if (p.is_relative()) p = base / p;
            std::ifstream f(p, std::ios::binary);
            std::string bytes((std::istreambuf_iterator<char>(f)), {});
            std::string ext = p.extension().string().substr(std::min<size_t>(1, p.extension().string().size()));
            for (auto& c : ext) c = char(std::tolower((unsigned char)c));
            if (ext == "jpg") ext = "jpeg";
            out += "<figure>";
            if (bytes.empty()) out += "<p>Image not found: " + htmlEsc(path) + "</p>";
            else out += "<img alt=\"" + htmlEsc(alt) + "\" src=\"data:image/" + ext + ";base64," + base64(bytes) + "\">";  // one shareable file
            if (!alt.empty()) out += "<figcaption>" + htmlEsc(alt) + "</figcaption>";
            out += "</figure>\n";
            break;
        }
        case Line::Plot: {
            open("");
            Plot pl = plot(ed, row);
            out += "<p>" + body + "</p>\n";
            if (!pl.series.empty() || pl.is3d()) out += svgPlot(pl) + "\n";
            break;
        }
        case Line::Table: {
            open("");
            size_t end = k;
            while (end < doc.size() && lineStyle(doc[end]).kind == Line::Table) end++;
            out += "<table>\n";
            for (size_t r = k; r < end; r++) {
                out += "<tr>";
                for (auto [a, b] : tableCells(doc[r])) {
                    while (a < b && isSpace(doc[r][a])) a++;
                    while (b > a && isSpace(doc[r][b - 1])) b--;
                    out += (r == k ? "<th>" : "<td>") + htmlText(doc[r], a, b) + (r == k ? "</th>" : "</td>");
                }
                out += "</tr>\n";
            }
            out += "</table>\n";
            k = end - 1;
            break;
        }
        default: {
            open("");
            int a = ls.len;
            while (a < int(row.size()) && isSpace(row[a])) a++;
            if (a == int(row.size())) break;
            auto mask = mathMask(row, true);
            if (std::all_of(mask.begin() + a, mask.end(), [](bool m) { return m; }))
                out += (eqNum(doc[k], nums).empty() ? "<p>" : "<p><span class=eqn>" + eqNum(doc[k], nums) + "</span>") + "<math display=\"block\">" + mathml(Row(row.begin() + a, row.end())) + "</math></p>\n";
            else out += "<p>" + body + "</p>\n";
        }
        }
    }
    open("");
    return out + "</body></html>\n";
}

}  // namespace om
