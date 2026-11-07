#pragma once

#include <string>

#include "imforge/imforge_expr.h"
#include "imforge/imforge_value.h"

namespace imforge::builder {

struct ExprCheck {
    bool ok = true;
    bool empty = true;
    std::string error;
    int col = 0;
    imforge::expr::Type type = imforge::expr::Type::Error;
};

// expected == Type::Error means any type, conditions pass Bool
bool CheckExpr(const std::string& src, imforge::expr::TypeEnv* env, imforge::expr::Type expected, ExprCheck& out);
bool CheckInterpolation(const std::string& text, imforge::expr::TypeEnv* env, ExprCheck& out);

// Single-line input that parses and type checks as you type (red border, caret underline, error text)
// and pops up path completion on identifiers (Tab or click accepts, Up/Down move). Completion only
// works with an EditorTypeEnv, other TypeEnvs still get the checks.
bool ExprEditor(const char* label, std::string& expr, imforge::expr::TypeEnv* env, imforge::expr::Type expected);

// For text with `{path}`, `{path:fmt}` and `{icon:name}`. Checks ParseInterpolation plus every embedded
// expression, completes paths inside braces and has little insert helpers for icons and {path:fmt}.
bool InterpStringEditor(const char* label, std::string& text, imforge::expr::TypeEnv* env);

struct PathStatus {
    enum class Kind { Empty, Ok, Unknown, Mismatch };
    Kind kind = Kind::Empty;
    std::string message;
    std::string type_name;
    CType type;
};

// Offers the roots when empty. `accepted_types` is a BindDef pipe list like "float|float[N]|vec2|vec4",
// nullptr accepts anything.
bool PathEditor(const char* label, std::string& path, imforge::expr::TypeEnv* env, const char* accepted_types,
                PathStatus* status_out = nullptr);
PathStatus CheckPath(const std::string& path, imforge::expr::TypeEnv* env, const char* accepted_types);
void PathStatusBadge(const PathStatus& st);

std::string BindTypeName(const CType& t);
bool CTypeMatchesBindTypes(const CType& t, const char* accepted_types);

// printf format {path} uses for this type, "" if we don't know
const char* DefaultFormatFor(const CType& t);

}  // namespace imforge::builder
