#pragma once

#include <algorithm>
#include <clocale>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace imforge {

struct Json {
    enum class Kind { Null, Bool, Number, String, Array, Object };
    Kind kind = Kind::Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<Json> arr;
    std::vector<std::pair<std::string, Json>> obj;

    Json() {}
    Json(bool v) : kind(Kind::Bool), b(v) {}
    Json(int v) : kind(Kind::Number), num(v) {}
    Json(unsigned v) : kind(Kind::Number), num(v) {}
    Json(long v) : kind(Kind::Number), num(static_cast<double>(v)) {}
    Json(unsigned long v) : kind(Kind::Number), num(static_cast<double>(v)) {}
    Json(long long v) : kind(Kind::Number), num(static_cast<double>(v)) {}
    Json(unsigned long long v) : kind(Kind::Number), num(static_cast<double>(v)) {}
    Json(float v) : kind(Kind::Number), num(v) {}
    Json(double v) : kind(Kind::Number), num(v) {}
    Json(const char* v) : kind(Kind::String), str(v ? v : "") {}
    Json(std::string v) : kind(Kind::String), str(std::move(v)) {}

    static Json Array() { Json j; j.kind = Kind::Array; return j; }
    static Json Object() { Json j; j.kind = Kind::Object; return j; }
    static const Json& Null() { static const Json n; return n; }

    bool IsNull() const { return kind == Kind::Null; }
    bool IsBool() const { return kind == Kind::Bool; }
    bool IsNumber() const { return kind == Kind::Number; }
    bool IsString() const { return kind == Kind::String; }
    bool IsArray() const { return kind == Kind::Array; }
    bool IsObject() const { return kind == Kind::Object; }

    bool AsBool(bool def = false) const { return kind == Kind::Bool ? b : def; }
    int AsInt(int def = 0) const {
        if (kind != Kind::Number || num != num) return def;
        if (num >= 2147483647.0) return 2147483647;
        if (num <= -2147483648.0) return (-2147483647 - 1);
        return static_cast<int>(num);
    }
    long long AsInt64(long long def = 0) const {
        if (kind != Kind::Number || num != num) return def;
        if (num >= 9223372036854775807.0) return 9223372036854775807LL;
        if (num <= -9223372036854775808.0) return (-9223372036854775807LL - 1);
        return static_cast<long long>(num);
    }
    double AsDouble(double def = 0) const { return kind == Kind::Number ? num : def; }
    const std::string& AsString() const {
        static const std::string empty;
        return kind == Kind::String ? str : empty;
    }
    std::string AsString(const std::string& def) const { return kind == Kind::String ? str : def; }

    const Json& operator[](const char* key) const {
        const Json* f = Find(key);
        return f ? *f : Null();
    }
    const Json& operator[](size_t i) const {
        return (kind == Kind::Array && i < arr.size()) ? arr[i] : Null();
    }
    const Json& operator[](int i) const { return i < 0 ? Null() : (*this)[static_cast<size_t>(i)]; }
    const Json& operator[](const std::string& key) const { return (*this)[key.c_str()]; }

    Json* Find(const char* key) {
        if (kind != Kind::Object || !key) return nullptr;
        for (auto& kv : obj)
            if (kv.first == key) return &kv.second;
        return nullptr;
    }
    const Json* Find(const char* key) const {
        if (kind != Kind::Object || !key) return nullptr;
        for (const auto& kv : obj)
            if (kv.first == key) return &kv.second;
        return nullptr;
    }
    bool Has(const char* key) const { return Find(key) != nullptr; }

    Json& Set(const std::string& key, Json v) {
        if (kind != Kind::Object) {
            kind = Kind::Object;
            obj.clear();
        }
        for (auto& kv : obj) {
            if (kv.first == key) {
                kv.second = std::move(v);
                return *this;
            }
        }
        obj.emplace_back(key, std::move(v));
        return *this;
    }
    Json& Push(Json v) {
        if (kind != Kind::Array) {
            kind = Kind::Array;
            arr.clear();
        }
        arr.push_back(std::move(v));
        return *this;
    }
    size_t Size() const {
        if (kind == Kind::Array) return arr.size();
        if (kind == Kind::Object) return obj.size();
        return 0;
    }
    void Erase(const char* key) {
        if (kind != Kind::Object || !key) return;
        for (size_t i = 0; i < obj.size(); ++i) {
            if (obj[i].first == key) {
                obj.erase(obj.begin() + static_cast<std::ptrdiff_t>(i));
                return;
            }
        }
    }

    bool operator==(const Json& o) const {
        if (kind != o.kind) return false;
        switch (kind) {
        case Kind::Null: return true;
        case Kind::Bool: return b == o.b;
        case Kind::Number: return num == o.num;
        case Kind::String: return str == o.str;
        case Kind::Array: return arr == o.arr;
        case Kind::Object: {
            if (obj.size() != o.obj.size()) return false;
            for (const auto& kv : obj) {
                const Json* other = o.Find(kv.first.c_str());
                if (!other || !(kv.second == *other)) return false;
            }
            return true;
        }
        }
        return false;
    }
    bool operator!=(const Json& o) const { return !(*this == o); }
};

struct JsonError {
    std::string message;
    int line = 0, col = 0;
};

namespace json_detail {

inline char DecimalPoint() {
    const char* dp = std::localeconv()->decimal_point;
    return dp && dp[0] ? dp[0] : '.';
}

// strtod that ignores the C locale, JSON always uses '.'
inline double StrToDoubleC(const char* s, size_t len) {
    char buf[64];
    std::string heap;
    char* p = buf;
    if (len >= sizeof(buf)) {
        heap.assign(s, len);
        p = &heap[0];
    } else {
        std::memcpy(buf, s, len);
        buf[len] = 0;
    }
    char dp = DecimalPoint();
    if (dp != '.') {
        for (size_t i = 0; i < len; ++i)
            if (p[i] == '.') p[i] = dp;
    }
    return std::strtod(p, nullptr);
}

struct Parser {
    const char* s;
    size_t n;
    size_t pos = 0;
    size_t line_start = 0;
    int line = 1;
    int depth = 0;
    JsonError* err;
    size_t values = 0;
    static const int kMaxDepth = 256;
    static const size_t kMaxValues = 500000;

    bool Fail(const char* msg) {
        if (err && err->message.empty()) {
            err->message = msg;
            err->line = line;
            err->col = static_cast<int>(pos - line_start) + 1;
        }
        return false;
    }
    void SkipWs() {
        while (pos < n) {
            char c = s[pos];
            if (c == '\n') {
                ++pos;
                ++line;
                line_start = pos;
            } else if (c == ' ' || c == '\t' || c == '\r') {
                ++pos;
            } else {
                break;
            }
        }
    }
    bool Match(const char* lit) {
        size_t l = std::strlen(lit);
        if (pos + l > n || std::memcmp(s + pos, lit, l) != 0) return false;
        pos += l;
        return true;
    }
    static void AppendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    bool Hex4(unsigned& out) {
        if (pos + 4 > n) return Fail("truncated \\u escape");
        out = 0;
        for (int k = 0; k < 4; ++k) {
            char c = s[pos];
            unsigned v;
            if (c >= '0' && c <= '9') v = static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') v = static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v = static_cast<unsigned>(c - 'A' + 10);
            else return Fail("invalid hex digit in \\u escape");
            out = (out << 4) | v;
            ++pos;
        }
        return true;
    }
    bool ParseString(std::string& out) {
        ++pos;
        out.clear();
        while (true) {
            if (pos >= n) return Fail("unterminated string");
            unsigned char c = static_cast<unsigned char>(s[pos]);
            if (c == '"') {
                ++pos;
                return true;
            }
            if (c < 0x20) return Fail("control character in string");
            if (c != '\\') {
                out.push_back(static_cast<char>(c));
                ++pos;
                continue;
            }
            ++pos;
            if (pos >= n) return Fail("unterminated escape");
            char e = s[pos++];
            switch (e) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                unsigned cp = 0;
                if (!Hex4(cp)) return false;
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    if (pos + 2 > n || s[pos] != '\\' || s[pos + 1] != 'u')
                        return Fail("missing low surrogate");
                    pos += 2;
                    unsigned lo = 0;
                    if (!Hex4(lo)) return false;
                    if (lo < 0xDC00 || lo > 0xDFFF) return Fail("invalid low surrogate");
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    return Fail("unexpected low surrogate");
                }
                AppendUtf8(out, cp);
                break;
            }
            default:
                --pos;
                return Fail("invalid escape sequence");
            }
        }
    }
    bool ParseNumber(Json& out) {
        size_t start = pos;
        if (pos < n && s[pos] == '-') ++pos;
        if (pos >= n) return Fail("invalid number");
        if (s[pos] == '0') {
            ++pos;
        } else if (s[pos] >= '1' && s[pos] <= '9') {
            while (pos < n && s[pos] >= '0' && s[pos] <= '9') ++pos;
        } else {
            return Fail("invalid number");
        }
        if (pos < n && s[pos] == '.') {
            ++pos;
            if (pos >= n || s[pos] < '0' || s[pos] > '9') return Fail("invalid number");
            while (pos < n && s[pos] >= '0' && s[pos] <= '9') ++pos;
        }
        if (pos < n && (s[pos] == 'e' || s[pos] == 'E')) {
            ++pos;
            if (pos < n && (s[pos] == '+' || s[pos] == '-')) ++pos;
            if (pos >= n || s[pos] < '0' || s[pos] > '9') return Fail("invalid number");
            while (pos < n && s[pos] >= '0' && s[pos] <= '9') ++pos;
        }
        out.kind = Json::Kind::Number;
        out.num = StrToDoubleC(s + start, pos - start);
        return true;
    }
    bool ParseValue(Json& out) {
        SkipWs();
        if (pos >= n) return Fail("unexpected end of input");
        if (++values > kMaxValues) return Fail("too many values");
        char c = s[pos];
        if (c == '{') {
            if (++depth > kMaxDepth) return Fail("nesting too deep");
            ++pos;
            out = Json::Object();
            SkipWs();
            if (pos < n && s[pos] == '}') {
                ++pos;
                --depth;
                return true;
            }
            while (true) {
                SkipWs();
                if (pos >= n || s[pos] != '"') return Fail("expected string key");
                std::string key;
                if (!ParseString(key)) return false;
                SkipWs();
                if (pos >= n || s[pos] != ':') return Fail("expected ':'");
                ++pos;
                Json v;
                if (!ParseValue(v)) return false;
                out.obj.emplace_back(std::move(key), std::move(v));
                SkipWs();
                if (pos >= n) return Fail("unterminated object");
                if (s[pos] == ',') {
                    ++pos;
                    continue;
                }
                if (s[pos] == '}') {
                    ++pos;
                    --depth;
                    return true;
                }
                return Fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            if (++depth > kMaxDepth) return Fail("nesting too deep");
            ++pos;
            out = Json::Array();
            SkipWs();
            if (pos < n && s[pos] == ']') {
                ++pos;
                --depth;
                return true;
            }
            while (true) {
                Json v;
                if (!ParseValue(v)) return false;
                out.arr.push_back(std::move(v));
                SkipWs();
                if (pos >= n) return Fail("unterminated array");
                if (s[pos] == ',') {
                    ++pos;
                    continue;
                }
                if (s[pos] == ']') {
                    ++pos;
                    --depth;
                    return true;
                }
                return Fail("expected ',' or ']'");
            }
        }
        if (c == '"') {
            out = Json("");
            return ParseString(out.str);
        }
        if (c == 't') {
            if (!Match("true")) return Fail("invalid literal");
            out = Json(true);
            return true;
        }
        if (c == 'f') {
            if (!Match("false")) return Fail("invalid literal");
            out = Json(false);
            return true;
        }
        if (c == 'n') {
            if (!Match("null")) return Fail("invalid literal");
            out = Json();
            return true;
        }
        if (c == '-' || (c >= '0' && c <= '9')) return ParseNumber(out);
        return Fail("unexpected character");
    }
};

inline void FormatNumber(double v, std::string& out) {
    char buf[32];
    if (v != v || v == HUGE_VAL || v == -HUGE_VAL) {
        out += "null";
        return;
    }
    if (v == std::floor(v) && std::fabs(v) < 1e15) {
        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v));
        out += buf;
        return;
    }
    static const char* const fmts[] = {"%.15g", "%.16g", "%.17g"};
    for (int k = 0; k < 3; ++k) {
        std::snprintf(buf, sizeof(buf), fmts[k], v);
        if (k == 2 || std::strtod(buf, nullptr) == v) break;
    }
    for (char* p = buf; *p; ++p)
        if (*p == ',') *p = '.';
    out += buf;
}

inline void EscapeString(const std::string& s, std::string& out) {
    out.push_back('"');
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out.push_back(static_cast<char>(c));
            }
        }
    }
    out.push_back('"');
}

inline void Dump(const Json& j, int indent, bool sort_keys, int level, std::string& out) {
    switch (j.kind) {
    case Json::Kind::Null: out += "null"; return;
    case Json::Kind::Bool: out += j.b ? "true" : "false"; return;
    case Json::Kind::Number: FormatNumber(j.num, out); return;
    case Json::Kind::String: EscapeString(j.str, out); return;
    case Json::Kind::Array: {
        if (j.arr.empty()) {
            out += "[]";
            return;
        }
        out.push_back('[');
        for (size_t i = 0; i < j.arr.size(); ++i) {
            if (i) out.push_back(',');
            if (indent > 0) {
                out.push_back('\n');
                out.append(static_cast<size_t>(indent * (level + 1)), ' ');
            }
            Dump(j.arr[i], indent, sort_keys, level + 1, out);
        }
        if (indent > 0) {
            out.push_back('\n');
            out.append(static_cast<size_t>(indent * level), ' ');
        }
        out.push_back(']');
        return;
    }
    case Json::Kind::Object: {
        if (j.obj.empty()) {
            out += "{}";
            return;
        }
        std::vector<size_t> order(j.obj.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        if (sort_keys) {
            std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
                return j.obj[a].first < j.obj[b].first;
            });
        }
        out.push_back('{');
        for (size_t k = 0; k < order.size(); ++k) {
            const auto& kv = j.obj[order[k]];
            if (k) out.push_back(',');
            if (indent > 0) {
                out.push_back('\n');
                out.append(static_cast<size_t>(indent * (level + 1)), ' ');
            }
            EscapeString(kv.first, out);
            out.push_back(':');
            if (indent > 0) out.push_back(' ');
            Dump(kv.second, indent, sort_keys, level + 1, out);
        }
        if (indent > 0) {
            out.push_back('\n');
            out.append(static_cast<size_t>(indent * level), ' ');
        }
        out.push_back('}');
        return;
    }
    }
}

}  // namespace json_detail

inline bool ParseJson(const char* text, size_t len, Json& out, JsonError* err = nullptr) {
    if (err) {
        err->message.clear();
        err->line = err->col = 0;
    }
    if (!text) {
        text = "";
        len = 0;
    }
    json_detail::Parser p{text, len, 0, 0, 1, 0, err};
    if (len >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
        p.pos = 3;
    Json result;
    if (!p.ParseValue(result)) return false;
    p.SkipWs();
    if (p.pos != len) return p.Fail("trailing characters after JSON value");
    out = std::move(result);
    return true;
}

inline bool ParseJson(const std::string& text, Json& out, JsonError* err = nullptr) {
    return ParseJson(text.data(), text.size(), out, err);
}

inline std::string DumpJson(const Json& j, int indent = 2, bool sort_keys = true) {
    std::string out;
    json_detail::Dump(j, indent < 0 ? 0 : indent, sort_keys, 0, out);
    return out;
}

}  // namespace imforge
