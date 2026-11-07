#include "type_database.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "type_database_detail.h"

namespace imforge {
namespace binding {

using namespace detail;

namespace {

bool Fail(Error* err, const std::string& msg) {
    if (err) err->message = msg;
    return false;
}

struct Fundamental {
    const char* spelling;
    CKind kind;
    size_t size;
};

const Fundamental kFundamentals[] = {
    {"bool", CKind::Bool, 1},
    {"char", CKind::I8, 1},
    {"signed char", CKind::I8, 1},
    {"int8_t", CKind::I8, 1},
    {"std::int8_t", CKind::I8, 1},
    {"ImS8", CKind::I8, 1},
    {"unsigned char", CKind::U8, 1},
    {"uint8_t", CKind::U8, 1},
    {"std::uint8_t", CKind::U8, 1},
    {"ImU8", CKind::U8, 1},
    {"short", CKind::I16, 2},
    {"short int", CKind::I16, 2},
    {"signed short", CKind::I16, 2},
    {"int16_t", CKind::I16, 2},
    {"std::int16_t", CKind::I16, 2},
    {"ImS16", CKind::I16, 2},
    {"unsigned short", CKind::U16, 2},
    {"unsigned short int", CKind::U16, 2},
    {"uint16_t", CKind::U16, 2},
    {"std::uint16_t", CKind::U16, 2},
    {"ImU16", CKind::U16, 2},
    {"int", CKind::I32, 4},
    {"signed", CKind::I32, 4},
    {"signed int", CKind::I32, 4},
    {"int32_t", CKind::I32, 4},
    {"std::int32_t", CKind::I32, 4},
    {"ImS32", CKind::I32, 4},
    {"long", sizeof(long) == 4 ? CKind::I32 : CKind::I64, sizeof(long)},
    {"long int", sizeof(long) == 4 ? CKind::I32 : CKind::I64, sizeof(long)},
    {"unsigned", CKind::U32, 4},
    {"unsigned int", CKind::U32, 4},
    {"uint32_t", CKind::U32, 4},
    {"std::uint32_t", CKind::U32, 4},
    {"ImU32", CKind::U32, 4},
    {"unsigned long", sizeof(long) == 4 ? CKind::U32 : CKind::U64, sizeof(long)},
    {"unsigned long int", sizeof(long) == 4 ? CKind::U32 : CKind::U64, sizeof(long)},
    {"long long", CKind::I64, 8},
    {"long long int", CKind::I64, 8},
    {"signed long long", CKind::I64, 8},
    {"int64_t", CKind::I64, 8},
    {"std::int64_t", CKind::I64, 8},
    {"ImS64", CKind::I64, 8},
    {"intptr_t", CKind::I64, sizeof(void*)},
    {"ptrdiff_t", CKind::I64, sizeof(void*)},
    {"std::ptrdiff_t", CKind::I64, sizeof(void*)},
    {"unsigned long long", CKind::U64, 8},
    {"unsigned long long int", CKind::U64, 8},
    {"uint64_t", CKind::U64, 8},
    {"std::uint64_t", CKind::U64, 8},
    {"ImU64", CKind::U64, 8},
    {"size_t", CKind::U64, sizeof(size_t)},
    {"std::size_t", CKind::U64, sizeof(size_t)},
    {"uintptr_t", CKind::U64, sizeof(void*)},
    {"float", CKind::Float, 4},
    {"double", CKind::Double, 8},
    {"std::string", CKind::StdString, sizeof(std::string)},
    {"string", CKind::StdString, sizeof(std::string)},
    {"ImVec2", CKind::Vec2, 8},
    {"imforge::Vec2", CKind::Vec2, 8},
    {"Vec2", CKind::Vec2, 8},
    {"ImVec4", CKind::Vec4, 16},
    {"imforge::Vec4", CKind::Vec4, 16},
    {"Vec4", CKind::Vec4, 16},
    {"ImTextureID", CKind::TexID, sizeof(void*)},
    {"ImTextureRef", CKind::TexID, 0},
};

const Fundamental* FindFundamental(const std::string& spelling) {
    for (const Fundamental& f : kFundamentals)
        if (spelling == f.spelling) return &f;
    return nullptr;
}

const char* DefaultSpelling(CKind k) {
    switch (k) {
    case CKind::Bool: return "bool";
    case CKind::I8: return "signed char";
    case CKind::U8: return "unsigned char";
    case CKind::I16: return "short";
    case CKind::U16: return "unsigned short";
    case CKind::I32: return "int";
    case CKind::U32: return "unsigned";
    case CKind::I64: return "long long";
    case CKind::U64: return "unsigned long long";
    case CKind::Float: return "float";
    case CKind::Double: return "double";
    case CKind::StdString: return "std::string";
    case CKind::CharArray: return "char";
    case CKind::Vec2: return "ImVec2";
    case CKind::Vec4: return "ImVec4";
    case CKind::FloatArray: return "float";
    case CKind::IntArray: return "int";
    case CKind::Enum: return "enum";
    case CKind::Struct: return "struct";
    case CKind::Vector: return "std::vector";
    case CKind::Array: return "array";
    case CKind::TexID: return "ImTextureID";
    case CKind::Unknown: return "unknown";
    }
    return "unknown";
}

// ignore '[' inside template args
size_t FirstArrayBracket(const std::string& s) {
    int depth = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '<') ++depth;
        else if (s[i] == '>') --depth;
        else if (s[i] == '[' && depth == 0) return i;
    }
    return std::string::npos;
}

bool ParseDims(const std::string& s, std::vector<int>& dims) {
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] != '[') return false;
        ++i;
        size_t start = i;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
        if (i == start || i >= s.size() || s[i] != ']') return false;
        dims.push_back(std::atoi(s.substr(start, i - start).c_str()));
        ++i;
    }
    return !dims.empty();
}

std::string FirstTemplateArg(const std::string& inner) {
    int depth = 0;
    for (size_t i = 0; i < inner.size(); ++i) {
        char c = inner[i];
        if (c == '<' || c == '(' || c == '[') ++depth;
        else if (c == '>' || c == ')' || c == ']') --depth;
        else if (c == ',' && depth == 0) return inner.substr(0, i);
    }
    return inner;
}

void ReplaceAll(std::string& s, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
}

bool ParseEnumValue(const Json& v, std::pair<std::string, long long>& out) {
    if (v.IsArray() && v.Size() == 2 && v[0].IsString() && v[1].IsNumber()) {
        out = {v[0].AsString(), v[1].AsInt64()};
        return true;
    }
    if (v.IsObject() && v["name"].IsString() && v["value"].IsNumber()) {
        out = {v["name"].AsString(), v["value"].AsInt64()};
        return true;
    }
    return false;
}

}  // namespace

std::string NormalizeTypeName(const std::string& cpp_type) {
    std::string out;
    bool pending_space = false;
    for (char c : cpp_type) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (!out.empty() && IsIdentChar(out.back())) pending_space = true;
            continue;
        }
        if (pending_space && IsIdentChar(c)) out.push_back(' ');
        pending_space = false;
        out.push_back(c);
    }
    ReplaceAll(out, "std::__cxx11::", "std::");
    ReplaceAll(out, "std::__1::", "std::");
    static const char* const kPrefixes[] = {"const ", "volatile ", "struct ", "class ",
                                            "enum class ", "enum struct ", "enum ",
                                            "typename ", "::"};
    for (bool again = true; again;) {
        again = false;
        for (const char* p : kPrefixes) {
            if (StartsWith(out, p)) {
                out.erase(0, std::strlen(p));
                again = true;
            }
        }
    }
    for (bool again = true; again;) {
        again = false;
        static const char* const kSuffixes[] = {"&&", "&", " const", " volatile"};
        for (const char* s : kSuffixes) {
            if (EndsWith(out, s) && out.size() > std::strlen(s)) {
                out.erase(out.size() - std::strlen(s));
                again = true;
            }
        }
    }
    return out;
}

bool CKindFromName(const std::string& name, CKind& out) {
    for (int i = 0; i <= static_cast<int>(CKind::Unknown); ++i) {
        if (name == CKindName(static_cast<CKind>(i))) {
            out = static_cast<CKind>(i);
            return true;
        }
    }
    return false;
}

std::string SpellCType(const CType& t) {
    switch (t.kind) {
    case CKind::Struct: return t.strct ? t.strct->name : t.name;
    case CKind::Enum:
        if (t.enm) return t.enm->name ? std::string(t.enm->name) : t.enm->name_str;
        return t.name;
    case CKind::Vector: return "std::vector<" + (t.elem ? SpellCType(*t.elem) : "?") + ">";
    case CKind::Array: return (t.elem ? SpellCType(*t.elem) : "?") + Bracketed(t.count);
    case CKind::CharArray: return "char" + Bracketed(t.count);
    case CKind::FloatArray: return "float" + Bracketed(t.count);
    case CKind::IntArray: return "int" + Bracketed(t.count);
    default: return t.name.empty() ? DefaultSpelling(t.kind) : t.name;
    }
}

FieldDesc DescribeCType(const CType& t) {
    FieldDesc d;
    d.type = SpellCType(t);
    d.ctype = CKindName(t.kind);
    if ((t.kind == CKind::Vector || t.kind == CKind::Array) && t.elem) d.elem = SpellCType(*t.elem);
    d.count = t.count;
    return d;
}

bool TypeDatabase::Load(const Json& j, Error* err) {
    if (!j.IsObject()) return Fail(err, "types.json: expected an object");
    if (j.Has("kind") && j["kind"].AsString() != "types")
        return Fail(err, "types.json: kind is '" + j["kind"].AsString() + "', expected 'types'");
    std::vector<StructDesc> structs;
    std::vector<EnumDesc> enums;
    const Json& sj = j["structs"];
    if (!sj.IsNull() && !sj.IsObject()) return Fail(err, "types.json: 'structs' must be an object");
    for (const auto& kv : sj.obj) {
        const Json& v = kv.second;
        if (!v.IsObject()) return Fail(err, "types.json: struct '" + kv.first + "' must be an object");
        StructDesc d;
        d.name = NormalizeTypeName(kv.first);
        if (d.name.empty()) return Fail(err, "types.json: empty struct name");
        d.header = v["header"].AsString();
        d.size = v["size"].AsInt64();
        const Json& fields = v["fields"];
        if (!fields.IsNull() && !fields.IsArray())
            return Fail(err, "types.json: struct '" + kv.first + "': 'fields' must be an array");
        for (const Json& fj : fields.arr) {
            if (!fj.IsObject() || !fj["name"].IsString() || fj["name"].AsString().empty())
                return Fail(err, "types.json: struct '" + kv.first + "': every field needs a name");
            FieldDesc f;
            f.name = fj["name"].AsString();
            f.type = fj["type"].AsString();
            f.ctype = fj["ctype"].AsString();
            f.elem = fj["elem"].AsString();
            f.count = fj["count"].AsInt();
            f.offset = fj.Has("offset") ? fj["offset"].AsInt64() : -1;
            CKind k;
            if (!f.ctype.empty() && !CKindFromName(f.ctype, k))
                return Fail(err, "types.json: struct '" + kv.first + "' field '" + f.name +
                                     "': unknown ctype '" + f.ctype + "'");
            d.fields.push_back(std::move(f));
        }
        structs.push_back(std::move(d));
    }
    const Json& ej = j["enums"];
    if (!ej.IsNull() && !ej.IsObject()) return Fail(err, "types.json: 'enums' must be an object");
    for (const auto& kv : ej.obj) {
        const Json& v = kv.second;
        if (!v.IsObject()) return Fail(err, "types.json: enum '" + kv.first + "' must be an object");
        EnumDesc d;
        d.name = NormalizeTypeName(kv.first);
        if (d.name.empty()) return Fail(err, "types.json: empty enum name");
        d.header = v["header"].AsString();
        d.underlying = v["underlying"].AsString();
        d.scoped = v["scoped"].AsBool(false);
        const Json& values = v["values"];
        if (values.IsArray()) {
            for (const Json& ev : values.arr) {
                std::pair<std::string, long long> p;
                if (!ParseEnumValue(ev, p))
                    return Fail(err, "types.json: enum '" + kv.first + "': bad value entry");
                d.values.push_back(std::move(p));
            }
        } else if (values.IsObject()) {
            for (const auto& ev : values.obj) {
                if (!ev.second.IsNumber())
                    return Fail(err, "types.json: enum '" + kv.first + "': bad value entry");
                d.values.emplace_back(ev.first, ev.second.AsInt64());
            }
        } else if (!values.IsNull()) {
            return Fail(err, "types.json: enum '" + kv.first + "': 'values' must be an array");
        }
        enums.push_back(std::move(d));
    }
    for (const StructDesc& d : structs) struct_src_[d.name] = d;
    for (const EnumDesc& d : enums) enum_src_[d.name] = d;
    Rebuild();
    return true;
}

Json TypeDatabase::Save() const {
    Json root = Json::Object();
    root.Set("schemaVersion", Json(1));
    root.Set("kind", Json("types"));
    Json structs = Json::Object();
    for (const auto& kv : struct_src_) {
        const StructDesc& d = kv.second;
        Json s = Json::Object();
        Json fields = Json::Array();
        for (const FieldDesc& f : d.fields) {
            Json fj = Json::Object();
            fj.Set("name", Json(f.name));
            fj.Set("type", Json(f.type));
            fj.Set("ctype", Json(f.ctype));
            if (!f.elem.empty()) fj.Set("elem", Json(f.elem));
            if (f.count > 0) fj.Set("count", Json(f.count));
            if (f.offset >= 0) fj.Set("offset", Json(f.offset));
            fields.Push(std::move(fj));
        }
        s.Set("fields", std::move(fields));
        if (!d.header.empty()) s.Set("header", Json(d.header));
        if (d.size > 0) s.Set("size", Json(d.size));
        structs.Set(kv.first, std::move(s));
    }
    root.Set("structs", std::move(structs));
    Json enums = Json::Object();
    for (const auto& kv : enum_src_) {
        const EnumDesc& d = kv.second;
        Json e = Json::Object();
        Json values = Json::Array();
        for (const auto& v : d.values) {
            Json pair = Json::Array();
            pair.Push(Json(v.first));
            pair.Push(Json(v.second));
            values.Push(std::move(pair));
        }
        e.Set("values", std::move(values));
        if (!d.header.empty()) e.Set("header", Json(d.header));
        if (!d.underlying.empty()) e.Set("underlying", Json(d.underlying));
        if (d.scoped) e.Set("scoped", Json(true));
        enums.Set(kv.first, std::move(e));
    }
    root.Set("enums", std::move(enums));
    return root;
}

void TypeDatabase::Clear() {
    struct_src_.clear();
    enum_src_.clear();
    structs_.clear();
    enums_.clear();
    interned_.clear();
}

void TypeDatabase::AddStruct(const StructDesc& d) {
    StructDesc copy = d;
    copy.name = NormalizeTypeName(d.name);
    struct_src_[copy.name] = std::move(copy);
    Rebuild();
}

void TypeDatabase::AddEnum(const EnumDesc& d) {
    EnumDesc copy = d;
    copy.name = NormalizeTypeName(d.name);
    enum_src_[copy.name] = std::move(copy);
    Rebuild();
}

bool TypeDatabase::FromRegistry(const TypeRegistry& reg) {
    for (const auto& e : reg.enums) {
        if (!e) continue;
        EnumDesc d;
        d.name = NormalizeTypeName(e->name ? std::string(e->name) : e->name_str);
        if (d.name.empty()) continue;
        d.values = e->values;
        enum_src_[d.name] = std::move(d);
    }
    for (const auto& s : reg.structs) {
        if (!s) continue;
        StructDesc d;
        d.name = NormalizeTypeName(s->name);
        if (d.name.empty()) continue;
        d.size = static_cast<long long>(s->size);
        for (const FieldInfo& f : s->fields) {
            FieldDesc fd = DescribeCType(f.type);
            fd.name = f.name;
            fd.offset = static_cast<long long>(f.offset);
            d.fields.push_back(std::move(fd));
        }
        struct_src_[d.name] = std::move(d);
    }
    Rebuild();
    return !reg.structs.empty() || !reg.enums.empty();
}

const StructInfo* TypeDatabase::Struct(const std::string& name) const {
    auto it = structs_.find(NormalizeTypeName(name));
    return it == structs_.end() ? nullptr : &it->second;
}

const EnumInfo* TypeDatabase::Enum(const std::string& name) const {
    auto it = enums_.find(NormalizeTypeName(name));
    return it == enums_.end() ? nullptr : &it->second;
}

const StructDesc* TypeDatabase::StructSource(const std::string& name) const {
    auto it = struct_src_.find(NormalizeTypeName(name));
    return it == struct_src_.end() ? nullptr : &it->second;
}

const EnumDesc* TypeDatabase::EnumSource(const std::string& name) const {
    auto it = enum_src_.find(NormalizeTypeName(name));
    return it == enum_src_.end() ? nullptr : &it->second;
}

std::vector<std::string> TypeDatabase::StructNames() const {
    std::vector<std::string> out;
    for (const auto& kv : struct_src_) out.push_back(kv.first);
    return out;
}

std::vector<std::string> TypeDatabase::EnumNames() const {
    std::vector<std::string> out;
    for (const auto& kv : enum_src_) out.push_back(kv.first);
    return out;
}

void TypeDatabase::Rebuild() {
    for (const auto& kv : enum_src_) {
        EnumInfo& e = enums_[kv.first];
        e.name_str = kv.first;
        e.name = e.name_str.c_str();
        e.values = kv.second.values;
    }
    for (const auto& kv : struct_src_) {
        StructInfo& s = structs_[kv.first];
        s.name = kv.first;
        s.size = kv.second.size > 0 ? static_cast<size_t>(kv.second.size) : 0;
        s.fields.clear();
    }
    for (auto& kv : interned_) kv.second = Compute(kv.first);
    for (const auto& kv : struct_src_) {
        StructInfo& s = structs_[kv.first];
        for (const FieldDesc& f : kv.second.fields) {
            FieldInfo fi;
            fi.name = f.name;
            fi.type = FieldType(f);
            fi.offset = f.offset > 0 ? static_cast<size_t>(f.offset) : 0;
            s.fields.push_back(std::move(fi));
        }
    }
}

const CType* TypeDatabase::Intern(const std::string& spelling) const {
    auto it = interned_.find(spelling);
    if (it != interned_.end()) return &it->second;
    CType t = Compute(spelling);
    return &interned_.emplace(spelling, std::move(t)).first->second;
}

CType TypeDatabase::Compute(const std::string& spelling) const {
    if (spelling.empty()) return Simple(CKind::Unknown, "unknown", 0);
    if (const Fundamental* f = FindFundamental(spelling)) return Simple(f->kind, spelling, f->size);
    if (spelling.back() == ']') {
        size_t br = FirstArrayBracket(spelling);
        std::vector<int> dims;
        if (br == std::string::npos || br == 0 || !ParseDims(spelling.substr(br), dims))
            return Simple(CKind::Unknown, spelling, 0);
        std::string base = spelling.substr(0, br);
        int n = dims[0];
        if (dims.size() == 1) {
            if (base == "char") {
                CType t = Simple(CKind::CharArray, "char", static_cast<size_t>(n));
                t.count = n;
                return t;
            }
            if (base == "float") {
                CType t = Simple(CKind::FloatArray, "float", 4 * static_cast<size_t>(n));
                t.count = n;
                return t;
            }
            if (base == "int") {
                CType t = Simple(CKind::IntArray, "int", 4 * static_cast<size_t>(n));
                t.count = n;
                return t;
            }
        } else {
            for (size_t i = 1; i < dims.size(); ++i) base += Bracketed(dims[i]);
        }
        CType t;
        t.kind = CKind::Array;
        t.count = n;
        t.elem = Intern(base);
        t.name = t.elem->name;
        t.size = t.elem->size * static_cast<size_t>(n);
        return t;
    }
    if ((StartsWith(spelling, "std::vector<") || StartsWith(spelling, "vector<")) &&
        spelling.back() == '>') {
        size_t open = spelling.find('<');
        std::string inner = spelling.substr(open + 1, spelling.size() - open - 2);
        CType t;
        t.kind = CKind::Vector;
        t.elem = Intern(NormalizeTypeName(FirstTemplateArg(inner)));
        t.name = "std::vector<" + SpellCType(*t.elem) + ">";
        t.size = sizeof(std::vector<int>);
        return t;
    }
    auto sit = structs_.find(spelling);
    if (sit != structs_.end()) {
        CType t;
        t.kind = CKind::Struct;
        t.strct = &sit->second;
        t.name = spelling;
        t.size = sit->second.size;
        return t;
    }
    auto eit = enums_.find(spelling);
    if (eit != enums_.end()) {
        CType t;
        t.kind = CKind::Enum;
        t.enm = &eit->second;
        t.name = spelling;
        const EnumDesc& src = enum_src_.find(spelling)->second;
        std::string under = src.underlying.empty() ? "int" : NormalizeTypeName(src.underlying);
        if (!FindFundamental(under)) under = "int";
        t.elem = Intern(under);
        t.size = t.elem->size;
        return t;
    }
    return Simple(CKind::Unknown, spelling, 0);
}

CType TypeDatabase::FieldType(const FieldDesc& f) const {
    std::string spelling = NormalizeTypeName(f.type);
    CKind hinted = CKind::Unknown;
    bool has_hint = !f.ctype.empty() && CKindFromName(f.ctype, hinted);
    std::string elem = NormalizeTypeName(f.elem);
    if (spelling.empty() && has_hint) {
        switch (hinted) {
        case CKind::Vector: spelling = "std::vector<" + elem + ">"; break;
        case CKind::Array: spelling = elem + Bracketed(f.count); break;
        case CKind::CharArray: spelling = "char" + Bracketed(f.count); break;
        case CKind::FloatArray: spelling = "float" + Bracketed(f.count); break;
        case CKind::IntArray: spelling = "int" + Bracketed(f.count); break;
        default: spelling = DefaultSpelling(hinted); break;
        }
    }
    CType t = Compute(spelling);
    if (has_hint && t.kind != hinted) {
        t.kind = hinted;
        switch (hinted) {
        case CKind::Vector:
        case CKind::Array:
            if (!t.elem && !elem.empty()) t.elem = Intern(elem);
            if (hinted == CKind::Vector && t.elem) t.name = "std::vector<" + SpellCType(*t.elem) + ">";
            break;
        case CKind::Struct: t.strct = Struct(spelling); break;
        case CKind::Enum:
            t.enm = Enum(spelling);
            if (!t.elem) t.elem = Intern("int");
            break;
        default: break;
        }
    }
    if (f.count > 0 && t.count == 0) t.count = f.count;
    return t;
}

bool TypeDatabase::ResolveTypeName(const std::string& cpp_type, CType& out) const {
    out = Compute(NormalizeTypeName(cpp_type));
    return out.kind != CKind::Unknown;
}

}  // namespace binding
}  // namespace imforge
