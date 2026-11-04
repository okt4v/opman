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

// JS Number formatting, shared with the tests: String(x) and +x.toPrecision(p)
std::string jsNum(double x);
double toPrecision(double x, int p);

}  // namespace om
