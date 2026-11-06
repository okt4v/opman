// The system's open/save/folder dialogs (portable-file-dialogs: Windows and macOS natively, Linux through zenity or
// kdialog). Kept in its own file so windows.h and friends stay out of main.cpp.
#include "dialog.hpp"
#include <portable-file-dialogs.h>

namespace om::dialog {

bool available() { return pfd::settings::available(); }

static std::vector<std::string> filterList(const std::vector<std::pair<std::string, std::string>>& f) {
    std::vector<std::string> out;
    for (auto& [name, pattern] : f) out.push_back(name), out.push_back(pattern);
    if (out.empty()) out = {"All files", "*"};
    return out;
}

std::optional<std::string> open(const std::string& title, const std::string& dir, const std::vector<std::pair<std::string, std::string>>& f) {
    auto r = pfd::open_file(title, dir, filterList(f), pfd::opt::none).result();
    if (r.empty()) return std::nullopt;
    return r[0];
}

std::optional<std::string> save(const std::string& title, const std::string& path, const std::vector<std::pair<std::string, std::string>>& f) {
    std::string r = pfd::save_file(title, path, filterList(f), pfd::opt::none).result();
    if (r.empty()) return std::nullopt;
    return r;
}

std::optional<std::string> folder(const std::string& title, const std::string& dir) {
    std::string r = pfd::select_folder(title, dir).result();
    if (r.empty()) return std::nullopt;
    return r;
}

}  // namespace om::dialog
