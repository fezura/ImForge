#pragma once

#include <string>

#include "imforge/imforge_value.h"

namespace imforge {
namespace binding {
namespace detail {

inline bool IsIdentStart(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}
inline bool IsIdentChar(char c) { return IsIdentStart(c) || (c >= '0' && c <= '9'); }
inline bool IsIdentifier(const std::string& s) {
    if (s.empty() || !IsIdentStart(s[0])) return false;
    for (char c : s)
        if (!IsIdentChar(c)) return false;
    return true;
}
inline bool StartsWith(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}
inline bool EndsWith(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;
}
inline bool Fail(std::string* err, const std::string& msg) {
    if (err) *err = msg;
    return false;
}
inline CType Simple(CKind k, const std::string& name, size_t size) {
    CType t;
    t.kind = k;
    t.name = name;
    t.size = size;
    return t;
}
inline bool IsContainer(CKind k) {
    return k == CKind::Vector || k == CKind::Array || k == CKind::CharArray ||
           k == CKind::FloatArray || k == CKind::IntArray;
}
inline std::string Bracketed(int n) { return "[" + std::to_string(n) + "]"; }

}  // namespace detail
}  // namespace binding
}  // namespace imforge
