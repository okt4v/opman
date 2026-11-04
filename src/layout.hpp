// Math typesetting with ImGui's draw lists (stands in for corca's MathML): boxes with ascent/descent around a
// baseline, STIX Two Math glyphs, fractions, scripts, radicals, stretchy delimiters, big operators, matrices.
// Drawing records caret stops, which serve both for drawing the caret and for mouse hit-testing.
#pragma once
#include "edit.hpp"
#include <imgui.h>

namespace om {

struct Palette {
    ImU32 bg, fg, muted, constant, fn, sel, active, side, line, pop;
    float varL;  // lightness of variable colours
};
extern const Palette DARK, LIGHT;

struct Box {
    float w = 0, asc = 0, desc = 0;
};

struct Stop {
    const Row* row;
    int i;
    float x, y0, y1;
};

struct Layout {
    ImFont* text = nullptr;
    ImFont* math = nullptr;
    ImFont* bold = nullptr;  // bold text, headings
    ImFont* mono = nullptr;  // inline code
    float textSize = 18, mathSize = 20.7f;  // corca: 18px body, math at 1.15em
    const Palette* pal = &DARK;
    const Editor* ed = nullptr;  // selection and active-slot highlight; null for previews
    ImDrawList* dl = nullptr;
    std::vector<Stop> stops;     // filled by draw calls

    // a top-level line: text with math runs
    Box measureLine(const Row& row);
    void drawLine(const Row& row, float x, float base);
    // a row that is all math (popup previews)
    Box measureMath(const Row& row, float size);
    void drawMath(const Row& row, float x, float base, float size, ImU32 col = 0);
    // plain text at the text size
    float textWidth(const std::string& s, float size);
    void drawText(const std::string& s, float x, float base, float size, ImU32 col, ImFont* f = nullptr, bool slant = false);
};

ImU32 varColor(const std::string& v, float lightness);

}  // namespace om
