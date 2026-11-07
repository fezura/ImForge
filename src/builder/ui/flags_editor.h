#pragma once

#include <string>
#include <vector>

namespace imforge::builder {

// `enum_name` works with or without the "ImGui" prefix ("WindowFlags" is fine). Flags newer than
// `imgui_version` (IMGUI_VERSION_NUM, e.g. 19190) show up disabled with a tooltip. Unknown names already
// in `flags` are still listed so they can be removed.
bool FlagsEditor(const char* label, std::vector<std::string>& flags, const char* enum_name, int imgui_version);

// same popup for any list of names, style classes etc.
bool MultiSelectEditor(const char* label, std::vector<std::string>& selected, const std::vector<std::string>& options,
                       const char* empty_text = "(none)");

std::string JoinNames(const std::vector<std::string>& names, size_t max_shown = 3);

}  // namespace imforge::builder
