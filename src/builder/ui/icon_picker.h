#pragma once

#include <string>

#include "imgui.h"

namespace imforge {
namespace builder {

bool IconPicker(const char* id, std::string& icon_name);
void SetIconPickerFont(ImFont* font);
ImFont* IconPickerFont();
std::string IconToken(const std::string& name);
std::string IconNameFromToken(const std::string& token);

}  // namespace builder
}  // namespace imforge
