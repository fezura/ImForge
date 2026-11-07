#include "types_db.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace imforge {
namespace codegen {

namespace {

std::string Trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t')) --e;
    return s.substr(b, e - b);
}

bool ParseKind(const std::string& name, CKind& out) {
    for (int i = 0; i <= static_cast<int>(CKind::Unknown); ++i) {
        if (name == CKindName(static_cast<CKind>(i))) {
            out = static_cast<CKind>(i);
            return true;
        }
    }
    return false;
}

CType Simple(CKind k, const char* name, size_t size) {
    CType t;
    t.kind = k;
    t.name = name;
    t.size = size;
    return t;
}

}  // namespace

CType TypesDb::ScalarType(const std::string& t) {
    if (t == "bool") return Simple(CKind::Bool, "bool", sizeof(bool));
    if (t == "int") return Simple(CKind::I32, "int", sizeof(int));
    if (t == "float") return Simple(CKind::Float, "float", sizeof(float));
    if (t == "double") return Simple(CKind::Double, "double", sizeof(double));
    if (t == "string") return Simple(CKind::StdString, "std::string", sizeof(std::string));
    if (t == "vec2") return Simple(CKind::Vec2, "ImVec2", sizeof(float) * 2);
    if (t == "vec4" || t == "color") return Simple(CKind::Vec4, "ImVec4", sizeof(float) * 4);
    if (t == "texture") return Simple(CKind::TexID, "ImTextureID", sizeof(void*));
    CType u;
    u.name = t;
    return u;
}

CType TypesDb::FromSpelling(const std::string& raw) {
    std::string s = Trim(raw);
    size_t br = s.find('[');
    if (br != std::string::npos && s.back() == ']') {
        std::string base = Trim(s.substr(0, br));
        int count = std::atoi(s.c_str() + br + 1);
        CType t;
        t.count = count;
        t.name = s;
        if (base == "char") t.kind = CKind::CharArray;
        else if (base == "float") t.kind = CKind::FloatArray;
        else if (base == "int") t.kind = CKind::IntArray;
        else t.kind = CKind::Array;
        return t;
    }
    if (s == "bool") return Simple(CKind::Bool, "bool", 1);
    if (s == "char" || s == "int8_t" || s == "signed char") return Simple(CKind::I8, s.c_str(), 1);
    if (s == "unsigned char" || s == "uint8_t") return Simple(CKind::U8, s.c_str(), 1);
    if (s == "short" || s == "int16_t") return Simple(CKind::I16, s.c_str(), 2);
    if (s == "unsigned short" || s == "uint16_t") return Simple(CKind::U16, s.c_str(), 2);
    if (s == "int" || s == "int32_t" || s == "long") return Simple(CKind::I32, s.c_str(), 4);
    if (s == "unsigned" || s == "unsigned int" || s == "uint32_t" || s == "unsigned long") return Simple(CKind::U32, s.c_str(), 4);
    if (s == "long long" || s == "int64_t") return Simple(CKind::I64, s.c_str(), 8);
    if (s == "unsigned long long" || s == "uint64_t" || s == "size_t") return Simple(CKind::U64, s.c_str(), 8);
    if (s == "float") return Simple(CKind::Float, "float", 4);
    if (s == "double") return Simple(CKind::Double, "double", 8);
    if (s == "std::string" || s == "string") return Simple(CKind::StdString, "std::string", sizeof(std::string));
    if (s == "ImVec2") return Simple(CKind::Vec2, "ImVec2", 8);
    if (s == "ImVec4") return Simple(CKind::Vec4, "ImVec4", 16);
    if (s == "ImTextureID" || s == "ImTextureRef") return Simple(CKind::TexID, s.c_str(), sizeof(void*));
    CType u;
    u.name = s;
    return u;
}

CType TypesDb::ParseType(const std::string& spelling, const std::string& ctype_name, const Json& field) {
    CType t = FromSpelling(spelling);
    CKind k;
    if (!ctype_name.empty() && ParseKind(ctype_name, k)) t.kind = k;
    if (field["count"].IsNumber()) t.count = field["count"].AsInt();
    if (t.kind == CKind::Vector || t.kind == CKind::Array) {
        std::string elem = field["elem"].AsString();
        if (elem.empty() && t.kind == CKind::Vector) {
            size_t lt = spelling.find('<'), gt = spelling.rfind('>');
            if (lt != std::string::npos && gt != std::string::npos && gt > lt) elem = Trim(spelling.substr(lt + 1, gt - lt - 1));
        }
        if (elem.empty() && t.kind == CKind::Array) {
            size_t br = spelling.find('[');
            if (br != std::string::npos) elem = Trim(spelling.substr(0, br));
        }
        if (!elem.empty()) {
            elems.push_back(FromSpelling(elem));
            t.elem = &elems.back();
        }
    }
    return t;
}

bool TypesDb::Load(const Json& j, Error* err) {
    if (!j.IsObject()) {
        if (err) err->message = "types.json: expected an object";
        return false;
    }
    const Json& enums = j["enums"];
    for (const auto& kv : enums.obj) {
        EnumInfo e;
        e.name_str = kv.first;
        const Json& vals = kv.second["values"];
        for (size_t i = 0; i < vals.Size(); ++i) {
            const Json& v = vals[i];
            if (v.IsArray() && v.Size() >= 2) e.values.emplace_back(v[0].AsString(), v[1].AsInt64());
            else if (v.IsString()) e.values.emplace_back(v.AsString(), static_cast<long long>(i));
        }
        registry.RegisterEnum(std::move(e));
    }
    const Json& structs = j["structs"];
    for (const auto& kv : structs.obj) {
        StructInfo s;
        s.name = kv.first;
        registry.RegisterStruct(std::move(s));
        headers.emplace_back(kv.first, kv.second["header"].AsString());
    }
    for (const auto& kv : structs.obj) {
        StructInfo info;
        info.name = kv.first;
        const Json& fields = kv.second["fields"];
        for (size_t i = 0; i < fields.Size(); ++i) {
            const Json& f = fields[i];
            FieldInfo fi;
            fi.name = f["name"].AsString();
            fi.type = ParseType(f["type"].AsString(), f["ctype"].AsString(), f);
            fi.offset = static_cast<size_t>(f["offset"].AsInt(0));
            info.fields.push_back(std::move(fi));
        }
        registry.RegisterStruct(std::move(info));
    }
    auto link = [&](CType& t) {
        if (t.kind == CKind::Struct || (t.kind == CKind::Unknown && !t.name.empty())) {
            if (const StructInfo* si = registry.FindStruct(t.name.c_str())) {
                t.kind = CKind::Struct;
                t.strct = si;
            }
        }
        if (t.kind == CKind::Enum || (t.kind == CKind::Unknown && !t.name.empty())) {
            if (const EnumInfo* ei = registry.FindEnum(t.name.c_str())) {
                t.kind = CKind::Enum;
                t.enm = ei;
            }
        }
    };
    for (auto& sp : registry.structs)
        for (FieldInfo& f : sp->fields) link(f.type);
    for (CType& e : elems) link(e);
    loaded = true;
    return true;
}

bool TypesDb::LoadFile(const std::string& path, Error* err) {
    std::FILE* f = nullptr;
#ifdef _MSC_VER
    if (fopen_s(&f, path.c_str(), "rb") != 0) f = nullptr;
#else
    f = std::fopen(path.c_str(), "rb");
#endif
    if (!f) {
        if (err) err->message = "cannot open " + path;
        return false;
    }
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
    std::fclose(f);
    Json j;
    JsonError je;
    if (!ParseJson(text, j, &je)) {
        if (err) {
            err->message = path + ": " + je.message;
            err->line = je.line;
            err->col = je.col;
        }
        return false;
    }
    return Load(j, err);
}

const StructInfo* TypesDb::FindStruct(const std::string& name) const { return registry.FindStruct(name.c_str()); }
const EnumInfo* TypesDb::FindEnum(const std::string& name) const { return registry.FindEnum(name.c_str()); }

std::string TypesDb::HeaderOf(const std::string& struct_name) const {
    for (const auto& h : headers)
        if (h.first == struct_name) return h.second;
    return std::string();
}

bool TypesDb::Resolve(const CType& root, const std::string& rest, CType& out, bool* is_size, CType* container) const {
    if (is_size) *is_size = false;
    CType cur = root;
    size_t i = 0;
    while (i < rest.size()) {
        if (rest[i] == '.') {
            size_t e = i + 1;
            while (e < rest.size() && rest[e] != '.' && rest[e] != '[') ++e;
            std::string field = rest.substr(i + 1, e - i - 1);
            i = e;
            if (field == "size" && i >= rest.size() &&
                (cur.kind == CKind::Vector || cur.kind == CKind::Array || cur.kind == CKind::FloatArray ||
                 cur.kind == CKind::IntArray || cur.kind == CKind::CharArray)) {
                if (container) *container = cur;
                if (is_size) *is_size = true;
                out = ScalarType("int");
                return true;
            }
            if (cur.kind != CKind::Struct || !cur.strct) return false;
            const FieldInfo* fi = cur.strct->Find(field.c_str());
            if (!fi) return false;
            cur = fi->type;
        } else if (rest[i] == '[') {
            size_t e = rest.find(']', i);
            if (e == std::string::npos) return false;
            i = e + 1;
            if (cur.kind == CKind::FloatArray) cur = ScalarType("float");
            else if (cur.kind == CKind::IntArray) cur = ScalarType("int");
            else if (cur.kind == CKind::CharArray) cur = FromSpelling("char");
            else if ((cur.kind == CKind::Vector || cur.kind == CKind::Array) && cur.elem) cur = *cur.elem;
            else return false;
        } else {
            return false;
        }
    }
    out = cur;
    return true;
}

std::vector<std::string> TypesDb::OrderedStructs(const std::vector<std::string>& roots) const {
    std::vector<std::string> order;
    std::vector<std::string> visiting;
    struct Visitor {
        const TypesDb& db;
        std::vector<std::string>& order;
        std::vector<std::string>& visiting;
        void Visit(const std::string& name) {
            for (const std::string& s : order)
                if (s == name) return;
            for (const std::string& s : visiting)
                if (s == name) return;
            const StructInfo* si = db.FindStruct(name);
            if (!si) return;
            visiting.push_back(name);
            for (const FieldInfo& f : si->fields) {
                const CType* t = &f.type;
                if ((t->kind == CKind::Vector || t->kind == CKind::Array) && t->elem) t = t->elem;
                if (t->kind == CKind::Struct && t->strct) Visit(t->strct->name);
                else if (t->kind == CKind::Unknown && db.FindStruct(t->name)) Visit(t->name);
            }
            visiting.pop_back();
            order.push_back(name);
        }
    } v{*this, order, visiting};
    if (roots.empty()) {
        for (const auto& sp : registry.structs) v.Visit(sp->name);
    } else {
        for (const std::string& r : roots) v.Visit(r);
    }
    return order;
}

std::vector<std::string> TypesDb::EnumsUsedBy(const std::vector<std::string>& structs) const {
    std::vector<std::string> out;
    auto add = [&](const std::string& n) {
        for (const std::string& s : out)
            if (s == n) return;
        out.push_back(n);
    };
    for (const std::string& name : structs) {
        const StructInfo* si = FindStruct(name);
        if (!si) continue;
        for (const FieldInfo& f : si->fields) {
            const CType* t = &f.type;
            if ((t->kind == CKind::Vector || t->kind == CKind::Array) && t->elem) t = t->elem;
            if (t->kind == CKind::Enum && t->enm) add(t->enm->name_str);
        }
    }
    return out;
}

}  // namespace codegen
}  // namespace imforge
