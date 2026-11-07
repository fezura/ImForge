#pragma once

#include <string>
#include <vector>

#include "imforge/imforge_core.h"

namespace imforge {
namespace codegen {

struct Options {
    CodegenSettings settings;
    int imgui_version = 19190;
    bool features_animations = true;
    bool features_fx = true;
    bool features_icons = true;
    // preloaded types.json; if null we read <project.dir>/<project.types_json> when it exists
    const Json* types = nullptr;
};

Options OptionsFromProject(const Project& project);

struct FileOut {
    std::string path;
    std::string content;
};

// A prop literal in the generated C++. token_index counts tokens of Tokenize(content of `file`).
// block_token_index counts inside the owning node's block body (between @imb:begin and @imb:end,
// each direct child block collapsed to one @child:<id> token, i.e. the hashed text), -1 when
// markers are off. Flags span several tokens (A | B), the ref points at the first.
struct LiteralRef {
    std::string node_id, prop;
    int token_index = -1;
    std::string file;
    int block_token_index = -1;
};

struct Result {
    std::vector<FileOut> files;
    std::vector<Diagnostic> diagnostics;
    std::vector<LiteralRef> literals;
    bool HasErrors() const;
    const FileOut* Find(const std::string& path) const;
};

Result GeneratePanel(const Project&, const Panel&, const Options&);
Result GenerateComponents(const Project&, const Options&);
Result GenerateTheme(const Project&, const Theme&, const Options&);
Result GenerateStyles(const Project&, const Options&);
Result GenerateFonts(const Project&, const Options&);
Result GenerateReflection(const Project&, const Options&);
Result GenerateDevGlue(const Project&, const Options&);
Result GenerateAll(const Project&, const Options&);

std::string EmitExpression(const Panel&, const std::string& expr, const Options&);
std::string EmitSize(const SizeSpec& w, const SizeSpec& h, const Options&);

// also used by roundtrip. Hash is FNV-1a 32 as 8 lowercase hex digits; normalize turns CRLF
// into LF and strips leading whitespace on each line
std::string Fnv1a32Hex(const std::string& text);
std::string NormalizeForHash(const std::string& block_body);

// rough C++ tokenizer for LiteralRef indices and the roundtrip literal diff. Skips comments,
// a preprocessor line is one Directive, @child:<id> is one ChildRef, and a minus glued to a
// digit belongs to the Number.
struct Token {
    enum class Kind { Ident, Number, String, Punct, Directive, ChildRef };
    Kind kind = Kind::Punct;
    std::string text;
    size_t offset = 0;
    int line = 1;
};
std::vector<Token> Tokenize(const std::string& source);

}  // namespace codegen
}  // namespace imforge
