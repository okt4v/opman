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
#include <ctime>
#include <fstream>
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
            if (lower(titleOf(f.meta)).find(q) != std::string::npos) out.push_back(f);
        std::stable_sort(out.begin(), out.end(), [&](auto& a, auto& b) {
            if (sort == 2) return lower(titleOf(a.meta)) < lower(titleOf(b.meta));
            const char* k = sort == 1 ? "created" : "edited";
            return str(a.meta, k) > str(b.meta, k);
        });
        return out;
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
        changed(true);
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
        } else if (name == "trash") { if (!current.empty()) trash(current); }
        else if (name == "set") {
            if (arg == "dark" || arg == "light") { dark = arg == "dark"; saveCfg(); }
            else message = "Unknown option: " + arg + " (dark, light)";
        } else if (name == "e") {
            std::string q = lower(arg);
            for (auto& f : sortedFiles())
                if (f.name == arg || lower(titleOf(f.meta)).find(q) != std::string::npos) return openDoc(f.name);
            message = "No document matches \"" + arg + "\"";
        }
    }

    // after any edit or cursor move: live results, autosave timer, keep the caret in view
    void changed(bool force = false) {
        std::string j = docJson(ed.doc).dump();
        if (force || j != lastJson) {
            lastJson = j;
            ghosts.clear();
            for (auto& r : ed.doc) ghosts.push_back(ghost(ed, r));  // ponytail: every line on every edit; cache per line if docs get long
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
        for (size_t k = 0; k < ed.doc.size(); k++) {
            const Row& row = ed.doc[k];
            Box b = L.measureLine(row);
            float h = std::max(L.textSize * 1.7f, b.asc + b.desc + 10);
            float base = std::round(y + (h - b.asc - b.desc) / 2 + b.asc);
            if (vl && int(k) >= vl->first && int(k) <= vl->second) dl->AddRectFilled({x0 - 6, y}, {x0 + colW - 58, y + h}, p.sel);
            L.drawLine(row, x0, base);
            float w = b.w;
            if (k < ghosts.size() && ghosts[k]) {
                L.drawText(ghosts[k]->text, x0 + b.w + 0.5f * L.textSize, base, L.textSize * 0.9f, alpha(p.muted, 0.85f));
                w += L.textWidth(ghosts[k]->text, L.textSize * 0.9f) + L.textSize;
            }
            maxW = std::max(maxW, w);
            y += h;
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
        ImGui::Dummy({std::max(maxW + 64, colW), y - origin.y + ImGui::GetWindowHeight() * 0.4f});
        // mouse: click places the cursor, drag selects
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0)) {
            sideFocus = false;
            vi.mouse(ed, hit(ImGui::GetMousePos()), false);
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
            std::string kbd = sel ? (e.insert ? "Tab" : "Enter") : j < 9 ? "Ctrl " + std::to_string(j + 1) : "";
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
        float metaH = 96;
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
            L.drawText(ago(str(f.meta, "edited")), a.x + 10, a.y + 34, 12, p.muted, ui);
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
        ImGui::Unindent(12);
        ImGui::PopFont();
        ImGui::PopFont();
    }

    void drawStatus(float h) {
        const Palette& p = pal();
        ImDrawList* dl = L.dl = ImGui::GetWindowDrawList();
        ImVec2 a = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x, base = a.y + h / 2 + 5;
        dl->AddRectFilled(a, {a.x + w, a.y + h}, p.side);
        dl->AddLine(a, {a.x + w, a.y}, p.line);
        std::string left = vi.mode == Mode::Insert ? "-- INSERT --" : vi.mode == Mode::Visual ? "-- VISUAL --"
                         : vi.mode == Mode::VisualLine ? "-- VISUAL LINE --" : vi.mode == Mode::Command ? ":" + vi.cmdline : "";
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
        if (!msg.empty()) L.drawText(msg, x + 16, base, 14, p.muted, ui);
        else if (left.empty())
            L.drawText("i insert · Esc normal · Ctrl-m math · :w save · :e find · :new · Ctrl-b sidebar · Ctrl-w files", x + 4, base, 12, alpha(p.muted, 0.8f), ui);
        int k = ed.lineOf(ed.topOf(ed.cur.row)) + 1;
        std::string right = titleOf(meta) + (docJson(ed.doc).dump() == savedJson ? "" : " [+]") + "   " + std::to_string(k) + "/" +
                            std::to_string(ed.doc.size());
        L.drawText(right, a.x + w - L.textWidth(right, 13) - 12, base, 13, p.muted, ui);
        ImGui::Dummy({w, h});
    }

    void frame() {
        input();
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
        ImGui::BeginChild("doc", {0, -statusH}, 0, ImGuiWindowFlags_HorizontalScrollbar);
        drawDoc();
        ImGui::EndChild();
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
    glfwSwapInterval(1);
    glfwSetKeyCallback(win, onKey);  // before ImGui's, which chain to these
    glfwSetCharCallback(win, onChar);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigInputTrickleEventQueue = false;  // the UI scale rewrites queued mouse events once per frame
    applyStyle();
    ImGui_ImplGlfw_InitForOpenGL(win, true);
    ImGui_ImplOpenGL3_Init(glsl);

    App app;
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
