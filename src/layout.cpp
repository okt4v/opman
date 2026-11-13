#include "layout.hpp"
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace om {

const Palette DARK = {IM_COL32(0x1b, 0x1b, 0x1d, 255), IM_COL32(0xe8, 0xe8, 0xea, 255), IM_COL32(0x8a, 0x8a, 0x8e, 255),
                      IM_COL32(0xf0, 0xa3, 0x5e, 255), IM_COL32(0x7a, 0xa7, 0xff, 255), IM_COL32(0x34, 0x50, 0x7e, 255),
                      IM_COL32(0x25, 0x63, 0xeb, 40),  IM_COL32(0x22, 0x22, 0x25, 255), IM_COL32(0x3a, 0x3a, 0x3d, 255),
                      IM_COL32(0x2a, 0x2a, 0x2d, 255), 0.70f, IM_COL32(0x12, 0x12, 0x14, 255)};
const Palette LIGHT = {IM_COL32(0xfd, 0xfd, 0xfc, 255), IM_COL32(0x1d, 0x1d, 0x1f, 255), IM_COL32(0x8a, 0x8a, 0x8e, 255),
                       IM_COL32(0xb4, 0x53, 0x0a, 255), IM_COL32(0x25, 0x63, 0xeb, 255), IM_COL32(0xcf, 0xe0, 0xff, 255),
                       IM_COL32(0x25, 0x63, 0xeb, 23),  IM_COL32(0xf6, 0xf5, 0xf3, 255), IM_COL32(0xe5, 0xe5, 0xe5, 255),
                       IM_COL32(0xff, 0xff, 0xff, 255), 0.40f, IM_COL32(0xec, 0xeb, 0xe8, 255)};

static unsigned decode(const std::string& s, size_t& i) {
    unsigned char b = s[i];
    unsigned cp = b, n = b < 0x80 ? 1 : b < 0xE0 ? 2 : b < 0xF0 ? 3 : 4;
    if (n > 1) cp = b & (0xFF >> (n + 1));
    for (unsigned k = 1; k < n && i + k < s.size(); k++) cp = (cp << 6) | (s[i + k] & 0x3F);
    i += n;
    return cp;
}
static std::string encode(unsigned cp) {
    std::string s;
    if (cp < 0x80) s += char(cp);
    else if (cp < 0x800) s += char(0xC0 | cp >> 6), s += char(0x80 | (cp & 0x3F));
    else if (cp < 0x10000) s += char(0xE0 | cp >> 12), s += char(0x80 | ((cp >> 6) & 0x3F)), s += char(0x80 | (cp & 0x3F));
    else s += char(0xF0 | cp >> 18), s += char(0x80 | ((cp >> 12) & 0x3F)), s += char(0x80 | ((cp >> 6) & 0x3F)), s += char(0x80 | (cp & 0x3F));
    return s;
}

// variables are italic: Mathematical Italic letters (MathML <mi> does the same)
static std::string italic(const std::string& v) {
    std::string out;
    for (size_t i = 0; i < v.size();) {
        unsigned cp = decode(v, i);
        if (cp == 'h') cp = 0x210E;
        else if (cp >= 'a' && cp <= 'z') cp = 0x1D44E + cp - 'a';
        else if (cp >= 'A' && cp <= 'Z') cp = 0x1D434 + cp - 'A';
        else if (cp >= 0x3B1 && cp <= 0x3C9) cp = 0x1D6FC + cp - 0x3B1;
        else if (cp >= 0x391 && cp <= 0x3A9) cp = 0x1D6E2 + cp - 0x391;
        out += encode(cp);
    }
    return out;
}

// stable colour per variable name, so the same variable has the same colour everywhere
ImU32 varColor(const std::string& v, float l) {
    unsigned h = 0;
    for (size_t i = 0; i < v.size();) h = (h * 31 + decode(v, i)) % 360;
    float hue = float((h * 137) % 360) / 360.f, s = 0.65f;
    float q = l < 0.5f ? l * (1 + s) : l + s - l * s, p = 2 * l - q;
    auto ch = [&](float t) {
        t = t < 0 ? t + 1 : t > 1 ? t - 1 : t;
        float c = t < 1.f / 6 ? p + (q - p) * 6 * t : t < 0.5f ? q : t < 2.f / 3 ? p + (q - p) * (2.f / 3 - t) * 6 : p;
        return int(c * 255 + 0.5f);
    };
    return IM_COL32(ch(hue + 1.f / 3), ch(hue), ch(hue - 1.f / 3), 255);
}

namespace {

struct Ctx {
    float size;
    bool display;  // display style: fractions keep their size (corca's math-style: normal)
    bool top;      // a top-level line: text and math mixed
    ImU32 col;     // colour override (popup tiles), 0 = natural colours
};

struct RowLay {
    std::vector<float> xs, lsp, w;  // item start (caret stop), left operator space and width, relative to the row
    std::vector<Box> boxes;
    std::vector<bool> mask;
    Box total;
    // a wrapped top-level line: the visual line of each item (and of the end), each one's baseline below the first, its box
    std::vector<int> ln;
    std::vector<float> ly;
    std::vector<Box> lines;
};

const char* REL = "=≠≈≡≔∝<>≤≥≪≫∈∉⊂⊆⊃→←↔↦⇒⇔∥⊥";
const char* BIN = "+-−*⋅×÷±∓∪∩∖∧∨⊕⊗∘";
int opClass(const Item& it) {
    if (!((it.t == T::Ch && it.m) || it.t == T::Sym) || it.c.empty()) return 0;
    if (std::string(REL).find(it.c) != std::string::npos) return 2;
    if (std::string(BIN).find(it.c) != std::string::npos) return 1;
    if (it.t == T::Sym && it.l == "\\mid") return 2;  // {x | x > 0}: spaced like a relation
    return it.c == "," || it.c == ";" ? 3 : 0;
}

struct Impl {
    Layout& L;
    float minSize() const { return L.mathSize * 0.5f; }
    Ctx script(Ctx c) const { return {std::max(c.size * 0.71f, minSize()), false, false, c.col}; }

    // ---------- glyphs ----------
    Box glyphBox(ImFont* f, float size, const std::string& s) {
        return {f->CalcTextSizeA(size, FLT_MAX, 0, s.c_str()).x, 0.72f * size, 0.24f * size};
    }
    void glyph(ImFont* f, float size, const std::string& s, float x, float base, ImU32 col, bool slant = false) {
        if (!L.dl) return;
        int v0 = L.dl->VtxBuffer.Size;
        L.dl->AddText(f, size, {x, base - f->GetFontBaked(size)->Ascent}, col, s.c_str());
        if (slant)  // ponytail: synthetic italic (shear), no italic font file shipped
            for (int v = v0; v < L.dl->VtxBuffer.Size; v++) L.dl->VtxBuffer[v].pos.x += (base - L.dl->VtxBuffer[v].pos.y) * 0.2f;
    }

    // ---------- top-level line style ----------
    struct Top {
        LineStyle ls;
        float ts, scale = 1;  // text size, heading scale
        bool heading = false, header = false, caption = false;
    };
    Top top(const Row& row) {
        Top t{lineStyle(row), L.textSize};
        t.heading = (t.ls.kind >= Line::H1 && t.ls.kind <= Line::H3) || t.ls.kind == Line::Section;
        t.caption = t.ls.kind == Line::Image;
        t.scale = t.ls.kind == Line::H1 ? 1.75f : t.ls.kind == Line::H2 ? 1.4f : t.ls.kind == Line::H3 ? 1.18f
                : t.ls.kind == Line::Section ? 1.1f : t.caption ? 0.85f : 1;
        if (t.ls.kind == Line::Number && L.listNum) t.ls.num = L.listNum;
        t.ts = L.textSize * t.scale;
        t.header = t.ls.kind == Line::Table && L.tableHeader;
        return t;
    }
    ImFont* textFont(const Item& it, const Top& t) const {
        return it.f & Code ? L.mono : (it.f & Bold) || t.heading || t.header ? L.bold : L.text;
    }
    float textSize(const Item& it, const Top& t) const {
        return (it.f & Code ? t.ts * 0.88f : t.ts) * (it.f & Script ? 0.7f : 1) * (it.f & Small ? 0.82f : it.f & Large ? 1.3f : 1);
    }
    float textRise(const Item& it, const Top& t) const { return it.f & Sup ? -0.38f * t.ts : it.f & Sub ? 0.18f * t.ts : 0; }
    static bool pipe(const Item& it, const Top& t) { return t.ls.kind == Line::Table && it.t == T::Ch && !it.m && it.c == "|"; }
    // width of the drawn prefix: bullet, number, quote bar, checkbox; headings show theirs in the margin
    float decoWidth(const Top& t) {
        switch (t.ls.kind) {
        case Line::Bullet: return 1.25f * t.ts;
        case Line::Number: return textWidth(std::string(std::to_string(t.ls.num).size(), '0') + ".", t.ts) + 0.5f * t.ts;
        case Line::Quote: return 1.0f * t.ts;
        case Line::Todo: case Line::Done: return 1.45f * t.ts;
        default: return 0;
        }
    }
    float textWidth(const std::string& s, float size) { return L.text->CalcTextSizeA(size, FLT_MAX, 0, s.c_str()).x; }
    struct Ink {
        float adv, top, bottom;  // advance, ink above / below the baseline
    };
    Ink ink(float size, const std::string& s) {
        size_t i = 0;
        ImFontBaked* b = L.math->GetFontBaked(size);
        ImFontGlyph* g = b->FindGlyph(ImWchar(decode(s, i)));
        if (!g) return {size * 0.6f, size * 0.7f, size * 0.2f};
        return {g->AdvanceX, b->Ascent - g->Y0, g->Y1 - b->Ascent};
    }
    ImU32 col(Ctx c, ImU32 natural) const { return c.col ? c.col : natural; }
    float axis(Ctx c) const { return 0.25f * c.size; }

    // equation tags and references away from the cursor: a tag is hidden (the app numbers the line at the right edge),
    // a reference shows its number. Per item: "" as is, "\x01" hidden, else the text drawn in its place
    std::vector<std::string> eqShow(const Row& row, Ctx c) {
        std::vector<std::string> out;
        if (!c.top || !L.ed || L.ed->topOf(L.ed->cur.row) == &row) return out;
        auto t = eqTag(row);
        auto refs = eqRefs(row);
        if (t.first < 0 && refs.empty()) return out;
        out.resize(row.size());
        if (t.first >= 0)
            for (int j = t.first - 1; j < t.second; j++) out[j] = "\x01";
        for (auto sp : refs) {
            auto f = L.eqNums.find(eqName(row, sp));
            for (int j = sp.first; j < sp.second; j++) out[j] = "\x01";
            out[sp.first] = f == L.eqNums.end() ? "(?)" : "(" + std::to_string(f->second) + ")";
        }
        return out;
    }

    // ---------- rows ----------
    RowLay lay(const Row& row, Ctx c) {
        RowLay r;
        size_t n = row.size();
        r.mask = c.top ? mathMask(row, true) : std::vector<bool>(n, true);
        r.xs.resize(n + 1);
        r.lsp.resize(n);
        r.boxes.resize(n);
        Top t = c.top ? top(row) : Top{};
        Ctx mc = c.top ? Ctx{L.mathSize * t.scale, true, false, c.col} : c;
        float x = 0;
        Box prev{0, 0.72f * mc.size, 0.24f * mc.size};
        r.total = {0, c.top ? 0.75f * t.ts : 0, c.top ? 0.25f * t.ts : 0};
        size_t cell = 0;
        auto eq = eqShow(row, c);
        int tag = c.top ? eqTag(row).first : -1;
        for (size_t j = 0; j < n; j++) {
            r.xs[j] = x;
            const Item& it = row[j];
            Box b;
            float w;
            if (!eq.empty() && !eq[j].empty()) {
                w = eq[j] == "\x01" ? 0 : textWidth(eq[j], t.ts);
                b = {w, 0.72f * t.ts, 0.24f * t.ts};
            } else if (tag >= 0 && int(j) >= tag - 1) {  // the tag while editing: small plain text
                w = isSpace(it) ? 0.3f * t.ts : textWidth(it.c, t.ts * 0.85f);
                b = {w, 0.72f * t.ts, 0.24f * t.ts};
            } else if (c.top && int(j) >= int(n) - t.ls.tail) {  // a picture's "](path)": never shown
                w = 0;
                b = {0, 0.72f * t.ts, 0.24f * t.ts};
            } else if (int(j) < t.ls.len) {  // the line's prefix: drawn as one decoration in its last item's place
                w = int(j) == t.ls.len - 1 ? decoWidth(t) : 0;
                b = {w, 0.72f * t.ts, 0.24f * t.ts};
            } else if (c.top && pipe(it, t)) {  // a cell border: snaps to the column stop, drawn by the app as a grid line
                if (cell < L.tabs.size()) x = r.xs[j] = std::max(x, L.tabs[cell]);
                cell++;
                b = {0.9f * t.ts, 0.72f * t.ts, 0.24f * t.ts};
                w = b.w;
            } else if (!r.mask[j]) {
                b = glyphBox(textFont(it, t), textSize(it, t), it.c);
                b.asc = std::max(b.asc, 0.72f * t.ts);
                w = b.w;
            } else if (isSpace(it)) {
                b = {};  // spaces inside math are not drawn (MathML ignores them)
                w = 0;
            } else {
                auto [ls, rs] = spacing(row, j, r.mask, mc);
                b = item(it, mc, prev, false, 0, 0);
                r.lsp[j] = ls;
                w = ls + b.w + rs;
            }
            r.boxes[j] = b;
            x += w;
            r.total.asc = std::max(r.total.asc, b.asc);
            r.total.desc = std::max(r.total.desc, b.desc);
            if (!isSpace(it)) prev = b;
        }
        r.xs[n] = x;
        if (!n && !c.top) {  // empty slot: placeholder
            Box b = glyphBox(L.math, mc.size, "⬚");
            x = b.w;
            r.total.asc = b.asc;
            r.total.desc = b.desc;
        }
        r.total.w = x;
        r.w.resize(n);
        for (size_t j = 0; j < n; j++) r.w[j] = r.xs[j + 1] - r.xs[j];
        r.ln.assign(n + 1, 0);
        r.ly = {0};
        if (c.top && L.wrapW > 0 && t.ls.kind != Line::Table && (x > L.wrapW || row.align || std::any_of(row.begin(), row.end(), isBreak))) wrap(row, t, r);
        else r.lines = {r.total};
        return r;
    }

    // break a top-level line so it fits L.wrapW: at spaces, before a math operator (+ − · = …) and at a Shift-Enter break.
    // Later lines start under the text, past a bullet or number; one that starts with an operator is indented, as a
    // calculation is written by hand. Then each visual line is aligned in the width
    void wrap(const Row& row, const Top& t, RowLay& r) {
        size_t n = row.size(), first = std::min<size_t>(t.ls.len, n), end = n - t.ls.tail, lineFirst = first;
        float hang = r.xs[first], shift = 0;
        int line = 0;
        bool forced = false;
        auto op = [&](size_t k) { return opClass(row[k]) == 1 || opClass(row[k]) == 2; };
        for (size_t j = first; j < end;) {
            size_t e = j, wend = j;  // the word ends at wend; the spaces after it may hang over the edge
            bool brk = false;
            for (bool more = true; more;) {
                size_t a = e;
                while (e < end && !isSpace(row[e]) && !(e > a && op(e) && !op(e - 1))) e++;
                wend = e;
                while (e < end && isSpace(row[e]) && !isBreak(row[e])) e++;
                if (e < end && isBreak(row[e])) e++, brk = true;
                more = !brk && e < end && wend == a + 1 && op(a) && e > wend;  // "+ " stays with what it adds
            }
            if (forced || (r.xs[wend] - shift > L.wrapW && j > lineFirst))
                shift = r.xs[j] - hang - (op(j) ? 1.5f * t.ts : 0), line++, lineFirst = j;
            forced = brk;
            for (size_t k = j; k < e; k++) r.ln[k] = line, r.xs[k] -= shift;
            j = e;
        }
        for (size_t k = end; k <= n; k++) r.ln[k] = line, r.xs[k] -= shift;
        r.lines.assign(line + 1, Box{0, 0.75f * t.ts, 0.25f * t.ts});
        r.lines[line].w = r.xs[n];
        for (size_t k = 0; k < n; k++) {
            Box& lb = r.lines[r.ln[k]];
            lb.asc = std::max(lb.asc, r.boxes[k].asc);
            lb.desc = std::max(lb.desc, r.boxes[k].desc);
            if (!isSpace(row[k])) lb.w = std::max(lb.w, r.xs[k] + r.w[k]);
        }
        r.ly.resize(line + 1);
        for (int i = 1; i <= line; i++) r.ly[i] = r.ly[i - 1] + r.lines[i - 1].desc + 0.3f * t.ts + r.lines[i].asc;
        r.total = {0, r.lines[0].asc, r.ly[line] + r.lines[line].desc};
        for (int i = 0; i <= line; i++) {
            float off = row.align ? std::max(0.f, L.wrapW - r.lines[i].w) * (row.align == 1 ? 0.5f : 1.f) : 0;
            for (size_t k = 0; k <= n && off > 0; k++)
                if (r.ln[k] == i) r.xs[k] += off;
            r.total.w = std::max(r.total.w, r.lines[i].w + off);
        }
    }

    // operator spacing like MathML's operator dictionary; none in scripts, none for a prefix +/−
    std::pair<float, float> spacing(const Row& row, size_t j, const std::vector<bool>& mask, Ctx c) {
        if (isDecimalComma(row, j)) return {0, 0};  // 2,5
        if (c.size < L.mathSize * 0.9f) return {0, 0};
        const Item& it = row[j];
        float em = c.size;
        if (it.t == T::Fn) {
            size_t k = j + 1;
            while (k < row.size() && isSpace(row[k])) k++;
            return {0, k < row.size() && row[k].t == T::Paren ? 0 : 0.1667f * em};
        }
        if (it.t == T::Big || it.t == T::Lim) return {0, 0.1667f * em};
        if (isStepBar(it)) return {1.5f * em, 0.3f * em};  // 2x = 4    | :2
        int cls = opClass(it);
        if (cls == 2) return {0.2778f * em, 0.2778f * em};
        if (cls == 3) return {0, 0.1667f * em};
        if (cls == 1) {
            int p = int(j) - 1;
            while (p >= 0 && isSpace(row[p])) p--;
            bool prefix = p < 0 || !mask[p] || opClass(row[p]) != 0 || isStepBar(row[p]);  // "| −1": a sign
            return prefix ? std::pair{0.f, 0.f} : std::pair{0.2222f * em, 0.2222f * em};
        }
        return {0, 0};
    }

    Box rowBox(const Row& row, Ctx c) { return lay(row, c).total; }

    void drawRow(const Row& row, Ctx c, float x, float base) {
        RowLay r = lay(row, c);
        const Editor* ed = L.ed;
        float y0 = base - r.total.asc, y1 = base + r.total.desc;
        auto top0 = [&](size_t j) { return base + r.ly[r.ln[j]] - r.lines[r.ln[j]].asc; };  // the visual line item j is on
        auto bot0 = [&](size_t j) { return base + r.ly[r.ln[j]] + r.lines[r.ln[j]].desc; };
        if (ed && L.dl) {
            if (ed->cur.row == &row && !c.top) L.dl->AddRectFilled({x - 1, y0}, {x + r.total.w + 1, y1}, L.pal->active, 3);
            if (auto s = ed->selOn(row)) {
                if (row.empty()) L.dl->AddRectFilled({x + r.xs[0], y0}, {x + r.xs[0] + 6, y1}, L.pal->sel);  // an empty line in a selection still shows
                for (int j = s->first; j < s->second; j++) L.dl->AddRectFilled({x + r.xs[j], top0(j)}, {x + r.xs[j] + r.w[j], bot0(j)}, L.pal->sel);
            }
        }
        for (size_t j = 0; j <= row.size(); j++) L.stops.push_back({&row, int(j), x + r.xs[j], top0(j), bot0(j)});
        if (c.top) L.endX = r.xs[row.size()], L.endY = r.ly[r.ln[row.size()]];
        if (row.empty() && !c.top) {
            ImU32 m = L.pal->muted;
            glyph(L.math, c.size, "⬚", x, base, (m & 0x00FFFFFF) | (0x80u << IM_COL32_A_SHIFT));
            return;
        }
        Top t = c.top ? top(row) : Top{};
        Ctx mc = c.top ? Ctx{L.mathSize * t.scale, true, false, c.col} : c;
        if (c.top && L.dl) decorate(row, t, r, x + r.xs[0], base, y0, y1);
        bool done = t.ls.kind == Line::Done;
        size_t shown = c.top ? row.size() - t.ls.tail : row.size();
        if (c.top && L.dl && !L.placeholder.empty() && int(shown) <= t.ls.len)
        {
            ImFont* pf = t.heading ? L.bold : L.text;
            float pw = pf->CalcTextSizeA(t.ts, FLT_MAX, 0, L.placeholder.c_str()).x;
            glyph(pf, t.ts, L.placeholder, x + r.xs[shown] - pw * (row.align == 1 ? 0.5f : row.align == 2 ? 1.f : 0.f), base,
                  (L.pal->muted & 0x00FFFFFF) | (0x90u << IM_COL32_A_SHIFT));
        }
        // text backgrounds (code, highlight) first, so they never cover a neighbouring glyph
        for (size_t j = t.ls.len; j < shown && L.dl; j++) {
            const Item& it = row[j];
            if (r.mask[j] || !(it.f & (Code | Mark))) continue;
            ImU32 bg = it.f & Mark ? IM_COL32(250, 204, 21, 90) : L.pal->line;
            float by = base + r.ly[r.ln[j]];
            L.dl->AddRectFilled({x + r.xs[j], by - 0.8f * t.ts}, {x + r.xs[j] + r.w[j], by + 0.3f * t.ts}, bg);
        }
        Box prev{0, 0.72f * mc.size, 0.24f * mc.size};
        auto links = c.top ? linkSpans(row) : std::vector<std::pair<int, int>>{};
        auto eq = eqShow(row, c);
        auto refs = c.top ? eqRefs(row) : std::vector<std::pair<int, int>>{};
        auto tag = c.top ? eqTag(row) : std::pair{-1, -1};
        int bar = c.top ? stepBar(row) : -1;
        const float base0 = base;
        for (size_t j = t.ls.len; j < shown; j++) {
            const Item& it = row[j];
            const float base = base0 + r.ly[r.ln[j]];
            if (c.top && pipe(it, t)) continue;
            if (!eq.empty() && !eq[j].empty()) {
                if (eq[j] != "\x01") glyph(L.text, t.ts, eq[j], x + r.xs[j], base, col(c, L.pal->fn));
                continue;
            }
            if (int(j) >= tag.first - 1 && tag.first >= 0) {  // the tag, while editing its line
                if (!isSpace(it)) glyph(L.text, t.ts * 0.85f, it.c, x + r.xs[j], base, col(c, L.pal->muted));
                continue;
            }
            bool ref = std::any_of(refs.begin(), refs.end(), [&](auto sp) { return int(j) >= sp.first && int(j) < sp.second; });
            if (ref) {
                glyph(L.text, t.ts, it.c, x + r.xs[j], base, col(c, L.pal->fn));
                continue;
            }
            int link = 0;  // 1: a link's title, 2: its brackets
            for (auto [a, b] : links)
                if (int(j) >= a && int(j) < b) link = int(j) < a + 2 || int(j) >= b - 2 ? 2 : 1;
            if (!r.mask[j]) {
                float gx = x + r.xs[j], gw = r.w[j], th = std::max(1.f, 0.065f * t.ts);
                unsigned rgb = fmtColor(it.f);
                ImU32 hue = rgb ? IM_COL32(rgb >> 16, (rgb >> 8) & 0xFF, rgb & 0xFF, 255) : t.caption ? L.pal->muted : L.pal->fg;
                if (link) hue = link == 1 ? L.pal->fn : L.pal->muted;
                ImU32 tc = col(c, done ? L.pal->muted : it.f & Code ? L.pal->constant : hue);
                glyph(textFont(it, t), textSize(it, t), it.c, gx, base + textRise(it, t), tc, it.f & Italic);
                if (L.dl && ((it.f & Underline) || link == 1)) L.dl->AddRectFilled({gx, base + 0.1f * t.ts}, {gx + gw, base + 0.1f * t.ts + th}, tc);
                if (L.dl && ((it.f & Strike) || done)) L.dl->AddRectFilled({gx, base - 0.3f * t.ts}, {gx + gw, base - 0.3f * t.ts + th}, tc);
            } else if (!isSpace(it)) {
                bool step = c.top && bar >= 0 && int(j) >= bar;  // a transformation step: quieter than the equation
                item(it, step ? Ctx{mc.size, mc.display, mc.top, col(c, L.pal->muted)} : mc, prev, true, x + r.xs[j] + r.lsp[j], base);
            }
            if (!isSpace(it)) prev = r.boxes[j];
        }
    }

    void decorate(const Row& row, const Top& t, const RowLay& r, float x, float base, float y0, float y1) {
        const Palette& p = *L.pal;
        float s = t.ts;
        switch (t.ls.kind) {
        case Line::Bullet: {  // a dot, a ring, a square: one per nesting level
            ImVec2 c{x + 0.55f * s, base - 0.3f * s};
            float q = 0.11f * s;
            if (row.indent % 3 == 0) L.dl->AddCircleFilled(c, q, p.muted);
            else if (row.indent % 3 == 1) L.dl->AddCircle(c, q, p.muted, 0, std::max(1.f, 0.06f * s));
            else L.dl->AddRectFilled({c.x - q, c.y - q}, {c.x + q, c.y + q}, p.muted);
            break;
        }
        case Line::Number: glyph(L.text, s, std::to_string(t.ls.num) + ".", x + 0.1f * s, base, p.muted); break;
        case Line::Quote: L.dl->AddRectFilled({x + 0.2f * s, y0}, {x + 0.2f * s + 3, y1}, p.line, 1.5f); break;
        case Line::Todo: case Line::Done: {
            float b = 0.8f * s, bx = x + 0.15f * s, by = base - 0.72f * s;
            if (t.ls.kind == Line::Done) {
                L.dl->AddRectFilled({bx, by}, {bx + b, by + b}, p.fn, 4);
                ImVec2 ck[] = {{bx + 0.22f * b, by + 0.52f * b}, {bx + 0.42f * b, by + 0.72f * b}, {bx + 0.78f * b, by + 0.3f * b}};
                L.dl->AddPolyline(ck, 3, p.bg, 0, std::max(1.5f, 0.1f * b));
            } else L.dl->AddRect({bx, by}, {bx + b, by + b}, p.muted, 4, 0, 1.5f);
            break;
        }
        default: break;
        }
        (void)r;
    }

    // ---------- delimiters ----------
    // a delimiter around content with ascent/descent; stretches symmetrically around the math axis
    Box delim(const std::string& d, Box content, Ctx c, bool draw, float x, float base) {
        if (d.empty()) return {};
        float s = c.size, ax = axis(c);
        float e = std::max({content.asc - ax, content.desc + ax, 0.5f * s}) + 0.06f * s;
        Box b{0, ax + e, e - ax};
        float top = base - b.asc, bot = base + b.desc, h = bot - top, th = std::max(1.2f, 0.06f * s);
        bool curve = d == "(" || d == ")", square = d == "[" || d == "]";
        bool bar = d == "|" || d == "‖", flc = d == "⌊" || d == "⌋" || d == "⌈" || d == "⌉";
        if (curve) b.w = 0.28f * s + 0.04f * h;
        else if (square || flc) b.w = 0.32f * s;
        else if (d == "|") b.w = 0.24f * s;
        else if (d == "‖") b.w = 0.36f * s;
        else {  // { } and anything else: the glyph, scaled to the height
            Ink k = ink(s, d);
            float inkH = std::max(k.top + k.bottom, 1.f), gs = std::max(s, s * h / inkH);
            Ink g = ink(gs, d);
            b.w = g.adv;
            if (draw) glyph(L.math, gs, d, x, base - ax + (g.top - g.bottom) / 2, col(c, L.pal->fg));
            return b;
        }
        if (!draw || !L.dl) return b;
        ImU32 cl = col(c, L.pal->fg);
        auto line = [&](float x0, float y0, float x1, float y1) { L.dl->AddLine({x0, y0}, {x1, y1}, cl, th); };
        float w = b.w, l = x + w * 0.25f, r = x + w * 0.75f;
        if (d == "(" || d == ")") {
            bool open = d == "(";
            float xe = open ? x + w * 0.85f : x + w * 0.15f, xc = open ? x - w * 0.15f : x + w * 1.15f;
            L.dl->PathLineTo({xe, top});
            L.dl->PathBezierQuadraticCurveTo({xc, (top + bot) / 2}, {xe, bot}, 24);
            L.dl->PathStroke(cl, 0, th);
        } else if (d == "[") line(r, top, l, top), line(l, top, l, bot), line(l, bot, r, bot);
        else if (d == "]") line(l, top, r, top), line(r, top, r, bot), line(r, bot, l, bot);
        else if (d == "|") line(x + w / 2, top, x + w / 2, bot);
        else if (d == "‖") line(x + w * 0.35f, top, x + w * 0.35f, bot), line(x + w * 0.65f, top, x + w * 0.65f, bot);
        else if (d == "⌊") line(l, top, l, bot), line(l, bot, r, bot);
        else if (d == "⌋") line(r, top, r, bot), line(r, bot, l, bot);
        else if (d == "⌈") line(l, bot, l, top), line(l, top, r, top);
        else if (d == "⌉") line(r, bot, r, top), line(r, top, l, top);
        (void)bar;
        return b;
    }

    // ---------- items ----------
    Box item(const Item& it, Ctx c, Box prev, bool draw, float x, float base) {
        float s = c.size;
        auto text = [&](ImFont* f, const std::string& str, ImU32 natural) {
            Box b = glyphBox(f, s, str);
            if (draw) glyph(f, s, str, x, base, col(c, natural));
            return b;
        };
        switch (it.t) {
        case T::Ch: {
            if (isLetter(it.c)) return text(L.math, italic(it.c), varColor(it.c, L.pal->varL));
            std::string g = it.c == "*" ? "⋅" : it.c == "-" ? "−" : it.c;
            return text(L.math, g, L.pal->fg);
        }
        case T::Var: return text(L.math, italic(it.c), varColor(it.c, L.pal->varL));
        case T::Const: return text(L.math, it.c, L.pal->constant);
        case T::Unit: return text(L.math, it.c, L.pal->muted);
        case T::Sym: return text(L.math, it.c, L.pal->fg);
        case T::Fn: return text(L.math, it.c, L.pal->fn);
        case T::Frac: {
            Ctx cc = c.display ? Ctx{s, false, false, c.col} : script(c);
            Box a = rowBox(it.s[0], cc), b = rowBox(it.s[1], cc);
            float pad = 0.1f * s, t = std::max(1.f, 0.055f * s), gap = 0.14f * s, ax = axis(c);
            Box r{std::max(a.w, b.w) + 2 * pad, ax + t / 2 + gap + a.desc + a.asc, t / 2 + gap + b.asc + b.desc - ax};
            if (draw) {
                float ya = base - ax;
                if (L.dl) L.dl->AddRectFilled({x, ya - t / 2}, {x + r.w, ya + t / 2}, col(c, L.pal->fg));
                drawRow(it.s[0], cc, x + (r.w - a.w) / 2, ya - t / 2 - gap - a.desc);
                drawRow(it.s[1], cc, x + (r.w - b.w) / 2, ya + t / 2 + gap + b.asc);
            }
            return r;
        }
        case T::Sup: case T::Sub: {
            Ctx cc = script(c);
            Box k = rowBox(it.s[0], cc);
            bool up = it.t == T::Sup;
            float shift = up ? std::max(0.42f * s, prev.asc - 0.6f * k.asc) : std::max(0.2f * s, prev.desc - 0.3f * k.asc);
            Box r = up ? Box{k.w + 0.05f * s, shift + k.asc, std::max(0.f, k.desc - shift)}
                       : Box{k.w + 0.05f * s, std::max(0.f, k.asc - shift), shift + k.desc};
            if (draw) drawRow(it.s[0], cc, x, up ? base - shift : base + shift);
            return r;
        }
        case T::Sqrt: {
            Box k = rowBox(it.s[0], c);
            float gap = 0.12f * s, t = std::max(1.f, 0.055f * s), wl = 0.6f * s;
            Box r{wl + k.w + 0.1f * s, k.asc + gap + t, k.desc + 0.12f * s};
            if (draw) {
                float top = base - r.asc + t / 2, bot = base + r.desc;
                if (L.dl) {
                    ImU32 cl = col(c, L.pal->fg);
                    ImVec2 pts[] = {{x + 0.05f * s, base - 0.28f * s}, {x + 0.17f * s, base - 0.36f * s}, {x + 0.33f * s, bot},
                                    {x + wl - 0.04f * s, top}, {x + r.w, top}};
                    L.dl->AddPolyline(pts, 5, cl, 0, t);
                }
                drawRow(it.s[0], c, x + wl, base);
            }
            return r;
        }
        case T::Paren: {
            Box k = rowBox(it.s[0], c);
            float pad = 0.04f * s;
            Box l = delim(it.o, k, c, false, 0, 0), rr = delim(it.cl, k, c, false, 0, 0);
            Box r{l.w + pad + k.w + pad + rr.w, std::max({k.asc, l.asc, rr.asc}), std::max({k.desc, l.desc, rr.desc})};
            if (draw) {
                delim(it.o, k, c, true, x, base);
                drawRow(it.s[0], c, x + l.w + pad, base);
                delim(it.cl, k, c, true, x + l.w + 2 * pad + k.w, base);
            }
            return r;
        }
        case T::Mat: {
            Ctx cc{s, false, false, c.col};
            int rows = matRows(it), cols = it.cols;
            std::vector<Box> cell(it.s.size());
            std::vector<float> cw(cols, 0), ra(rows, 0), rd(rows, 0);
            for (int i = 0; i < rows; i++)
                for (int j = 0; j < cols; j++) {
                    Box b = cell[i * cols + j] = rowBox(it.s[i * cols + j], cc);
                    cw[j] = std::max(cw[j], b.w);
                    ra[i] = std::max(ra[i], b.asc);
                    rd[i] = std::max(rd[i], b.desc);
                }
            float rowGap = 0.3f * s, colGap = 0.8f * s, H = 0, W = 0, ax = axis(c);
            for (int i = 0; i < rows; i++) H += ra[i] + rd[i] + (i ? rowGap : 0);
            for (int j = 0; j < cols; j++) W += cw[j] + (j ? colGap : 0);
            Box inner{W + 0.2f * s, ax + H / 2, H / 2 - ax};
            Box l = delim(it.o, inner, c, false, 0, 0), rr = delim(it.cl, inner, c, false, 0, 0);
            Box r{l.w + inner.w + rr.w, std::max(inner.asc, l.asc), std::max(inner.desc, l.desc)};
            if (draw) {
                delim(it.o, inner, c, true, x, base);
                float y = base - inner.asc;
                for (int i = 0; i < rows; i++) {
                    float cx = x + l.w + 0.1f * s;
                    for (int j = 0; j < cols; j++) {
                        drawRow(it.s[i * cols + j], cc, cx + (cw[j] - cell[i * cols + j].w) / 2, y + ra[i]);
                        cx += cw[j] + colGap;
                    }
                    y += ra[i] + rd[i] + rowGap;
                }
                delim(it.cl, inner, c, true, x + l.w + inner.w, base);
            }
            return r;
        }
        case T::Big: {
            Ctx cc = script(c);
            bool limits = it.m;
            float opSize = limits ? (c.display ? 1.45f : 1.1f) * s : (c.display ? 1.8f : 1.3f) * s, ax = axis(c), gap = 0.1f * s;
            Ink k = ink(opSize, it.c);
            float opAsc = ax + (k.top + k.bottom) / 2, opDesc = (k.top + k.bottom) / 2 - ax;
            float opBase = base - ax + (k.top - k.bottom) / 2;
            Box lo = rowBox(it.s[0], cc), hi = rowBox(it.s[1], cc);
            if (limits) {
                float w = std::max({k.adv, lo.w, hi.w});
                Box r{w, opAsc + gap + hi.desc + hi.asc, opDesc + gap + lo.asc + lo.desc};
                if (draw) {
                    glyph(L.math, opSize, it.c, x + (w - k.adv) / 2, opBase, col(c, L.pal->fg));
                    drawRow(it.s[1], cc, x + (w - hi.w) / 2, base - opAsc - gap - hi.desc);
                    drawRow(it.s[0], cc, x + (w - lo.w) / 2, base + opDesc + gap + lo.asc);
                }
                return r;
            }
            float hiBase = base - opAsc + hi.asc, loBase = base + opDesc - lo.desc;
            float hx = x + k.adv + 0.02f * s, lx = x + k.adv - 0.12f * s;
            Box r{std::max(hx + hi.w, lx + lo.w) - x + 0.05f * s, std::max(opAsc, base - hiBase + hi.asc), std::max(opDesc, loBase - base + lo.desc)};
            if (draw) {
                glyph(L.math, opSize, it.c, x, opBase, col(c, L.pal->fg));
                drawRow(it.s[1], cc, hx, hiBase);
                drawRow(it.s[0], cc, lx, loBase);
            }
            return r;
        }
        case T::Lim: {
            Ctx cc = script(c);
            Box l = glyphBox(L.math, s, "lim"), a = rowBox(it.s[0], cc);
            float gap = 0.08f * s, w = std::max(l.w, a.w);
            Box r{w, l.asc, l.desc + gap + a.asc + a.desc};
            if (draw) {
                glyph(L.math, s, "lim", x + (w - l.w) / 2, base, col(c, L.pal->fg));
                drawRow(it.s[0], cc, x + (w - a.w) / 2, base + l.desc + gap + a.asc);
            }
            return r;
        }
        }
        return {};
    }
};

}  // namespace

Box Layout::measureLine(const Row& row) {
    ImDrawList* keep = dl;
    dl = nullptr;
    Box b = Impl{*this}.rowBox(row, {mathSize, true, true, 0});
    dl = keep;
    return b;
}
void Layout::drawLine(const Row& row, float x, float base) { Impl{*this}.drawRow(row, {mathSize, true, true, 0}, x, base); }
std::vector<float> Layout::pipeXs(const Row& row) {
    auto keep = std::move(tabs);
    tabs.clear();
    RowLay r = Impl{*this}.lay(row, {mathSize, true, true, 0});
    tabs = std::move(keep);
    std::vector<float> out;
    for (size_t j = 0; j < row.size(); j++)
        if (row[j].t == T::Ch && !row[j].m && row[j].c == "|") out.push_back(r.xs[j]);
    return out;
}
Box Layout::measureMath(const Row& row, float size) { return Impl{*this}.rowBox(row, {size, true, false, 0}); }
void Layout::drawMath(const Row& row, float x, float base, float size, ImU32 c) { Impl{*this}.drawRow(row, {size, true, false, c}, x, base); }
float Layout::textWidth(const std::string& s, float size) { return text->CalcTextSizeA(size, FLT_MAX, 0, s.c_str()).x; }
void Layout::drawText(const std::string& s, float x, float base, float size, ImU32 col, ImFont* f, bool slant) {
    Impl{*this}.glyph(f ? f : text, size, s, x, base, col, slant);
}

}  // namespace om
