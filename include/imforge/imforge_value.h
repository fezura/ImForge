#pragma once

#include "imforge_json.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace imforge {

struct Error {
    std::string message;
    int line = 0, col = 0;
};

struct Diagnostic {
    enum class Level { Info, Warning, Error };
    Level level = Level::Error;
    std::string message, panel, node_id, prop;
};

struct Vec2 {
    float x = 0, y = 0;
};
struct Vec4 {
    float x = 0, y = 0, z = 0, w = 0;
};
inline bool operator==(const Vec2& a, const Vec2& b) { return a.x == b.x && a.y == b.y; }
inline bool operator!=(const Vec2& a, const Vec2& b) { return !(a == b); }
inline bool operator==(const Vec4& a, const Vec4& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}
inline bool operator!=(const Vec4& a, const Vec4& b) { return !(a == b); }

#ifdef IMGUI_VERSION
inline Vec2 ToVec2(const ImVec2& v) { return Vec2{v.x, v.y}; }
inline Vec4 ToVec4(const ImVec4& v) { return Vec4{v.x, v.y, v.z, v.w}; }
inline ImVec2 ToImVec2(const Vec2& v) { return ImVec2(v.x, v.y); }
inline ImVec4 ToImVec4(const Vec4& v) { return ImVec4(v.x, v.y, v.z, v.w); }
#endif

namespace value_detail {

inline int HexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

inline bool ParseHexColor(const std::string& s, Vec4& out) {
    if (s.size() != 7 && s.size() != 9) return false;
    if (s[0] != '#') return false;
    float ch[4] = {0, 0, 0, 1};
    size_t n = (s.size() - 1) / 2;
    for (size_t i = 0; i < n; ++i) {
        int hi = HexNibble(s[1 + i * 2]), lo = HexNibble(s[2 + i * 2]);
        if (hi < 0 || lo < 0) return false;
        ch[i] = static_cast<float>(hi * 16 + lo) / 255.0f;
    }
    out = Vec4{ch[0], ch[1], ch[2], ch[3]};
    return true;
}

inline unsigned ColorByte(float f) {
    if (f != f || f <= 0) return 0;
    if (f >= 1) return 255;
    return static_cast<unsigned>(f * 255.0f + 0.5f) & 0xFFu;
}

inline std::string HexColor(const Vec4& c) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x%02x", ColorByte(c.x) & 0xFFu, ColorByte(c.y) & 0xFFu,
                  ColorByte(c.z) & 0xFFu, ColorByte(c.w) & 0xFFu);
    return buf;
}

inline std::string FormatDouble(double d) {
    char buf[32];
    if (d > -1e15 && d < 1e15 && d == static_cast<double>(static_cast<long long>(d))) {
        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(d));
    } else {
        std::snprintf(buf, sizeof(buf), "%g", d);
        for (char* p = buf; *p; ++p)
            if (*p == ',') *p = '.';
    }
    return buf;
}

inline long long DoubleToInt64(double d) {
    if (d != d) return 0;
    if (d >= 9223372036854775807.0) return 9223372036854775807LL;
    if (d <= -9223372036854775808.0) return (-9223372036854775807LL - 1);
    return static_cast<long long>(d);
}

}  // namespace value_detail

struct Value {
    enum class Kind { Null, Bool, Int, Float, String, Vec2, Vec4, Color, List };
    Kind kind = Kind::Null;
    bool b = false;
    long long i = 0;
    double f = 0;
    std::string s;
    Vec2 v2;
    Vec4 v4;
    std::vector<Value> list;

    Value() {}
    Value(bool v) : kind(Kind::Bool), b(v) {}
    Value(int v) : kind(Kind::Int), i(v) {}
    Value(unsigned v) : kind(Kind::Int), i(v) {}
    Value(long v) : kind(Kind::Int), i(v) {}
    Value(unsigned long v) : kind(Kind::Int), i(static_cast<long long>(v)) {}
    Value(long long v) : kind(Kind::Int), i(v) {}
    Value(unsigned long long v) : kind(Kind::Int), i(static_cast<long long>(v)) {}
    Value(float v) : kind(Kind::Float), f(v) {}
    Value(double v) : kind(Kind::Float), f(v) {}
    Value(const char* v) : kind(Kind::String), s(v ? v : "") {}
    Value(std::string v) : kind(Kind::String), s(std::move(v)) {}
    Value(const Vec2& v) : kind(Kind::Vec2), v2(v) {}
    Value(const Vec4& v) : kind(Kind::Vec4), v4(v) {}
    Value(std::vector<Value> v) : kind(Kind::List), list(std::move(v)) {}
#ifdef IMGUI_VERSION
    Value(const ImVec2& v) : kind(Kind::Vec2), v2(Vec2{v.x, v.y}) {}
    Value(const ImVec4& v) : kind(Kind::Vec4), v4(Vec4{v.x, v.y, v.z, v.w}) {}
#endif

    static Value Color(const Vec4& c) {
        Value v(c);
        v.kind = Kind::Color;
        return v;
    }
    static Value Color(float r, float g, float b, float a = 1.0f) { return Color(Vec4{r, g, b, a}); }
    static Value List() {
        Value v;
        v.kind = Kind::List;
        return v;
    }

    bool IsNull() const { return kind == Kind::Null; }
    bool IsBool() const { return kind == Kind::Bool; }
    bool IsInt() const { return kind == Kind::Int; }
    bool IsFloat() const { return kind == Kind::Float; }
    bool IsNumber() const { return kind == Kind::Int || kind == Kind::Float; }
    bool IsString() const { return kind == Kind::String; }
    bool IsVec2() const { return kind == Kind::Vec2; }
    bool IsVec4() const { return kind == Kind::Vec4; }
    bool IsColor() const { return kind == Kind::Color; }
    bool IsList() const { return kind == Kind::List; }

    double ToDouble() const {
        switch (kind) {
        case Kind::Bool: return b ? 1.0 : 0.0;
        case Kind::Int: return static_cast<double>(i);
        case Kind::Float: return f;
        case Kind::String: return std::strtod(s.c_str(), nullptr);
        case Kind::Vec2: return v2.x;
        case Kind::Vec4:
        case Kind::Color: return v4.x;
        case Kind::List: return list.empty() ? 0.0 : list[0].ToDouble();
        default: return 0.0;
        }
    }
    long long ToInt() const {
        switch (kind) {
        case Kind::Bool: return b ? 1 : 0;
        case Kind::Int: return i;
        case Kind::Float: return value_detail::DoubleToInt64(f);
        case Kind::String: return std::strtoll(s.c_str(), nullptr, 0);
        case Kind::List: return list.empty() ? 0 : list[0].ToInt();
        default: return value_detail::DoubleToInt64(ToDouble());
        }
    }
    bool ToBool() const {
        switch (kind) {
        case Kind::Null: return false;
        case Kind::Bool: return b;
        case Kind::Int: return i != 0;
        case Kind::Float: return f != 0.0;
        case Kind::String: return !s.empty() && s != "false" && s != "0";
        case Kind::List: return !list.empty();
        default: return true;
        }
    }
    std::string ToString() const {
        char buf[128];
        switch (kind) {
        case Kind::Null: return std::string();
        case Kind::Bool: return b ? "true" : "false";
        case Kind::Int: std::snprintf(buf, sizeof(buf), "%lld", i); return buf;
        case Kind::Float: return value_detail::FormatDouble(f);
        case Kind::String: return s;
        case Kind::Vec2:
            return "(" + value_detail::FormatDouble(v2.x) + ", " + value_detail::FormatDouble(v2.y) + ")";
        case Kind::Vec4:
            return "(" + value_detail::FormatDouble(v4.x) + ", " + value_detail::FormatDouble(v4.y) + ", " +
                   value_detail::FormatDouble(v4.z) + ", " + value_detail::FormatDouble(v4.w) + ")";
        case Kind::Color: return value_detail::HexColor(v4);
        case Kind::List: {
            std::string r = "[";
            for (size_t k = 0; k < list.size(); ++k) {
                if (k) r += ", ";
                r += list[k].ToString();
            }
            return r + "]";
        }
        }
        return std::string();
    }
    Vec2 ToVec2() const {
        switch (kind) {
        case Kind::Vec2: return v2;
        case Kind::Vec4:
        case Kind::Color: return Vec2{v4.x, v4.y};
        case Kind::List:
            return Vec2{list.size() > 0 ? static_cast<float>(list[0].ToDouble()) : 0.0f,
                        list.size() > 1 ? static_cast<float>(list[1].ToDouble()) : 0.0f};
        default: {
            float d = static_cast<float>(ToDouble());
            return Vec2{d, d};
        }
        }
    }
    Vec4 ToVec4() const {
        switch (kind) {
        case Kind::Vec2: return Vec4{v2.x, v2.y, 0, 0};
        case Kind::Vec4:
        case Kind::Color: return v4;
        case Kind::List:
            return Vec4{list.size() > 0 ? static_cast<float>(list[0].ToDouble()) : 0.0f,
                        list.size() > 1 ? static_cast<float>(list[1].ToDouble()) : 0.0f,
                        list.size() > 2 ? static_cast<float>(list[2].ToDouble()) : 0.0f,
                        list.size() > 3 ? static_cast<float>(list[3].ToDouble()) : 0.0f};
        case Kind::String: {
            Vec4 c;
            if (value_detail::ParseHexColor(s, c)) return c;
            return Vec4{};
        }
        default: {
            float d = static_cast<float>(ToDouble());
            return Vec4{d, d, d, d};
        }
        }
    }

    static Value FromJson(const Json& j, Kind hint = Kind::Null) {
        switch (hint) {
        case Kind::Null: return Infer(j);
        case Kind::Bool:
            if (j.IsBool()) return Value(j.b);
            if (j.IsNumber()) return Value(j.num != 0);
            if (j.IsString()) return Value(j.str == "true" || j.str == "1");
            return Value(false);
        case Kind::Int:
            if (j.IsNumber()) return Value(value_detail::DoubleToInt64(j.num));
            if (j.IsBool()) return Value(static_cast<long long>(j.b ? 1 : 0));
            if (j.IsString()) return Value(std::strtoll(j.str.c_str(), nullptr, 0));
            return Value(0LL);
        case Kind::Float:
            if (j.IsNumber()) return Value(j.num);
            if (j.IsBool()) return Value(j.b ? 1.0 : 0.0);
            if (j.IsString()) return Value(std::strtod(j.str.c_str(), nullptr));
            return Value(0.0);
        case Kind::String:
            if (j.IsString()) return Value(j.str);
            if (j.IsNull()) return Value(std::string());
            return Value(Infer(j).ToString());
        case Kind::Vec2: return Value(Infer(j).ToVec2());
        case Kind::Vec4: return Value(Infer(j).ToVec4());
        case Kind::Color: return Color(Infer(j).ToVec4());
        case Kind::List: {
            Value v = List();
            if (j.IsArray()) {
                for (const Json& e : j.arr) v.list.push_back(Infer(e));
            } else if (!j.IsNull()) {
                v.list.push_back(Infer(j));
            }
            return v;
        }
        }
        return Value();
    }

    Json ToJson() const {
        switch (kind) {
        case Kind::Null: return Json();
        case Kind::Bool: return Json(b);
        case Kind::Int: return Json(static_cast<double>(i));
        case Kind::Float: return Json(f);
        case Kind::String: return Json(s);
        case Kind::Vec2: {
            Json o = Json::Object();
            o.Set("x", Json(static_cast<double>(v2.x)));
            o.Set("y", Json(static_cast<double>(v2.y)));
            return o;
        }
        case Kind::Vec4: {
            Json o = Json::Object();
            o.Set("x", Json(static_cast<double>(v4.x)));
            o.Set("y", Json(static_cast<double>(v4.y)));
            o.Set("z", Json(static_cast<double>(v4.z)));
            o.Set("w", Json(static_cast<double>(v4.w)));
            return o;
        }
        case Kind::Color: return Json(value_detail::HexColor(v4));
        case Kind::List: {
            Json a = Json::Array();
            for (const Value& e : list) a.Push(e.ToJson());
            return a;
        }
        }
        return Json();
    }

    bool operator==(const Value& o) const {
        if (kind != o.kind) return false;
        switch (kind) {
        case Kind::Null: return true;
        case Kind::Bool: return b == o.b;
        case Kind::Int: return i == o.i;
        case Kind::Float: return f == o.f;
        case Kind::String: return s == o.s;
        case Kind::Vec2: return v2 == o.v2;
        case Kind::Vec4:
        case Kind::Color: return v4 == o.v4;
        case Kind::List: return list == o.list;
        }
        return false;
    }
    bool operator!=(const Value& o) const { return !(*this == o); }

    static Value Infer(const Json& j) {
        switch (j.kind) {
        case Json::Kind::Null: return Value();
        case Json::Kind::Bool: return Value(j.b);
        case Json::Kind::Number:
            if (j.num > -9.2e18 && j.num < 9.2e18 &&
                j.num == static_cast<double>(static_cast<long long>(j.num)))
                return Value(static_cast<long long>(j.num));
            return Value(j.num);
        case Json::Kind::String: return Value(j.str);
        case Json::Kind::Array: {
            Value v = List();
            for (const Json& e : j.arr) v.list.push_back(Infer(e));
            return v;
        }
        case Json::Kind::Object: {
            const Json* x = j.Find("x");
            const Json* y = j.Find("y");
            if (x && y && x->IsNumber() && y->IsNumber()) {
                const Json* z = j.Find("z");
                const Json* w = j.Find("w");
                if (z && w && z->IsNumber() && w->IsNumber())
                    return Value(Vec4{static_cast<float>(x->num), static_cast<float>(y->num),
                                      static_cast<float>(z->num), static_cast<float>(w->num)});
                return Value(Vec2{static_cast<float>(x->num), static_cast<float>(y->num)});
            }
            return Value();
        }
        }
        return Value();
    }
};

enum class CKind {
    Bool, I8, U8, I16, U16, I32, U32, I64, U64, Float, Double, StdString, CharArray,
    Vec2, Vec4, FloatArray, IntArray, Enum, Struct, Vector, Array, TexID, Unknown
};

inline const char* CKindName(CKind k) {
    static const char* const names[] = {
        "Bool", "I8", "U8", "I16", "U16", "I32", "U32", "I64", "U64", "Float", "Double", "StdString",
        "CharArray", "Vec2", "Vec4", "FloatArray", "IntArray", "Enum", "Struct", "Vector", "Array",
        "TexID", "Unknown"};
    int i = static_cast<int>(k);
    return (i >= 0 && i < static_cast<int>(sizeof(names) / sizeof(names[0]))) ? names[i] : "?";
}

struct StructInfo;

struct EnumInfo {
    const char* name = nullptr;
    std::vector<std::pair<std::string, long long>> values;
    std::string name_str;
    const char* NameOf(long long v) const {
        for (const auto& kv : values)
            if (kv.second == v) return kv.first.c_str();
        return nullptr;
    }
    bool ValueOf(const char* n, long long& out) const {
        for (const auto& kv : values) {
            if (kv.first == n) {
                out = kv.second;
                return true;
            }
        }
        return false;
    }
};

struct CType {
    CKind kind = CKind::Unknown;
    int count = 0;
    const StructInfo* strct = nullptr;
    const EnumInfo* enm = nullptr;
    const CType* elem = nullptr;
    std::string name;
    size_t size = 0;
    void* (*vec_data)(void*) = nullptr;
    size_t (*vec_size)(const void*) = nullptr;
};

struct FieldInfo {
    std::string name;
    CType type;
    size_t offset = 0;
};

struct StructInfo {
    std::string name;
    size_t size = 0;
    std::vector<FieldInfo> fields;
    const FieldInfo* Find(const char* field) const {
        for (const auto& f : fields)
            if (f.name == field) return &f;
        return nullptr;
    }
};

struct TypeRegistry {
    std::vector<std::unique_ptr<StructInfo>> structs;
    std::vector<std::unique_ptr<EnumInfo>> enums;

    const StructInfo* RegisterStruct(StructInfo info) {
        for (auto& s : structs) {
            if (s->name == info.name) {
                *s = std::move(info);
                return s.get();
            }
        }
        structs.emplace_back(new StructInfo(std::move(info)));
        return structs.back().get();
    }
    const EnumInfo* RegisterEnum(EnumInfo info) {
        if (!info.name && !info.name_str.empty()) {
            info.name_str.shrink_to_fit();
        } else if (info.name) {
            info.name_str = info.name;
        }
        for (auto& e : enums) {
            if (e->name_str == info.name_str) {
                bool literal = info.name != nullptr;
                *e = std::move(info);
                if (!literal) e->name = e->name_str.c_str();
                return e.get();
            }
        }
        bool literal = info.name != nullptr;
        enums.emplace_back(new EnumInfo(std::move(info)));
        if (!literal) enums.back()->name = enums.back()->name_str.c_str();
        return enums.back().get();
    }
    const StructInfo* FindStruct(const char* name) const {
        if (!name) return nullptr;
        for (const auto& s : structs)
            if (s->name == name) return s.get();
        return nullptr;
    }
    const EnumInfo* FindEnum(const char* name) const {
        if (!name) return nullptr;
        for (const auto& e : enums)
            if (e->name_str == name) return e.get();
        return nullptr;
    }
    static TypeRegistry& Global() {
        static TypeRegistry r;
        return r;
    }
};

template <class T> struct TypeName {
    static constexpr const char* value = nullptr;
};
template <class T> struct Reflector {
    static const StructInfo* Info() { return nullptr; }
};
template <class T> struct EnumReflector {
    static const EnumInfo* Info() { return nullptr; }
};

template <class T> CType CTypeOf();

namespace value_detail {

template <class T> struct Holder {
    static const CType& Get() {
        static const CType t = CTypeOf<T>();
        return t;
    }
};

template <class T> struct IntKind {
    static const bool s = std::is_signed<T>::value;
    static const CKind kind = std::is_same<T, bool>::value ? CKind::Bool
                              : sizeof(T) == 1          ? (s ? CKind::I8 : CKind::U8)
                              : sizeof(T) == 2          ? (s ? CKind::I16 : CKind::U16)
                              : sizeof(T) == 4          ? (s ? CKind::I32 : CKind::U32)
                                                        : (s ? CKind::I64 : CKind::U64);
};

template <class T> struct IntName { static const char* Get() { return "int"; } };
template <> struct IntName<signed char> { static const char* Get() { return "signed char"; } };
template <> struct IntName<unsigned char> { static const char* Get() { return "unsigned char"; } };
template <> struct IntName<short> { static const char* Get() { return "short"; } };
template <> struct IntName<unsigned short> { static const char* Get() { return "unsigned short"; } };
template <> struct IntName<int> { static const char* Get() { return "int"; } };
template <> struct IntName<unsigned> { static const char* Get() { return "unsigned"; } };
template <> struct IntName<long> { static const char* Get() { return "long"; } };
template <> struct IntName<unsigned long> { static const char* Get() { return "unsigned long"; } };
template <> struct IntName<long long> { static const char* Get() { return "long long"; } };
template <> struct IntName<unsigned long long> { static const char* Get() { return "unsigned long long"; } };
template <> struct IntName<char> { static const char* Get() { return "char"; } };
template <> struct IntName<bool> { static const char* Get() { return "bool"; } };

inline CType Simple(CKind k, const char* name, size_t size) {
    CType t;
    t.kind = k;
    t.name = name;
    t.size = size;
    return t;
}

template <class V> void* VecData(void* p) { return static_cast<void*>(static_cast<V*>(p)->data()); }
template <class V> size_t VecSize(const void* p) { return static_cast<const V*>(p)->size(); }

template <class T, class = void> struct HasRef : std::false_type {};
template <class T> struct HasRef<T, std::void_t<decltype(Reflector<T>::Ref())>> : std::true_type {};
template <class T> const StructInfo* StructRef() {
    if constexpr (HasRef<T>::value) return Reflector<T>::Ref();
    else return Reflector<T>::Info();
}

template <class T> long long TexToInt(T v) {
    if constexpr (std::is_pointer<T>::value) return static_cast<long long>(reinterpret_cast<uintptr_t>(v));
    else return static_cast<long long>(v);
}

template <class T, class = void> struct Maker {
    static CType Make() {
        if constexpr (std::is_integral<T>::value) {
            return Simple(IntKind<T>::kind, IntName<T>::Get(), sizeof(T));
        } else if constexpr (std::is_enum<T>::value) {
            CType t;
            t.kind = CKind::Enum;
            t.enm = EnumReflector<T>::Info();
            t.name = TypeName<T>::value ? TypeName<T>::value : "enum";
            t.size = sizeof(T);
            t.elem = &Holder<typename std::underlying_type<T>::type>::Get();
            return t;
        } else if constexpr (std::is_pointer<T>::value) {
            return Simple(CKind::TexID, "ImTextureID", sizeof(T));
        } else if constexpr (std::is_class<T>::value && TypeName<T>::value != nullptr) {
            CType t;
            t.kind = CKind::Struct;
            t.strct = StructRef<T>();
            t.name = TypeName<T>::value;
            t.size = sizeof(T);
            return t;
        } else {
            return Simple(CKind::Unknown, "unknown", sizeof(T));
        }
    }
};

template <> struct Maker<float> {
    static CType Make() { return Simple(CKind::Float, "float", sizeof(float)); }
};
template <> struct Maker<double> {
    static CType Make() { return Simple(CKind::Double, "double", sizeof(double)); }
};
template <> struct Maker<std::string> {
    static CType Make() { return Simple(CKind::StdString, "std::string", sizeof(std::string)); }
};
template <> struct Maker<Vec2> {
    static CType Make() { return Simple(CKind::Vec2, "imforge::Vec2", sizeof(Vec2)); }
};
template <> struct Maker<Vec4> {
    static CType Make() { return Simple(CKind::Vec4, "imforge::Vec4", sizeof(Vec4)); }
};
#ifdef IMGUI_VERSION
template <> struct Maker<ImVec2> {
    static CType Make() { return Simple(CKind::Vec2, "ImVec2", sizeof(ImVec2)); }
};
template <> struct Maker<ImVec4> {
    static CType Make() { return Simple(CKind::Vec4, "ImVec4", sizeof(ImVec4)); }
};
#if IMGUI_VERSION_NUM >= 19200
template <> struct Maker<ImTextureRef> {
    static CType Make() { return Simple(CKind::TexID, "ImTextureRef", sizeof(ImTextureRef)); }
};
#endif
#endif
template <size_t N> struct Maker<char[N]> {
    static CType Make() {
        CType t = Simple(CKind::CharArray, "char", sizeof(char[N]));
        t.count = static_cast<int>(N);
        return t;
    }
};
template <size_t N> struct Maker<float[N]> {
    static CType Make() {
        CType t = Simple(CKind::FloatArray, "float", sizeof(float[N]));
        t.count = static_cast<int>(N);
        return t;
    }
};
template <size_t N> struct Maker<int[N]> {
    static CType Make() {
        CType t = Simple(CKind::IntArray, "int", sizeof(int[N]));
        t.count = static_cast<int>(N);
        return t;
    }
};
template <class T, size_t N> struct Maker<T[N]> {
    static CType Make() {
        CType t;
        t.kind = CKind::Array;
        t.count = static_cast<int>(N);
        t.elem = &Holder<T>::Get();
        t.name = t.elem->name;
        t.size = sizeof(T[N]);
        return t;
    }
};
template <class T, class A> struct Maker<std::vector<T, A>> {
    static CType Make() {
        CType t;
        t.kind = CKind::Vector;
        t.elem = &Holder<T>::Get();
        t.name = "std::vector<" + t.elem->name + ">";
        t.size = sizeof(std::vector<T, A>);
        t.vec_data = &VecData<std::vector<T, A>>;
        t.vec_size = &VecSize<std::vector<T, A>>;
        return t;
    }
};

}  // namespace value_detail

template <class T> inline CType CTypeOf() {
    return value_detail::Maker<typename std::remove_cv<T>::type>::Make();
}

}  // namespace imforge

#include "imforge_value_reflect.h"
#include "imforge_value_access.h"
