#pragma once

#include "imforge_value.h"

namespace imforge {

namespace value_detail {

inline CType IntTypeOfSize(size_t size) {
    switch (size) {
    case 1: return Simple(CKind::I8, "signed char", 1);
    case 2: return Simple(CKind::I16, "short", 2);
    case 8: return Simple(CKind::I64, "long long", 8);
    default: return Simple(CKind::I32, "int", 4);
    }
}

inline CType EnumStorage(const CType& t) {
    if (t.elem) return *t.elem;
    return IntTypeOfSize(t.size);
}

inline size_t ElemStride(const CType& t) {
    switch (t.kind) {
    case CKind::FloatArray: return sizeof(float);
    case CKind::IntArray: return sizeof(int);
    case CKind::CharArray: return 1;
    default: return t.elem ? t.elem->size : 0;
    }
}

inline bool ElemType(const CType& t, CType& out) {
    switch (t.kind) {
    case CKind::FloatArray: out = Simple(CKind::Float, "float", sizeof(float)); return true;
    case CKind::IntArray: out = Simple(CKind::I32, "int", sizeof(int)); return true;
    case CKind::CharArray: out = Simple(CKind::I8, "char", 1); return true;
    case CKind::Vec2:
    case CKind::Vec4: out = Simple(CKind::Float, "float", sizeof(float)); return true;
    case CKind::Array:
    case CKind::Vector:
        if (!t.elem) return false;
        out = *t.elem;
        return true;
    default: return false;
    }
}

inline bool ElemCount(const void* ptr, const CType& t, size_t& out) {
    switch (t.kind) {
    case CKind::FloatArray:
    case CKind::IntArray:
    case CKind::CharArray:
    case CKind::Array: out = static_cast<size_t>(t.count < 0 ? 0 : t.count); return true;
    case CKind::Vec2: out = 2; return true;
    case CKind::Vec4: out = 4; return true;
    case CKind::Vector:
        if (!t.vec_size) return false;
        out = t.vec_size(ptr);
        return true;
    default: return false;
    }
}

inline bool ElemBase(void* ptr, const CType& t, void*& out) {
    if (t.kind == CKind::Vector) {
        if (!t.vec_data) return false;
        out = t.vec_data(ptr);
        return true;
    }
    out = ptr;
    return true;
}

inline long long& SizeSlot() {
    static thread_local long long slot = 0;
    return slot;
}

inline bool IsIdentStart(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}
inline bool IsIdentChar(char c) { return IsIdentStart(c) || (c >= '0' && c <= '9'); }

inline bool PathFail(std::string* err, const std::string& msg) {
    if (err) *err = msg;
    return false;
}

}  // namespace value_detail

// path is relative to root, e.g. "health", "items[2].name", "items.size". "" is the root itself.
// ".size" points at a thread-local I64 copy of the count, don't write through it
inline bool ResolvePath(void* root, const StructInfo* root_type, const char* path, void** out_ptr,
                        CType* out_type, std::string* err) {
    using namespace value_detail;
    if (!root || !root_type) return PathFail(err, "null root");
    if (!path) path = "";
    char* cur = static_cast<char*>(root);
    CType type;
    type.kind = CKind::Struct;
    type.strct = root_type;
    type.name = root_type->name;
    type.size = root_type->size;
    const char* p = path;
    bool first = true;
    while (*p) {
        if (*p == '.') {
            if (first) return PathFail(err, "path must not start with '.'");
            ++p;
            if (!IsIdentStart(*p)) return PathFail(err, "expected field name after '.'");
        } else if (!first && *p != '[') {
            return PathFail(err, std::string("expected '.' or '[' before '") + *p + "'");
        }
        if (*p == '[') {
            ++p;
            if (*p < '0' || *p > '9') return PathFail(err, "expected index after '['");
            size_t idx = 0;
            while (*p >= '0' && *p <= '9') {
                idx = idx * 10 + static_cast<size_t>(*p - '0');
                ++p;
            }
            if (*p != ']') return PathFail(err, "expected ']'");
            ++p;
            size_t n;
            if (!ElemCount(cur, type, n)) return PathFail(err, "cannot index type '" + type.name + "'");
            if (idx >= n) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "index %zu out of range (size %zu)", idx, n);
                return PathFail(err, buf);
            }
            CType et;
            if (!ElemType(type, et)) return PathFail(err, "unknown element type of '" + type.name + "'");
            size_t stride = type.kind == CKind::Vec2 || type.kind == CKind::Vec4 ? sizeof(float) : ElemStride(type);
            if (stride == 0) return PathFail(err, "unknown element size of '" + type.name + "'");
            void* base;
            if (!ElemBase(cur, type, base)) return PathFail(err, "vector type '" + type.name + "' has no accessors");
            cur = static_cast<char*>(base) + idx * stride;
            type = et;
            first = false;
            continue;
        }
        if (!IsIdentStart(*p)) {
            if (first) return PathFail(err, "expected field name");
            return PathFail(err, std::string("unexpected character '") + *p + "' in path");
        }
        const char* start = p;
        while (IsIdentChar(*p)) ++p;
        std::string name(start, static_cast<size_t>(p - start));
        first = false;
        if (type.kind == CKind::Struct) {
            if (!type.strct) return PathFail(err, "struct '" + type.name + "' has no type info");
            const FieldInfo* f = type.strct->Find(name.c_str());
            if (!f) return PathFail(err, "no field '" + name + "' in '" + type.strct->name + "'");
            cur += f->offset;
            type = f->type;
            continue;
        }
        if (type.kind == CKind::Vec2 || type.kind == CKind::Vec4) {
            int comp = name == "x" ? 0 : name == "y" ? 1 : name == "z" ? 2 : name == "w" ? 3 : -1;
            int limit = type.kind == CKind::Vec2 ? 2 : 4;
            if (comp < 0 || comp >= limit)
                return PathFail(err, "no component '" + name + "' in '" + type.name + "'");
            cur += static_cast<size_t>(comp) * sizeof(float);
            type = Simple(CKind::Float, "float", sizeof(float));
            continue;
        }
        if (name == "size") {
            size_t n;
            if (ElemCount(cur, type, n) && type.kind != CKind::Vec2 && type.kind != CKind::Vec4) {
                long long& slot = SizeSlot();
                slot = static_cast<long long>(n);
                cur = reinterpret_cast<char*>(&slot);
                type = Simple(CKind::I64, "long long", sizeof(long long));
                continue;
            }
        }
        return PathFail(err, "cannot access field '" + name + "' of type '" + type.name + "'");
    }
    if (out_ptr) *out_ptr = cur;
    if (out_type) *out_type = type;
    return true;
}

inline bool ReadValue(const void* ptr, const CType& t, Value& out) {
    using namespace value_detail;
    if (!ptr) return false;
    switch (t.kind) {
    case CKind::Bool: out = Value(*static_cast<const bool*>(ptr)); return true;
    case CKind::I8: out = Value(static_cast<long long>(*static_cast<const int8_t*>(ptr))); return true;
    case CKind::U8: out = Value(static_cast<long long>(*static_cast<const uint8_t*>(ptr))); return true;
    case CKind::I16: {
        int16_t v;
        std::memcpy(&v, ptr, sizeof(v));
        out = Value(static_cast<long long>(v));
        return true;
    }
    case CKind::U16: {
        uint16_t v;
        std::memcpy(&v, ptr, sizeof(v));
        out = Value(static_cast<long long>(v));
        return true;
    }
    case CKind::I32: {
        int32_t v;
        std::memcpy(&v, ptr, sizeof(v));
        out = Value(static_cast<long long>(v));
        return true;
    }
    case CKind::U32: {
        uint32_t v;
        std::memcpy(&v, ptr, sizeof(v));
        out = Value(static_cast<long long>(v));
        return true;
    }
    case CKind::I64: {
        int64_t v;
        std::memcpy(&v, ptr, sizeof(v));
        out = Value(static_cast<long long>(v));
        return true;
    }
    case CKind::U64: {
        uint64_t v;
        std::memcpy(&v, ptr, sizeof(v));
        out = Value(static_cast<long long>(v));
        return true;
    }
    case CKind::Float: {
        float v;
        std::memcpy(&v, ptr, sizeof(v));
        out = Value(static_cast<double>(v));
        return true;
    }
    case CKind::Double: {
        double v;
        std::memcpy(&v, ptr, sizeof(v));
        out = Value(v);
        return true;
    }
    case CKind::StdString: out = Value(*static_cast<const std::string*>(ptr)); return true;
    case CKind::CharArray: {
        const char* s = static_cast<const char*>(ptr);
        size_t n = t.count > 0 ? static_cast<size_t>(t.count) : 0;
        size_t len = 0;
        while (len < n && s[len]) ++len;
        out = Value(std::string(s, len));
        return true;
    }
    case CKind::Vec2: {
        Vec2 v;
        std::memcpy(&v, ptr, sizeof(float) * 2);
        out = Value(v);
        return true;
    }
    case CKind::Vec4: {
        Vec4 v;
        std::memcpy(&v, ptr, sizeof(float) * 4);
        out = Value(v);
        return true;
    }
    case CKind::Enum: {
        Value tmp;
        if (!ReadValue(ptr, EnumStorage(t), tmp)) return false;
        out = Value(tmp.ToInt());
        return true;
    }
    case CKind::TexID: {
#if defined(IMGUI_VERSION) && IMGUI_VERSION_NUM >= 19200
        if (t.size == sizeof(ImTextureRef) && t.name == "ImTextureRef") {
            out = Value(TexToInt(static_cast<const ImTextureRef*>(ptr)->GetTexID()));
            return true;
        }
#endif
        if (t.size == 4) {
            uint32_t v;
            std::memcpy(&v, ptr, sizeof(v));
            out = Value(static_cast<long long>(v));
            return true;
        }
        if (t.size == 8 || (t.size == 0 && sizeof(void*) == 8)) {
            uint64_t v;
            std::memcpy(&v, ptr, sizeof(v));
            out = Value(static_cast<long long>(v));
            return true;
        }
        return false;
    }
    case CKind::FloatArray:
    case CKind::IntArray:
    case CKind::Array:
    case CKind::Vector: {
        size_t n;
        CType et;
        if (!ElemCount(ptr, t, n) || !ElemType(t, et)) return false;
        size_t stride = ElemStride(t);
        if (stride == 0 && n > 0) return false;
        void* base;
        if (!ElemBase(const_cast<void*>(ptr), t, base)) return false;
        Value list = Value::List();
        list.list.reserve(n);
        for (size_t k = 0; k < n; ++k) {
            Value e;
            if (!ReadValue(static_cast<const char*>(base) + k * stride, et, e)) return false;
            list.list.push_back(std::move(e));
        }
        out = std::move(list);
        return true;
    }
    case CKind::Struct:
    case CKind::Unknown: return false;
    }
    return false;
}

inline bool WriteValue(void* ptr, const CType& t, const Value& v) {
    using namespace value_detail;
    if (!ptr) return false;
    switch (t.kind) {
    case CKind::Bool: *static_cast<bool*>(ptr) = v.ToBool(); return true;
    case CKind::I8: *static_cast<int8_t*>(ptr) = static_cast<int8_t>(v.ToInt()); return true;
    case CKind::U8: *static_cast<uint8_t*>(ptr) = static_cast<uint8_t>(v.ToInt()); return true;
    case CKind::I16: {
        int16_t x = static_cast<int16_t>(v.ToInt());
        std::memcpy(ptr, &x, sizeof(x));
        return true;
    }
    case CKind::U16: {
        uint16_t x = static_cast<uint16_t>(v.ToInt());
        std::memcpy(ptr, &x, sizeof(x));
        return true;
    }
    case CKind::I32: {
        int32_t x = static_cast<int32_t>(v.ToInt());
        std::memcpy(ptr, &x, sizeof(x));
        return true;
    }
    case CKind::U32: {
        uint32_t x = static_cast<uint32_t>(v.ToInt());
        std::memcpy(ptr, &x, sizeof(x));
        return true;
    }
    case CKind::I64: {
        int64_t x = static_cast<int64_t>(v.ToInt());
        std::memcpy(ptr, &x, sizeof(x));
        return true;
    }
    case CKind::U64: {
        uint64_t x = static_cast<uint64_t>(v.ToInt());
        std::memcpy(ptr, &x, sizeof(x));
        return true;
    }
    case CKind::Float: {
        float x = static_cast<float>(v.ToDouble());
        std::memcpy(ptr, &x, sizeof(x));
        return true;
    }
    case CKind::Double: {
        double x = v.ToDouble();
        std::memcpy(ptr, &x, sizeof(x));
        return true;
    }
    case CKind::StdString: *static_cast<std::string*>(ptr) = v.ToString(); return true;
    case CKind::CharArray: {
        if (t.count <= 0) return false;
        std::string s = v.ToString();
        size_t cap = static_cast<size_t>(t.count);
        size_t n = s.size() < cap - 1 ? s.size() : cap - 1;
        char* dst = static_cast<char*>(ptr);
        std::memcpy(dst, s.data(), n);
        dst[n] = 0;
        return true;
    }
    case CKind::Vec2: {
        Vec2 x = v.ToVec2();
        std::memcpy(ptr, &x, sizeof(float) * 2);
        return true;
    }
    case CKind::Vec4: {
        Vec4 x = v.ToVec4();
        std::memcpy(ptr, &x, sizeof(float) * 4);
        return true;
    }
    case CKind::Enum: return WriteValue(ptr, EnumStorage(t), Value(v.ToInt()));
    case CKind::TexID:
    case CKind::Struct:
    case CKind::Unknown: return false;
    case CKind::FloatArray:
    case CKind::IntArray:
    case CKind::Array:
    case CKind::Vector: {
        size_t n;
        CType et;
        if (!ElemCount(ptr, t, n) || !ElemType(t, et)) return false;
        size_t stride = ElemStride(t);
        if (stride == 0 && n > 0) return false;
        void* base;
        if (!ElemBase(ptr, t, base)) return false;
        std::vector<Value> src;
        if (v.kind == Value::Kind::List) src = v.list;
        else if (v.kind == Value::Kind::Vec2) src = {Value(static_cast<double>(v.v2.x)), Value(static_cast<double>(v.v2.y))};
        else if (v.kind == Value::Kind::Vec4 || v.kind == Value::Kind::Color)
            src = {Value(static_cast<double>(v.v4.x)), Value(static_cast<double>(v.v4.y)),
                   Value(static_cast<double>(v.v4.z)), Value(static_cast<double>(v.v4.w))};
        else src = {v};
        size_t m = src.size() < n ? src.size() : n;
        for (size_t k = 0; k < m; ++k)
            if (!WriteValue(static_cast<char*>(base) + k * stride, et, src[k])) return false;
        return true;
    }
    }
    return false;
}

}  // namespace imforge
