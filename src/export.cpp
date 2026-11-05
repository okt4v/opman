// LaTeX export: an article with amsmath; lists, quotes and tables become environments, plots become pgfplots with the
// same sampled points the app draws, images \includegraphics.
#include "math.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>

namespace om {

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
    std::string out = PREAMBLE, env;
    auto open = [&](const std::string& e) {  // the list/quote environment the next line needs ("" = none)
        if (env == e) return;
        if (!env.empty()) out += "\\end{" + env + "}\n";
        if (!e.empty()) out += "\\begin{" + e + "}\n";
        env = e;
    };
    const Doc& doc = ed.doc;
    for (size_t k = 0; k < doc.size(); k++) {
        const Row& row = doc[k];
        LineStyle ls = lineStyle(row);
        std::string body = latexText(row, ls.len, int(row.size()));
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
            if (pl.series.empty()) { out += "\\texttt{" + esc(plain(row)) + "}\n\n"; break; }
            static const char* COLS[] = {"blue", "red", "green!60!black", "orange", "violet", "teal"};
            double span = pl.y1 - pl.y0;
            out += "\\begin{center}\n\\begin{tikzpicture}\n\\begin{axis}[width=0.8\\linewidth, height=6.5cm, grid=major, "
                   "grid style={gray!25}, unbounded coords=jump, no markers, xmin=" + num(pl.x0) + ", xmax=" + num(pl.x1) +
                   ", ymin=" + num(pl.y0) + ", ymax=" + num(pl.y1) + ", restrict y to domain=" + num(pl.y0 - 4 * span) + ":" +
                   num(pl.y1 + 4 * span) + ", legend pos=outer north east, legend style={draw=none}]\n";
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
            if (allMath) out += "\\[ " + tex(Row(row.begin() + a, row.end())) + " \\]\n\n";  // a line of only math is displayed
            else out += body + "\n\n";
        }
        }
    }
    open("");
    return out + "\\end{document}\n";
}

// ---------- HTML ----------
static std::string base64(const std::string& in) {
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

static std::string htmlEsc(const std::string& s) {
    std::string out;
    for (char c : s) out += c == '&' ? "&amp;" : c == '<' ? "&lt;" : c == '>' ? "&gt;" : c == '"' ? "&quot;" : std::string(1, c);
    return out;
}

// a top-level line's items [a, b): text with formatting tags, math as \( … \) for KaTeX
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
            std::string t = tex(Row(row.begin() + j, row.begin() + k));
            if (!t.empty()) out += "\\(" + htmlEsc(t) + "\\)";
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

static std::string svgPlot(const Plot& pl) {
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
             num(std::fabs(t) < 1e-12 ? 0 : t) + "</text>";
    for (double t = std::ceil(pl.y0 / sy) * sy; t <= pl.y1 + 1e-9; t += sy)
        s += "<line x1=44 x2=630 y1=" + num(Y(t)) + " y2=" + num(Y(t)) + " class=grid /><text x=38 y=" + num(Y(t) + 4) +
             " text-anchor=end>" + num(std::fabs(t) < 1e-12 ? 0 : t) + "</text>";
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
<link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/katex@0.16.11/dist/katex.min.css">
<script defer src="https://cdn.jsdelivr.net/npm/katex@0.16.11/dist/katex.min.js"></script>
<script defer src="https://cdn.jsdelivr.net/npm/katex@0.16.11/dist/contrib/auto-render.min.js"
  onload="renderMathInElement(document.body)"></script>
<style>
:root { --fg: #1d1d1f; --muted: #8a8a8e; --line: #e5e5e5; --bg: #fdfdfc; --side: #f6f5f3; }
@media (prefers-color-scheme: dark) { :root { --fg: #e8e8ea; --muted: #8a8a8e; --line: #3a3a3d; --bg: #1b1b1d; --side: #222225; } }
body { font: 17px/1.6 Inter, system-ui, sans-serif; color: var(--fg); background: var(--bg); max-width: 760px; margin: 40px auto; padding: 0 16px; }
p { margin: .4em 0; } hr { border: 0; border-top: 1px solid var(--line); margin: 1.2em 0; }
.section { margin: 2em 0 .6em; padding-bottom: .2em; border-bottom: 2px solid var(--fg); font-weight: 700; font-size: 1.15em; }
blockquote { border-left: 3px solid var(--line); margin: .4em 0; padding-left: 1em; color: var(--muted); }
table { border-collapse: collapse; margin: .6em 0; } td, th { border: 1px solid var(--line); padding: 4px 12px; text-align: left; }
th { background: var(--side); } img { max-width: 100%; border-radius: 6px; } figure { margin: 1em 0; } figcaption { color: var(--muted); font-size: .9em; }
code { font-family: "JetBrains Mono", monospace; background: var(--side); padding: 0 4px; border-radius: 4px; }
mark { background: #facc1566; color: inherit; } .r { color: #e5484d; } .g { color: #2f9e5b; } .b { color: #3b82f6; }
.link { color: #3b82f6; } li.done { text-decoration: line-through; color: var(--muted); }
.plot { width: 100%; background: var(--side); border-radius: 8px; } .plot text { font-size: 12px; fill: var(--muted); }
.plot .grid { stroke: var(--line); } ul.todo { list-style: none; padding-left: 1.2em; }
</style></head><body>
)HTML";

std::string html(Editor& ed, const std::filesystem::path& base, const std::string& title) {
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
    for (size_t k = 0; k < doc.size(); k++) {
        const Row& row = doc[k];
        LineStyle ls = lineStyle(row);
        std::string body = htmlText(row, ls.len, int(row.size()));
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
            if (!pl.series.empty()) out += svgPlot(pl) + "\n";
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
                out += "<p>\\[" + htmlEsc(tex(Row(row.begin() + a, row.end()))) + "\\]</p>\n";
            else out += "<p>" + body + "</p>\n";
        }
        }
    }
    open("");
    return out + "</body></html>\n";
}

}  // namespace om
