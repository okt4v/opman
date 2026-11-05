// Evaluation (port of corca math.js): exact/float numbers, parsing editor rows into expressions, definitions,
// steps, polynomials, solving, matrices and units. Results come back as completion entries.
#pragma once
#include "edit.hpp"

namespace om {

// cursor right after "=" offers the value (exact, decimal, with steps) or the expanded polynomial;
// right after "⇒" (type =>) the solutions of the equation before it
std::vector<Entry> evalSuggestions(Editor& ed);

// the faint "= 17" after a line; Tab at the end of the line writes it in
struct Ghost {
    std::string text;
    Row insert;
};
std::optional<Ghost> ghost(Editor& ed, const Row& row);

// names the document defines (for the file's meta)
std::vector<std::string> definedNames(Editor& ed);

bool unitStartsWith(const std::string& c);

// "plot sin(x), f(x), x^2/4, −3 < x < 3": curves sampled over the domain (default −10…10), with a y range that ignores
// spikes (tan, 1/x). A failed point is NaN, a gap in the curve.
struct Plot {
    double x0 = -10, x1 = 10, y0 = -1, y1 = 1;
    struct Series {
        std::string tex, label;
        std::vector<std::pair<double, double>> pts;
        bool points = false;  // table data: markers, not a curve
    };
    std::vector<Series> series;
    std::string error;
};
// view: {x0, x1, y0, y1} instead of the line's own ranges (the app's pan and zoom)
Plot plot(Editor& ed, const Row& row, const double* view = nullptr);
// the document as one HTML file: KaTeX for math, images inlined, plots as SVG (export.cpp)
std::string html(Editor& ed, const std::filesystem::path& base, const std::string& title);
// the document as a LaTeX article (export.cpp); image paths are relative to base
std::string latex(Editor& ed, const std::filesystem::path& base);

// JS Number formatting, shared with the tests: String(x) and +x.toPrecision(p)
std::string jsNum(double x);
double toPrecision(double x, int p);

}  // namespace om
