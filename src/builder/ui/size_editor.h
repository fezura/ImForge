#pragma once

#include "imforge/imforge_core.h"

namespace imforge::builder {

// Mode combo (auto / px / fill / % / fill-N) plus a drag if the mode has a value. Takes the current
// item width; the visible part of `label` goes after the editor.
bool SizeSpecEditor(const char* label, imforge::SizeSpec& spec);

// {"w": SizeSpec, "h": SizeSpec}, one row each
bool SizePairEditor(const char* id, imforge::Json& size_json);

}  // namespace imforge::builder
