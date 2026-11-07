#include <algorithm>
#include <cstdlib>

#include "type_database.h"
#include "type_database_detail.h"

namespace imforge {
namespace binding {

using namespace detail;

bool TypeDatabase::Member(const CType& cur, const std::string& name, CType& out,
                          std::string* err) const {
    if (cur.kind == CKind::Struct) {
        if (!cur.strct)
            return Fail(err, "struct '" + SpellCType(cur) + "' has no type information");
        const FieldInfo* f = cur.strct->Find(name.c_str());
        if (!f) return Fail(err, "no field '" + name + "' in '" + cur.strct->name + "'");
        out = f->type;
        return true;
    }
    if (cur.kind == CKind::Vec2 || cur.kind == CKind::Vec4) {
        int comp = name == "x" ? 0 : name == "y" ? 1 : name == "z" ? 2 : name == "w" ? 3 : -1;
        if (comp < 0 || comp >= (cur.kind == CKind::Vec2 ? 2 : 4))
            return Fail(err, "no component '" + name + "' in '" + SpellCType(cur) + "'");
        out = Simple(CKind::Float, "float", 4);
        return true;
    }
    if (name == "size" && IsContainer(cur.kind)) {
        out = Simple(CKind::I64, "long long", 8);
        return true;
    }
    if (cur.kind == CKind::Unknown)
        return Fail(err, "type '" + SpellCType(cur) + "' has no type information");
    return Fail(err, "cannot access field '" + name + "' of type '" + SpellCType(cur) + "'");
}

bool TypeDatabase::Element(const CType& cur, const std::string& index, CType& out,
                           std::string* err) const {
    bool literal = !index.empty();
    for (char c : index)
        if (c < '0' || c > '9') literal = false;
    switch (cur.kind) {
    case CKind::FloatArray:
    case CKind::Vec2:
    case CKind::Vec4: out = Simple(CKind::Float, "float", 4); break;
    case CKind::IntArray: out = Simple(CKind::I32, "int", 4); break;
    case CKind::CharArray: out = Simple(CKind::I8, "char", 1); break;
    case CKind::Array:
    case CKind::Vector:
        if (!cur.elem) return Fail(err, "unknown element type of '" + SpellCType(cur) + "'");
        out = *cur.elem;
        break;
    default: return Fail(err, "cannot index type '" + SpellCType(cur) + "'");
    }
    int count = cur.kind == CKind::Vec2 ? 2 : cur.kind == CKind::Vec4 ? 4 : cur.count;
    if (literal && count > 0 && std::atoll(index.c_str()) >= count)
        return Fail(err, "index " + index + " out of range for '" + SpellCType(cur) + "' (size " +
                             std::to_string(count) + ")");
    return true;
}

bool TypeDatabase::WalkPath(const CType& root, const std::string& rest, CType& out,
                            std::string* err) const {
    CType cur = root;
    size_t pos = 0;
    while (pos < rest.size()) {
        char c = rest[pos];
        if (c == '.') {
            ++pos;
            size_t start = pos;
            while (pos < rest.size() && IsIdentChar(rest[pos])) ++pos;
            std::string name = rest.substr(start, pos - start);
            if (!IsIdentifier(name)) return Fail(err, "expected field name after '.'");
            CType next;
            if (!Member(cur, name, next, err)) return false;
            cur = next;
            continue;
        }
        if (c == '[') {
            int depth = 0;
            size_t end = pos;
            for (; end < rest.size(); ++end) {
                if (rest[end] == '[') ++depth;
                else if (rest[end] == ']' && --depth == 0) break;
            }
            if (end >= rest.size()) return Fail(err, "expected ']'");
            std::string index = rest.substr(pos + 1, end - pos - 1);
            if (index.empty()) return Fail(err, "expected index after '['");
            CType next;
            if (!Element(cur, index, next, err)) return false;
            cur = next;
            pos = end + 1;
            continue;
        }
        return Fail(err, std::string("unexpected character '") + c + "' in path");
    }
    out = cur;
    return true;
}

bool TypeDatabase::TypeOfPath(const std::vector<Param>& params, const std::string& path, CType& out,
                              std::string* err) const {
    size_t pos = 0;
    while (pos < path.size() && IsIdentChar(path[pos])) ++pos;
    std::string root = path.substr(0, pos);
    if (!IsIdentifier(root)) return Fail(err, "expected a parameter name at the start of the path");
    const Param* param = nullptr;
    for (const Param& p : params)
        if (p.name == root) param = &p;
    if (!param) {
        if (root == "state") return Fail(err, "'state' paths are resolved by the panel");
        return Fail(err, "unknown parameter '" + root + "'");
    }
    CType root_type;
    if (!ResolveTypeName(param->type, root_type))
        return Fail(err, "parameter '" + root + "' has unknown type '" + param->type + "'");
    return WalkPath(root_type, path.substr(pos), out, err);
}

std::vector<std::string> TypeDatabase::Members(const CType& t) const {
    std::vector<std::string> out;
    switch (t.kind) {
    case CKind::Struct:
        if (t.strct)
            for (const FieldInfo& f : t.strct->fields) out.push_back(f.name);
        break;
    case CKind::Vec2: out = {"x", "y"}; break;
    case CKind::Vec4: out = {"x", "y", "z", "w"}; break;
    default:
        if (IsContainer(t.kind)) out.push_back("size");
        break;
    }
    return out;
}

std::vector<std::string> TypeDatabase::Complete(const std::vector<Param>& params,
                                                const std::string& prefix) const {
    std::vector<std::string> out;
    size_t scope = prefix.find("::");
    if (scope != std::string::npos) {
        if (prefix.find('.') != std::string::npos || prefix.find('[') != std::string::npos) return out;
        std::string ename = prefix.substr(0, scope), partial = prefix.substr(scope + 2);
        if (const EnumDesc* e = EnumSource(ename))
            for (const auto& v : e->values)
                if (StartsWith(v.first, partial)) out.push_back(ename + "::" + v.first);
        std::sort(out.begin(), out.end());
        return out;
    }
    int depth = 0;
    size_t last_dot = std::string::npos;
    for (size_t i = 0; i < prefix.size(); ++i) {
        char c = prefix[i];
        if (c == '[') ++depth;
        else if (c == ']') {
            if (depth == 0) return out;
            --depth;
        } else if (c == '.' && depth == 0) last_dot = i;
    }
    if (depth > 0) return out;
    std::string base, partial;
    if (!prefix.empty() && prefix.back() == ']') {
        base = prefix + ".";
    } else if (last_dot == std::string::npos) {
        for (const Param& p : params)
            if (StartsWith(p.name, prefix)) out.push_back(p.name);
        for (const auto& kv : enum_src_)
            if (StartsWith(kv.first, prefix)) out.push_back(kv.first);
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    } else {
        base = prefix.substr(0, last_dot + 1);
        partial = prefix.substr(last_dot + 1);
    }
    for (char c : partial)
        if (!IsIdentChar(c)) return out;
    CType t;
    if (!TypeOfPath(params, base.substr(0, base.size() - 1), t, nullptr)) return out;
    for (const std::string& m : Members(t))
        if (StartsWith(m, partial)) out.push_back(base + m);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

}  // namespace binding
}  // namespace imforge
