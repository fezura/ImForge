#include "naming.h"

#include <cstring>

#include "imforge/imforge_expr.h"

namespace imforge {
namespace codegen {

namespace {

bool IsUpper(char c) { return c >= 'A' && c <= 'Z'; }
bool IsLower(char c) { return c >= 'a' && c <= 'z'; }
bool IsDigit(char c) { return c >= '0' && c <= '9'; }
bool IsAlnum(char c) { return IsUpper(c) || IsLower(c) || IsDigit(c); }
char Lower(char c) { return IsUpper(c) ? static_cast<char>(c - 'A' + 'a') : c; }
char Upper(char c) { return IsLower(c) ? static_cast<char>(c - 'a' + 'A') : c; }

const char* const kKeywords[] = {
    "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor", "bool", "break",
    "case", "catch", "char", "char16_t", "char32_t", "char8_t", "class", "compl", "concept",
    "const", "const_cast", "consteval", "constexpr", "constinit", "continue", "co_await",
    "co_return", "co_yield", "decltype", "default", "delete", "do", "double", "dynamic_cast",
    "else", "enum", "explicit", "export", "extern", "false", "float", "for", "friend", "goto",
    "if", "inline", "int", "long", "mutable", "namespace", "new", "noexcept", "not", "not_eq",
    "nullptr", "operator", "or", "or_eq", "private", "protected", "public", "register",
    "reinterpret_cast", "requires", "return", "short", "signed", "sizeof", "static",
    "static_assert", "static_cast", "struct", "switch", "template", "this", "thread_local",
    "throw", "true", "try", "typedef", "typeid", "typename", "union", "unsigned", "using",
    "virtual", "void", "volatile", "wchar_t", "while", "xor", "xor_eq",
};

}  // namespace

bool IsCppKeyword(const std::string& word) {
    for (const char* k : kKeywords)
        if (word == k) return true;
    return false;
}

std::vector<std::string> SplitWords(const std::string& ident) {
    std::vector<std::string> words;
    std::string cur;
    auto flush = [&]() {
        if (!cur.empty()) words.push_back(cur);
        cur.clear();
    };
    for (size_t i = 0; i < ident.size(); ++i) {
        char c = ident[i];
        if (!IsAlnum(c)) {
            flush();
            continue;
        }
        if (!cur.empty()) {
            char prev = cur.back();
            bool boundary = false;
            if (IsUpper(c) && IsLower(prev)) boundary = true;
            else if (IsUpper(c) && IsDigit(prev)) boundary = true;
            else if (IsDigit(c) != IsDigit(prev) && !IsDigit(c)) boundary = true;
            else if (IsUpper(c) && IsUpper(prev) && i + 1 < ident.size() && IsLower(ident[i + 1]))
                boundary = true;
            if (boundary) flush();
        }
        cur.push_back(c);
    }
    flush();
    return words;
}

std::string ToPascal(const std::string& ident) {
    std::string out;
    for (const std::string& w : SplitWords(ident)) {
        out.push_back(Upper(w[0]));
        out.append(w, 1, std::string::npos);
    }
    return out;
}

std::string ToCamel(const std::string& ident) {
    std::string p = ToPascal(ident);
    if (!p.empty()) p[0] = Lower(p[0]);
    return p;
}

std::string ToSnake(const std::string& ident) {
    std::string out;
    for (const std::string& w : SplitWords(ident)) {
        if (!out.empty()) out.push_back('_');
        for (char c : w) out.push_back(Lower(c));
    }
    return out;
}

std::string ApplyCase(const std::string& ident, const std::string& style) {
    if (style == "pascal") return ToPascal(ident);
    if (style == "camel") return ToCamel(ident);
    if (style == "snake") return ToSnake(ident);
    return ident;
}

std::string SanitizeIdentifier(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 1);
    for (char c : text) out.push_back(IsAlnum(c) || c == '_' ? c : '_');
    if (out.empty()) return "_";
    if (IsDigit(out[0])) out.insert(out.begin(), '_');
    if (IsCppKeyword(out)) out.push_back('_');
    return out;
}

std::string CppStringLiteral(const std::string& text) {
    std::string lit = expr::detail::CStringLiteral(text);
    std::string out;
    out.reserve(lit.size());
    for (size_t i = 0; i < lit.size(); ++i) {
        if (lit[i] == '?' && i + 1 < lit.size() && lit[i + 1] == '?') {
            out += "?\\?";
            ++i;
        } else {
            out += lit[i];
        }
    }
    return out;
}

}  // namespace codegen
}  // namespace imforge
