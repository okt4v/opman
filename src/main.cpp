// opman: window, document column, popup, sidebar, files. The editing model lives in edit/math/vi, the typesetting
// in layout. Dev hooks: OPMAN_DOCS=<dir> documents folder, OPMAN_KEYS="<vi keys>" typed at start,
// OPMAN_SHOT=<file.ppm> saves a screenshot after OPMAN_SHOT_AFTER seconds (default 1.5) and quits.
#include "doc.hpp"
#include "layout.hpp"
#include "math.hpp"
#include "vi.hpp"
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
#include <fstream>
#include <future>
#include <map>
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

static void onKey(GLFWwindow*, int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_CAPS_LOCK) {
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
    default: {
        if (!ctrl || alt) return;  // typed characters come through onChar; Ctrl+Alt is AltGr on Windows
        const char* name = glfwGetKeyName(key, scancode);  // the layout's letter, so Ctrl-z is z on every layout
        std::string s = name ? name : "";
        if (s.size() != 1) {
            if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) s = std::string(1, char('a' + key - GLFW_KEY_A));
            else if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9) s = std::string(1, char('0' + key - GLFW_KEY_0));
            else return;
        }
        s[0] = char(std::tolower((unsigned char)s[0]));
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
    std::future<int> pdfJob;                          // pdflatex in the background
    fs::path pdfOut, pdfTmp;
    std::vector<std::string> history;                 // notes opened before, for Ctrl-o
    bool goingBack = false;
    mutable std::map<std::string, std::pair<fs::file_time_type, std::string>> texts;  // lowercase text per note
    std::vector<FileInfo> backlinks;                  // notes with a [[link]] to the open one
    double sliderV0 = 0;
    float sliderX0 = 0;
    double changedAt = -1;
    bool sidebar = true, sideFocus = false, focusFilter = false, dark = true, quit = false, noSave = false;
    bool scrollToCaret = true, dragging = false;
    int sideSel = 0, sort = 0;
    char filter[128] = "";
    ImVec2 caretPos{-1, -1};

    // ---------- config and files ----------
    void loadCfg() {
        readFile(cfgFile, cfg);
        if (!cfg.is_object()) cfg = json::object();
        sidebar = cfg.value("sidebar", true);
        sort = cfg.value("sort", 0);
        dark = cfg.value("theme", std::string("dark")) != "light";
        if (const char* d = std::getenv("OPMAN_DOCS")) dir = d;
        else if (cfg.contains("dir") && cfg["dir"].is_string()) dir = fs::path(cfg["dir"].get<std::string>());
        else dir = home() / "Documents" / "opman";
        std::error_code ec;
        fs::create_directories(dir, ec);
    }
    void saveCfg() {
        cfg["sidebar"] = sidebar;
        cfg["sort"] = sort;
        cfg["theme"] = dark ? "dark" : "light";
        writeFile(cfgFile, cfg);
    }
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

    // a note's plain text, lowercase, read again when the file changes (search, backlinks)
    const std::string& noteText(const std::string& name) const {
        std::error_code ec;
        auto t = fs::last_write_time(dir / name, ec);
        auto& e = texts[name];
        if (e.first != t || e.second.empty()) {
            json d;
            std::string s;
            if (readFile(dir / name, d) && d.is_object())
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

    // writes only when the document changed, so moving the cursor doesn't bump "last edited"
    void saveNow() {
        changedAt = -1;
        if (current.empty() || noSave) return;
        std::string j = docJson(ed.doc).dump();
        if (j == savedJson) return;
        savedJson = j;
        json st = stats();
        for (auto& [k, v] : st.items()) meta[k] = v;
        meta["edited"] = nowIso();
        if (!writeFile(dir / current, {{"meta", meta}, {"doc", docJson(ed.doc)}, {"path", pathJson(ed.curPath())}})) {
            message = "Could not write " + (dir / current).string();
            savedJson.clear();
            return;
        }
        auto f = std::find_if(files.begin(), files.end(), [&](auto& x) { return x.name == current; });
        if (f != files.end()) f->meta = meta;
        else files.push_back({current, meta});
    }

    void openDoc(const std::string& name) {
        saveNow();
        if (!current.empty() && current != name && !goingBack) {
            history.push_back(current);
            if (history.size() > 50) history.erase(history.begin());
        }
        json d;
        if (!readFile(dir / name, d) || !d.is_object()) {
            message = "Could not read " + name;
            return;
        }
        current = name;
        meta = d.contains("meta") && d["meta"].is_object() ? d["meta"] : json::object();
        ed.undoStack.clear();
        ed.redoStack.clear();
        ed.load(docFromJson(d.value("doc", json::array())), pathFromJson(d.value("path", json::object())));
        savedJson = lastJson = docJson(ed.doc).dump();
        cfg["lastDoc"] = name;
        saveCfg();
        vi.mode = Mode::Normal;
        views.clear();
        changed(true);
        findBacklinks();
    }

    void newDoc(const std::string& title = "") {
        saveNow();
        std::string t = nowIso();
        long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        std::string name = "doc-" + std::to_string(ms) + ".json";
        json m = {{"created", t}, {"edited", t}, {"autoTitle", "Untitled"}, {"words", 0}, {"lines", 1}, {"defines", json::array()}};
        if (!title.empty()) m["title"] = title;
        if (!writeFile(dir / name, {{"meta", m}, {"doc", docJson(Doc{Row{}})}, {"path", pathJson({})}})) {
            message = "Could not write " + (dir / name).string();
            return;
        }
        files.push_back({name, m});
        openDoc(name);
    }

    void openMostRecent() {
        std::string last = str(cfg, "lastDoc");
        if (std::any_of(files.begin(), files.end(), [&](auto& f) { return f.name == last; })) return openDoc(last);
        int keep = sort;
        sort = 0;
        auto s = sortedFiles();
        sort = keep;
        if (!s.empty()) return openDoc(s[0].name);
        newDoc();
    }

    // ponytail: "trash" is a .trash folder inside the documents folder; system trash APIs differ per OS
    void trash(const std::string& name) {
        std::error_code ec;
        fs::create_directories(dir / ".trash", ec);
        fs::rename(dir / name, dir / ".trash" / name, ec);
        if (ec) { message = "Could not move " + name + " to the trash"; return; }
        std::erase_if(files, [&](auto& f) { return f.name == name; });
        if (name == current) {
            current.clear();
            openMostRecent();
        }
        message = "Moved to " + (dir / ".trash").string();
    }

    void run(const std::string& cmd) {
        std::string name = cmd.substr(0, cmd.find(' ')), arg = cmd.size() > name.size() ? cmd.substr(name.size() + 1) : "";
        if (name == "sidebar") { sidebar = !sidebar; sideFocus = false; saveCfg(); }
        else if (name == "w") { saveNow(); message = "\"" + titleOf(meta) + "\" written"; }
        else if (name == "q") quit = true;
        else if (name == "q!") quit = noSave = true;
        else if (name == "wq" || name == "x") { saveNow(); quit = true; }
        else if (name == "new") newDoc(arg);
        else if (name == "rename") {
            if (arg.empty()) meta.erase("title");
            else meta["title"] = arg;
            savedJson.clear();
            saveNow();
            findBacklinks();
        } else if (name == "trash") { if (!current.empty()) trash(current); }
        else if (name == "set") {
            if (arg == "dark" || arg == "light") { dark = arg == "dark"; saveCfg(); }
            else message = "Unknown option: " + arg + " (dark, light)";
        } else if (name == "export") exportTo(arg);
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
        } else if (name == "e") {
            std::string q = lower(arg);
            for (auto& f : sortedFiles())
                if (f.name == arg || lower(titleOf(f.meta)).find(q) != std::string::npos) return openDoc(f.name);
            message = "No document matches \"" + arg + "\"";
        }
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

    // an image file: copied into the documents folder's media/, so notes stay self-contained
    void addImage(std::string src) {
        if (src.starts_with("~/")) src = (home() / src.substr(2)).string();
        std::string ext = lower(fs::path(src).extension().string());
        if (ext != ".png" && ext != ".jpg" && ext != ".jpeg" && ext != ".gif" && ext != ".bmp") {
            message = "Not an image (png, jpg, gif, bmp): " + src;
            return;
        }
        std::error_code ec;
        fs::create_directories(dir / "media", ec);
        std::string stem = fs::path(src).stem().string();
        for (auto& c : stem) if (!std::isalnum((unsigned char)c) && c != '-' && c != '_') c = '-';  // LaTeX-safe file names
        fs::path to = dir / "media" / (stem + ext);
        for (int n = 2; fs::exists(to) && !fs::equivalent(src, to, ec); n++) to = dir / "media" / (stem + "-" + std::to_string(n) + ext);
        if (!fs::exists(to)) fs::copy_file(src, to, ec);
        if (ec) { message = "Could not copy " + src + ": " + ec.message(); return; }
        insertLines({"![](media/" + to.filename().string() + ")"}, 2);
    }

    // :export [file.md|file.tex|file.pdf|md|tex|pdf]; PDF goes through pdflatex
    void exportTo(std::string arg) {
        std::string t = titleOf(meta);
        for (auto& c : t) if (std::strchr("/\\:*?\"<>| ", c)) c = '-';
        if (arg.empty()) arg = "md";
        if (arg == "md" || arg == "tex" || arg == "pdf" || arg == "html") arg = (home() / (t + "." + arg)).string();
        fs::path out = arg.starts_with("~/") ? home() / arg.substr(2) : fs::path(arg);
        std::string ext = lower(out.extension().string());
        if (ext != ".tex" && ext != ".pdf") {
            std::ofstream f(out, std::ios::binary);
            bool ok = ext == ".html" || ext == ".htm" ? bool(f << html(ed, dir, titleOf(meta))) : bool(f << markdown(ed.doc, dir));
            message = ok ? "Exported to " + out.string() : "Could not write " + out.string();
            return;
        }
        if (pdfJob.valid()) { message = "A PDF export is still running"; return; }
        std::string tex = latex(ed, dir);
        fs::path file = out;
        std::error_code ec;
        fs::path tmp = fs::temp_directory_path(ec) / "opman-export";
        if (ext == ".pdf") fs::create_directories(tmp, ec), file = tmp / "note.tex";
        if (!(std::ofstream(file, std::ios::binary) << tex)) { message = "Could not write " + file.string(); return; }
        if (ext == ".tex") { message = "Exported to " + out.string(); return; }
        fs::remove(tmp / "note.pdf", ec);
        std::string q = "\"", cmd = "pdflatex -interaction=nonstopmode -halt-on-error -output-directory " + q + tmp.string() + q + " " + q +
                                    file.string() + q + " > " + q + (tmp / "out.txt").string() + q + " 2>&1";
#ifdef _WIN32
        cmd = "\"" + cmd + "\"";  // cmd.exe strips the outer quotes
#endif
        pdfOut = out, pdfTmp = tmp;
        pdfJob = std::async(std::launch::async, [cmd] { return std::system(cmd.c_str()); });
        message = "Exporting PDF…";
    }

    void pdfDone() {  // polled each frame; the PDF's existence is the result
        if (!pdfJob.valid() || pdfJob.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        pdfJob.get();
        std::error_code ec;
        if (!fs::exists(pdfTmp / "note.pdf")) { message = "pdflatex failed, see " + (pdfTmp / "note.log").string(); return; }
        fs::copy_file(pdfTmp / "note.pdf", pdfOut, fs::copy_options::overwrite_existing, ec);
        message = ec ? "Could not write " + pdfOut.string() : "Exported to " + pdfOut.string();
    }

    Tex& texture(const std::string& path) {
        auto [it, fresh] = texs.try_emplace(path);
        if (!fresh) return it->second;
        int w, h, n;
        fs::path p = fs::path(path).is_relative() ? dir / path : fs::path(path);
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
                    else c.ghost = ghost(ed, ed.doc[k]);
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
        if (ImGui::GetIO().WantTextInput) return;  // a sidebar text field has them
        for (auto& k : keys) {
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
            if (it.t != T::Ch || !(isDigit(it.c) || it.c == "." || ((it.c == "-" || it.c == "−") && j == 2))) return false;
            num += it.c == "−" ? "-" : it.c;
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
                for (char c : s) row.push_back(M(c == '-' ? "−" : std::string(1, c)));
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
    float drawPlot(const Plot& pl, float x, float y, float w, int k) {
        const Palette& p = pal();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float h = std::min(320.f, w * 0.55f), padL = 44, padB = 22, padT = 8, padR = 10;
        ImVec2 a{x + padL, y + padT}, b{x + w - padR, y + h - padB};
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(k);
        ImGui::InvisibleButton("plot", {b.x - a.x, b.y - a.y});
        ImGui::PopID();
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
            if (ImGui::IsMouseDoubleClicked(0)) views.erase(k), viewPlots.erase(k);
            else if (moved) views[k] = v, viewPlots[k] = plot(ed, ed.doc[k], v.data());
            if (!ImGui::IsItemActive()) ImGui::SetTooltip("Drag to pan · wheel to zoom · double-click to reset");
        }
        dl->AddRectFilled({x, y}, {x + w, y + h}, p.side, 8);
        auto X = [&](double v) { return float(a.x + (v - pl.x0) / (pl.x1 - pl.x0) * (b.x - a.x)); };
        auto Y = [&](double v) { return float(b.y - (v - pl.y0) / (pl.y1 - pl.y0) * (b.y - a.y)); };
        auto label = [](double v) {
            char s[32];
            std::snprintf(s, sizeof s, "%g", std::fabs(v) < 1e-12 ? 0.0 : v);
            return std::string(s);
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
        return h;
    }

    void drawDoc() {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 origin = ImGui::GetCursorScreenPos();
        float avail = ImGui::GetContentRegionAvail().x, colW = std::min(820.f, avail);
        float x0 = origin.x + std::max(0.f, (avail - colW) / 2) + 32, y = origin.y + 48, maxW = 0;
        L.dl = dl;
        L.ed = &ed;
        L.stops.clear();
        auto vl = vi.visualLines(ed);
        const Palette& p = pal();
        float blockW = colW - 64;
        bool slid = false;  // a slider changed the doc during this frame
        for (size_t k = 0; k < ed.doc.size(); k++) {
            const Row& row = ed.doc[k];
            Line kind = lineStyle(row).kind;
            bool here = ed.topOf(ed.cur.row) == &row, first = k == 0 || lineStyle(ed.doc[k - 1]).kind != kind;
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
            if (kind == Line::Image) h += drawImage(row, x0, y + h, blockW, true) + 8;
            if (kind == Line::Plot && k < plots.size() && plots[k]) {
                if (views.count(int(k)) && !viewPlots.count(int(k))) viewPlots[int(k)] = plot(ed, row, views[int(k)].data());
                h += drawPlot(views.count(int(k)) ? viewPlots[int(k)] : *plots[k], x0, y + h, blockW, int(k)) + 12;
            }
            float w = b.w;
            if (k < ghosts.size() && ghosts[k]) {
                L.drawText(ghosts[k]->text, x0 + b.w + 0.5f * L.textSize, base, L.textSize * 0.9f, alpha(p.muted, 0.85f));
                w += L.textWidth(ghosts[k]->text, L.textSize * 0.9f) + L.textSize;
            }
            if (kind == Line::Text) slid = slider(ed.doc[k], x0 + w + 20, base, int(k)) || slid;
            maxW = std::max(maxW, w);
            y += h;
        }
        if (slid) {
            ed.reindex();
            changed();
            scrollToCaret = false;  // dragging a slider shouldn't scroll back to the cursor
        }
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
            bool tab = e.insert || e.cat == "Program", takes = sel && (tab || vi.enterPicks(ed));  // a text word needs ↓ first
            std::string kbd = takes ? (tab ? "Tab" : "Enter") : j < 9 ? "Ctrl " + std::to_string(j + 1) : "";
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
        ImGui::TextDisabled("%s", dir.filename().string().c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\nset with OPMAN_DOCS or \"dir\" in %s", dir.string().c_str(), cfgFile.string().c_str());
        ImGui::SameLine(ImGui::GetWindowWidth() - 36);
        if (ImGui::Button("+")) newDoc();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("New document (:new)");
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
        auto btn = [&](const char* id, const char* tip, bool on, auto icon, float w = 30) {
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
            dl->AddLine({x, o.y + 11}, {x, o.y + h - 11}, p.line);
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
        // sidebar
        if (btn("side", "Sidebar  (Ctrl-\\ · Space e)", sidebar, [&](ImVec2 c, ImU32 col) {
                dl->AddRect({c.x - 8, c.y - 7}, {c.x + 8, c.y + 7}, col, 3, 0, 1.5f);
                dl->AddRectFilled({c.x - 8, c.y - 7}, {c.x - 3, c.y + 7}, col, 3);
            }))
            run("sidebar");
        sep();
        if (btn("undo", "Undo  (u)", false, text("↶", L.math, 20))) ed.undo();
        if (btn("redo", "Redo  (Ctrl-r)", false, text("↷", L.math, 20))) ed.redo();
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
            run("table 3 2");
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
        if (btn("img", "Image: drag a picture onto the window  (:image path)", kind == Line::Image, [&](ImVec2 c, ImU32 col) {
                dl->AddRect({c.x - 8, c.y - 7}, {c.x + 8, c.y + 7}, col, 2, 0, 1.5f);
                ImVec2 m[] = {{c.x - 6, c.y + 5}, {c.x - 1, c.y - 1}, {c.x + 2, c.y + 2}, {c.x + 4, c.y}, {c.x + 7, c.y + 5}};
                dl->AddPolyline(m, 5, col, 0, 1.4f);
                dl->AddCircleFilled({c.x + 3, c.y - 3.5f}, 1.6f, col);
            }))
            message = "Drag a picture onto the window, or :image <path>";
        sep();
        if (btn("m", "Math mode: everything typed is math  (Ctrl-m · Space m)", ed.mathMode, text("∑", L.math, 18))) ed.mathMode = !ed.mathMode;
        // theme, at the right edge
        x = std::max(x, o.x + W - 40);
        if (btn("theme", dark ? "Light theme  (:set light)" : "Dark theme  (:set dark)", false, [&](ImVec2 c, ImU32 col) {
                dl->AddCircle(c, 7, col, 0, 1.5f);
                dl->PathArcTo(c, 7, -IM_PI / 2, IM_PI / 2);
                dl->PathFillConvex(col);
            }))
            run(dark ? "set light" : "set dark");
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
            L.drawText("i insert · Esc normal · Space formatting · Ctrl-m math · :w save · :e find · / search · :export · Ctrl-w files", x + 4, base, 12, alpha(p.muted, 0.8f), ui);
        int k = ed.lineOf(ed.topOf(ed.cur.row)) + 1;
        std::string right = titleOf(meta) + (docJson(ed.doc).dump() == savedJson ? "" : " [+]") + "   " + std::to_string(k) + "/" +
                            std::to_string(ed.doc.size());
        L.drawText(right, a.x + w - L.textWidth(right, 13) - 12, base, 13, p.muted, ui);
        ImGui::Dummy({w, h});
    }

    void frame() {
        input();
        pdfDone();
        if (changedAt >= 0 && nowSec() - changedAt > 0.5) saveNow();
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
        ImGui::EndChild();
        ImGui::EndGroup();
        drawStatus(statusH);
        ImGui::End();
        ImGui::PopStyleVar(2);
        drawPopup();
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
    (void)argc, (void)argv;
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
    app.files = listDocs(app.dir);
    app.openMostRecent();
    const char* keys = std::getenv("OPMAN_KEYS");
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
    }
    app.saveNow();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(win);
    glfwTerminate();
}
