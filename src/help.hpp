// The in-app help (F1, :help, the ? button): topics in a small markup the help window renders.
//   "## Heading", "- bullet", "`keys` text" (a row of a key table), a blank line, anything else a paragraph
#pragma once
#include <vector>

namespace om {
struct HelpTopic {
    const char* title;
    const char* body;
};
const std::vector<HelpTopic>& helpTopics();
// the guided tour: vi keys (vim mode) that type an example note showing the main features
const char* tourKeys();
}  // namespace om
