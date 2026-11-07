#pragma once

#include <string>
#include <vector>

namespace imforge {
namespace codegen {

std::vector<std::string> SplitWords(const std::string& ident);
std::string ToPascal(const std::string& ident);
std::string ToCamel(const std::string& ident);
std::string ToSnake(const std::string& ident);
// style: "pascal" | "camel" | "snake"; anything else returns the input unchanged.
std::string ApplyCase(const std::string& ident, const std::string& style);
// Replaces characters outside [A-Za-z0-9_], prefixes a leading digit with '_' and suffixes
// C++ keywords with '_'.
std::string SanitizeIdentifier(const std::string& text);
bool IsCppKeyword(const std::string& word);
// C string literal for `text`; "??" is split as "?\?" so no trigraph can form.
std::string CppStringLiteral(const std::string& text);

}  // namespace codegen
}  // namespace imforge
