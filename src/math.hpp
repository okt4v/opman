// Evaluation (port of corca math.js): exact/float numbers, parsing editor rows into expressions, definitions,
// steps, polynomials, solving, matrices and units. Results come back as completion entries.
#pragma once
#include "edit.hpp"
#include <array>

namespace om {

// user settings for results (the app sets them; defaults match corca)
struct MathOpts {
    int digits = 10;        // significant digits of decimal results
    bool degrees = false;   // sin/cos/tan take degrees, arcsin/arccos/arctan return them
    bool decimals = false;  // live results show 0.75 rather than 3/4
    double plotX0 = -10, plotX1 = 10;  // a plot's x range when the line gives none
    double plotAspect = 2.4;           // width / height of the plot area, so a circle comes out round (the app sets it)
};
extern MathOpts mathOpts;

// cursor right after "=" offers the value (exact, decimal, with steps) or the expanded polynomial;
// right after "⇒" (type =>) the solutions of the equation before it
std::vector<Entry> evalSuggestions(Editor& ed);

// the faint "= 17" after a line; Tab at the end of the line writes it in
struct Ghost {
    std::string text;
    Row insert;
    bool warn = false;  // not a result: this line doesn't follow from the transformation step above it
};
std::optional<Ghost> ghost(Editor& ed, const Row& row);

// names the document defines (for the file's meta)
std::vector<std::string> definedNames(Editor& ed);

bool unitStartsWith(const std::string& c);

// "plot sin(x), f(x), x^2/4, −3 < x < 3": curves sampled over the domain (default −10…10), with a y range that ignores
// spikes (tan, 1/x). A failed point is NaN, a gap in the curve. Also parametric (cos(t), sin(t)), polar r = 1 + cos(θ),
// implicit x^2 + y^2 = 4, "table" data and "fit" curves through it; with y as a variable (sin(x) cos(y), z = …) or a
// triple (cos(t), sin(t), t) the plot is 3D.
struct Plot {
    double x0 = -10, x1 = 10, y0 = -1, y1 = 1, z0 = -1, z1 = 1;
    struct Series {
        std::string tex, label;
        std::vector<std::pair<double, double>> pts;
        bool points = false;  // table data: markers, not a curve
    };
    std::vector<Series> series;
    // 3D: z = f(x, y) on an n×n grid over [x0, x1]×[y0, y1] (z[j·n + i] at x_i, y_j), and space curves
    struct Surface {
        std::string tex, label;
        int n = 0;
        std::vector<double> z;
    };
    struct Curve3 {
        std::string tex, label;
        std::vector<std::array<double, 3>> pts;
    };
    std::vector<Surface> surfaces;
    std::vector<Curve3> curves;
    bool is3d() const { return !surfaces.empty() || !curves.empty(); }
    bool equal = false;  // only shapes: x and y on one scale, fitted to mathOpts.plotAspect
    std::string error;
};

// least squares: polynomial coefficients c0…cdeg, or for degree −1 an exponential a·e^(bx) as {a, b}; r2 = R²
std::vector<double> fitCurve(const std::vector<double>& xs, const std::vector<double>& ys, int degree, double& r2);
// view: {x0, x1, y0, y1} instead of the line's own ranges (the app's pan and zoom)
Plot plot(Editor& ed, const Row& row, const double* view = nullptr);
// the document as one HTML file: KaTeX for math, images inlined, plots as SVG (export.cpp)
std::string html(Editor& ed, const std::filesystem::path& base, const std::string& title);
// the document as a LaTeX article (export.cpp); image paths are relative to base
std::string latex(Editor& ed, const std::filesystem::path& base);

// a PDF of whole-page pictures: RGB pixels, w x h, top row first, each page filling A4 (export.cpp)
std::string imagePdf(const std::vector<std::string>& pages, int w, int h);

// JS Number formatting, shared with the tests: String(x) and +x.toPrecision(p)
std::string jsNum(double x);
double toPrecision(double x, int p);

}  // namespace om
