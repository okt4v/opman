// Document model (port of corca core.js/render.js): a document is a list of lines; a line, and every slot inside a
// node, is a row of items. Items are typed chars ({t:'ch', c, m}; m = belongs to math) or nodes with slots.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>

namespace om {

struct Item;
using Row = std::vector<Item>;
using Doc = std::vector<Row>;

enum class T : uint8_t { Ch, Var, Const, Unit, Sym, Fn, Frac, Sup, Sub, Sqrt, Paren, Mat, Big, Lim };

struct Item {
    T t = T::Ch;
    std::string c;       // Ch: the character (UTF-8); Var/Const/Unit/Fn: the name; Sym/Big: the symbol
    std::string l;       // Sym/Big: LaTeX
    bool m = false;      // Ch: belongs to math; Big: limits above/below (∑) rather than beside (∫)
    std::string o, cl;   // Paren/Mat: delimiters
    int cols = 0;        // Mat: columns; cells are the slots, row-major
    std::vector<Row> s;  // slots: frac [a,b], big [lo,hi], mat cells, sup/sub/sqrt/paren/lim [a]
    uint8_t f = 0;       // Ch text: Fmt bits ("f" in JSON; corca ignores the key)
    bool operator==(const Item&) const = default;
};

// inline text formatting
enum Fmt : uint8_t { Bold = 1, Italic = 2, Underline = 4, Strike = 8, Code = 16, Mark = 32 };

// line formatting is a Markdown prefix typed into the line itself ("# ", "- ", "1. ", "> ", "- [ ] "), so notes stay
// plain corca documents; the renderer draws the prefix as a heading size, bullet, number, quote bar or checkbox
enum class Line : uint8_t { Text, H1, H2, H3, Bullet, Number, Quote, Todo, Done };
struct LineStyle {
    Line kind = Line::Text;
    int len = 0;  // items in the prefix
    int num = 0;  // Number: the number typed
};
LineStyle lineStyle(const Row& row);
std::string linePrefix(Line kind, int num = 1);

// constructors
Item ch(std::string c, bool m);
inline Item M(std::string c) { return ch(std::move(c), true); }
Item named(T t, std::string v);  // Var, Const, Unit, Fn
Item sym(std::string s, std::string l);
Item node(T t);                  // Frac, Sup, Sub, Sqrt, Lim with empty slots
Item paren(std::string o, std::string c, Row a = {});
Item big(std::string s, std::string l, bool limits);
Item mat(std::string o, std::string c, std::vector<std::vector<Row>> rows);

// predicates
bool isLetter(const std::string& c);  // ASCII A-Za-z, one char
bool isDigit(const std::string& c);
inline bool isMath(const Item& it) { return it.t != T::Ch || it.m; }
inline bool isSpace(const Item& it) { return it.t == T::Ch && it.c == " "; }
bool isOp(const Item* it);
bool isRel(const Item* it);
const Item* prevNonSpace(const Row& row, int i);
std::vector<bool> mathMask(const Row& row, bool top);
int matRows(const Item& n);

// text forms
extern const std::vector<std::pair<std::string, std::string>> GREEK;  // name, letter
std::string tex(const Row& row);
std::string lineTex(const Row& row, bool top = true);
std::string plain(const Row& row);
std::string docTitle(const Doc& doc);

// JSON, same shape as corca's files: {meta, doc, path}
nlohmann::json toJson(const Row& row);
Row rowFromJson(const nlohmann::json& j);
nlohmann::json docJson(const Doc& doc);
Doc docFromJson(const nlohmann::json& j);

// cursor path: line k, then (item, slot) steps down, then index i
struct Path {
    int k = 0;
    std::vector<std::pair<int, int>> p;
    int i = 0;
};
nlohmann::json pathJson(const Path& p);
Path pathFromJson(const nlohmann::json& j);

// files: one JSON per document in a folder
namespace fs = std::filesystem;
struct FileInfo {
    std::string name;
    nlohmann::json meta;
};
bool validName(const std::string& name);
std::vector<FileInfo> listDocs(const fs::path& dir);
bool readFile(const fs::path& file, nlohmann::json& out);
bool writeFile(const fs::path& file, const nlohmann::json& j);  // atomic: .tmp then rename

}  // namespace om
