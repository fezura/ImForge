#pragma once

#include <string>
#include <vector>

#include "imforge/imforge_json.h"

namespace imforge {
namespace binding {

struct ScanOptions {
    std::string compile_commands;
    std::vector<std::string> include_dirs, defines, headers, types;
    std::string std = "c++17";
};

bool HaveLibClang();
const char* LibClangVersion();

// With no headers, every TU in compile_commands.json gets parsed. Pulls in everything reachable
// from the requested types' fields. On false, `log` says why.
bool ScanTypes(const ScanOptions& options, Json& types_out, std::vector<std::string>& log);

}  // namespace binding
}  // namespace imforge
