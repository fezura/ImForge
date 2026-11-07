#pragma once

#include <string>
#include <vector>

#include "imforge/imforge_json.h"

namespace imforge {
namespace binding {

struct CompileCommand {
    std::string directory, file;
    std::vector<std::string> args;
};

bool ReadTextFile(const std::string& path, std::string& out, std::string* err = nullptr);
bool WriteTextFile(const std::string& path, const std::string& data, std::string* err = nullptr);

std::vector<std::string> SplitCommandLine(const std::string& command);
bool ParseCompileCommands(const Json& doc, std::vector<CompileCommand>& out, std::string* err);
bool LoadCompileCommands(const std::string& path, std::vector<CompileCommand>& out,
                         std::string* err);

// drops the compiler, -o/dep flags and the source file, adds -working-directory, and cl.exe
// commands get --driver-mode=cl
std::vector<std::string> ClangArgsFor(const CompileCommand& cc);
std::vector<std::string> IncludeDirsOf(const CompileCommand& cc);
std::string JoinPath(const std::string& dir, const std::string& rel);
std::string NormalizePath(const std::string& path);
std::string FileBaseName(const std::string& path);

}  // namespace binding
}  // namespace imforge
