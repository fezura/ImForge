#pragma once

#include <string>

namespace imforge::builder {

struct EditorContext;

void DrawAboutDialog(EditorContext& ctx, bool& open, const char* backend_name);
std::string HomeDirOrCwd();

}  // namespace imforge::builder
