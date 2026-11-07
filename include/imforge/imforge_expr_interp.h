#pragma once

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "imforge_value.h"

namespace imforge {
namespace expr {

struct InterpSegment {
    bool is_expr = false;
    std::string text_or_expr, fmt;
    bool is_icon = false;
};

namespace detail {

inline bool InterpError(Error* err, const std::string& msg, int pos) {
    if (err) {
        err->message = msg;
        err->line = 1;
        err->col = pos + 1;
    }
    return false;
}

inline std::string Trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\n' || s[b] == '\r')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\n' || s[e - 1] == '\r')) --e;
    return s.substr(b, e - b);
}

inline size_t FindInterpClose(const std::string& s, size_t from) {
    char q = 0;
    for (size_t j = from; j < s.size(); ++j) {
        char d = s[j];
        if (q) {
            if (d == '\\') ++j;
            else if (d == q) q = 0;
        } else if (d == '"' || d == '\'') {
            q = d;
        } else if (d == '}') {
            return j;
        }
    }
    return std::string::npos;
}

inline size_t FindFmtSep(const std::string& body) {
    char q = 0;
    for (size_t j = 0; j < body.size(); ++j) {
        char d = body[j];
        if (q) {
            if (d == '\\') ++j;
            else if (d == q) q = 0;
        } else if (d == '"' || d == '\'') {
            q = d;
        } else if (d == ':') {
            if (j + 1 < body.size() && body[j + 1] == ':') {
                ++j;
                continue;
            }
            size_t k = j + 1;
            while (k < body.size() && (body[k] == ' ' || body[k] == '\t')) ++k;
            if (k < body.size() && body[k] == '%') return j;
        }
    }
    return std::string::npos;
}

template <class T>
inline std::string FormatOne(const std::string& spec, T arg) {
    int n = std::snprintf(nullptr, 0, spec.c_str(), arg);
    if (n < 0) return std::string();
    std::vector<char> buf((size_t)n + 1, '\0');
    std::snprintf(buf.data(), buf.size(), spec.c_str(), arg);
    return std::string(buf.data(), (size_t)n);
}

inline std::string Unpercent(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        out += s[i];
        if (s[i] == '%' && i + 1 < s.size() && s[i + 1] == '%') ++i;
    }
    return out;
}

inline std::string DefaultString(const Value& v) {
    switch (v.kind) {
    case Value::Kind::Null: return std::string();
    case Value::Kind::Bool: return v.b ? "true" : "false";
    case Value::Kind::Int: return FormatOne("%lld", v.i);
    case Value::Kind::Float: return FormatOne("%.2f", v.f);
    case Value::Kind::String: return v.s;
    case Value::Kind::Vec2: return FormatOne("%g", (double)v.v2.x) + ", " + FormatOne("%g", (double)v.v2.y);
    case Value::Kind::Vec4:
        return FormatOne("%g", (double)v.v4.x) + ", " + FormatOne("%g", (double)v.v4.y) + ", " +
               FormatOne("%g", (double)v.v4.z) + ", " + FormatOne("%g", (double)v.v4.w);
    case Value::Kind::Color: {
        char buf[16];
        auto ch = [](float f) { return (int)value_detail::ColorByte(f); };
        std::snprintf(buf, sizeof(buf), "#%02x%02x%02x%02x", ch(v.v4.x), ch(v.v4.y), ch(v.v4.z), ch(v.v4.w));
        return buf;
    }
    case Value::Kind::List: {
        std::string out = "[";
        for (size_t i = 0; i < v.list.size(); ++i) out += (i ? ", " : "") + DefaultString(v.list[i]);
        return out + "]";
    }
    }
    return std::string();
}

struct FmtSpec {
    size_t start = std::string::npos, end = std::string::npos;
    std::string flags, width, precision;
    char conv = 0;
};

inline bool ParseFmtSpec(const std::string& fmt, FmtSpec& spec) {
    size_t i = 0, n = fmt.size();
    bool found = false;
    while (i < n) {
        if (fmt[i] != '%') { ++i; continue; }
        if (i + 1 < n && fmt[i + 1] == '%') { i += 2; continue; }
        if (found) return false;
        found = true;
        spec.start = i;
        size_t j = i + 1;
        while (j < n && std::strchr("-+ #0", fmt[j])) spec.flags += fmt[j++];
        while (j < n && fmt[j] >= '0' && fmt[j] <= '9') spec.width += fmt[j++];
        if (j < n && fmt[j] == '.') {
            spec.precision += fmt[j++];
            while (j < n && fmt[j] >= '0' && fmt[j] <= '9') spec.precision += fmt[j++];
        }
        while (j < n && std::strchr("hlLjzt", fmt[j])) ++j;
        if (j >= n || spec.width.size() > 4 || spec.precision.size() > 5) return false;
        spec.conv = fmt[j];
        spec.end = j + 1;
        i = spec.end;
    }
    return true;
}

}  // namespace detail

inline const char* DefaultFormatFor(Value::Kind kind) {
    switch (kind) {
    case Value::Kind::Int: return "%d";
    case Value::Kind::Float: return "%.2f";
    default: return "%s";
    }
}

inline std::string FormatValue(const Value& v, const std::string& fmt) {
    using namespace detail;
    if (fmt.empty()) return DefaultString(v);
    FmtSpec spec;
    if (!ParseFmtSpec(fmt, spec)) return DefaultString(v);
    if (spec.start == std::string::npos) return Unpercent(fmt);
    std::string head = Unpercent(fmt.substr(0, spec.start));
    std::string tail = Unpercent(fmt.substr(spec.end));
    std::string base = "%" + spec.flags + spec.width + spec.precision;
    bool is_int = std::strchr("diuxXoc", spec.conv) != nullptr;
    bool is_flt = std::strchr("fFeEgGaA", spec.conv) != nullptr;
    bool numeric = v.kind == Value::Kind::Int || v.kind == Value::Kind::Float || v.kind == Value::Kind::Bool;
    std::string body;
    if (is_int && numeric) {
        long long x = v.kind == Value::Kind::Int ? v.i
                      : v.kind == Value::Kind::Float ? value_detail::DoubleToInt64(v.f) : (v.b ? 1 : 0);
        if (spec.conv == 'c') body = FormatOne(base + "c", (int)x);
        else if (spec.conv == 'd' || spec.conv == 'i') body = FormatOne(base + "lld", x);
        else body = FormatOne(base + "ll" + spec.conv, (unsigned long long)x);
    } else if (is_flt && numeric) {
        double d = v.kind == Value::Kind::Int ? (double)v.i : v.kind == Value::Kind::Float ? v.f : (v.b ? 1.0 : 0.0);
        body = FormatOne(base + spec.conv, d);
    } else if (spec.conv == 's' || is_int || is_flt) {
        std::string s = DefaultString(v);
        body = spec.conv == 's' ? FormatOne(base + "s", s.c_str()) : s;
    } else {
        return DefaultString(v);
    }
    return head + body + tail;
}

inline bool ParseInterpolation(const std::string& s, std::vector<InterpSegment>& out, Error* err) {
    using namespace detail;
    out.clear();
    std::string lit;
    size_t i = 0, n = s.size();
    while (i < n) {
        char c = s[i];
        if (c == '{') {
            if (i + 1 < n && s[i + 1] == '{') {
                lit += '{';
                i += 2;
                continue;
            }
            size_t close = FindInterpClose(s, i + 1);
            if (close == std::string::npos) return InterpError(err, "unterminated '{' in interpolation", (int)i);
            std::string body = Trim(s.substr(i + 1, close - i - 1));
            if (body.empty()) return InterpError(err, "empty '{}' in interpolation", (int)i);
            if (!lit.empty()) {
                InterpSegment text;
                text.text_or_expr = lit;
                out.push_back(text);
                lit.clear();
            }
            InterpSegment seg;
            if (body.compare(0, 5, "icon:") == 0 && (body.size() == 5 || body[5] != '%')) {
                seg.is_icon = true;
                seg.text_or_expr = Trim(body.substr(5));
                if (seg.text_or_expr.empty()) return InterpError(err, "missing icon name", (int)i);
            } else {
                seg.is_expr = true;
                size_t p = FindFmtSep(body);
                if (p != std::string::npos) {
                    seg.fmt = Trim(body.substr(p + 1));
                    body = Trim(body.substr(0, p));
                }
                if (body.empty()) return InterpError(err, "missing expression before ':'", (int)i);
                seg.text_or_expr = body;
            }
            out.push_back(seg);
            i = close + 1;
            continue;
        }
        if (c == '}' && i + 1 < n && s[i + 1] == '}') {
            lit += '}';
            i += 2;
            continue;
        }
        lit += c;
        ++i;
    }
    if (!lit.empty()) {
        InterpSegment text;
        text.text_or_expr = lit;
        out.push_back(text);
    }
    return true;
}

}  // namespace expr
}  // namespace imforge
