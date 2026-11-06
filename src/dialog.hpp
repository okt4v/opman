// System file dialogs. Each call blocks until the user picks or cancels (nullopt). When available() is false (Linux
// without zenity/kdialog) the app uses its own file browser instead.
#pragma once
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace om::dialog {
using Filters = std::vector<std::pair<std::string, std::string>>;  // {"opman notes", "*.opman"}
bool available();
std::optional<std::string> open(const std::string& title, const std::string& dir, const Filters& filters);
std::optional<std::string> save(const std::string& title, const std::string& path, const Filters& filters);
std::optional<std::string> folder(const std::string& title, const std::string& dir);
}  // namespace om::dialog
