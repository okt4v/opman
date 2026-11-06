// opman: window, document column, popup, sidebar, files. The editing model lives in edit/math/vi, the typesetting
// in layout. Dev hooks: OPMAN_DOCS=<dir> documents folder, OPMAN_KEYS="<vi keys>" typed at start,
// OPMAN_SHOT=<file.ppm> saves a screenshot after OPMAN_SHOT_AFTER seconds (default 1.5) and quits.
#include "doc.hpp"
#include "layout.hpp"
#include "math.hpp"
#include "vi.hpp"
#include "dialog.hpp"
#include "help.hpp"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <array>
#include <bit>
#include <fstream>
#include <functional>
#include <future>
#include <map>
#include <random>
#include <thread>
#include <unordered_map>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_WINDOWS_UTF8
#include <stb_image.h>
#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

using namespace om;
using nlohmann::json;

// ---------- paths (from opcan) ----------
static std::string exeDir() {
#ifdef _WIN32
    wchar_t buf[32768];
    DWORD n = GetModuleFileNameW(nullptr, buf, 32768);
    if (n == 0 || n >= 32768) return ".";
    return fs::path(std::wstring(buf, n)).parent_path().string();
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buf(size, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) != 0) return ".";
    std::error_code ec;
    auto p = fs::canonical(buf.c_str(), ec);
    return (ec ? fs::path(buf.c_str()) : p).parent_path().string();
#else
    std::error_code ec;
    auto p = fs::read_symlink("/proc/self/exe", ec);
    return ec ? "." : p.parent_path().string();
#endif
}

// assets next to the exe (dev build, Windows), <prefix>/share/opman (Linux), Contents/Resources (macOS)
static std::string resDir() {
    fs::path exe = exeDir();
    std::error_code ec;
    for (fs::path d : {exe, exe / "../share/opman", exe / "../Resources"})
        if (fs::is_directory(d / "assets", ec)) return d.lexically_normal().string();
    return exe.string();
}

static fs::path home() {
#ifdef _WIN32
    if (const wchar_t* h = _wgetenv(L"USERPROFILE")) return h;
#else
    if (const char* h = std::getenv("HOME")) return h;
#endif
    return ".";
}

static fs::path configDir() {
    fs::path p;
#ifdef _WIN32
    if (const wchar_t* a = _wgetenv(L"APPDATA")) p = fs::path(a) / "opman";
#elif defined(__APPLE__)
    p = home() / "Library/Application Support/opman";
#else
    if (const char* x = std::getenv("XDG_CONFIG_HOME"); x && *x) p = fs::path(x) / "opman";
    else p = home() / ".config/opman";
#endif
    std::error_code ec;
    fs::create_directories(p, ec);
    return p;
}

// ---------- time ----------
static std::string nowIso() {
    using namespace std::chrono;
    auto now = system_clock::now();
    std::time_t t = system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[40];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%S", &tm);
    int ms = int(duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000);
    return std::string(buf) + "." + std::to_string(1000 + ms).substr(1) + "Z";
}
// seconds since the epoch of an ISO time (UTC); days from civil, H. Hinnant's algorithm
static double isoSeconds(const std::string& iso) {
    int y, m, d, H = 0, M = 0, S = 0;
    if (std::sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d", &y, &m, &d, &H, &M, &S) < 3) return 0;
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400, yoe = y - era * 400, doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long days = long(era) * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
    return days * 86400.0 + H * 3600 + M * 60 + S;
}
static std::string ago(const std::string& iso) {
    if (iso.empty()) return "";
    double s = double(std::time(nullptr)) - isoSeconds(iso);
    if (s < 60) return "just now";
    if (s < 3600) return std::to_string(int(s / 60)) + " min ago";
    if (s < 86400) return std::to_string(int(s / 3600)) + " h ago";
    if (s < 7 * 86400) return std::to_string(int(s / 86400)) + " d ago";
    return iso.substr(0, 10);
}
static double nowSec() { return glfwGetTime(); }

// ---------- keyboard: GLFW callbacks keep keys and typed characters in order (ImGui chains them) ----------
static std::vector<Key> g_keys;

static std::string utf8(unsigned cp) {
    std::string s;
    if (cp < 0x80) s += char(cp);
    else if (cp < 0x800) s += char(0xC0 | cp >> 6), s += char(0x80 | (cp & 0x3F));
    else if (cp < 0x10000) s += char(0xE0 | cp >> 12), s += char(0x80 | ((cp >> 6) & 0x3F)), s += char(0x80 | (cp & 0x3F));
    else s += char(0xF0 | cp >> 18), s += char(0x80 | ((cp >> 12) & 0x3F)), s += char(0x80 | ((cp >> 6) & 0x3F)), s += char(0x80 | (cp & 0x3F));
    return s;
}

static bool g_capsEscape = true;  // setting: CapsLock remapped to Escape (caps:escape) works as Escape
static void onKey(GLFWwindow*, int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_CAPS_LOCK) {
        if (!g_capsEscape) return;
        // xkb "caps:escape" (sway, GNOME, KDE) remaps the keysym, but GLFW reports the physical key. A real CapsLock
        // flips the lock state, which shows in the mods of its press (turning off) or release (turning on); a key
        // that never touches the lock is an Escape. Fires on release, which is not noticeable.
        static bool wasLocked = false;
        if (action == GLFW_PRESS) wasLocked = mods & GLFW_MOD_CAPS_LOCK;
        else if (action == GLFW_RELEASE && !wasLocked && !(mods & GLFW_MOD_CAPS_LOCK)) {
            Key k;
            k.sp = Key::Esc;
            g_keys.push_back(k);
        }
        return;
    }
    if (action == GLFW_RELEASE) return;
    bool ctrl = mods & GLFW_MOD_CONTROL, alt = mods & GLFW_MOD_ALT;
#ifdef __APPLE__
    ctrl = ctrl || (mods & GLFW_MOD_SUPER);
#endif
    Key k;
    k.shift = mods & GLFW_MOD_SHIFT;
    switch (key) {
    case GLFW_KEY_ESCAPE: k.sp = Key::Esc; break;
    case GLFW_KEY_ENTER: case GLFW_KEY_KP_ENTER: k.sp = Key::Enter; break;
    case GLFW_KEY_TAB: k.sp = Key::Tab; break;
    case GLFW_KEY_BACKSPACE: k.sp = Key::Backspace; break;
    case GLFW_KEY_DELETE: k.sp = Key::Delete; break;
    case GLFW_KEY_LEFT: k.sp = Key::Left; break;
    case GLFW_KEY_RIGHT: k.sp = Key::Right; break;
    case GLFW_KEY_UP: k.sp = Key::Up; break;
    case GLFW_KEY_DOWN: k.sp = Key::Down; break;
    case GLFW_KEY_HOME: k.sp = Key::Home; break;
    case GLFW_KEY_END: k.sp = Key::End; break;
    case GLFW_KEY_F1: k.sp = Key::F1; break;
    default: {
        if (!ctrl || alt) return;  // typed characters come through onChar; Ctrl+Alt is AltGr on Windows
        const char* name = glfwGetKeyName(key, scancode);  // the layout's letter, so Ctrl-z is z on every layout
        std::string s = name ? name : "";
        if (key == GLFW_KEY_SPACE) s = " ";  // Ctrl-Space: open the suggestions
        if (s.size() != 1) {
            if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) s = std::string(1, char('a' + key - GLFW_KEY_A));
            else if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9) s = std::string(1, char('0' + key - GLFW_KEY_0));
            else return;
        }
        if (s != " ") s[0] = char(std::tolower((unsigned char)s[0]));
        if (s == "[") k.sp = Key::Esc;  // Ctrl-[ is Esc, as in vim
        else k.ch = s, k.ctrl = true;
    }
    }
    if (ctrl && k.sp != Key::None && k.sp != Key::Esc) k.ctrl = true;
    g_keys.push_back(k);
}

static std::vector<std::string> g_drops;  // files dropped on the window
static void onDrop(GLFWwindow*, int n, const char** paths) {
    for (int i = 0; i < n; i++) g_drops.push_back(paths[i]);
}

static void onChar(GLFWwindow* w, unsigned cp) {
    if (cp < 32 || cp == 127) return;
    Key k;
    k.ch = utf8(cp);
    k.shift = glfwGetKey(w, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(w, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
    g_keys.push_back(k);
}

// ---------- app ----------
struct App {
    Editor ed;
    Vi vi;
    Layout L;
    ImFont *ui = nullptr, *bold = nullptr;
    fs::path dir, cfgFile = configDir() / "config.json";
    json cfg = json::object();
    std::vector<FileInfo> files;
    std::string current;  // open file name
    json meta = json::object();
    std::string savedJson, lastJson, message;
    std::vector<std::optional<Ghost>> ghosts;
    std::vector<std::optional<Plot>> plots;  // per line, recomputed on edits like the ghosts
    struct Tex {
        ImTextureID id = 0;
        int w = 0, h = 0;
    };
    std::map<std::string, Tex> texs;  // ponytail: images are loaded once and kept for the session
    struct LineCache {
        std::optional<Ghost> ghost;
        std::optional<Plot> plot;
    };
    std::unordered_map<size_t, LineCache> lineCache;  // by (section content, line): an edit recomputes only its section
    std::map<int, std::array<double, 4>> views;       // panned/zoomed plots by line: x0 x1 y0 y1 (ponytail: by index)
    std::map<int, Plot> viewPlots;
    std::vector<std::pair<int, std::string>> plotLines;  // plot lines at the last edit, so views and turns can follow them
    std::future<int> pdfJob;                          // pdflatex in the background
    fs::path pdfOut, pdfTmp;
    bool printing = false;  // the running PDF export is for :print
    int pdfEngine = 0;      // 0 pdflatex when it's installed, else built-in; 1 pdflatex; 2 built-in
    bool pdfWanted = false; // the main loop renders the built-in PDF to pdfOut after this frame
    bool printMode = false; // drawing a PDF page: no caret, results or selection
    float printTop = 48;    // the page's first line, in document coordinates (the first line is at 48)
    std::vector<float> lineStarts;  // document y of each line, drawn in print mode: where pages can break
    std::vector<std::string> history;                 // notes opened before, for Ctrl-o
    bool goingBack = false;
    mutable std::map<std::string, std::pair<fs::file_time_type, std::string>> texts;  // lowercase text per note
    std::vector<FileInfo> backlinks;                  // notes with a [[link]] to the open one
    double sliderV0 = 0;
    float sliderX0 = 0;
    double changedAt = -1;
    bool sidebar = true, sideFocus = false, focusFilter = false, dark = true, quit = false, noSave = false;
    bool scrollToCaret = true, dragging = false;
    int sideSel = 0, sort = 0, sideTab = 0;  // sideTab: 0 notes, 1 outline
    bool showHelp = false, showSettings = false, showPalette = false;
    std::string helpTopic;
    char paletteQuery[128] = "", helpQuery[128] = "";
    int paletteSel = 0;
    bool panelFocused = false;  // a settings/help/browser window had the keyboard last frame: keys aren't the document's
    char filter[128] = "";
    ImVec2 caretPos{-1, -1};

    // ---------- settings (config.json) ----------
    float textSize = 18, pageWidth = 820, autosave = 0.5f;
    bool toolbar = true, vim = true, results = true, sliders = true, openLast = true, nativeDialogs = true;
    std::string exportDir, pdflatex = "pdflatex";
    std::vector<std::string> recent;  // notes opened from outside the library folder, newest first

    void loadCfg() {
        readFile(cfgFile, cfg);
        if (!cfg.is_object()) cfg = json::object();
        sidebar = cfg.value("sidebar", true);
        sort = cfg.value("sort", 0);
        dark = cfg.value("theme", std::string("dark")) != "light";
        textSize = std::clamp(cfg.value("textSize", 18.f), 12.f, 32.f);
        pageWidth = std::clamp(cfg.value("pageWidth", 820.f), 500.f, 2000.f);
        autosave = std::clamp(cfg.value("autosave", 0.5f), 0.f, 60.f);
        toolbar = cfg.value("toolbar", true);
        vim = cfg.value("keys", std::string("vim")) == "vim";
        g_capsEscape = cfg.value("capsEscape", true);
        results = cfg.value("results", true);
        sliders = cfg.value("sliders", true);
        openLast = cfg.value("openLast", true);
        nativeDialogs = cfg.value("nativeDialogs", true);
        exportDir = cfg.value("exportDir", std::string());
        pdflatex = cfg.value("pdflatex", std::string("pdflatex"));
        pdfEngine = std::clamp(cfg.value("pdfEngine", 0), 0, 2);
        completeOpts.popup = cfg.value("completePopup", true);
        completeOpts.minLetters = std::clamp(cfg.value("completeMinLetters", 1), 1, 4);
        completeOpts.maxItems = std::clamp(cfg.value("completeMaxItems", 9), 3, 15);
        completeOpts.accept = std::clamp(cfg.value("completeAccept", 0), 0, 2);
        completeOpts.convert = cfg.value("completeConvert", true);
        completeOpts.combos = cfg.value("completeCombos", true);
        completeOpts.learn = cfg.value("completeLearn", true);
        mathOpts.digits = std::clamp(cfg.value("digits", 10), 2, 15);
        mathOpts.degrees = cfg.value("angles", std::string("radians")) == "degrees";
        mathOpts.decimals = cfg.value("decimals", false);
        decimalComma = cfg.value("decimalComma", false);
        mathOpts.plotX0 = cfg.value("plotX0", -10.0);
        mathOpts.plotX1 = cfg.value("plotX1", 10.0);
        if (!(mathOpts.plotX0 < mathOpts.plotX1)) mathOpts.plotX0 = -10, mathOpts.plotX1 = 10;
        recent.clear();
        if (cfg.contains("recent") && cfg["recent"].is_array())
            for (auto& r : cfg["recent"])
                if (r.is_string()) recent.push_back(r.get<std::string>());
        if (const char* d = std::getenv("OPMAN_DOCS")) dir = d;
        else if (cfg.contains("dir") && cfg["dir"].is_string() && !cfg["dir"].get<std::string>().empty()) dir = fs::path(cfg["dir"].get<std::string>());
        else dir = home() / "Documents" / "opman";
        std::error_code ec;
        fs::create_directories(dir, ec);
        applySizes();
    }
    void saveCfg() {
        cfg["sidebar"] = sidebar;
        cfg["sort"] = sort;
        cfg["theme"] = dark ? "dark" : "light";
        cfg["textSize"] = textSize;
        cfg["pageWidth"] = pageWidth;
        cfg["autosave"] = autosave;
        cfg["toolbar"] = toolbar;
        cfg["keys"] = vim ? "vim" : "standard";
        cfg["capsEscape"] = g_capsEscape;
        cfg["results"] = results;
        cfg["sliders"] = sliders;
        cfg["openLast"] = openLast;
        cfg["nativeDialogs"] = nativeDialogs;
        cfg["exportDir"] = exportDir;
        cfg["pdflatex"] = pdflatex;
        cfg["pdfEngine"] = pdfEngine;
        cfg["completePopup"] = completeOpts.popup;
        cfg["completeMinLetters"] = completeOpts.minLetters;
        cfg["completeMaxItems"] = completeOpts.maxItems;
        cfg["completeAccept"] = completeOpts.accept;
        cfg["completeConvert"] = completeOpts.convert;
        cfg["completeCombos"] = completeOpts.combos;
        cfg["completeLearn"] = completeOpts.learn;
        cfg["digits"] = mathOpts.digits;
        cfg["angles"] = mathOpts.degrees ? "degrees" : "radians";
        cfg["decimals"] = mathOpts.decimals;
        cfg["decimalComma"] = decimalComma;
        cfg["plotX0"] = mathOpts.plotX0;
        cfg["plotX1"] = mathOpts.plotX1;
        cfg["recent"] = recent;
        if (!std::getenv("OPMAN_DOCS")) cfg["dir"] = dir.string();
        writeFile(cfgFile, cfg);
    }
    void applySizes() {
        L.textSize = textSize;
        L.mathSize = textSize * 1.15f;
    }
    // settings that change results: recompute every line
    void mathChanged() {
        lineCache.clear();
        changed(true);
        saveCfg();
    }

    // ---------- files ----------
    static std::string str(const json& m, const char* k) { return m.contains(k) && m[k].is_string() ? m[k].get<std::string>() : ""; }
    static std::string titleOf(const json& m) {
        std::string t = str(m, "title");
        if (t.empty()) t = str(m, "autoTitle");
        return t.empty() ? "Untitled" : t;
    }
    static std::string lower(std::string s) {
        for (auto& c : s) c = char(std::tolower((unsigned char)c));
        return s;
    }
    bool inLibrary(const fs::path& f) const {
        std::error_code ec;
        return fs::equivalent(f.parent_path(), dir, ec);
    }
    // a file name from a title: no characters Windows forbids, unique in the folder ("Notes 2.opman")
    fs::path freeName(const fs::path& folder, std::string title, const fs::path& self = {}) const {
        for (auto& c : title)
            if (std::strchr("/\\:*?\"<>|", c) || (unsigned char)c < 32) c = '-';
        while (!title.empty() && (title.back() == '.' || title.back() == ' ')) title.pop_back();
        if (title.empty()) title = "Untitled";
        if (title.size() > 80) title.resize(80);
        fs::path f = folder / (title + ".opman");
        std::error_code ec;
        for (int n = 2; fs::exists(f) && !(!self.empty() && fs::equivalent(f, self, ec)); n++)
            f = folder / (title + " " + std::to_string(n) + ".opman");
        return f;
    }
    std::vector<FileInfo> sortedFiles() const {
        std::vector<FileInfo> out;
        std::string q = lower(filter);
        for (auto& f : files)
            if (lower(titleOf(f.meta)).find(q) != std::string::npos || (!q.empty() && noteText(f.name).find(q) != std::string::npos))
                out.push_back(f);
        std::stable_sort(out.begin(), out.end(), [&](auto& a, auto& b) {
            if (sort == 2) return lower(titleOf(a.meta)) < lower(titleOf(b.meta));
            const char* k = sort == 1 ? "created" : "edited";
            return str(a.meta, k) > str(b.meta, k);
        });
        return out;
    }
    // the library's notes and the recent ones from elsewhere
    void listFiles() {
        files = listDocs(dir);
        std::erase_if(recent, [&](auto& r) { std::error_code ec; return !fs::exists(r, ec); });
        for (auto& r : recent) {
            if (std::any_of(files.begin(), files.end(), [&](auto& f) { std::error_code ec; return fs::equivalent(f.name, r, ec); })) continue;
            json d;
            if (readFile(r, d) && d.is_object()) files.push_back({r, d.contains("meta") && d["meta"].is_object() ? d["meta"] : json::object()});
        }
    }
    void remember(const std::string& path) {
        if (inLibrary(path)) return;
        std::erase(recent, path);
        recent.insert(recent.begin(), path);
        if (recent.size() > 20) recent.resize(20);
    }

    // a note's plain text, lowercase, read again when the file changes (search, backlinks)
    const std::string& noteText(const std::string& name) const {
        std::error_code ec;
        auto t = fs::last_write_time(name, ec);
        auto& e = texts[name];
        if (e.first != t || e.second.empty()) {
            json d;
            std::string s;
            if (readFile(name, d) && d.is_object())
                for (auto& r : docFromJson(d.value("doc", json::array()))) s += plain(r) + "\n";
            e = {t, lower(s)};
        }
        return e.second;
    }

    void findBacklinks() {
        backlinks.clear();
        std::string want = "[[" + lower(titleOf(meta)) + "]]";
        for (auto& f : files)
            if (f.name != current && noteText(f.name).find(want) != std::string::npos) backlinks.push_back(f);
    }

    json stats() {
        std::string text;
        for (auto& r : ed.doc) {
            for (auto& it : r)
                if (it.t == T::Ch && !it.m) text += it.c;
            text += ' ';
        }
        int words = 0;
        bool in = false;
        for (char c : text) {
            bool w = c != ' ' && c != '\t';
            words += w && !in;
            in = w;
        }
        return {{"autoTitle", docTitle(ed.doc)}, {"words", words}, {"lines", ed.doc.size()}, {"defines", definedNames(ed)}};
    }

    // ---------- media: pictures travel inside the .opman file ----------
    // While a note is open its pictures are files in a cache folder (by the note's id), so drawing and the exports
    // read ordinary files; saving packs every picture the note shows back into the file, base64.
    fs::path media;                                   // the open note's cache folder; image lines are relative to it
    std::map<std::string, std::pair<uintmax_t, std::string>> packed;  // path -> (size, base64), not to re-encode

    std::vector<std::string> imagePaths() const {
        std::vector<std::string> out;
        for (auto& r : ed.doc) {
            std::string alt, path;
            if (lineStyle(r).kind == Line::Image && imageRef(r, alt, path) && fs::path(path).is_relative()) out.push_back(path);
        }
        return out;
    }
    void unpackMedia(const json& d, const fs::path& noteFolder) {
        std::error_code ec;
        media = configDir() / "media" / str(meta, "id");
        fs::create_directories(media, ec);
        packed.clear();
        if (d.contains("media") && d["media"].is_object())
            for (auto& [rel, b64] : d["media"].items()) {
                if (!b64.is_string() || rel.find("..") != std::string::npos || fs::path(rel).is_absolute()) continue;
                std::string bytes = unbase64(b64.get<std::string>());
                fs::path f = media / rel;
                fs::create_directories(f.parent_path(), ec);
                if (fs::file_size(f, ec) != bytes.size() || ec) std::ofstream(f, std::ios::binary) << bytes;
                packed[rel] = {bytes.size(), b64.get<std::string>()};
            }
        for (auto& rel : imagePaths())  // an older note: its pictures sit next to it
            if (!fs::exists(media / rel, ec) && fs::exists(noteFolder / rel, ec)) {
                fs::create_directories((media / rel).parent_path(), ec);
                fs::copy_file(noteFolder / rel, media / rel, ec);
            }
    }
    json packMedia() {
        json out = json::object();
        std::error_code ec;
        for (auto& rel : imagePaths()) {
            uintmax_t size = fs::file_size(media / rel, ec);
            if (ec) continue;
            auto& p = packed[rel];
            if (p.first != size || p.second.empty()) {
                std::ifstream in(media / rel, std::ios::binary);
                p = {size, base64(std::string(std::istreambuf_iterator<char>(in), {}))};
            }
            out[rel] = p.second;
        }
        return out;
    }

    json fileJson() {
        return {{"format", "opman"}, {"version", 1}, {"meta", meta}, {"doc", docJson(ed.doc)}, {"path", pathJson(ed.curPath())},
                {"media", packMedia()}};
    }

    // writes only when the document changed, so moving the cursor doesn't bump "last edited"
    void saveNow(bool force = false) {
        changedAt = -1;
        if (current.empty() || noSave) return;
        std::string j = docJson(ed.doc).dump();
        if (j == savedJson && !force) return;
        savedJson = j;
        json st = stats();
        for (auto& [k, v] : st.items()) meta[k] = v;
        meta["edited"] = nowIso();
        if (!writeFile(current, fileJson())) {
            message = "Could not write " + current;
            savedJson.clear();
            return;
        }
        // a library note's file name follows its title ("Untitled.opman" becomes "Vectors.opman")
        if (inLibrary(current) && meta.value("autoName", false)) {
            fs::path want = freeName(dir, titleOf(meta), current);
            std::error_code ec;
            if (want != fs::path(current) && !fs::exists(want, ec)) {
                fs::rename(current, want, ec);
                if (!ec) moved(current, want.string());
            }
        }
        auto f = std::find_if(files.begin(), files.end(), [&](auto& x) { return x.name == current; });
        if (f != files.end()) f->meta = meta;
        else files.push_back({current, meta});
    }
    // the open note's file has a new path
    void moved(const std::string& from, const std::string& to) {
        for (auto& f : files)
            if (f.name == from) f.name = to;
        for (auto& h : history)
            if (h == from) h = to;
        for (auto& r : recent)
            if (r == from) r = to;
        if (current == from) current = to;
        cfg["lastDoc"] = current;
        saveCfg();
    }

    void openDoc(const std::string& name) {
        saveNow();
        json d;
        if (!readFile(name, d) || !d.is_object() || !d.contains("doc")) {
            message = "Could not read " + name;
            return;
        }
        if (!current.empty() && current != name && !goingBack) {
            history.push_back(current);
            if (history.size() > 50) history.erase(history.begin());
        }
        current = name;
        meta = d.contains("meta") && d["meta"].is_object() ? d["meta"] : json::object();
        if (str(meta, "id").empty()) meta["id"] = newId();
        ed.undoStack.clear();
        ed.redoStack.clear();
        ed.load(docFromJson(d.value("doc", json::array())), pathFromJson(d.value("path", json::object())));
        if (!d.contains("format")) upgradePlots();
        unpackMedia(d, fs::path(name).parent_path());
        texs.clear();
        savedJson = lastJson = docJson(ed.doc).dump();
        if (fs::path(name).extension() == ".json") convert();  // corca's format: becomes name.opman
        if (std::none_of(files.begin(), files.end(), [&](auto& f) { return f.name == current; })) files.push_back({current, meta});
        remember(current);
        cfg["lastDoc"] = current;
        saveCfg();
        vi.mode = vim ? Mode::Normal : Mode::Insert;
        views.clear();
        turns.clear();
        changed(true);
        findBacklinks();
    }
    // notes from before plot was a program function: a line starting with the text "plot " becomes a plot
    void upgradePlots() {
        for (auto& r : ed.doc)
            if (r.size() > 5 && plain(Row(r.begin(), r.begin() + 5)) == "plot " && r[0].t == T::Ch) {
                r.erase(r.begin(), r.begin() + 5);
                r.insert(r.begin(), {named(T::Fn, "plot"), M(" ")});
            }
        ed.reindex();
    }
    // an open .json note becomes a .opman beside it; the .json moves into ".old" in that folder
    void convert() {
        fs::path from = current, to = freeName(from.parent_path(), titleOf(meta));
        if (!writeFile(to, fileJson())) return;
        std::error_code ec;
        fs::create_directories(from.parent_path() / ".old", ec);
        fs::rename(from, from.parent_path() / ".old" / from.filename(), ec);
        std::erase_if(files, [&](auto& f) { return f.name == current; });
        moved(current, to.string());
    }
    // every corca .json in the library becomes a .opman (once, at start)
    void convertLibrary() {
        for (auto& f : listDocs(dir))
            if (fs::path(f.name).extension() == ".json") {
                json d;
                if (!readFile(f.name, d)) continue;
                std::string keep = current;
                current = f.name;
                meta = d.contains("meta") && d["meta"].is_object() ? d["meta"] : json::object();
                if (str(meta, "id").empty()) meta["id"] = newId();
                meta["autoName"] = !meta.contains("title");
                ed.load(docFromJson(d.value("doc", json::array())), pathFromJson(d.value("path", json::object())));
                upgradePlots();
                unpackMedia(d, dir);
                convert();
                if (cfg.value("lastDoc", std::string()) == fs::path(f.name).filename().string()) cfg["lastDoc"] = current;
                current = keep;
            }
        current.clear();
        meta = json::object();
    }
    static std::string newId() {
        static std::mt19937_64 rng(std::random_device{}() ^ uint64_t(std::chrono::steady_clock::now().time_since_epoch().count()));
        char buf[20];
        std::snprintf(buf, sizeof buf, "%016llx", (unsigned long long)rng());
        return buf;
    }

    void newDoc(const std::string& title = "") {
        saveNow();
        std::string t = nowIso();
        fs::path file = freeName(dir, title.empty() ? "Untitled" : title);
        json m = {{"created", t}, {"edited", t}, {"autoTitle", "Untitled"}, {"words", 0}, {"lines", 1}, {"defines", json::array()},
                  {"id", newId()}, {"autoName", title.empty()}};
        if (!title.empty()) m["title"] = title;
        if (!writeFile(file, {{"format", "opman"}, {"version", 1}, {"meta", m}, {"doc", docJson(Doc{Row{}})}, {"path", pathJson({})}})) {
            message = "Could not write " + file.string();
            return;
        }
        files.push_back({file.string(), m});
        openDoc(file.string());
    }

    // Save As: the note continues in the new file (the old one stays as it was)
    void saveAs(std::string to) {
        if (to.empty()) return;
        if (to.starts_with("~/")) to = (home() / to.substr(2)).string();
        fs::path p = fs::absolute(to);
        if (p.extension() != ".opman") p += ".opman";
        json keepMeta = meta;
        // a note that was only ever in the library under an automatic name moves; one you placed before is copied
        bool move = inLibrary(current) && meta.value("autoName", false);
        json st = stats();
        for (auto& [k, v] : st.items()) meta[k] = v;
        meta["autoName"] = false;
        meta["edited"] = nowIso();
        if (!writeFile(p, fileJson())) {
            meta = keepMeta;
            message = "Could not write " + p.string();
            return;
        }
        std::error_code ec;
        if (move && !fs::equivalent(current, p, ec)) {
            fs::remove(current, ec);
            std::erase_if(files, [&](auto& f) { return f.name == current; });
            std::erase(history, current);
        }
        current = p.string();
        savedJson = docJson(ed.doc).dump();
        if (std::none_of(files.begin(), files.end(), [&](auto& f) { return f.name == current; })) files.push_back({current, meta});
        remember(current);
        cfg["lastDoc"] = current;
        saveCfg();
        message = "Saved as " + current;
    }
    void openPath(std::string path) {
        if (path.starts_with("~/")) path = (home() / path.substr(2)).string();
        std::error_code ec;
        fs::path p = fs::absolute(path, ec);
        if (!fs::exists(p, ec)) { message = "No such file: " + path; return; }
        for (auto& f : files)
            if (fs::equivalent(f.name, p, ec)) return openDoc(f.name);
        openDoc(p.string());
    }

    void openMostRecent() {
        std::string last = str(cfg, "lastDoc");
        if (openLast && std::any_of(files.begin(), files.end(), [&](auto& f) { return f.name == last; })) return openDoc(last);
        if (openLast) {
            int keep = sort;
            sort = 0;
            auto s = sortedFiles();
            sort = keep;
            if (!s.empty()) return openDoc(s[0].name);
        }
        newDoc();
    }

    // ponytail: "trash" is a .trash folder in the library; system trash APIs differ per OS
    void trash(const std::string& name) {
        std::error_code ec;
        fs::create_directories(dir / ".trash", ec);
        fs::path to = dir / ".trash" / fs::path(name).filename();
        fs::rename(name, to, ec);
        if (ec) {  // another drive: copy, then remove
            ec.clear();
            fs::copy_file(name, to, fs::copy_options::overwrite_existing, ec);
            if (!ec) fs::remove(name, ec);
        }
        if (ec) { message = "Could not move " + name + " to the trash"; return; }
        std::erase_if(files, [&](auto& f) { return f.name == name; });
        std::erase(recent, name);
        if (name == current) {
            current.clear();
            openMostRecent();
        }
        message = "Moved to " + (dir / ".trash").string();
    }

    // ---------- asking for a path: the system dialog, else the built-in browser ----------
    struct Browser {
        bool open = false, save = false, folders = false;
        std::string title, ext;  // ext: ".opman", ".md"…; empty: any
        fs::path at;
        char name[256] = "";
        std::function<void(std::string)> done;
    } browser;
    void askPath(bool save, const std::string& title, const fs::path& start, const dialog::Filters& filters, std::function<void(std::string)> done,
                 bool folders = false) {
        if (nativeDialogs && dialog::available() && !std::getenv("OPMAN_KEYS")) {
            auto r = folders ? dialog::folder(title, start.string())
                   : save    ? dialog::save(title, start.string(), filters)
                             : dialog::open(title, start.string(), filters);
            if (r) done(*r);
            return;
        }
        browser = {};
        browser.open = true, browser.save = save, browser.folders = folders, browser.title = title, browser.done = std::move(done);
        if (!filters.empty() && filters[0].second.size() > 1 && filters[0].second[0] == '*') browser.ext = filters[0].second.substr(1);
        std::error_code ec;
        fs::path s = start.empty() ? dir : start;
        if (fs::is_directory(s, ec)) browser.at = s;
        else {
            browser.at = s.parent_path().empty() ? dir : s.parent_path();
            std::snprintf(browser.name, sizeof browser.name, "%s", s.filename().string().c_str());
        }
    }
    static dialog::Filters noteFilter() { return {{"opman notes", "*.opman"}, {"corca notes", "*.json"}}; }
    void openDialog() {
        askPath(false, "Open note", dir, noteFilter(), [this](std::string p) { openPath(p); });
    }
    void saveAsDialog() {
        fs::path start = current.empty() ? freeName(dir, titleOf(meta)) : fs::path(current).parent_path() / fs::path(current).filename();
        askPath(true, "Save note as", start, {{"opman notes", "*.opman"}}, [this](std::string p) { saveAs(p); });
    }
    void exportDialog(const std::string& ext) {
        std::string t = titleOf(meta);
        for (auto& c : t) if (std::strchr("/\\:*?\"<>|", c)) c = '-';
        fs::path start = (exportDir.empty() ? home() : fs::path(exportDir)) / (t + "." + ext);
        static const std::map<std::string, std::string> names = {{"md", "Markdown"}, {"html", "Web page"}, {"tex", "LaTeX"}, {"pdf", "PDF"}};
        askPath(true, "Export as " + names.at(ext), start, {{names.at(ext), "*." + ext}}, [this, ext](std::string p) {
            if (fs::path(p).extension() != "." + ext) p += "." + ext;
            exportTo(p);
        });
    }

    void run(const std::string& cmd) {
        std::string name = cmd.substr(0, cmd.find(' ')), arg = cmd.size() > name.size() ? cmd.substr(name.size() + 1) : "";
        if (name == "sidebar") { sidebar = !sidebar; sideFocus = false; saveCfg(); }
        else if (name == "w" && !arg.empty()) saveAs(arg);
        else if (name == "w") { saveNow(true); message = "\"" + titleOf(meta) + "\" saved to " + current; }
        else if (name == "saveas") { if (arg.empty()) saveAsDialog(); else saveAs(arg); }
        else if (name == "open") { if (arg.empty()) openDialog(); else openPath(arg); }
        else if (name == "help") { showHelp = true; helpTopic = arg; }
        else if (name == "settings") showSettings = true;
        else if (name == "palette") { showPalette = true; paletteQuery[0] = 0; paletteSel = 0; }
        else if (name == "outline") { sidebar = true; sideTab = 1; saveCfg(); }
        else if (name == "zoom") {
            textSize = arg == "in" ? textSize + 1 : arg == "out" ? textSize - 1 : 18;
            textSize = std::clamp(textSize, 12.f, 32.f);
            applySizes();
            saveCfg();
            message = "Text size " + std::to_string(int(textSize));
        }
        else if (name.starts_with("s/") || name.starts_with("%s/")) substitute(cmd);
        else if (name == "folder") revealFolder();
        else if (name == "q") quit = true;
        else if (name == "q!") quit = noSave = true;
        else if (name == "wq" || name == "x") { saveNow(); quit = true; }
        else if (name == "new") newDoc(arg);
        else if (name == "rename") {
            if (arg.empty()) meta.erase("title");
            else meta["title"] = arg;
            if (inLibrary(current)) meta["autoName"] = true;  // the file name follows
            savedJson.clear();
            saveNow();
            findBacklinks();
        } else if (name == "trash") { if (!current.empty()) trash(current); }
        else if (name == "set") {
            int n = 0;
            if (arg == "dark" || arg == "light") dark = arg == "dark";
            else if (arg == "degrees" || arg == "radians") mathOpts.degrees = arg == "degrees";
            else if (arg == "decimals" || arg == "fractions") mathOpts.decimals = arg == "decimals";
            else if (arg == "comma" || arg == "point") decimalComma = arg == "comma";
            else if (arg == "pdf=auto" || arg == "pdf=latex" || arg == "pdf=builtin") pdfEngine = arg == "pdf=auto" ? 0 : arg == "pdf=latex" ? 1 : 2;
            else if (arg == "vim" || arg == "standard") { vim = arg == "vim"; vi.mode = vim ? Mode::Normal : Mode::Insert; }
            else if (std::sscanf(arg.c_str(), "digits=%d", &n) == 1) mathOpts.digits = std::clamp(n, 2, 15);
            else { message = "Unknown option: " + arg + " (dark light · degrees radians · decimals fractions · digits=N · vim standard)"; return; }
            mathChanged();
            message = "Set " + arg;
        } else if (name == "export") exportTo(arg);
        else if (name == "print") print();
        else if (name == "tour") tour();
        else if (name == "image") addImage(arg);
        else if (name == "rule") insertLines({"---"}, 0);
        else if (name == "section") {  // :section [title]; the default numbers the exercises
            int n = 1;
            for (auto& r : ed.doc) n += lineStyle(r).kind == Line::Section;
            insertLines({"=== " + (arg.empty() ? "Exercise " + std::to_string(n) : arg), ""}, 0);
            ed.cur = {&ed.doc[ed.lineOf(ed.cur.row) + 1], 0};  // ready to write the exercise; k edits the title
        } else if (name == "link") {  // [[title]]: open the note with that title, or start it
            for (auto& f : files)
                if (lower(titleOf(f.meta)) == lower(arg)) return openDoc(f.name);
            newDoc(arg);
            message = "New note \"" + arg + "\" (Ctrl-o goes back)";
        } else if (name == "back") {
            if (history.empty()) { message = "No earlier note"; return; }
            std::string to = history.back();
            history.pop_back();
            goingBack = true;
            openDoc(to);
            goingBack = false;
        }
        else if (name == "table") {  // :table [rows] [cols]
            int r = 3, c = 2;
            std::sscanf(arg.c_str(), "%d %d", &r, &c);
            std::string line = "|";
            for (int k = 0; k < std::clamp(c, 1, 20); k++) line += "  |";
            insertLines(std::vector<std::string>(std::clamp(r, 1, 100), line), 2);
        } else if (name == "row" || name == "col") {  // :row above|below|delete, :col left|right|delete
            char op = name == "row" ? (arg == "above" ? 'k' : arg == "delete" ? 'r' : 'j') : (arg == "left" ? 'h' : arg == "delete" ? 'c' : 'l');
            ed.snap();
            if (!ed.gridOp(op)) message = "Not in a table or matrix";
            changed();
        } else if (name == "e") {
            std::error_code ec;
            if (!arg.empty() && fs::is_regular_file(arg.starts_with("~/") ? home() / arg.substr(2) : fs::path(arg), ec)) return openPath(arg);
            if (arg.empty()) return openDialog();
            std::string q = lower(arg);
            for (auto& f : sortedFiles())
                if (f.name == arg || lower(titleOf(f.meta)).find(q) != std::string::npos) return openDoc(f.name);
            message = "No document matches \"" + arg + "\"";
        }
    }

    // :s/old/new/[g] on the cursor's line, :%s/old/new/[g] everywhere (text, smartcase like /)
    void substitute(const std::string& cmd) {
        bool all = cmd[0] == '%';
        std::vector<std::string> part;
        std::string cur;
        for (size_t j = all ? 2 : 1; j < cmd.size(); j++) {
            if (cmd[j] == '\\' && j + 1 < cmd.size() && cmd[j + 1] == '/') cur += '/', j++;
            else if (cmd[j] == '/') part.push_back(cur), cur.clear();
            else cur += cmd[j];
        }
        part.push_back(cur);  // "", old, new, flags
        if (part.size() < 3 || part[1].empty()) { message = "Use :s/old/new/ or :%s/old/new/g"; return; }
        bool global = part.size() > 3 && part[3].find('g') != std::string::npos;
        int here = ed.lineOf(ed.topOf(ed.cur.row)), count = 0;
        auto ms = Vi::matches(ed, part[1]);
        ed.snap();
        for (int k = int(ed.doc.size()) - 1; k >= 0; k--) {
            if (!all && k != here) continue;
            std::vector<Vi::Match> line;
            for (auto& m : ms)
                if (m.line == k && (global || line.empty())) line.push_back(m);
            for (auto m = line.rbegin(); m != line.rend(); ++m) {
                Row& r = ed.doc[k];
                bool math = r[m->a].t == T::Ch && r[m->a].m;
                uint16_t f = r[m->a].t == T::Ch ? r[m->a].f : 0;
                r.erase(r.begin() + m->a, r.begin() + m->b);
                Row ins;
                for (size_t i = 0; i < part[2].size();) {
                    unsigned char b = part[2][i];
                    size_t n = b < 0x80 ? 1 : b < 0xE0 ? 2 : b < 0xF0 ? 3 : 4;
                    Item it = ch(part[2].substr(i, n), math);
                    it.f = f;
                    ins.push_back(it);
                    i += n;
                }
                r.insert(r.begin() + m->a, ins.begin(), ins.end());
                count++;
            }
        }
        ed.cur = {&ed.doc[here], 0};
        ed.reindex();
        changed();
        message = count ? std::to_string(count) + (count == 1 ? " replacement" : " replacements") : "Not found: " + part[1];
    }

    // the open note's folder in the file manager
    void revealFolder() {
        std::string f = fs::path(current).parent_path().string();
#ifdef _WIN32
        std::string cmd = "explorer \"" + f + "\"";
#elif defined(__APPLE__)
        std::string cmd = "open \"" + f + "\"";
#else
        std::string cmd = "xdg-open \"" + f + "\" >/dev/null 2>&1 &";
#endif
        std::thread([cmd] { (void)std::system(cmd.c_str()); }).detach();
    }

    // new text lines below the cursor's line; the cursor goes to item `at` of the first
    void insertLines(const std::vector<std::string>& lines, int at) {
        ed.snap();
        int k = ed.lineOf(ed.topOf(ed.cur.row)) + 1;
        if (ed.doc[k - 1].empty()) ed.doc.erase(ed.doc.begin() + --k);  // replace an empty line
        for (size_t n = 0; n < lines.size(); n++) {
            Row r;
            for (auto& c : Vi::parse(lines[n])) r.push_back(ch(c.ch, false));
            ed.doc.insert(ed.doc.begin() + k + n, r);
        }
        ed.cur = {&ed.doc[k], std::min(at, int(ed.doc[k].size()))};
        ed.reindex();
        changed();
    }

    // an image file: copied into the note (saved inside the .opman file)
    void addImage(std::string src) {
        if (src.starts_with("~/")) src = (home() / src.substr(2)).string();
        std::string ext = lower(fs::path(src).extension().string());
        if (ext != ".png" && ext != ".jpg" && ext != ".jpeg" && ext != ".gif" && ext != ".bmp") {
            message = "Not an image (png, jpg, gif, bmp): " + src;
            return;
        }
        std::error_code ec;
        fs::create_directories(media / "media", ec);
        std::string stem = fs::path(src).stem().string();
        for (auto& c : stem) if (!std::isalnum((unsigned char)c) && c != '-' && c != '_') c = '-';  // LaTeX-safe file names
        fs::path to = media / "media" / (stem + ext);
        for (int n = 2; fs::exists(to) && !fs::equivalent(src, to, ec); n++) to = media / "media" / (stem + "-" + std::to_string(n) + ext);
        if (!fs::exists(to)) fs::copy_file(src, to, ec);
        if (ec) { message = "Could not copy " + src + ": " + ec.message(); return; }
        insertLines({"![](media/" + to.filename().string() + ")"}, 2);
    }

    // :export [file.md|file.tex|file.pdf|md|tex|pdf]; PDF goes through pdflatex
    void exportTo(std::string arg) {
        std::string t = titleOf(meta);
        for (auto& c : t) if (std::strchr("/\\:*?\"<>| ", c)) c = '-';
        if (arg.empty()) arg = "md";
        if (arg == "md" || arg == "tex" || arg == "pdf" || arg == "html") arg = ((exportDir.empty() ? home() : fs::path(exportDir)) / (t + "." + arg)).string();
        fs::path out = arg.starts_with("~/") ? home() / arg.substr(2) : fs::path(arg);
        std::string ext = lower(out.extension().string());
        if (ext != ".tex" && ext != ".pdf") {
            std::ofstream f(out, std::ios::binary);
            bool ok = ext == ".html" || ext == ".htm" ? bool(f << html(ed, media, titleOf(meta))) : bool(f << markdown(ed.doc, media));
            message = ok ? "Exported to " + out.string() : "Could not write " + out.string();
            return;
        }
        if (pdfJob.valid() || pdfWanted) { message = "A PDF export is still running"; return; }
        if (ext == ".pdf" && (pdfEngine == 2 || (pdfEngine == 0 && !onPath(pdflatex.empty() ? "pdflatex" : pdflatex)))) {
            pdfOut = out, pdfWanted = true;
            message = "Exporting PDF…";
            return;
        }
        std::string tex = latex(ed, media);
        fs::path file = out;
        std::error_code ec;
        fs::path tmp = fs::temp_directory_path(ec) / "opman-export";
        if (ext == ".pdf") fs::create_directories(tmp, ec), file = tmp / "note.tex";
        if (!(std::ofstream(file, std::ios::binary) << tex)) { message = "Could not write " + file.string(); return; }
        if (ext == ".tex") { message = "Exported to " + out.string(); return; }
        fs::remove(tmp / "note.pdf", ec);
        std::string q = "\"", cmd = (pdflatex.empty() ? "pdflatex" : pdflatex) + " -interaction=nonstopmode -halt-on-error -output-directory " + q + tmp.string() + q + " " + q +
                                    file.string() + q + " > " + q + (tmp / "out.txt").string() + q + " 2>&1";
#ifdef _WIN32
        cmd = "\"" + cmd + "\"";  // cmd.exe strips the outer quotes
#endif
        pdfOut = out, pdfTmp = tmp;
        pdfJob = std::async(std::launch::async, [cmd] { return std::system(cmd.c_str()); });
        message = "Exporting PDF…";
    }

    // :tour: a new note typed by the tour's key script (in vim mode, whatever the setting)
    void tour() {
        newDoc("Tour");
        bool std0 = vi.standard;
        vi.standard = false;
        vi.mode = Mode::Normal;
        for (auto& k : Vi::parse(tourKeys())) vi.key(ed, k);
        vi.standard = std0;
        vi.mode = std0 ? Mode::Insert : Mode::Normal;
        vi.cmds.clear();
        showHelp = false;
        changed(true);
    }

    // :print: a PDF to the default printer (lp; on Windows the PDF viewer's Print verb). Without pdflatex, or if lp
    // fails, the note opens in the browser with its print dialog
    void print() {
        std::error_code ec;
        fs::path tmp = fs::temp_directory_path(ec) / "opman-print";
        fs::create_directories(tmp, ec);
        printing = true;
        exportTo((tmp / "note.pdf").string());
        if (!pdfJob.valid() && !pdfWanted) printing = false, printHtml();
        else message = "Preparing to print…";
    }
    void printHtml() {
        std::error_code ec;
        fs::path f = fs::temp_directory_path(ec) / "opman-print" / "note.html";
        fs::create_directories(f.parent_path(), ec);
        std::string h = html(ed, media, titleOf(meta));
        h.insert(h.rfind("</body>"), "<script>addEventListener('load', () => setTimeout(print, 200))</script>\n");
        if (!(std::ofstream(f, std::ios::binary) << h)) { message = "Could not write " + f.string(); return; }
        openExternal(f.string());
        message = "Opened in the browser to print";
    }
    static void openExternal(const std::string& f) {
#ifdef _WIN32
        std::string cmd = "start \"\" \"" + f + "\"";
#elif defined(__APPLE__)
        std::string cmd = "open \"" + f + "\"";
#else
        std::string cmd = "xdg-open \"" + f + "\" >/dev/null 2>&1 &";
#endif
        std::thread([cmd] { (void)std::system(cmd.c_str()); }).detach();
    }

    void sendToPrinter(const std::string& f) {
#ifdef _WIN32
        std::string cmd = "powershell -NoProfile -Command \"Start-Process -FilePath '" + f + "' -Verb Print\"";
#else
        std::string cmd = "lp \"" + f + "\" >/dev/null 2>&1";
#endif
        message = "Sent to the printer";
        std::thread([cmd, f] {
            if (std::system(cmd.c_str()) != 0) openExternal(f);  // no lp or no printer: open the PDF to print from there
        }).detach();
    }

    // is this command a file, or on PATH?
    static bool onPath(const std::string& cmd) {
        std::error_code ec;
        if (cmd.find_first_of("/\\") != std::string::npos) return fs::exists(cmd, ec);
        const char* env = std::getenv("PATH");
        std::string path = env ? env : "", dir;
#ifdef _WIN32
        const char sep = ';';
#else
        const char sep = ':';
#endif
        for (char c : path + sep) {
            if (c != sep) { dir += c; continue; }
            if (!dir.empty() && (fs::exists(fs::path(dir) / cmd, ec) || fs::exists(fs::path(dir) / (cmd + ".exe"), ec))) return true;
            dir.clear();
        }
        return false;
    }

    void pdfDone() {  // polled each frame; the PDF's existence is the result
        if (!pdfJob.valid() || pdfJob.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        pdfJob.get();
        std::error_code ec;
        if (!fs::exists(pdfTmp / "note.pdf") && pdfEngine == 0) { pdfWanted = true; return; }  // LaTeX failed (a missing package): still give a PDF
        if (printing) {
            printing = false;
            if (!fs::exists(pdfTmp / "note.pdf")) return printHtml();
            fs::copy_file(pdfTmp / "note.pdf", pdfOut, fs::copy_options::overwrite_existing, ec);
            return sendToPrinter(pdfOut.string());
        }
        if (!fs::exists(pdfTmp / "note.pdf")) { message = "pdflatex failed, see " + (pdfTmp / "note.log").string(); return; }
        fs::copy_file(pdfTmp / "note.pdf", pdfOut, fs::copy_options::overwrite_existing, ec);
        message = ec ? "Could not write " + pdfOut.string() : "Exported to " + pdfOut.string();
    }

    // ---------- the built-in PDF: A4 pages drawn by the app itself into a texture, as pictures ----------
    // ponytail: pictures, so a PDF's text can't be selected; pdflatex gives real text when it's installed
    void renderPdf() {
        pdfWanted = false;
#ifdef _WIN32
#define OM_GL __stdcall
#else
#define OM_GL
#endif
        auto genFb = (void(OM_GL*)(int, unsigned*))glfwGetProcAddress("glGenFramebuffers");
        auto bindFb = (void(OM_GL*)(unsigned, unsigned))glfwGetProcAddress("glBindFramebuffer");
        auto attach = (void(OM_GL*)(unsigned, unsigned, unsigned, unsigned, int))glfwGetProcAddress("glFramebufferTexture2D");
        auto delFb = (void(OM_GL*)(int, const unsigned*))glfwGetProcAddress("glDeleteFramebuffers");
        if (!genFb || !bindFb || !attach || !delFb) { message = "Could not render the PDF"; printing = false; return; }
        const unsigned FRAMEBUFFER = 0x8D40, COLOR0 = 0x8CE0;
        const float W = 794, H = 1123, S = 2.5f;  // A4 at 96 dpi, drawn at 240
        const int pw = int(W * S), ph = int(H * S);
        ImGuiIO& io = ImGui::GetIO();
        unsigned fb = 0, tex = 0;
        genFb(1, &fb);
        bindFb(FRAMEBUFFER, fb);
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, pw, ph, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        attach(FRAMEBUFFER, COLOR0, GL_TEXTURE_2D, tex, 0);
        bool dark0 = dark;
        dark = false;
        printMode = true;
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        auto page = [&](float top, float bottom, std::string* out) {
            printTop = top;
            lineStarts.clear();
            ImGui_ImplOpenGL3_NewFrame();
            io.DisplaySize = {W, H};
            io.DisplayFramebufferScale = {S, S};
            ImGui::NewFrame();
            L.pal = &LIGHT;
            ImGui::SetNextWindowPos({0, 0});
            ImGui::SetNextWindowSize({W, H});
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 0});
            ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32_WHITE);
            ImGui::Begin("##pdf", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
            ImGui::PushClipRect({0, 46}, {W, bottom - top + 48}, true);  // this page's lines, not its neighbours'
            drawDoc();
            ImGui::PopClipRect();
            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
            ImGui::Render();
            bindFb(FRAMEBUFFER, fb);
            glViewport(0, 0, pw, ph);
            glClearColor(1, 1, 1, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            if (!out) return;
            std::string flipped(size_t(pw) * ph * 3, 0), px(size_t(pw) * ph * 3, 0);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, pw, ph, GL_RGB, GL_UNSIGNED_BYTE, px.data());
            for (int y = 0; y < ph; y++) std::memcpy(&flipped[size_t(y) * pw * 3], &px[size_t(ph - 1 - y) * pw * 3], size_t(pw) * 3);
            *out = std::move(flipped);
        };
        page(48, 1e9f, nullptr);  // measures the lines, and the plots' shape on a page
        if (plotAspect > 0) {
            mathOpts.plotAspect = plotAspect;
            plotAspect = 0;
            std::erase_if(lineCache, [](auto& e) { return e.second.plot && e.second.plot->equal; });
            changed(true);
            page(48, 1e9f, nullptr);
        }
        std::vector<float> starts = lineStarts;  // the last entry is where the note ends
        std::vector<std::string> pages;
        for (float top = 48; top < starts.back() - 0.5f || pages.empty();) {
            float bottom = top;
            for (float s : starts)
                if (s <= top + H - 96) bottom = std::max(bottom, s);
            if (bottom <= top) {  // a line taller than a page: it gets the page, cut off
                auto next = std::upper_bound(starts.begin(), starts.end(), top);
                bottom = next == starts.end() ? starts.back() : *next;
            }
            pages.emplace_back();
            page(top, bottom, &pages.back());
            top = bottom;
        }
        bindFb(FRAMEBUFFER, 0);
        delFb(1, &fb);
        glDeleteTextures(1, &tex);
        printMode = false;
        dark = dark0;
        L.pal = &pal();
        std::string file = imagePdf(pages, pw, ph);
        bool ok = bool(std::ofstream(pdfOut, std::ios::binary) << file);
        if (ok && printing) { printing = false; return sendToPrinter(pdfOut.string()); }
        printing = false;
        message = ok ? "Exported to " + pdfOut.string() + " (pictures: text can't be selected)" : "Could not write " + pdfOut.string();
    }

    Tex& texture(const std::string& path) {
        auto [it, fresh] = texs.try_emplace(path);
        if (!fresh) return it->second;
        int w, h, n;
        fs::path p = fs::path(path).is_relative() ? media / path : fs::path(path);
        if (unsigned char* px = stbi_load(p.string().c_str(), &w, &h, &n, 4)) {
            GLuint t;
            glGenTextures(1, &t);
            glBindTexture(GL_TEXTURE_2D, t);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
            stbi_image_free(px);
            it->second = {ImTextureID(intptr_t(t)), w, h};
        }
        return it->second;
    }

    // after any edit or cursor move: live results, autosave timer, keep the caret in view
    void changed(bool force = false) {
        std::string j = docJson(ed.doc).dump();
        if (force || j != lastJson) {
            lastJson = j;
            int n = int(ed.doc.size());
            std::vector<std::pair<int, std::string>> nowPlots;
            for (int k = 0; k < n; k++)
                if (lineStyle(ed.doc[k]).kind == Line::Plot) nowPlots.push_back({k, toJson(ed.doc[k]).dump()});
            auto moved = followLines(plotLines, nowPlots);
            auto follow = [&](auto& state) {
                std::decay_t<decltype(state)> next;
                for (auto& [k, v] : state)
                    if (auto f = moved.find(k); f != moved.end()) next[f->second] = v;
                state = std::move(next);
            };
            follow(views);
            follow(turns);
            plotLines = std::move(nowPlots);
            ghosts.assign(n, std::nullopt);
            plots.assign(n, std::nullopt);
            viewPlots.clear();
            std::unordered_map<size_t, LineCache> next;
            for (int a = 0; a < n;) {  // results only depend on their section, so a key per section and line
                int b = sectionOf(ed.doc, a).second;
                std::string sec;
                for (int k = a; k < b; k++) sec += toJson(ed.doc[k]).dump() + "\n";
                size_t h = std::hash<std::string>{}(sec);
                for (int k = a; k < b; k++) {
                    size_t key = h ^ (size_t(k - a + 1) * 0x9e3779b97f4a7c15ull);
                    auto f = lineCache.find(key);
                    LineCache c;
                    if (f != lineCache.end()) c = std::move(f->second);
                    else if (lineStyle(ed.doc[k]).kind == Line::Plot) c.plot = plot(ed, ed.doc[k]);
                    else if (results) c.ghost = ghost(ed, ed.doc[k]);
                    ghosts[k] = c.ghost;
                    plots[k] = c.plot;
                    next[key] = std::move(c);
                }
                a = b;
            }
            lineCache = std::move(next);
            vi.refresh(ed);
        }
        if (j != savedJson && changedAt < 0) changedAt = nowSec();
        scrollToCaret = true;
    }

    void sideKey(const Key& k) {
        auto list = sortedFiles();
        int n = int(list.size());
        if (k.sp == Key::Esc || (k.ctrl && k.ch == "w") || k.ch == "h") sideFocus = false;
        else if (k.ch == "j" || k.sp == Key::Down) sideSel = std::min(sideSel + 1, n - 1);
        else if (k.ch == "k" || k.sp == Key::Up) sideSel = std::max(sideSel - 1, 0);
        else if (k.ch == "g") sideSel = 0;
        else if (k.ch == "G") sideSel = n - 1;
        else if (k.ch == "/") focusFilter = true;
        else if ((k.sp == Key::Enter || k.ch == "l" || k.ch == "o") && sideSel >= 0 && sideSel < n) {
            openDoc(list[sideSel].name);
            sideFocus = false;
        }
    }

    void input() {
        for (auto& d : std::exchange(g_drops, {})) run("image " + d);
        if (g_keys.empty()) return;
        auto keys = std::move(g_keys);
        g_keys.clear();
        if (ImGui::GetIO().WantTextInput || panelFocused) return;  // a text field or a window has them
        vi.standard = !vim;
        for (auto& k : keys) {
            // app shortcuts, in both key modes
            if (k.sp == Key::F1) { showHelp = !showHelp; continue; }
            if (k.ctrl && k.shift && k.ch == "p") { run("palette"); continue; }
            if (k.ctrl && k.shift && k.ch == "s") { saveAsDialog(); continue; }
            if (k.ctrl && k.shift && k.ch == "o") { openDialog(); continue; }
            if (k.ctrl && k.shift && k.ch == "n") { newDoc(); continue; }
            if (k.ctrl && k.ch == ",") { showSettings = !showSettings; continue; }
            if (k.ctrl && (k.ch == "=" || k.ch == "+")) { run("zoom in"); continue; }
            if (k.ctrl && k.ch == "-") { run("zoom out"); continue; }
            if (k.ctrl && k.ch == "0") { run("zoom reset"); continue; }
            if (!vim && k.ctrl && !k.shift && k.ch == "o" && vi.mode == Mode::Insert) { openDialog(); continue; }
            if (!vim && k.ctrl && !k.shift && k.ch == "p" && vi.mode == Mode::Insert) { run("print"); continue; }
            if (k.ctrl && k.ch == "w") {  // Ctrl-w: focus the file list (or back)
                sideFocus = !sideFocus;
                if (sideFocus) sidebar = true;
                continue;
            }
            if (sideFocus) { sideKey(k); continue; }
            vi.key(ed, k);
            for (auto& c : vi.cmds) run(c);
            vi.cmds.clear();
            if (quit) return;
        }
        changed();
    }

    // ---------- theme ----------
    const Palette& pal() const { return dark ? DARK : LIGHT; }
    void applyColors() {
        const Palette& p = pal();
        L.pal = &p;
        auto c = [](ImU32 u, float a = 1) {
            ImVec4 v = ImGui::ColorConvertU32ToFloat4(u);
            v.w *= a;
            return v;
        };
        auto& s = ImGui::GetStyle().Colors;
        s[ImGuiCol_WindowBg] = c(p.bg);
        s[ImGuiCol_ChildBg] = c(p.bg, 0);
        s[ImGuiCol_PopupBg] = c(p.pop);
        s[ImGuiCol_Text] = c(p.fg);
        s[ImGuiCol_TextDisabled] = c(p.muted);
        s[ImGuiCol_Border] = c(p.line);
        s[ImGuiCol_FrameBg] = c(p.bg);
        s[ImGuiCol_FrameBgHovered] = c(p.line);
        s[ImGuiCol_FrameBgActive] = c(p.line);
        s[ImGuiCol_Header] = c(p.line);
        s[ImGuiCol_HeaderHovered] = c(p.line, 0.7f);
        s[ImGuiCol_HeaderActive] = c(p.line);
        s[ImGuiCol_Button] = c(p.bg, 0);
        s[ImGuiCol_ButtonHovered] = c(p.line);
        s[ImGuiCol_ButtonActive] = c(p.line);
        s[ImGuiCol_ScrollbarBg] = c(p.bg, 0);
        s[ImGuiCol_ScrollbarGrab] = c(p.line);
        s[ImGuiCol_TextSelectedBg] = c(p.sel);
        // settings, help, palette: the accent for what is on or chosen
        s[ImGuiCol_TitleBg] = s[ImGuiCol_TitleBgActive] = s[ImGuiCol_TitleBgCollapsed] = c(p.side);
        s[ImGuiCol_CheckMark] = s[ImGuiCol_SliderGrab] = c(p.fn);
        s[ImGuiCol_SliderGrabActive] = c(p.fn, 0.8f);
        s[ImGuiCol_Tab] = c(p.bg, 0);
        s[ImGuiCol_TabHovered] = c(p.line);
        s[ImGuiCol_TabSelected] = c(p.fn, 0.25f);
        s[ImGuiCol_TabSelectedOverline] = c(p.fn);
        s[ImGuiCol_Separator] = c(p.line);
        s[ImGuiCol_ModalWindowDimBg] = c(0xFF000000, 0.35f);
    }
    static ImU32 alpha(ImU32 c, float a) { return (c & 0x00FFFFFF) | (ImU32((c >> IM_COL32_A_SHIFT & 0xFF) * a) << IM_COL32_A_SHIFT); }

    // ---------- document ----------
    Cur hit(ImVec2 m) {
        const Stop* best = nullptr;
        float bestScore = FLT_MAX;
        for (auto& s : L.stops) {
            float score = std::max({s.y0 - m.y, 0.f, m.y - s.y1}) * 4 + std::fabs(m.x - s.x);
            if (score < bestScore) bestScore = score, best = &s;
        }
        if (!best) return {&ed.doc.back(), int(ed.doc.back().size())};
        return {const_cast<Row*>(best->row), best->i};
    }

    // column stops for the table starting at line k: each column as wide as its widest cell
    void tableTabs(size_t k) {
        std::vector<float> width;
        for (size_t j = k; j < ed.doc.size() && lineStyle(ed.doc[j]).kind == Line::Table; j++) {
            auto xs = L.pipeXs(ed.doc[j]);
            for (size_t c = 0; c + 1 < xs.size(); c++) {
                if (width.size() <= c) width.push_back(0);
                width[c] = std::max(width[c], xs[c + 1] - xs[c]);
            }
        }
        L.tabs = {0};
        for (float w : width) L.tabs.push_back(L.tabs.back() + std::max(w, 2.5f * L.textSize));
    }

    // returns the height; draw=false only measures
    float drawImage(const Row& row, float x, float y, float maxW, bool draw) {
        std::string alt, path;
        imageRef(row, alt, path);
        Tex& t = texture(path);
        const Palette& p = pal();
        if (!t.id) {
            if (draw) L.drawText("Image not found: " + path, x, y + L.textSize, L.textSize * 0.85f, p.muted, ui);
            return L.textSize * 1.4f;
        }
        float s = std::min({1.f, maxW / t.w, 420.f / t.h});
        float h = t.h * s, capH = alt.empty() ? 0 : L.textSize * 1.4f;
        if (draw) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddImageRounded(t.id, {x, y}, {x + t.w * s, y + h}, {0, 0}, {1, 1}, IM_COL32_WHITE, 6);
            if (!alt.empty()) L.drawText(alt, x, y + h + L.textSize * 1.1f, L.textSize * 0.82f, p.muted, ui);
        }
        return h + capH;
    }

    // "a = 2" (a name, =, a number): a drag handle after the line; dragging rewrites the number, and every result and
    // plot using a follows. Returns whether the doc changed.
    bool slider(Row& row, float x, float base, int k) {
        std::vector<int> v;
        for (int j = 0; j < int(row.size()); j++)
            if (!isSpace(row[j])) v.push_back(j);
        if (v.size() < 3 || !(row[v[0]].t == T::Var || (row[v[0]].t == T::Ch && row[v[0]].m && isLetter(row[v[0]].c))) ||
            row[v[1]].t != T::Ch || row[v[1]].c != "=")
            return false;
        std::string num;
        for (size_t j = 2; j < v.size(); j++) {
            const Item& it = row[v[j]];
            if (it.t != T::Ch || !(isDigit(it.c) || it.c == "." || (decimalComma && it.c == ",") || ((it.c == "-" || it.c == "−") && j == 2))) return false;
            num += it.c == "−" ? "-" : it.c == "," ? "." : it.c;
        }
        double val;
        try { val = std::stod(num); } catch (...) { return false; }
        const Palette& p = pal();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float w = 110, cy = base - L.textSize * 0.3f;
        ImGui::SetCursorScreenPos({x, cy - 10});
        ImGui::PushID(k);
        ImGui::InvisibleButton("slider", {w, 20});
        ImGui::PopID();
        bool active = ImGui::IsItemActive(), hov = ImGui::IsItemHovered();
        if (hov || active) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (hov && !active) ImGui::SetTooltip("Drag to change %s", plain(Row{row[v[0]]}).c_str());
        if (ImGui::IsItemActivated()) ed.snap(), sliderV0 = val, sliderX0 = ImGui::GetMousePos().x;
        float knob = x + w / 2;
        bool changedNum = false;
        if (active) {
            // ponytail: half the track is the value's own size; integers step by whole numbers
            double r = std::max(std::fabs(sliderV0), 1.0), dx = ImGui::GetMousePos().x - sliderX0;
            bool integer = num.find('.') == std::string::npos;
            double step = integer ? std::max(1.0, std::pow(10, std::floor(std::log10(r)) - 1)) : std::pow(10, std::floor(std::log10(r)) - 2);
            double nv = std::round((sliderV0 + dx / (w / 2) * r) / step) * step;
            knob = std::clamp(x + w / 2 + float(dx), x, x + w);
            char buf[48];
            std::snprintf(buf, sizeof buf, "%.*f", std::max(0, int(-std::floor(std::log10(step) + 1e-9))), nv);
            std::string s = buf;
            if (s != num) {
                row.erase(row.begin() + v[1] + 1, row.end());
                row.push_back(M(" "));
                for (char c : localNum(s)) row.push_back(M(c == '-' ? "−" : std::string(1, c)));
                if (ed.cur.row == &row) ed.cur.i = std::min(ed.cur.i, int(row.size()));
                changedNum = true;
            }
        }
        ImU32 c = active || hov ? p.fn : alpha(p.muted, 0.6f);
        dl->AddLine({x, cy}, {x + w, cy}, alpha(c, 0.5f), 2);
        for (int t = 0; t <= 4; t++) dl->AddLine({x + w * t / 4, cy - 3}, {x + w * t / 4, cy + 3}, alpha(c, 0.5f), 1);
        dl->AddCircleFilled({knob, cy}, active ? 7.f : 5.5f, c);
        return changedNum;
    }

    static double niceStep(double span) {
        double raw = span / 6, mag = std::pow(10, std::floor(std::log10(raw))), f = raw / mag;
        return (f < 1.5 ? 1 : f < 3.5 ? 2 : f < 7.5 ? 5 : 10) * mag;
    }

    // drag pans, the wheel zooms around the mouse, a double-click goes back to the line's own ranges
    // ---------- 3D plots: an orthographic view of the box [x0,x1]×[y0,y1]×[z0,z1], turned by dragging ----------
    std::map<int, std::array<float, 3>> turns;  // per plot line: yaw, pitch, zoom (ponytail: by line index, like views)
    float drawPlot3D(const Plot& pl, float x, float y, float w, int k) {
        const Palette& p = pal();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float h = std::min(420.f, w * 0.7f);
        auto [it, fresh] = turns.try_emplace(k, std::array<float, 3>{-0.65f, 0.45f, 1.f});
        auto& tn = it->second;
        ImGui::SetCursorScreenPos({x, y});
        ImGui::PushID(k);
        ImGui::InvisibleButton("plot3d", {w, h});
        ImGui::PopID();
        bool reset = false;
        if (ImGui::IsItemHovered()) {
            ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
            ImGuiIO& io = ImGui::GetIO();
            if (io.MouseWheel != 0) tn[2] = std::clamp(tn[2] * std::pow(1.12f, io.MouseWheel), 0.3f, 4.f);
            if (ImGui::IsItemActive()) tn[0] += io.MouseDelta.x * 0.01f, tn[1] = std::clamp(tn[1] + io.MouseDelta.y * 0.01f, -1.5f, 1.5f);
            if (ImGui::IsMouseDoubleClicked(0)) reset = true;
            if (!ImGui::IsItemActive()) ImGui::SetTooltip("Drag to turn · wheel to zoom · double-click to reset");
        }
        dl->AddRectFilled({x, y}, {x + w, y + h}, p.side, 8);
        float cy0 = std::cos(tn[0]), sy0 = std::sin(tn[0]), cp = std::cos(tn[1]), sp = std::sin(tn[1]);
        float scale = std::min(w, h) * 0.36f * tn[2];
        ImVec2 c{x + w / 2, y + h / 2 + h * 0.04f};
        // box coordinates in [-1, 1] (z a little flatter), then yaw about z and pitch towards the viewer
        auto norm = [&](double X, double Y, double Z) {
            return std::array<float, 3>{float((X - pl.x0) / (pl.x1 - pl.x0) * 2 - 1), float((Y - pl.y0) / (pl.y1 - pl.y0) * 2 - 1),
                                        float(((Z - pl.z0) / (pl.z1 - pl.z0) * 2 - 1) * 0.75)};
        };
        auto view = [&](std::array<float, 3> v) {  // screen x, screen y, depth (bigger is nearer)
            float rx = v[0] * cy0 - v[1] * sy0, ry = v[0] * sy0 + v[1] * cy0;
            float up = v[2] * cp + ry * sp, depth = -ry * cp + v[2] * sp;
            return std::array<float, 3>{c.x + rx * scale, c.y - up * scale, depth};
        };
        auto P = [&](std::array<float, 3> v) { auto s = view(v); return ImVec2{s[0], s[1]}; };
        dl->PushClipRect({x, y}, {x + w, y + h}, true);
        // the box: back edges first, labels at the ends of three axes
        float e = 0.75f;
        std::array<std::array<float, 3>, 8> corner;
        for (int i = 0; i < 8; i++) corner[i] = {i & 1 ? 1.f : -1.f, i & 2 ? 1.f : -1.f, i & 4 ? e : -e};
        for (int a = 0; a < 8; a++)
            for (int b = a + 1; b < 8; b++)
                if (std::popcount(unsigned(a ^ b)) == 1) dl->AddLine(P(corner[a]), P(corner[b]), alpha(p.line, 0.9f), 1);
        auto lbl = [&](std::array<float, 3> v, const std::string& t) {
            ImVec2 q = P(v);
            L.drawText(t, q.x - L.textWidth(t, 11) / 2, q.y + 4, 11, p.muted, ui);
        };
        auto num = [](double v) { char b[32]; std::snprintf(b, sizeof b, "%.3g", std::fabs(v) < 1e-12 ? 0.0 : v); return std::string(b); };
        lbl({-1, -1.12f, -e}, num(pl.x0)), lbl({1, -1.12f, -e}, num(pl.x1)), lbl({0, -1.25f, -e}, "x");
        lbl({1.12f, -1, -e}, num(pl.y0)), lbl({1.12f, 1, -e}, num(pl.y1)), lbl({1.25f, 0, -e}, "y");
        lbl({-1.1f, -1.1f, -e}, num(pl.z0)), lbl({-1.1f, -1.1f, e}, num(pl.z1)), lbl({-1.15f, -1.15f, 0}, "z");
        // surfaces: quads far to near, coloured by height and shaded by their tilt to the light
        struct Quad {
            ImVec2 v[4];
            float depth;
            ImU32 col;
        };
        std::vector<Quad> quads;
        static const ImVec4 RAMP[] = {{0.27f, 0.0f, 0.33f, 1}, {0.23f, 0.32f, 0.55f, 1}, {0.13f, 0.57f, 0.55f, 1}, {0.37f, 0.79f, 0.38f, 1}, {0.99f, 0.91f, 0.14f, 1}};
        for (auto& sf : pl.surfaces) {
            int n = sf.n;
            auto Z = [&](int i, int j) { return std::clamp(sf.z[j * n + i], pl.z0, pl.z1); };
            for (int j = 0; j + 1 < n; j++)
                for (int i = 0; i + 1 < n; i++) {
                    double zs[4] = {sf.z[j * n + i], sf.z[j * n + i + 1], sf.z[(j + 1) * n + i + 1], sf.z[(j + 1) * n + i]};
                    if (std::any_of(zs, zs + 4, [](double v) { return !std::isfinite(v); })) continue;
                    double X0 = pl.x0 + (pl.x1 - pl.x0) * i / (n - 1), X1 = pl.x0 + (pl.x1 - pl.x0) * (i + 1) / (n - 1);
                    double Y0 = pl.y0 + (pl.y1 - pl.y0) * j / (n - 1), Y1 = pl.y0 + (pl.y1 - pl.y0) * (j + 1) / (n - 1);
                    std::array<float, 3> w4[4] = {norm(X0, Y0, Z(i, j)), norm(X1, Y0, Z(i + 1, j)), norm(X1, Y1, Z(i + 1, j + 1)), norm(X0, Y1, Z(i, j + 1))};
                    Quad q;
                    float d = 0;
                    for (int m = 0; m < 4; m++) {
                        auto s3 = view(w4[m]);
                        q.v[m] = {s3[0], s3[1]};
                        d += s3[2];
                    }
                    q.depth = d / 4;
                    float t = float(((zs[0] + zs[1] + zs[2] + zs[3]) / 4 - pl.z0) / (pl.z1 - pl.z0));
                    t = std::clamp(t, 0.f, 0.9999f) * 4;
                    int r = int(t);
                    ImVec4 a = RAMP[r], b = RAMP[r + 1];
                    float f = t - r;
                    // normal of the quad (in box space) against a light from the upper front
                    float ux = w4[2][0] - w4[0][0], uy = w4[2][1] - w4[0][1], uz = w4[2][2] - w4[0][2];
                    float vx = w4[1][0] - w4[3][0], vy = w4[1][1] - w4[3][1], vz = w4[1][2] - w4[3][2];
                    float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx, nl = std::sqrt(nx * nx + ny * ny + nz * nz) + 1e-9f;
                    float light = 0.55f + 0.45f * std::fabs((nx * 0.3f + ny * -0.4f + nz * 0.86f) / nl);
                    q.col = ImGui::ColorConvertFloat4ToU32({(a.x + (b.x - a.x) * f) * light, (a.y + (b.y - a.y) * f) * light,
                                                            (a.z + (b.z - a.z) * f) * light, 0.94f});
                    quads.push_back(q);
                }
        }
        std::sort(quads.begin(), quads.end(), [](auto& a, auto& b) { return a.depth < b.depth; });
        for (auto& q : quads) {
            dl->AddQuadFilled(q.v[0], q.v[1], q.v[2], q.v[3], q.col);
            dl->AddQuad(q.v[0], q.v[1], q.v[2], q.v[3], IM_COL32(0, 0, 0, 40), 0.6f);
        }
        static const ImU32 COLS[] = {IM_COL32(0x3b, 0x82, 0xf6, 255), IM_COL32(0xe5, 0x48, 0x4d, 255), IM_COL32(0x2f, 0x9e, 0x5b, 255),
                                     IM_COL32(0xf5, 0x9e, 0x0b, 255), IM_COL32(0x8b, 0x5c, 0xf6, 255), IM_COL32(0x14, 0xb8, 0xa6, 255)};
        for (size_t s = 0; s < pl.curves.size(); s++) {
            std::vector<ImVec2> seg;
            auto flush = [&] {
                if (seg.size() > 1) dl->AddPolyline(seg.data(), int(seg.size()), COLS[s % 6], 0, 2.4f);
                seg.clear();
            };
            for (auto& q : pl.curves[s].pts) {
                if (!std::isfinite(q[0]) || !std::isfinite(q[1]) || !std::isfinite(q[2])) { flush(); continue; }
                seg.push_back(P(norm(q[0], q[1], std::clamp(q[2], pl.z0, pl.z1))));
            }
            flush();
        }
        dl->PopClipRect();
        float ly = y + 10;  // legend
        for (auto& sf : pl.surfaces) L.drawText("z = " + sf.label, x + 12, ly + 11, 13, p.fg, ui), ly += 18;
        for (size_t s = 0; s < pl.curves.size(); s++, ly += 18) {
            dl->AddRectFilled({x + 12, ly + 5}, {x + 26, ly + 8}, COLS[s % 6], 1);
            L.drawText(pl.curves[s].label, x + 32, ly + 11, 13, p.fg, ui);
        }
        if (!pl.error.empty()) L.drawText(pl.error, x + 12, y + h - 12, 13, IM_COL32(0xe5, 0x48, 0x4d, 255), ui);
        if (reset) turns.erase(k);
        return h;
    }

    float plotAspect = 0;  // the plot area's shape when it differs from what the plots were fitted to
    float drawPlot(const Plot& pl, float x, float y, float w, int k) {
        const Palette& p = pal();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float h = std::min(320.f, w * 0.55f), padL = 44, padB = 22, padT = 8, padR = 10;
        ImVec2 a{x + padL, y + padT}, b{x + w - padR, y + h - padB};
        if (float asp = (b.x - a.x) / (b.y - a.y); std::fabs(asp - mathOpts.plotAspect) > 0.02f) plotAspect = asp;
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(k);
        ImGui::InvisibleButton("plot", {b.x - a.x, b.y - a.y});
        ImGui::PopID();
        // pl may be viewPlots[k]: a new view is applied after drawing (erasing it now would leave pl dangling)
        std::optional<std::array<double, 4>> newView;
        bool reset = false;
        if (ImGui::IsItemHovered()) {
            ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);  // the wheel zooms the plot, not the page
            std::array<double, 4> v{pl.x0, pl.x1, pl.y0, pl.y1};
            ImGuiIO& io = ImGui::GetIO();
            bool moved = false;
            if (io.MouseWheel != 0) {
                double f = std::pow(0.85, io.MouseWheel);
                double mx = pl.x0 + (io.MousePos.x - a.x) / (b.x - a.x) * (pl.x1 - pl.x0);
                double my = pl.y0 + (b.y - io.MousePos.y) / (b.y - a.y) * (pl.y1 - pl.y0);
                v = {mx + (v[0] - mx) * f, mx + (v[1] - mx) * f, my + (v[2] - my) * f, my + (v[3] - my) * f};
                moved = true;
            }
            if (ImGui::IsItemActive() && (io.MouseDelta.x || io.MouseDelta.y)) {
                double dx = io.MouseDelta.x / (b.x - a.x) * (pl.x1 - pl.x0), dy = io.MouseDelta.y / (b.y - a.y) * (pl.y1 - pl.y0);
                v = {v[0] - dx, v[1] - dx, v[2] + dy, v[3] + dy};
                moved = true;
            }
            if (ImGui::IsMouseDoubleClicked(0)) reset = true;
            else if (moved) newView = v;
            if (!ImGui::IsItemActive()) ImGui::SetTooltip("Drag to pan · wheel to zoom · double-click to reset");
        }
        dl->AddRectFilled({x, y}, {x + w, y + h}, p.side, 8);
        auto X = [&](double v) { return float(a.x + (v - pl.x0) / (pl.x1 - pl.x0) * (b.x - a.x)); };
        auto Y = [&](double v) { return float(b.y - (v - pl.y0) / (pl.y1 - pl.y0) * (b.y - a.y)); };
        auto label = [](double v) {
            char s[32];
            std::snprintf(s, sizeof s, "%g", std::fabs(v) < 1e-12 ? 0.0 : v);
            return localNum(s);
        };
        float fs = 12;
        double sx = niceStep(pl.x1 - pl.x0), sy = niceStep(pl.y1 - pl.y0);
        for (double t = std::ceil(pl.x0 / sx) * sx; t <= pl.x1 + 1e-9; t += sx) {
            dl->AddLine({X(t), a.y}, {X(t), b.y}, alpha(p.line, 0.7f));
            std::string s = label(t);
            L.drawText(s, X(t) - L.textWidth(s, fs) / 2, b.y + 15, fs, p.muted, ui);
        }
        for (double t = std::ceil(pl.y0 / sy) * sy; t <= pl.y1 + 1e-9; t += sy) {
            dl->AddLine({a.x, Y(t)}, {b.x, Y(t)}, alpha(p.line, 0.7f));
            std::string s = label(t);
            L.drawText(s, a.x - L.textWidth(s, fs) - 6, Y(t) + 4, fs, p.muted, ui);
        }
        if (pl.x0 < 0 && pl.x1 > 0) dl->AddLine({X(0), a.y}, {X(0), b.y}, p.muted, 1.2f);
        if (pl.y0 < 0 && pl.y1 > 0) dl->AddLine({a.x, Y(0)}, {b.x, Y(0)}, p.muted, 1.2f);
        static const ImU32 COLS[] = {IM_COL32(0x3b, 0x82, 0xf6, 255), IM_COL32(0xe5, 0x48, 0x4d, 255), IM_COL32(0x2f, 0x9e, 0x5b, 255),
                                     IM_COL32(0xf5, 0x9e, 0x0b, 255), IM_COL32(0x8b, 0x5c, 0xf6, 255), IM_COL32(0x14, 0xb8, 0xa6, 255)};
        dl->PushClipRect(a, b, true);
        double span = pl.y1 - pl.y0;
        for (size_t s = 0; s < pl.series.size(); s++) {
            std::vector<ImVec2> seg;
            auto flush = [&] {
                if (seg.size() > 1) dl->AddPolyline(seg.data(), int(seg.size()), COLS[s % 6], 0, 2.2f);
                seg.clear();
            };
            if (pl.series[s].points) {  // table data
                for (auto& [px, py] : pl.series[s].pts)
                    if (!std::isnan(py)) dl->AddCircleFilled({X(px), Y(py)}, 4.5f, COLS[s % 6]);
                continue;
            }
            double prev = NAN;
            for (auto& [px, py] : pl.series[s].pts) {
                // ponytail: a jump over 3 view heights between samples is an asymptote, not a steep line
                if (std::isnan(py) || (!std::isnan(prev) && std::fabs(py - prev) > 3 * span)) flush();
                if (!std::isnan(py)) seg.push_back({X(px), Y(std::clamp(py, pl.y0 - 2 * span, pl.y1 + 2 * span))});
                prev = py;
            }
            flush();
        }
        dl->PopClipRect();
        float ly = a.y + 6;  // legend
        for (size_t s = 0; s < pl.series.size(); s++, ly += 18) {
            dl->AddRectFilled({a.x + 8, ly + 5}, {a.x + 22, ly + 8}, COLS[s % 6], 1);
            L.drawText(pl.series[s].label, a.x + 28, ly + 11, 13, p.fg, ui);
        }
        if (!pl.error.empty()) L.drawText(pl.error, a.x + 8, b.y - 8, 13, IM_COL32(0xe5, 0x48, 0x4d, 255), ui);
        if (reset) views.erase(k), viewPlots.erase(k);
        else if (newView) views[k] = *newView, viewPlots[k] = plot(ed, ed.doc[k], newView->data());
        return h;
    }

    void drawDoc() {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 origin = ImGui::GetCursorScreenPos();
        float avail = ImGui::GetContentRegionAvail().x, colW = printMode ? avail : std::min(pageWidth, avail);
        float x0 = origin.x + std::max(0.f, (avail - colW) / 2) + 32, y = origin.y + 48, maxW = 0;
        if (printMode) y += 48 - printTop;
        L.dl = dl;
        L.ed = printMode ? nullptr : &ed;
        L.stops.clear();
        auto vl = printMode ? std::nullopt : vi.visualLines(ed);
        const Palette& p = pal();
        float blockW = colW - 64;
        bool slid = false;  // a slider changed the doc during this frame
        L.eqNums = eqNumbers(ed.doc);  // ponytail: recounted every frame; cache on changes if long notes get slow
        for (size_t k = 0; k < ed.doc.size(); k++) {
            const Row& row = ed.doc[k];
            Line kind = lineStyle(row).kind;
            if (printMode) lineStarts.push_back(y + printTop - 48);
            bool here = !printMode && ed.topOf(ed.cur.row) == &row, first = k == 0 || lineStyle(ed.doc[k - 1]).kind != kind;
            if (kind == Line::Table && first) tableTabs(k);
            if (kind != Line::Table) L.tabs.clear();
            L.tableHeader = kind == Line::Table && first;
            // a divider or picture shows its source only while the cursor is on it; a stop keeps it clickable
            if ((kind == Line::Rule || kind == Line::Image || kind == Line::Section) && !here) {
                float h = kind == Line::Rule ? L.textSize * 1.4f : kind == Line::Section ? L.textSize * 3.2f
                        : drawImage(row, x0, y + 6, blockW, false) + 12;
                if (kind == Line::Rule) dl->AddLine({x0, y + h / 2}, {x0 + blockW, y + h / 2}, p.line, 1.5f);
                else if (kind == Line::Section) {  // a banner: the title over a heavy rule; its own variables below
                    float by = y + h - L.textSize * 0.7f;
                    std::string t = sectionTitle(row);
                    L.drawText("§", x0 - 22, by - 8, L.textSize, p.muted, bold);
                    L.drawText(t.empty() ? "Section" : t, x0, by - 8, L.textSize * 1.1f, p.fg, bold);
                    dl->AddRectFilled({x0, by}, {x0 + blockW, by + 2.5f}, p.fg, 1);
                } else drawImage(row, x0, y + 6, blockW, true);
                L.stops.push_back({&row, 0, x0, y, y + h});
                y += h;
                continue;
            }
            Box b = L.measureLine(row);
            y += L.textSize * (k == 0 ? 0 : kind == Line::H1 ? 0.9f : kind == Line::H2 ? 0.6f : kind == Line::H3 ? 0.35f : 0);
            float h = std::max(L.textSize * 1.7f, b.asc + b.desc + 10);
            float base = std::round(y + (h - b.asc - b.desc) / 2 + b.asc);
            if (vl && int(k) >= vl->first && int(k) <= vl->second) dl->AddRectFilled({x0 - 6, y}, {x0 + colW - 58, y + h}, p.sel);
            if (kind == Line::Table) {  // grid: borders at the column stops, a rule under the header
                bool last = k + 1 == ed.doc.size() || lineStyle(ed.doc[k + 1]).kind != Line::Table;
                float bx = x0 + 0.45f * L.textSize;  // the middle of a border's padding
                for (float t : L.tabs) dl->AddLine({bx + t, y}, {bx + t, y + h}, p.line, 1.2f);
                float r = bx + (L.tabs.empty() ? 0 : L.tabs.back());
                if (first) dl->AddRectFilled({bx, y}, {r, y + h}, alpha(p.line, 0.35f));
                dl->AddLine({bx, y}, {r, y}, p.line, first ? 1.2f : 1);
                if (first) dl->AddLine({bx, y + h}, {r, y + h}, p.muted, 1.2f);
                if (last) dl->AddLine({bx, y + h}, {r, y + h}, p.line, 1.2f);
            }
            L.drawLine(row, x0, base);
            if (auto t = eqTag(row); t.first >= 0) {  // the equation's number at the right edge
                auto f = L.eqNums.find(eqName(row, t));
                std::string n = "(" + std::to_string(f == L.eqNums.end() ? 0 : f->second) + ")";
                L.drawText(n, x0 + blockW - L.textWidth(n, L.textSize), base, L.textSize, p.fg, ui);
            }
            if (kind == Line::Image) h += drawImage(row, x0, y + h, blockW, true) + 8;
            if (kind == Line::Plot && k < plots.size() && plots[k]) {
                if (views.count(int(k)) && !viewPlots.count(int(k))) viewPlots[int(k)] = plot(ed, row, views[int(k)].data());
                if (plots[k]->is3d()) h += drawPlot3D(*plots[k], x0, y + h, blockW, int(k)) + 12;
                else h += drawPlot(views.count(int(k)) ? viewPlots[int(k)] : *plots[k], x0, y + h, blockW, int(k)) + 12;
            }
            float w = b.w;
            if (!printMode && k < ghosts.size() && ghosts[k]) {
                L.drawText(ghosts[k]->text, x0 + b.w + 0.5f * L.textSize, base, L.textSize * 0.9f, ghosts[k]->warn ? IM_COL32(0xe5, 0x48, 0x4d, 255) : alpha(p.muted, 0.85f));
                w += L.textWidth(ghosts[k]->text, L.textSize * 0.9f) + L.textSize;
            }
            if (kind == Line::Text && sliders && !printMode && !(k > 0 && stepBar(ed.doc[k - 1]) >= 0)) slid = slider(ed.doc[k], x0 + w + 20, base, int(k)) || slid;
            maxW = std::max(maxW, w);
            y += h;
        }
        if (slid) {
            ed.reindex();
            changed();
            scrollToCaret = false;  // dragging a slider shouldn't scroll back to the cursor
        }
        if (printMode) { lineStarts.push_back(y + printTop - 48); return; }
        // search matches: everything the pattern hits, as you type it after "/"
        if (std::string pat = vi.highlight(); !pat.empty()) {
            std::unordered_map<const Row*, std::vector<const Stop*>> at;
            for (auto& s : L.stops) at[s.row].push_back(&s);
            for (auto& m : Vi::matches(ed, pat)) {
                const Stop *a = nullptr, *b = nullptr;
                for (auto* s : at[&ed.doc[m.line]]) {
                    if (s->i == m.a) a = s;
                    if (s->i == m.b) b = s;
                }
                if (a && b) dl->AddRectFilled({a->x, a->y0}, {b->x, a->y1}, IM_COL32(250, 204, 21, 80), 3);
            }
        }
        // caret: a bar in Insert, a block over the item under the cursor otherwise
        caretPos = {-1, -1};
        for (size_t s = 0; s < L.stops.size(); s++) {
            const Stop& st = L.stops[s];
            if (st.row != ed.cur.row || st.i != ed.cur.i) continue;
            float y0 = st.y0, y1 = st.y1;
            if (vi.mode == Mode::Insert) dl->AddRectFilled({st.x - 0.5f, y0}, {st.x + 1.5f, y1}, p.fg);
            else {
                float x1 = s + 1 < L.stops.size() && L.stops[s + 1].row == st.row ? L.stops[s + 1].x : st.x;
                if (x1 - st.x < 0.4f * L.textSize) x1 = st.x + 0.45f * L.textSize;
                dl->AddRectFilled({st.x, y0}, {x1, y1}, alpha(p.fg, 0.4f));
            }
            caretPos = {st.x, y1};
            if (scrollToCaret) {
                ImVec2 wp = ImGui::GetWindowPos();
                float wh = ImGui::GetWindowHeight(), sy = ImGui::GetScrollY();
                if (y0 - wp.y < 24) ImGui::SetScrollY(sy + (y0 - wp.y) - 24);
                else if (y1 - wp.y > wh - 40) ImGui::SetScrollY(sy + (y1 - wp.y) - wh + 40);
                scrollToCaret = false;
            }
            break;
        }
        ImGui::SetCursorScreenPos(origin);  // sliders and plots placed widgets; size the scroll area from the top
        ImGui::Dummy({std::max(maxW + 64, colW), y - origin.y + ImGui::GetWindowHeight() * 0.4f});
        // mouse: click places the cursor (Ctrl-click follows a [[link]]), drag selects
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemHovered()) {
            sideFocus = false;
            vi.mouse(ed, hit(ImGui::GetMousePos()), false);
            if (ImGui::GetIO().KeyCtrl && !ed.inSlot())
                for (auto sp : linkSpans(*ed.cur.row))
                    if (ed.cur.i >= sp.first && ed.cur.i < sp.second) return run("link " + linkTitle(*ed.cur.row, sp));
            if (ImGui::GetIO().KeyCtrl && !ed.inSlot())  // Ctrl-click on [#tag]: go to that equation
                for (auto sp : eqRefs(*ed.cur.row))
                    if (ed.cur.i >= sp.first && ed.cur.i <= sp.second) {
                        std::string name = eqName(*ed.cur.row, sp);
                        for (auto& r : ed.doc)
                            if (auto t = eqTag(r); t.first >= 0 && eqName(r, t) == name) ed.cur = {&r, 0}, scrollToCaret = true;
                        return;
                    }
            Line kind = lineStyle(*ed.cur.row).kind;  // a click on a checkbox ticks it
            if ((kind == Line::Todo || kind == Line::Done) && ed.topOf(ed.cur.row) == ed.cur.row &&
                ImGui::GetMousePos().x < x0 + 1.3f * L.textSize && ImGui::GetMousePos().x > x0)
                vi.toggleDone(ed);
            dragging = true;
            changed();
        } else if (dragging && ImGui::IsMouseDragging(0, 3)) {
            vi.mouse(ed, hit(ImGui::GetMousePos()), true);
        }
        if (!ImGui::IsMouseDown(0)) dragging = false;
        // right-click in a table or matrix: add and remove rows and columns
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(1) && !ImGui::IsAnyItemHovered()) {
            vi.mouse(ed, hit(ImGui::GetMousePos()), false);
            if (ed.inGrid()) ImGui::OpenPopup("grid");
        }
        if (ImGui::BeginPopup("grid")) {
            bool m = ed.inSlot();
            ImGui::TextDisabled(m ? "Matrix" : "Table");
            static const std::pair<const char*, const char*> OPS[] = {{"Add row above", "row above"}, {"Add row below", "row below"},
                                                                      {"Add column left", "col left"}, {"Add column right", "col right"},
                                                                      {nullptr, nullptr}, {"Delete row", "row delete"}, {"Delete column", "col delete"}};
            for (auto& [label, cmd] : OPS)
                if (!label) ImGui::Separator();
                else if (ImGui::MenuItem(label)) run(cmd);
            ImGui::EndPopup();
        }
    }


    // ---------- windows: settings, help, palette, file browser ----------
    // a centred window over the editor; Esc closes it
    bool beginPanel(const char* name, bool& open, ImVec2 size) {
        ImVec2 d = ImGui::GetIO().DisplaySize;
        size = {std::min(size.x, d.x - 40), std::min(size.y, d.y - 60)};
        ImGui::SetNextWindowSize(size, ImGuiCond_Appearing);
        ImGui::SetNextWindowPos({d.x / 2, d.y / 2}, ImGuiCond_Appearing, {0.5f, 0.5f});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {16, 14});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {8, 7});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::ColorConvertU32ToFloat4(pal().pop));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::ColorConvertU32ToFloat4(alpha(pal().line, 0.7f)));
        bool vis = ImGui::Begin(name, &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
        bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        panelFocused = panelFocused || focused;
        if (focused && ImGui::IsKeyPressed(ImGuiKey_Escape)) open = false;
        return vis;
    }
    void endPanel() {
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(4);
    }
    void section(const char* t) {
        ImGui::Spacing();
        ImGui::PushFont(bold, 13);
        ImGui::TextDisabled("%s", t);
        ImGui::PopFont();
        ImGui::Separator();
    }
    void hint(const char* t) {
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", t);
    }

    void drawSettings() {
        if (!showSettings) return;
        if (!beginPanel("Settings", showSettings, {620, 620})) return endPanel();
        bool cfgDirty = false, math = false;
        auto check = [&](const char* label, bool& v, const char* tip = nullptr, bool isMath = false) {
            if (ImGui::Checkbox(label, &v)) cfgDirty = true, math = math || isMath;
            if (tip) hint(tip);
        };
        if (ImGui::BeginTabBar("tabs")) {
            if (ImGui::BeginTabItem("Appearance")) {
                section("Theme");
                int theme = dark ? 0 : 1;
                if (ImGui::RadioButton("Dark", &theme, 0)) dark = true, cfgDirty = true;
                ImGui::SameLine();
                if (ImGui::RadioButton("Light", &theme, 1)) dark = false, cfgDirty = true;
                section("Text");
                ImGui::SetNextItemWidth(260);
                if (ImGui::SliderFloat("Text size", &textSize, 12, 32, "%.0f px")) applySizes(), cfgDirty = true;
                hint("Also Ctrl-= and Ctrl-- (Ctrl-0 resets)");
                ImGui::SetNextItemWidth(260);
                if (ImGui::SliderFloat("Page width", &pageWidth, 500, 2000, "%.0f px")) cfgDirty = true;
                hint("The widest the writing column gets; a narrow window is always filled");
                section("Window");
                check("Formatting buttons in the toolbar", toolbar);
                check("Sidebar", sidebar, "Ctrl-\\ shows and hides it");
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Editor")) {
                section("Keys");
                int keys = vim ? 0 : 1;
                if (ImGui::RadioButton("Vim (modes: i to type, Esc to stop)", &keys, 0)) vim = true, cfgDirty = true;
                if (ImGui::RadioButton("Standard (always typing, Ctrl-z/c/x/v, Esc closes the popup)", &keys, 1)) vim = false, cfgDirty = true;
                check("CapsLock remapped to Escape works as Escape", g_capsEscape,
                      "When your system maps CapsLock to Escape (caps:escape), opman sees the physical CapsLock key; this makes it Escape");
                section("While you write");
                check("Live results after each line", results, "The faint \"= 17\" at the end of a line; Tab writes it in", true);
                check("Sliders on lines like a = 2", sliders, "Drag to change the number; results and plots follow");
                ImGui::SetNextItemWidth(200);
                if (ImGui::SliderFloat("Autosave after", &autosave, 0.2f, 10, "%.1f s idle")) cfgDirty = true;
                hint("Notes save themselves this long after you stop typing (Ctrl-s saves at once)");
                section("Start");
                check("Open the last note at start (otherwise a new note)", openLast);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Autocomplete")) {
                section("Suggestions popup");
                check("Show suggestions while typing", completeOpts.popup,
                      "Off: the popup only opens when you press Ctrl-Space (functions, symbols, results after =)");
                ImGui::BeginDisabled(!completeOpts.popup);
                ImGui::SetNextItemWidth(200);
                if (ImGui::SliderInt("Letters before it opens", &completeOpts.minLetters, 1, 4)) cfgDirty = true;
                hint("How much of a word you type before suggestions appear: 1 opens at once, 3 waits for \"sin\"");
                ImGui::EndDisabled();
                ImGui::SetNextItemWidth(200);
                if (ImGui::SliderInt("Most entries shown", &completeOpts.maxItems, 3, 15)) cfgDirty = true;
                ImGui::TextUnformatted("Accept a suggestion with");
                ImGui::SameLine(220);
                if (ImGui::RadioButton("Enter", &completeOpts.accept, 0)) cfgDirty = true;
                ImGui::SameLine();
                if (ImGui::RadioButton("Tab", &completeOpts.accept, 1)) cfgDirty = true;
                ImGui::SameLine();
                if (ImGui::RadioButton("Both", &completeOpts.accept, 2)) cfgDirty = true;
                hint("Enter: Enter takes the highlighted entry for a math word (Tab still takes results).\n"
                     "Tab: Enter always starts a new line unless you moved through the list with the arrows.\n"
                     "Ctrl-1…9 picks an entry directly, and Esc closes the popup, in every mode.");
                check("Rank entries by what you use most", completeOpts.learn, "Off: always the same order for the same letters");
                section("Automatic conversion in math");
                check("Math words turn into symbols", completeOpts.convert,
                      "sin, sqrt, alpha, pi, infty… become their function or symbol once you type past them (sqrt8 → √8)");
                check("Symbol combinations", completeOpts.combos, "-> → · <= ≤ · >= ≥ · != ≠ · => ⇒ · +- ± · ~= ≈ · ... …");
                section("Results");
                check("Live results after each line", results, "The faint \"= 17\" at the end of a line; Tab writes it in (also in Editor)", true);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Math")) {
                section("Results");
                ImGui::SetNextItemWidth(200);
                if (ImGui::SliderInt("Significant digits", &mathOpts.digits, 2, 15)) cfgDirty = math = true;
                hint("Decimal results are rounded to this many significant digits: 3.141592654 at 10, 3.142 at 4");
                int ang = mathOpts.degrees ? 1 : 0;
                ImGui::TextUnformatted("Angles");
                ImGui::SameLine(160);
                if (ImGui::RadioButton("Radians", &ang, 0)) mathOpts.degrees = false, cfgDirty = math = true;
                ImGui::SameLine();
                if (ImGui::RadioButton("Degrees", &ang, 1)) mathOpts.degrees = true, cfgDirty = math = true;
                hint("Degrees: sin(30) = 1/2, arctan(1) = 45");
                int dec = mathOpts.decimals ? 1 : 0;
                ImGui::TextUnformatted("Live results");
                ImGui::SameLine(160);
                if (ImGui::RadioButton("Exact (3/4)", &dec, 0)) mathOpts.decimals = false, cfgDirty = math = true;
                ImGui::SameLine();
                if (ImGui::RadioButton("Decimal (0.75)", &dec, 1)) mathOpts.decimals = true, cfgDirty = math = true;
                ImGui::TextUnformatted("Numbers");
                ImGui::SameLine(160);
                int comma = decimalComma;
                if (ImGui::RadioButton("Point (2.5)", &comma, 0)) decimalComma = false, cfgDirty = math = true;
                ImGui::SameLine();
                if (ImGui::RadioButton("Comma (2,5)", &comma, 1)) decimalComma = true, cfgDirty = math = true;
                hint("With a comma, a ; separates arguments: max(1; 2,5). A comma followed by a space still does: max(1, 2,5)");
                section("Plots");
                float r[2] = {float(mathOpts.plotX0), float(mathOpts.plotX1)};
                ImGui::SetNextItemWidth(260);
                if (ImGui::InputFloat2("Default x range", r, "%g") && r[0] < r[1]) mathOpts.plotX0 = r[0], mathOpts.plotX1 = r[1], cfgDirty = math = true;
                hint("Used when a plot line gives no range such as −3 < x < 3");
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Files")) {
                section("Library");
                ImGui::TextWrapped("New notes are saved in %s", dir.string().c_str());
                if (ImGui::Button("Change library folder…"))
                    askPath(false, "Library folder", dir, {}, [this](std::string f) {
                        saveNow();
                        dir = f;
                        saveCfg();
                        listFiles();
                    }, true);
                ImGui::SameLine();
                if (ImGui::Button("Open in file manager")) {
                    std::string keep = current;
                    current = (dir / "x").string();
                    revealFolder();
                    current = keep;
                }
                section("Export");
                ImGui::TextWrapped("Quick exports (:export pdf) go to %s", (exportDir.empty() ? home() : fs::path(exportDir)).string().c_str());
                if (ImGui::Button("Change export folder…"))
                    askPath(false, "Export folder", exportDir.empty() ? home() : fs::path(exportDir), {}, [this](std::string f) {
                        exportDir = f;
                        saveCfg();
                    }, true);
                static char tex[256];
                ImGui::SetNextItemWidth(260);
                if (ImGui::Combo("PDF export", &pdfEngine, "Automatic\0pdflatex\0Built-in (no LaTeX)\0")) cfgDirty = true;
                hint("Built-in needs nothing installed and looks like the app, but pages are pictures, so text can't be selected. Automatic uses pdflatex when it's installed");
                std::snprintf(tex, sizeof tex, "%s", pdflatex.c_str());
                ImGui::SetNextItemWidth(260);
                if (ImGui::InputText("pdflatex command", tex, sizeof tex)) pdflatex = tex, cfgDirty = true;
                hint("The program that turns LaTeX into PDF: pdflatex, or a full path such as C:\\texlive\\bin\\pdflatex.exe");
                section("Dialogs");
                check("Use the system's open and save dialogs", nativeDialogs,
                      "Off: opman's own file browser. On Linux the system dialogs need zenity or kdialog");
                if (nativeDialogs && !dialog::available()) ImGui::TextDisabled("No system dialog found here: the built-in browser is used.");
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        endPanel();
        vi.standard = !vim;
        if (cfgDirty) saveCfg();
        if (math) mathChanged();
    }

    // the help: topics on the left, the selected one (or every match of the search) on the right
    void drawHelpBody(const char* body, const std::string& q) {
        const Palette& p = pal();
        std::string b = body;
        size_t i = 0;
        while (i <= b.size()) {
            size_t e = b.find('\n', i);
            if (e == std::string::npos) e = b.size();
            std::string line = b.substr(i, e - i);
            i = e + 1;
            if (!q.empty() && lower(line).find(q) == std::string::npos && !line.starts_with("## ")) continue;
            if (line.empty()) { ImGui::Spacing(); continue; }
            if (line.starts_with("## ")) {
                ImGui::Spacing();
                ImGui::PushFont(bold, 15);
                ImGui::TextUnformatted(line.c_str() + 3);
                ImGui::PopFont();
            } else if (line.starts_with("`")) {  // `keys` `more keys` description
                std::string rest = line;
                float x = ImGui::GetCursorPosX();
                while (rest.starts_with("`")) {
                    size_t c = rest.find('`', 1);
                    if (c == std::string::npos) break;
                    std::string k = rest.substr(1, c - 1);
                    ImGui::PushFont(L.mono, 13);
                    ImVec2 sz = ImGui::CalcTextSize(k.c_str()), a = ImGui::GetCursorScreenPos();
                    ImGui::GetWindowDrawList()->AddRectFilled({a.x - 3, a.y - 1}, {a.x + sz.x + 3, a.y + sz.y + 1}, alpha(p.fn, 0.15f), 4);
                    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(p.fn), "%s", k.c_str());
                    ImGui::PopFont();
                    ImGui::SameLine(0, 10);
                    rest = rest.substr(c + 1);
                    while (rest.starts_with(" ")) rest.erase(0, 1);
                }
                ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), x + 150));
                ImGui::TextWrapped("%s", rest.c_str());
            } else if (line.starts_with("- ")) {
                ImGui::Bullet();
                ImGui::TextWrapped("%s", line.c_str() + 2);
            } else ImGui::TextWrapped("%s", line.c_str());
        }
    }
    void drawHelp() {
        if (!showHelp) return;
        auto& topics = helpTopics();
        static int sel = 0;
        if (!helpTopic.empty()) {  // :help files
            for (size_t t = 0; t < topics.size(); t++)
                if (lower(topics[t].title).find(lower(helpTopic)) != std::string::npos) { sel = int(t); break; }
            helpTopic.clear();
        }
        if (!beginPanel("Help", showHelp, {900, 680})) return endPanel();
        std::string q = lower(helpQuery);
        ImGui::BeginChild("topics", {210, 0});
        if (ImGui::Button("Open the tour", {-1, 0})) run("tour");
        ImGui::Spacing();
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##q", "Search help…", helpQuery, sizeof helpQuery);
        ImGui::Spacing();
        for (size_t t = 0; t < topics.size(); t++) {
            bool hit = q.empty() || lower(topics[t].title).find(q) != std::string::npos || lower(topics[t].body).find(q) != std::string::npos;
            if (!hit) continue;
            if (ImGui::Selectable(topics[t].title, q.empty() && int(t) == sel)) sel = int(t), helpQuery[0] = 0;
        }
        ImGui::EndChild();
        ImGui::SameLine(0, 18);
        ImGui::BeginChild("body", {0, 0});
        ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x - 10);
        if (q.empty()) {
            ImGui::PushFont(bold, 22);
            ImGui::TextUnformatted(topics[sel].title);
            ImGui::PopFont();
            ImGui::Spacing();
            drawHelpBody(topics[sel].body, "");
        } else {
            bool any = false;
            for (auto& t : topics) {
                if (lower(t.body).find(q) == std::string::npos && lower(t.title).find(q) == std::string::npos) continue;
                any = true;
                ImGui::PushFont(bold, 17);
                ImGui::TextUnformatted(t.title);
                ImGui::PopFont();
                drawHelpBody(t.body, lower(t.title).find(q) != std::string::npos ? "" : q);
                ImGui::Spacing();
                ImGui::Separator();
            }
            if (!any) ImGui::TextDisabled("Nothing about \"%s\"", helpQuery);
        }
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
        endPanel();
    }

    // ---------- command palette: every action, setting, note and help topic by name ----------
    struct Action {
        std::string name, keys;
        std::function<void()> run;
    };
    std::vector<Action> actions() {
        std::vector<Action> a = {
            {"New note", "Ctrl-Shift-n", [this] { newDoc(); }},
            {"Open note…", "Ctrl-Shift-o", [this] { openDialog(); }},
            {"Save", "Ctrl-s", [this] { run("w"); }},
            {"Save as…", "Ctrl-Shift-s", [this] { saveAsDialog(); }},
            {"Export as Markdown…", "", [this] { exportDialog("md"); }},
            {"Export as web page (HTML)…", "", [this] { exportDialog("html"); }},
            {"Export as LaTeX…", "", [this] { exportDialog("tex"); }},
            {"Export as PDF…", "", [this] { exportDialog("pdf"); }},
            {"Show in file manager", ":folder", [this] { revealFolder(); }},
            {"Rename note…", ":rename", [this] { exCmd("rename "); }},
            {"Move note to trash", ":trash", [this] { run("trash"); }},
            {"Go back to the previous note", "Ctrl-o", [this] { run("back"); }},
            {"Settings", "Ctrl-,", [this] { showSettings = true; }},
            {"Help", "F1", [this] { showHelp = true; }},
            {"Toggle sidebar", "Ctrl-\\", [this] { run("sidebar"); }},
            {"Outline of this note", ":outline", [this] { run("outline"); }},
            {"Find", "/", [this] { vi.mode = Mode::Command, vi.prompt = '/', vi.cmdline.clear(); }},
            {"Find and replace", ":%s/old/new/g", [this] { exCmd("%s/"); }},
            {"Open the tour (an example note)", ":tour", [this] { run("tour"); }},
            {"Print…", ":print · Ctrl-p (standard keys)", [this] { run("print"); }},
            {"Insert table", "Space |", [this] { run("table 3 2"); }},
            {"Table/matrix: add row below", ":row below · Shift-Enter", [this] { run("row below"); }},
            {"Table/matrix: add row above", ":row above", [this] { run("row above"); }},
            {"Table/matrix: add column right", ":col right", [this] { run("col right"); }},
            {"Table/matrix: add column left", ":col left", [this] { run("col left"); }},
            {"Table/matrix: delete row", ":row delete", [this] { run("row delete"); }},
            {"Table/matrix: delete column", ":col delete", [this] { run("col delete"); }},
            {"Insert section (new set of variables)", "Space =", [this] { run("section"); }},
            {"Insert divider", "Space /", [this] { run("rule"); }},
            {"Insert picture…", ":image", [this] {
                 askPath(false, "Insert picture", home(), {{"Pictures", "*.png *.jpg *.jpeg *.gif *.bmp"}}, [this](std::string f) { addImage(f); });
             }},
            {"Math mode on/off", "Ctrl-m", [this] { ed.mathMode = !ed.mathMode; }},
            {dark ? "Light theme" : "Dark theme", "", [this] { dark = !dark; saveCfg(); }},
            {"Bigger text", "Ctrl-=", [this] { run("zoom in"); }},
            {"Smaller text", "Ctrl--", [this] { run("zoom out"); }},
            {"Reset text size", "Ctrl-0", [this] { run("zoom reset"); }},
            {mathOpts.degrees ? "Angles in radians" : "Angles in degrees", "", [this] { run(mathOpts.degrees ? "set radians" : "set degrees"); }},
            {mathOpts.decimals ? "Exact results (fractions)" : "Decimal results", "", [this] { run(mathOpts.decimals ? "set fractions" : "set decimals"); }},
            {vim ? "Standard keys (no vim modes)" : "Vim keys", "", [this] { run(vim ? "set standard" : "set vim"); }},
            {results ? "Hide live results" : "Show live results", "", [this] { results = !results; mathChanged(); }},
            {"Quit", ":q", [this] { quit = true; }},
        };
        for (auto& t : helpTopics()) {
            std::string title = t.title;
            a.push_back({"Help: " + title, "", [this, title] { showHelp = true; helpTopic = title; }});
        }
        for (auto& f : files) {
            std::string n = f.name;
            a.push_back({"Open: " + titleOf(f.meta), "", [this, n] { openDoc(n); }});
        }
        return a;
    }
    void exCmd(const std::string& start) {  // the : line, typed in for the user to finish
        vi.mode = Mode::Command;
        vi.prompt = ':';
        vi.cmdline = start;
    }
    void drawPalette() {
        if (!showPalette) return;
        ImVec2 d = ImGui::GetIO().DisplaySize;
        float w = std::min(620.f, d.x - 40);
        ImGui::SetNextWindowPos({d.x / 2 - w / 2, 70}, ImGuiCond_Always);
        ImGui::SetNextWindowSizeConstraints({w, 0}, {w, FLT_MAX});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {10, 10});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::ColorConvertU32ToFloat4(pal().pop));
        ImGui::Begin("##palette", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(-1);
        bool typed = ImGui::InputTextWithHint("##pq", "Type a command, setting, note or help topic…", paletteQuery, sizeof paletteQuery);
        if (typed) paletteSel = 0;
        auto all = actions();
        std::string q = lower(paletteQuery);
        std::vector<std::pair<int, size_t>> hits;  // score, index: word starts first, then anywhere, then letters in order
        for (size_t j = 0; j < all.size(); j++) {
            std::string n = lower(all[j].name);
            int sc = q.empty() ? 1 : n.starts_with(q) ? 4 : n.find(" " + q) != std::string::npos ? 3 : n.find(q) != std::string::npos ? 2 : 0;
            if (!sc && q.size() > 1) {
                size_t at = 0;
                bool ok = true;
                for (char c : q) if ((at = n.find(c, at)) == std::string::npos) { ok = false; break; } else at++;
                sc = ok;
            }
            if (sc && (!q.empty() || !all[j].name.starts_with("Open: ") || hits.size() < 40)) hits.push_back({sc, j});
        }
        std::stable_sort(hits.begin(), hits.end(), [](auto& a, auto& b) { return a.first > b.first; });
        if (hits.size() > 12) hits.resize(12);
        int n = int(hits.size());
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) paletteSel = n ? (paletteSel + 1) % n : 0;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) paletteSel = n ? (paletteSel + n - 1) % n : 0;
        paletteSel = std::clamp(paletteSel, 0, std::max(0, n - 1));
        int chosen = -1;
        for (int j = 0; j < n; j++) {
            auto& act = all[hits[j].second];
            ImGui::PushID(j);
            if (ImGui::Selectable("##a", j == paletteSel, 0, {0, 24})) chosen = j;
            ImGui::SameLine(8);
            ImGui::TextUnformatted(act.name.c_str());
            if (!act.keys.empty()) {
                ImGui::SameLine(w - 20 - ImGui::CalcTextSize(act.keys.c_str()).x);
                ImGui::TextDisabled("%s", act.keys.c_str());
            }
            ImGui::PopID();
        }
        if (!n) ImGui::TextDisabled("No match");
        if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) chosen = paletteSel;
        bool close = ImGui::IsKeyPressed(ImGuiKey_Escape) || (!ImGui::IsWindowFocused() && !ImGui::IsWindowAppearing());
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);
        if (chosen >= 0 && chosen < n) {
            showPalette = false;
            all[hits[chosen].second].run();
            changed();
        } else if (close) showPalette = false;
    }

    // ---------- the built-in file browser (when there is no system dialog) ----------
    void drawBrowser() {
        if (!browser.open) return;
        bool open = true;
        if (!beginPanel((browser.title + "###browser").c_str(), open, {640, 520})) { endPanel(); browser.open = open; return; }
        std::error_code ec;
        static char path[1024];
        std::snprintf(path, sizeof path, "%s", browser.at.string().c_str());
        if (ImGui::Button("↑ Up")) browser.at = browser.at.parent_path();
        ImGui::SameLine();
        if (ImGui::Button("Home")) browser.at = home();
        ImGui::SameLine();
        if (ImGui::Button("Library")) browser.at = dir;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##path", path, sizeof path, ImGuiInputTextFlags_EnterReturnsTrue) && fs::is_directory(path, ec)) browser.at = path;
        std::vector<fs::directory_entry> dirs, list;
        for (auto& e : fs::directory_iterator(browser.at, fs::directory_options::skip_permission_denied, ec)) {
            std::string n = e.path().filename().string();
            if (n.starts_with(".")) continue;
            if (e.is_directory(ec)) dirs.push_back(e);
            else if (!browser.folders && (browser.ext.empty() || browser.ext.find(lower(e.path().extension().string())) != std::string::npos ||
                                          (browser.ext == ".opman" && e.path().extension() == ".json")))
                list.push_back(e);
        }
        auto byName = [](auto& a, auto& b) { return lower(a.path().filename().string()) < lower(b.path().filename().string()); };
        std::sort(dirs.begin(), dirs.end(), byName);
        std::sort(list.begin(), list.end(), byName);
        std::string pick;
        ImGui::BeginChild("entries", {0, -ImGui::GetFrameHeightWithSpacing() - 6}, ImGuiChildFlags_Borders);
        for (auto& e : dirs) {
            std::string n = "▸ " + e.path().filename().string();
            if (ImGui::Selectable(n.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) && ImGui::IsMouseDoubleClicked(0)) browser.at = e.path();
        }
        for (auto& e : list) {
            std::string n = e.path().filename().string();
            bool sel = n == browser.name;
            if (ImGui::Selectable(n.c_str(), sel, ImGuiSelectableFlags_AllowDoubleClick)) {
                std::snprintf(browser.name, sizeof browser.name, "%s", n.c_str());
                if (ImGui::IsMouseDoubleClicked(0)) pick = e.path().string();
            }
        }
        if (dirs.empty() && list.empty()) ImGui::TextDisabled("Empty folder");
        ImGui::EndChild();
        if (!browser.folders) {
            ImGui::SetNextItemWidth(-160);
            bool enter = ImGui::InputTextWithHint("##name", "File name", browser.name, sizeof browser.name, ImGuiInputTextFlags_EnterReturnsTrue);
            if (enter && browser.name[0]) pick = (browser.at / browser.name).string();
            ImGui::SameLine();
        }
        if (ImGui::Button(browser.folders ? "Choose this folder" : browser.save ? "Save" : "Open", {browser.folders ? 0.f : 70.f, 0})) {
            if (browser.folders) pick = browser.at.string();
            else if (browser.name[0]) pick = (browser.at / browser.name).string();
        }
        if (!browser.folders) ImGui::SameLine();
        if (ImGui::Button("Cancel")) open = false;
        endPanel();
        if (!pick.empty()) {
            if (browser.save && !browser.ext.empty() && fs::path(pick).extension().empty()) pick += browser.ext.substr(0, browser.ext.find(' '));
            if (!browser.save && !browser.folders && !fs::exists(pick, ec)) { message = "No such file: " + pick; return; }
            auto done = browser.done;
            browser.open = false;
            done(pick);
            return;
        }
        browser.open = open;
    }

    void fileMenu() {
        if (ImGui::MenuItem("New note", "Ctrl-Shift-n")) newDoc();
        if (ImGui::MenuItem("Open…", "Ctrl-Shift-o")) openDialog();
        if (ImGui::BeginMenu("Open recent")) {
            int keep = sort;
            sort = 0;
            auto list = sortedFiles();
            sort = keep;
            for (size_t j = 0; j < list.size() && j < 12; j++)
                if (ImGui::MenuItem((titleOf(list[j].meta) + "##r" + std::to_string(j)).c_str(), ago(str(list[j].meta, "edited")).c_str()))
                    openDoc(list[j].name);
            if (list.empty()) ImGui::TextDisabled("No notes yet");
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Save", "Ctrl-s")) run("w");
        if (ImGui::MenuItem("Save as…", "Ctrl-Shift-s")) saveAsDialog();
        if (ImGui::BeginMenu("Export")) {
            if (ImGui::MenuItem("Markdown (.md)…")) exportDialog("md");
            if (ImGui::MenuItem("Web page (.html)…")) exportDialog("html");
            if (ImGui::MenuItem("LaTeX (.tex)…")) exportDialog("tex");
            if (ImGui::MenuItem("PDF (.pdf)…")) exportDialog("pdf");
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Print…", vim ? ":print" : "Ctrl-p")) run("print");
        ImGui::Separator();
        if (ImGui::MenuItem("Rename…")) exCmd("rename ");
        if (ImGui::MenuItem("Show in file manager")) revealFolder();
        if (ImGui::MenuItem("Move to trash")) run("trash");
        ImGui::Separator();
        if (ImGui::MenuItem("Command palette", "Ctrl-Shift-p")) showPalette = true, paletteQuery[0] = 0, paletteSel = 0;
        if (ImGui::MenuItem("Settings", "Ctrl-,")) showSettings = true;
        if (ImGui::MenuItem("Help", "F1")) showHelp = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", ":q")) quit = true;
    }

    // headings and sections of the open note, for the sidebar's Outline tab
    void drawOutline() {
        const Palette& p = pal();
        bool any = false;
        int here = ed.lineOf(ed.topOf(ed.cur.row));
        for (size_t k = 0; k < ed.doc.size(); k++) {
            LineStyle st = lineStyle(ed.doc[k]);
            int depth = st.kind == Line::H1 ? 0 : st.kind == Line::H2 ? 1 : st.kind == Line::H3 ? 2 : st.kind == Line::Section ? 0 : -1;
            if (depth < 0) continue;
            any = true;
            std::string t = st.kind == Line::Section ? "§ " + sectionTitle(ed.doc[k]) : plain(Row(ed.doc[k].begin() + st.len, ed.doc[k].end()));
            if (t.empty() || t == "§ ") t += "(untitled)";
            int next = int(ed.doc.size());
            for (size_t j = k + 1; j < ed.doc.size(); j++) {
                Line q = lineStyle(ed.doc[j]).kind;
                if (q == Line::H1 || q == Line::H2 || q == Line::H3 || q == Line::Section) { next = int(j); break; }
            }
            ImGui::SetCursorPosX(10 + depth * 14.f);
            ImGui::PushID(int(k));
            if (st.kind == Line::Section) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(p.fn));
            if (ImGui::Selectable(t.c_str(), here >= int(k) && here < next, 0, {ImGui::GetContentRegionAvail().x - 8, 0})) {
                ed.cur = {&ed.doc[k], 0};
                if (!vim) ed.cur.i = int(ed.doc[k].size());
                scrollToCaret = true;
                sideFocus = false;
            }
            if (st.kind == Line::Section) ImGui::PopStyleColor();
            ImGui::PopID();
        }
        if (!any) {
            ImGui::SetCursorPosX(12);
            ImGui::TextDisabled("No headings or sections yet.");
            ImGui::SetCursorPosX(12);
            ImGui::TextDisabled("Start a line with # or ===.");
        }
    }

    void drawPopup() {
        if (vi.mode != Mode::Insert || vi.items.empty() || caretPos.x < 0) return;
        const Palette& p = pal();
        float rowH = 32, w = 380;
        float h = vi.items.size() * (rowH + 2) + 10;
        ImVec2 pos{caretPos.x - 8, caretPos.y + 6};
        ImVec2 disp = ImGui::GetIO().DisplaySize;
        if (pos.y + h > disp.y - 30) pos.y = caretPos.y - h - L.textSize * 1.6f;
        pos.x = std::clamp(pos.x, 8.f, std::max(8.f, disp.x - w - 24));
        ImGui::SetNextWindowPos(pos);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {4, 4});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 2});
        ImGui::Begin("##pop", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                                           ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        L.dl = dl;
        L.ed = nullptr;
        int clicked = -1;
        for (size_t j = 0; j < vi.items.size(); j++) {
            const Entry& e = vi.items[j];
            auto cat = CATS.find(e.cat);
            unsigned rgb = cat != CATS.end() ? cat->second : 0x8a8a8e;
            ImU32 cc = IM_COL32(rgb >> 16, (rgb >> 8) & 0xFF, rgb & 0xFF, 255);
            ImVec2 a = ImGui::GetCursorScreenPos();
            bool sel = int(j) == ed.pick;
            ImGui::PushID(int(j));
            if (ImGui::InvisibleButton("##e", {w, rowH})) clicked = int(j);
            ImGui::PopID();
            if (sel) {
                dl->AddRectFilled(a, {a.x + w, a.y + rowH}, alpha(cc, 0.16f), 6);
                dl->AddRectFilled(a, {a.x + 3, a.y + rowH}, cc);
            } else if (ImGui::IsItemHovered()) dl->AddRectFilled(a, {a.x + w, a.y + rowH}, alpha(p.line, 0.6f), 6);
            Row preview = e.insert ? *e.insert : Row{e.make};
            float ps = 16;
            Box pb = L.measureMath(preview, ps);
            float tileW = std::clamp(pb.w + 12, 34.f, 150.f);
            ImVec2 t0{a.x + 6, a.y + 2}, t1{t0.x + tileW, a.y + rowH - 2};
            dl->AddRectFilled(t0, t1, alpha(cc, 0.13f), 6);
            dl->PushClipRect(t0, t1, true);
            float pbase = (t0.y + t1.y) / 2 + (pb.asc - pb.desc) / 2;
            L.drawMath(preview, t0.x + std::max(6.f, (tileW - pb.w) / 2), pbase, ps, cc);
            dl->PopClipRect();
            float base = a.y + rowH / 2 + 5;
            L.drawText(e.name, t1.x + 10, base, 14, p.fg, ui);
            std::string catText = lower(e.cat);
            for (auto& ch : catText) ch = char(std::toupper((unsigned char)ch));
            bool tab = e.insert || e.cat == "Program", takes = sel && (tab || vi.navigated || vi.enterPicks(ed));  // a text word needs ↓ first
            int acc = completeOpts.accept;
            std::string kbd = !takes ? (j < 9 ? "Ctrl " + std::to_string(j + 1) : "")
                            : tab || acc == 1 ? "Tab" : acc == 2 ? "Enter/Tab" : "Enter";
            float kw = L.textWidth(kbd, 11), cw = L.textWidth(catText, 11);
            L.drawText(kbd, a.x + w - kw - 8, base - 1, 11, p.muted, ui);
            L.drawText(catText, a.x + w - kw - cw - 22, base - 1, 11, cc, bold);
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
        if (clicked >= 0) {
            vi.pickItem(ed, clicked);
            changed();
        }
    }

    void drawSidebar() {
        const Palette& p = pal();
        ImGui::PushFont(ui, 14);
        ImGui::SetCursorPos({10, 10});
        for (int t = 0; t < 2; t++) {  // Notes | Outline
            if (t) ImGui::SameLine(0, 4);
            bool on = sideTab == t;
            ImGui::PushStyleColor(ImGuiCol_Button, on ? ImGui::ColorConvertU32ToFloat4(p.line) : ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(on ? p.fg : p.muted));
            if (ImGui::Button(t ? "Outline" : "Notes")) sideTab = t;
            ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", t ? "Headings and sections of this note (:outline)" : ("Notes in " + dir.string() + " and ones opened elsewhere").c_str());
        }
        ImGui::SameLine(ImGui::GetWindowWidth() - 36);
        if (ImGui::Button("+")) newDoc();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("New note  (Ctrl-Shift-n · :new)");
        if (sideTab == 1) {
            ImGui::BeginChild("outline", {0, 0});
            drawOutline();
            ImGui::EndChild();
            ImGui::PopFont();
            return;
        }
        ImGui::SetCursorPosX(10);
        ImGui::SetNextItemWidth(ImGui::GetWindowWidth() - 120);
        if (focusFilter) {
            ImGui::SetKeyboardFocusHere();
            focusFilter = false;
        }
        ImGui::InputTextWithHint("##filter", "Filter…", filter, sizeof filter);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90);
        if (ImGui::Combo("##sort", &sort, "Last edited\0Created\0Name\0")) saveCfg();
        auto list = sortedFiles();
        sideSel = std::clamp(sideSel, 0, std::max(0, int(list.size()) - 1));
        float metaH = 96 + (backlinks.empty() ? 0 : 22 + 20 * std::min<size_t>(backlinks.size(), 4));
        ImGui::SetCursorPosX(6);
        ImGui::BeginChild("files", {ImGui::GetWindowWidth() - 12, -metaH});
        ImDrawList* dl = L.dl = ImGui::GetWindowDrawList();
        for (size_t j = 0; j < list.size(); j++) {
            auto& f = list[j];
            ImVec2 a = ImGui::GetCursorScreenPos();
            float w = ImGui::GetContentRegionAvail().x;
            ImGui::PushID(f.name.c_str());
            if (ImGui::InvisibleButton("##f", {w, 40})) {
                openDoc(f.name);
                sideFocus = false;
            }
            bool hov = ImGui::IsItemHovered();
            ImGui::PopID();
            bool open = f.name == current;
            if (open || hov) dl->AddRectFilled(a, {a.x + w, a.y + 40}, p.line, 6);
            if (open) dl->AddRectFilled(a, {a.x + 3, a.y + 40}, IM_COL32(0x25, 0x63, 0xeb, 255));
            if (sideFocus && int(j) == sideSel) dl->AddRect(a, {a.x + w, a.y + 40}, IM_COL32(0x25, 0x63, 0xeb, 255), 6, 0, 1.5f);
            dl->PushClipRect(a, {a.x + w - 4, a.y + 40}, true);
            L.drawText(titleOf(f.meta), a.x + 10, a.y + 18, 14, p.fg, bold);
            std::string q = lower(filter), sub = ago(str(f.meta, "edited"));
            if (!q.empty() && lower(titleOf(f.meta)).find(q) == std::string::npos) {  // matched in the text: show where
                const std::string& t = noteText(f.name);
                size_t at = t.find(q), s0 = at > 20 ? at - 20 : 0;
                while (s0 > 0 && (t[s0] & 0xC0) == 0x80) s0--;
                size_t s1 = std::min(t.size(), at + q.size() + 40);
                while (s1 < t.size() && (t[s1] & 0xC0) == 0x80) s1++;
                sub = (s0 ? "…" : "") + t.substr(s0, s1 - s0);
                std::replace(sub.begin(), sub.end(), '\n', ' ');
            }
            if (q.empty() && !inLibrary(f.name)) sub += "  ·  " + fs::path(f.name).parent_path().filename().string();
            L.drawText(sub, a.x + 10, a.y + 34, 12, p.muted, ui);
            dl->PopClipRect();
            if (hov) ImGui::SetTooltip("%s\n%s", titleOf(f.meta).c_str(), f.name.c_str());
        }
        if (list.empty()) ImGui::TextDisabled("  No documents");
        ImGui::EndChild();
        // meta of the open document
        ImGui::SetCursorPos({0, ImGui::GetWindowHeight() - metaH + 8});
        ImGui::Indent(12);
        ImGui::PushFont(bold, 13);
        ImGui::TextUnformatted(titleOf(meta).c_str());
        ImGui::PopFont();
        ImGui::PushFont(ui, 12);
        ImGui::TextDisabled("Edited %s", ago(str(meta, "edited")).c_str());
        ImGui::TextDisabled("%d words · %d lines", meta.value("words", 0), meta.value("lines", 0));
        if (meta.contains("defines") && meta["defines"].is_array() && !meta["defines"].empty()) {
            std::string d;
            for (auto& x : meta["defines"]) d += (d.empty() ? "" : ", ") + (x.is_string() ? x.get<std::string>() : "");
            ImGui::TextDisabled("Defines %s", d.c_str());
        }
        std::string open;  // opened after the font/indent pops
        if (!backlinks.empty()) {
            ImGui::TextDisabled("Linked from");
            for (size_t j = 0; j < backlinks.size() && j < 4; j++)
                if (ImGui::SmallButton((titleOf(backlinks[j].meta) + "##bl" + std::to_string(j)).c_str())) open = backlinks[j].name;
        }
        ImGui::Unindent(12);
        ImGui::PopFont();
        ImGui::PopFont();
        if (!open.empty()) openDoc(open);
    }

    // formatting at the cursor, for the toolbar's pressed state
    uint16_t fmtHere() const {
        if (vi.mode == Mode::Insert && !ed.range) return ed.style;
        const Row& r = ed.range ? *ed.range->row : *ed.cur.row;
        int i = ed.range ? std::min(ed.range->a, ed.range->b) : ed.cur.i;
        return i < int(r.size()) && r[i].t == T::Ch ? r[i].f : 0;
    }

    void drawToolbar(float h) {
        const Palette& p = pal();
        ImDrawList* dl = L.dl = ImGui::GetWindowDrawList();
        ImVec2 o = ImGui::GetCursorScreenPos();
        float W = ImGui::GetContentRegionAvail().x, x = o.x + 8, cy = o.y + h / 2;
        dl->AddLine({o.x, o.y + h - 1}, {o.x + W, o.y + h - 1}, p.line);
        uint16_t f = fmtHere();
        Line kind = lineStyle(*ed.topOf(ed.cur.row)).kind;
        bool act = false;
        float limit = FLT_MAX;  // buttons past it don't fit: hidden (the palette has them all)
        auto btn = [&](const char* id, const char* tip, bool on, auto icon, float w = 30) {
            if (x + w > limit) { x += w + 2; return false; }
            ImGui::SetCursorScreenPos({x, o.y + 5});
            bool click = ImGui::InvisibleButton(id, {w, h - 10});
            bool hov = ImGui::IsItemHovered();
            if (on || hov) dl->AddRectFilled({x, o.y + 5}, {x + w, o.y + h - 5}, on ? alpha(p.fn, 0.2f) : p.line, 6);
            icon(ImVec2{x + w / 2, cy}, on ? p.fn : p.fg);
            if (hov) ImGui::SetTooltip("%s", tip);
            x += w + 2;
            act = act || click;
            return click;
        };
        auto sep = [&] {
            x += 5;
            if (x < limit) dl->AddLine({x, o.y + 11}, {x, o.y + h - 11}, p.line);
            x += 7;
        };
        auto text = [&](const char* t, ImFont* fo, float size, bool slant = false) {
            return [=, this](ImVec2 c, ImU32 col) {
                float w = fo->CalcTextSizeA(size, FLT_MAX, 0, t).x;
                L.drawText(t, c.x - w / 2, c.y + size * 0.36f, size, col, fo, slant);
            };
        };
        auto lines = [&](ImVec2 c, ImU32 col, float x0) {
            for (int i = -1; i <= 1; i++) dl->AddLine({c.x + x0, c.y + i * 5.f}, {c.x + 8, c.y + i * 5.f}, col, 1.5f);
        };
        if (btn("menu", "File: new, open, save as, export, settings, help", false, [&](ImVec2 c, ImU32 col) {
                for (int i = -1; i <= 1; i++) dl->AddLine({c.x - 7, c.y + i * 5.f}, {c.x + 7, c.y + i * 5.f}, col, 1.6f);
            }))
            ImGui::OpenPopup("filemenu");
        if (ImGui::BeginPopup("filemenu")) {
            fileMenu();
            ImGui::EndPopup();
        }
        // sidebar
        if (btn("side", "Sidebar  (Ctrl-\\ · Space e)", sidebar, [&](ImVec2 c, ImU32 col) {
                dl->AddRect({c.x - 8, c.y - 7}, {c.x + 8, c.y + 7}, col, 3, 0, 1.5f);
                dl->AddRectFilled({c.x - 8, c.y - 7}, {c.x - 3, c.y + 7}, col, 3);
            }))
            run("sidebar");
        sep();
        float right = o.x + W - 3 * 32 - 12;  // palette, help, settings keep their place; formatting is cut off before them
        limit = right;
        if (toolbar) {
        if (btn("undo", vim ? "Undo  (u)" : "Undo  (Ctrl-z)", false, text("↶", L.math, 20))) ed.undo();
        if (btn("redo", vim ? "Redo  (Ctrl-r)" : "Redo  (Ctrl-y)", false, text("↷", L.math, 20))) ed.redo();
        sep();
        if (btn("b", "Bold  (Ctrl-b · Space b)", f & Bold, text("B", bold, 16))) vi.format(ed, Bold);
        if (btn("i", "Italic  (Ctrl-i · Space i)", f & Italic, text("I", ui, 16, true))) vi.format(ed, Italic);
        if (btn("u", "Underline  (Ctrl-u · Space u)", f & Underline, [&](ImVec2 c, ImU32 col) {
                text("U", ui, 16)(c, col);
                dl->AddLine({c.x - 6, c.y + 9}, {c.x + 6, c.y + 9}, col, 1.5f);
            }))
            vi.format(ed, Underline);
        if (btn("s", "Strikethrough  (Space s)", f & Strike, [&](ImVec2 c, ImU32 col) {
                text("S", ui, 16)(c, col);
                dl->AddLine({c.x - 7, c.y}, {c.x + 7, c.y}, col, 1.5f);
            }))
            vi.format(ed, Strike);
        if (btn("c", "Inline code  (Space c)", f & Code, text("</>", L.mono, 12), 34)) vi.format(ed, Code);
        if (btn("h", "Highlight  (Space h)", f & Mark, [&](ImVec2 c, ImU32 col) {
                dl->AddRectFilled({c.x - 9, c.y - 8}, {c.x + 9, c.y + 8}, IM_COL32(250, 204, 21, 110), 3);
                text("A", bold, 14)(c, col);
            }))
            vi.format(ed, Mark);
        if (btn("sup", "Superscript text  (Space ^)", f & Sup, [&](ImVec2 c, ImU32 col) {
                L.drawText("x", c.x - 7, c.y + 6, 15, col, ui);
                L.drawText("2", c.x + 1, c.y - 1, 10, col, ui);
            }))
            vi.format(ed, Sup);
        if (btn("sub", "Subscript text  (Space _)", f & Sub, [&](ImVec2 c, ImU32 col) {
                L.drawText("x", c.x - 7, c.y + 3, 15, col, ui);
                L.drawText("2", c.x + 1, c.y + 8, 10, col, ui);
            }))
            vi.format(ed, Sub);
        {  // colour: cycles red, green, blue, none
            uint16_t now = f & Colors, next = now == 0 ? Red : now == Red ? Green : now == Green ? Blue : 0;
            ImU32 sw = now == Red ? IM_COL32(0xe5, 0x48, 0x4d, 255) : now == Green ? IM_COL32(0x2f, 0x9e, 0x5b, 255)
                     : now == Blue ? IM_COL32(0x3b, 0x82, 0xf6, 255) : p.muted;
            if (btn("col", "Text colour: red, green, blue, none  (Space R · G · B)", now, [&](ImVec2 c, ImU32 col) {
                    text("A", bold, 15)(ImVec2{c.x, c.y - 2}, col);
                    dl->AddRectFilled({c.x - 8, c.y + 6}, {c.x + 8, c.y + 9}, sw, 1);
                }))
                vi.format(ed, next ? next : now);
        }
        if (btn("r", "Clear formatting  (Space r)", false, [&](ImVec2 c, ImU32 col) {
                text("T", ui, 16)(c, col);
                dl->AddLine({c.x - 7, c.y + 7}, {c.x + 7, c.y - 7}, col, 1.5f);
            }))
            vi.format(ed, 0);
        sep();
        if (btn("h1", "Heading 1  (Space 1)", kind == Line::H1, text("H1", bold, 13))) vi.setLine(ed, Line::H1);
        if (btn("h2", "Heading 2  (Space 2)", kind == Line::H2, text("H2", bold, 13))) vi.setLine(ed, Line::H2);
        if (btn("h3", "Heading 3  (Space 3)", kind == Line::H3, text("H3", bold, 13))) vi.setLine(ed, Line::H3);
        if (btn("ul", "Bullet list  (Space -)", kind == Line::Bullet, [&](ImVec2 c, ImU32 col) {
                for (int i = -1; i <= 1; i++) dl->AddCircleFilled({c.x - 7, c.y + i * 5.f}, 1.7f, col);
                lines(c, col, -3);
            }))
            vi.setLine(ed, Line::Bullet);
        if (btn("ol", "Numbered list  (Space n)", kind == Line::Number, [&](ImVec2 c, ImU32 col) {
                L.drawText("1", c.x - 10, c.y - 1, 9, col, bold);
                L.drawText("2", c.x - 10, c.y + 8, 9, col, bold);
                dl->AddLine({c.x - 2, c.y - 4}, {c.x + 8, c.y - 4}, col, 1.5f);
                dl->AddLine({c.x - 2, c.y + 4}, {c.x + 8, c.y + 4}, col, 1.5f);
            }))
            vi.setLine(ed, Line::Number);
        if (btn("q", "Quote  (Space >)", kind == Line::Quote, [&](ImVec2 c, ImU32 col) {
                dl->AddRectFilled({c.x - 8, c.y - 7}, {c.x - 5.5f, c.y + 7}, col, 1);
                lines(c, col, -2);
            }))
            vi.setLine(ed, Line::Quote);
        if (btn("t", "Checklist  (Space t · Space x or click the box to tick)", kind == Line::Todo || kind == Line::Done,
                [&](ImVec2 c, ImU32 col) {
                    dl->AddRect({c.x - 7, c.y - 7}, {c.x + 7, c.y + 7}, col, 3, 0, 1.5f);
                    ImVec2 ck[] = {{c.x - 4, c.y}, {c.x - 1, c.y + 3}, {c.x + 4, c.y - 3}};
                    dl->AddPolyline(ck, 3, col, 0, 1.5f);
                }))
            vi.setLine(ed, Line::Todo);
        sep();
        if (btn("tbl", "Table  (Space | · :table rows cols · | separates cells)", kind == Line::Table, [&](ImVec2 c, ImU32 col) {
                dl->AddRect({c.x - 8, c.y - 7}, {c.x + 8, c.y + 7}, col, 2, 0, 1.5f);
                dl->AddLine({c.x - 8, c.y - 2}, {c.x + 8, c.y - 2}, col, 1.5f);
                dl->AddLine({c.x, c.y - 7}, {c.x, c.y + 7}, col, 1.5f);
            }))
            ImGui::OpenPopup("tablesize");
        if (ImGui::BeginPopup("tablesize")) {  // pick a size by hovering a grid, like a word processor
            static int hr = 3, hc = 2;
            ImDrawList* pd = ImGui::GetWindowDrawList();
            const int R = 8, C = 8;
            const float cell = 20;
            ImVec2 o = ImGui::GetCursorScreenPos(), mp = ImGui::GetMousePos();
            bool pick = ImGui::InvisibleButton("grid", {C * cell, R * cell});
            if (ImGui::IsItemHovered()) hc = std::clamp(int((mp.x - o.x) / cell) + 1, 1, C), hr = std::clamp(int((mp.y - o.y) / cell) + 1, 1, R);
            for (int r = 0; r < R; r++)
                for (int c = 0; c < C; c++) {
                    ImVec2 a{o.x + c * cell + 2, o.y + r * cell + 2}, b{a.x + cell - 4, a.y + cell - 4};
                    bool on = r < hr && c < hc;
                    pd->AddRectFilled(a, b, on ? p.sel : p.side, 2);
                    pd->AddRect(a, b, on ? p.fn : p.line, 2);
                }
            ImGui::Text("%d × %d  (rows × columns)", hr, hc);
            if (pick) {
                run("table " + std::to_string(hr) + " " + std::to_string(hc));
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (btn("sec", "New section: its own variables, for the next exercise  (Space = · :section [title] · type ===)",
                kind == Line::Section, text("§", bold, 17)))
            run("section");
        if (btn("hr", "Divider  (Space / · type ---)", kind == Line::Rule, [&](ImVec2 c, ImU32 col) {
                dl->AddLine({c.x - 8, c.y}, {c.x + 8, c.y}, col, 2);
            }))
            run("rule");
        if (btn("plot", "Plot  (type plot at the start of a line, Tab, then: sin(x), x^2, −3 < x < 3)", kind == Line::Plot, [&](ImVec2 c, ImU32 col) {
                dl->AddLine({c.x - 8, c.y + 7}, {c.x + 8, c.y + 7}, col, 1.2f);
                dl->AddLine({c.x - 8, c.y + 7}, {c.x - 8, c.y - 7}, col, 1.2f);
                ImVec2 w[] = {{c.x - 7, c.y + 4}, {c.x - 3, c.y - 4}, {c.x + 1, c.y + 2}, {c.x + 7, c.y - 6}};
                dl->AddPolyline(w, 4, col, 0, 1.6f);
            }))
        {
            insertLines({""}, 0);
            *ed.cur.row = {named(T::Fn, "plot"), M(" ")};
            ed.cur.i = 2;
            ed.reindex();
            if (vi.mode == Mode::Normal) vi.key(ed, Vi::parse("a")[0]);
            changed();
        }
        if (btn("img", "Picture: choose one, or drag it onto the window  (:image path)", kind == Line::Image, [&](ImVec2 c, ImU32 col) {
                dl->AddRect({c.x - 8, c.y - 7}, {c.x + 8, c.y + 7}, col, 2, 0, 1.5f);
                ImVec2 m[] = {{c.x - 6, c.y + 5}, {c.x - 1, c.y - 1}, {c.x + 2, c.y + 2}, {c.x + 4, c.y}, {c.x + 7, c.y + 5}};
                dl->AddPolyline(m, 5, col, 0, 1.4f);
                dl->AddCircleFilled({c.x + 3, c.y - 3.5f}, 1.6f, col);
            }))
            askPath(false, "Insert picture", home(), {{"Pictures", "*.png *.jpg *.jpeg *.gif *.bmp"}}, [this](std::string f) { addImage(f); });
        sep();
        if (btn("m", "Math mode: everything typed is math  (Ctrl-m · Space m)", ed.mathMode, text("∑", L.math, 18))) ed.mathMode = !ed.mathMode;
        }
        limit = FLT_MAX;
        x = right + 8;
        if (btn("pal", "Command palette: find any action, setting or note  (Ctrl-Shift-p)", showPalette, text("⌘", L.math, 17)))
            showPalette = true, paletteQuery[0] = 0, paletteSel = 0;
        if (btn("help", "Help  (F1)", showHelp, text("?", bold, 16))) showHelp = !showHelp;
        if (btn("set", "Settings  (Ctrl-,)", showSettings, [&](ImVec2 c, ImU32 col) {
                dl->AddCircle(c, 4, col, 0, 1.6f);
                for (int i = 0; i < 8; i++) {
                    float t = i * IM_PI / 4;
                    dl->AddLine({c.x + 5.5f * std::cos(t), c.y + 5.5f * std::sin(t)}, {c.x + 8 * std::cos(t), c.y + 8 * std::sin(t)}, col, 2.2f);
                }
            }))
            showSettings = !showSettings;
        if (act) changed();
    }

    void drawStatus(float h) {
        const Palette& p = pal();
        ImDrawList* dl = L.dl = ImGui::GetWindowDrawList();
        ImVec2 a = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x, base = a.y + h / 2 + 5;
        dl->AddRectFilled(a, {a.x + w, a.y + h}, p.side);
        dl->AddLine(a, {a.x + w, a.y}, p.line);
        std::string left = vi.mode == Mode::Insert ? "-- INSERT --" : vi.mode == Mode::Visual ? "-- VISUAL --"
                         : vi.mode == Mode::VisualLine ? "-- VISUAL LINE --" : vi.mode == Mode::Command ? vi.prompt + vi.cmdline : "";
        if (sideFocus) left = "-- FILES --  j/k move · Enter open · / filter · Esc back";
        if (!vim && vi.mode == Mode::Insert) left.clear();  // standard keys: always typing, no mode to show
        float x = a.x + 12;
        {  // Ctrl-m toggle beside the mode: filled when everything typed is math
            std::string t = ed.mathMode ? "MATH" : "TEXT";
            float tw = L.textWidth(t, 11) + 14;
            ImU32 c = ed.mathMode ? p.fn : p.muted;
            if (ed.mathMode) dl->AddRectFilled({x, base - 14}, {x + tw, base + 5}, alpha(c, 0.2f), 9);
            dl->AddRect({x, base - 14}, {x + tw, base + 5}, alpha(c, ed.mathMode ? 1 : 0.5f), 9);
            L.drawText(t, x + 7, base - 1, 11, c, bold);
            x += tw + 10;
        }
        L.drawText(left, x, base, 14, vi.mode == Mode::Command ? p.fg : IM_COL32(0x25, 0x63, 0xeb, 255), vi.mode == Mode::Command ? ui : bold);
        x += L.textWidth(left, 14);
        if (vi.mode == Mode::Command) {
            dl->AddRectFilled({x + 1, base - 13}, {x + 8, base + 3}, alpha(p.fg, 0.4f));
            x += 10;
        }
        std::string msg = !vi.message.empty() ? vi.message : message;
        if (vi.leader())
            L.drawText("SPACE   b bold · i italic · u underline · s strike · c code · h highlight · r clear · 1 2 3 heading · 0 text · "
                       "^ sup · _ sub · R G B colour · - bullets · n numbers · > quote · t checklist · x tick · | table · / rule · = section · m math · e sidebar",
                       x + 4, base, 12, p.fg, ui);
        else if (!msg.empty()) L.drawText(msg, x + 16, base, 14, p.muted, ui);
        else if (left.empty())
            L.drawText(vim ? "i insert · Esc normal · Space formatting · Ctrl-m math · Ctrl-Shift-p commands · F1 help"
                           : "Ctrl-m math · Tab takes a result · Ctrl-z undo · Ctrl-f find · Ctrl-Shift-p commands · F1 help",
                       x + 4, base, 12, alpha(p.muted, 0.8f), ui);
        int k = ed.lineOf(ed.topOf(ed.cur.row)) + 1;
        std::string right = titleOf(meta) + (docJson(ed.doc).dump() == savedJson ? "" : " [+]") + "   " + std::to_string(k) + "/" +
                            std::to_string(ed.doc.size());
        L.drawText(right, a.x + w - L.textWidth(right, 13) - 12, base, 13, p.muted, ui);
        ImGui::Dummy({w, h});
    }

    void frame() {
        input();
        panelFocused = false;  // the panels drawn below set it again
        pdfDone();
        if (changedAt >= 0 && nowSec() - changedAt > std::max(0.2f, autosave)) saveNow();
        if (!vi.message.empty() || !message.empty()) {}
        applyColors();
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->Pos);
        ImGui::SetNextWindowSize(vp->Size);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 0});
        ImGui::Begin("##root", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        float statusH = 28;
        if (sidebar) {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(pal().side));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {6, 6});
            ImGui::BeginChild("side", {270, -statusH});
            drawSidebar();
            ImGui::EndChild();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
            ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddLine({b.x, a.y}, {b.x, b.y}, pal().line);
            ImGui::SameLine(0, 0);
        }
        ImGui::BeginGroup();
        float toolH = 40;
        ImGui::BeginChild("tools", {0, toolH}, 0, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        drawToolbar(toolH);
        ImGui::EndChild();
        ImGui::BeginChild("doc", {0, -statusH}, 0, ImGuiWindowFlags_HorizontalScrollbar);
        drawDoc();
        if (plotAspect > 0) {  // the window changed shape: shapes re-fit so a circle stays round
            mathOpts.plotAspect = plotAspect;
            plotAspect = 0;
            std::erase_if(lineCache, [](auto& e) { return e.second.plot && e.second.plot->equal; });
            bool scroll = scrollToCaret;
            changed(true);
            scrollToCaret = scroll;
        }
        ImGui::EndChild();
        ImGui::EndGroup();
        drawStatus(statusH);
        ImGui::End();
        ImGui::PopStyleVar(2);
        drawPopup();
        drawSettings();
        drawHelp();
        drawPalette();
        drawBrowser();
    }
};

static void applyStyle() {
    auto& s = ImGui::GetStyle();
    s.WindowRounding = 0;
    s.ChildRounding = 0;
    s.FrameRounding = 6;
    s.PopupRounding = 10;
    s.WindowBorderSize = 0;
    s.ChildBorderSize = 0;
    s.PopupBorderSize = 1;
    s.FramePadding = {6, 4};
    s.ScrollbarSize = 10;
    s.ScrollbarRounding = 6;
}

int main(int argc, char** argv) {
    if (!glfwInit()) return 1;
#ifdef __APPLE__
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    const char* glsl = "#version 150";
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    const char* glsl = "#version 130";
#endif
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    glfwWindowHintString(GLFW_WAYLAND_APP_ID, "opman");  // matches packaging/opman.desktop
    glfwWindowHintString(GLFW_X11_CLASS_NAME, "opman");
    const char* shot = std::getenv("OPMAN_SHOT");
    if (shot) glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);  // fixed size: tiling WMs float it, screenshots stay comparable
    GLFWwindow* win = glfwCreateWindow(1280, 820, "opman", nullptr, nullptr);
    if (!win) return 1;
    glfwMakeContextCurrent(win);
    glfwSwapInterval(shot ? 0 : 1);  // Wayland blocks vsync'd swaps of a hidden window; screenshots must not wait
    glfwSetKeyCallback(win, onKey);  // before ImGui's, which chain to these
    glfwSetCharCallback(win, onChar);
    glfwSetDropCallback(win, onDrop);
    glfwSetInputMode(win, GLFW_LOCK_KEY_MODS, GLFW_TRUE);  // CapsLock state in mods: see onKey

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigInputTrickleEventQueue = false;  // the UI scale rewrites queued mouse events once per frame
    applyStyle();
    ImGui_ImplGlfw_InitForOpenGL(win, true);
    ImGui_ImplOpenGL3_Init(glsl);

    App app;
    app.vi.getClip = [] { const char* s = ImGui::GetClipboardText(); return std::string(s ? s : ""); };
    app.vi.setClip = [](const std::string& s) { ImGui::SetClipboardText(s.c_str()); };
    std::string fonts = resDir() + "/assets/fonts/";
    app.ui = io.Fonts->AddFontFromFileTTF((fonts + "Inter-Regular.ttf").c_str(), 14);
    app.bold = io.Fonts->AddFontFromFileTTF((fonts + "Inter-SemiBold.ttf").c_str(), 14);
    app.L.math = io.Fonts->AddFontFromFileTTF((fonts + "STIXTwoMath-Regular.otf").c_str(), 20);
    if (!app.ui || !app.bold || !app.L.math) {
        std::fprintf(stderr, "opman: fonts missing in %s\n", fonts.c_str());
        return 1;
    }
    io.FontDefault = app.ui;
    app.L.text = app.ui;
    app.L.bold = app.bold;
    app.L.mono = io.Fonts->AddFontFromFileTTF((fonts + "JetBrainsMono-Regular.ttf").c_str(), 14);
    if (!app.L.mono) app.L.mono = app.ui;
    app.loadCfg();
    app.convertLibrary();
    app.listFiles();
    fs::path arg = argc > 1 ? fs::path(argv[1]) : fs::path();  // UTF-8 on Windows too (packaging/opman.manifest)
    std::error_code ec;
    if (!arg.empty() && fs::is_regular_file(arg, ec)) app.openPath(arg.string());  // opman note.opman (a double-click)
    else app.openMostRecent();
    const char* keys = std::getenv("OPMAN_KEYS");
    // OPMAN_MOUSE="m 600 400; d; u; d; u; w -2": one mouse step per frame after the keys (move to x y in window
    // coordinates, button down/up, wheel), for testing plots and sliders
    std::vector<std::string> mouse;
    if (const char* m = std::getenv("OPMAN_MOUSE")) {
        std::string all = m, step;
        for (char c : all + ";") {
            if (c == ';') { if (!step.empty()) mouse.push_back(step); step.clear(); }
            else if (c != ' ' || !step.empty()) step += c;
        }
    }
    double shotAt = std::getenv("OPMAN_SHOT_AFTER") ? std::atof(std::getenv("OPMAN_SHOT_AFTER")) : 1.5;
    int frames = 0;

    while (!glfwWindowShouldClose(win) && !app.quit) {
        // ponytail: idle-friendly loop; wakes 4x a second for the autosave timer
        glfwWaitEventsTimeout(shot ? 1.0 / 60 : 0.25);
        if (keys && frames == 2) {
            for (auto& k : Vi::parse(keys)) g_keys.push_back(k);
            keys = nullptr;
        }
        frames++;
        if (frames > 4 && !mouse.empty()) {
            std::string st = mouse.front();
            mouse.erase(mouse.begin());
            float mx, my, wv;
            if (std::sscanf(st.c_str(), "m %f %f", &mx, &my) == 2) io.AddMousePosEvent(mx, my);
            else if (st == "d" || st == "u") io.AddMouseButtonEvent(0, st == "d");
            else if (st == "rd" || st == "ru") io.AddMouseButtonEvent(1, st == "rd");
            else if (std::sscanf(st.c_str(), "w %f", &wv) == 1) io.AddMouseWheelEvent(0, wv);
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        // UI scale on Windows/X11 (pixel coordinates); Wayland/macOS scale through the framebuffer (from opcan)
        float xs = 1, ys = 1;
        int ww, wh, fw, fh;
        glfwGetWindowContentScale(win, &xs, &ys);
        glfwGetWindowSize(win, &ww, &wh);
        glfwGetFramebufferSize(win, &fw, &fh);
        float us = std::max(1.f, xs / (ww ? float(fw) / ww : 1.f));
        if (us != 1) {
            io.DisplaySize = {io.DisplaySize.x / us, io.DisplaySize.y / us};
            io.DisplayFramebufferScale = {io.DisplayFramebufferScale.x * us, io.DisplayFramebufferScale.y * us};
            for (auto& e : GImGui->InputEventsQueue)
                if (e.Type == ImGuiInputEventType_MousePos && e.MousePos.PosX != -FLT_MAX) e.MousePos.PosX /= us, e.MousePos.PosY /= us;
        }
        ImGui::NewFrame();
        app.frame();
        std::string title = "opman — " + App::titleOf(app.meta);
        static std::string shownTitle;
        if (title != shownTitle) glfwSetWindowTitle(win, (shownTitle = title).c_str());
        ImGui::Render();
        glViewport(0, 0, fw, fh);
        auto bg = ImGui::ColorConvertU32ToFloat4(app.pal().bg);
        glClearColor(bg.x, bg.y, bg.z, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (shot && glfwGetTime() > shotAt) {
            std::vector<unsigned char> px(size_t(fw) * fh * 3);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, fw, fh, GL_RGB, GL_UNSIGNED_BYTE, px.data());
            std::ofstream out(shot, std::ios::binary);
            out << "P6\n" << fw << " " << fh << "\n255\n";
            for (int y = fh - 1; y >= 0; y--) out.write((const char*)&px[size_t(y) * fw * 3], fw * 3);
            break;
        }
        glfwSwapBuffers(win);
        if (app.pdfWanted) app.renderPdf();
    }
    app.saveNow();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(win);
    glfwTerminate();
}
