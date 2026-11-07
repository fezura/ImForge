#include "editor_type_env.h"

#include <algorithm>

namespace imforge::builder {

namespace {

bool StartsWith(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

bool IsIdentChar(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || (c >= '0' && c <= '9');
}

bool SetErr(std::string* err, const std::string& msg) {
    if (err) *err = msg;
    return false;
}

CType Simple(CKind kind, const char* spelling, size_t size) {
    CType t;
    t.kind = kind;
    t.name = spelling;
    t.size = size;
    return t;
}

size_t RootEnd(const std::string& path) {
    size_t pos = 0;
    while (pos < path.size() && IsIdentChar(path[pos])) ++pos;
    return pos;
}

bool SplitPrefix(const std::string& prefix, std::string& base, std::string& partial) {
    int depth = 0;
    size_t last_dot = std::string::npos;
    for (size_t i = 0; i < prefix.size(); ++i) {
        char c = prefix[i];
        if (c == '[') ++depth;
        else if (c == ']') {
            if (depth == 0) return false;
            --depth;
        } else if (c == '.' && depth == 0) last_dot = i;
    }
    if (depth > 0) return false;
    if (!prefix.empty() && prefix.back() == ']') {
        base = prefix + ".";
        partial.clear();
        return true;
    }
    if (last_dot == std::string::npos) return false;
    base = prefix.substr(0, last_dot + 1);
    partial = prefix.substr(last_dot + 1);
    return true;
}

}  // namespace

EditorTypeEnv::EditorTypeEnv(const binding::TypeDatabase* database, const Panel& panel) : db(database) {
    SetPanel(panel);
}

EditorTypeEnv::EditorTypeEnv(const binding::TypeDatabase* database, const Component& component) : db(database) {
    SetComponent(component);
}

void EditorTypeEnv::Clear() {
    params.clear();
    state.clear();
    component_params.clear();
    enums_.clear();
    last_error.clear();
}

void EditorTypeEnv::SetPanel(const Panel& panel) {
    Clear();
    params = panel.params;
    state = panel.state;
}

void EditorTypeEnv::SetComponent(const Component& component) {
    Clear();
    component_params = component.params;
}

bool EditorTypeEnv::StateCType(const std::string& type_name, CType& out) {
    struct Entry {
        const char* name;
        CKind kind;
        const char* spelling;
        size_t size;
    };
    static const Entry kEntries[] = {
        {"bool", CKind::Bool, "bool", 1},
        {"int", CKind::I32, "int", 4},
        {"float", CKind::Float, "float", 4},
        {"double", CKind::Double, "double", 8},
        {"string", CKind::StdString, "std::string", sizeof(std::string)},
        {"vec2", CKind::Vec2, "ImVec2", 8},
        {"vec4", CKind::Vec4, "ImVec4", 16},
        {"color", CKind::Vec4, "ImVec4", 16},
    };
    for (const Entry& e : kEntries) {
        if (type_name == e.name) {
            out = Simple(e.kind, e.spelling, e.size);
            return true;
        }
    }
    return false;
}

bool EditorTypeEnv::ComponentParamCType(const Param& p, CType& out) {
    std::string t = p.type;
    if (t.compare(0, 8, "binding<") == 0 && t.size() > 9 && t.back() == '>') t = t.substr(8, t.size() - 9);
    if (t == "any") {
        out = CType();
        out.kind = CKind::Unknown;
        out.name = "any";
        return true;
    }
    if (StateCType(t, out)) return true;
    if (t.compare(0, 5, "enum:") == 0) {
        for (const EnumInfo& e : enums_) {
            if (e.name_str == p.name) {
                out = CType();
                out.kind = CKind::Enum;
                out.enm = &e;
                out.name = p.name;
                out.size = 4;
                return true;
            }
        }
        EnumInfo info;
        info.name_str = p.name;
        std::string rest = t.substr(5);
        size_t start = 0;
        long long idx = 0;
        while (start <= rest.size()) {
            size_t comma = rest.find(',', start);
            std::string item = rest.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
            if (!item.empty()) info.values.emplace_back(item, idx++);
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
        enums_.push_back(std::move(info));
        enums_.back().name = enums_.back().name_str.c_str();
        out = CType();
        out.kind = CKind::Enum;
        out.enm = &enums_.back();
        out.name = p.name;
        out.size = 4;
        return true;
    }
    return false;
}

const Param* EditorTypeEnv::ComponentParam(const std::string& name) const {
    for (const Param& p : component_params)
        if (p.name == name) return &p;
    return nullptr;
}

std::vector<std::string> EditorTypeEnv::MembersOf(const CType& t) const {
    if (db) return db->Members(t);
    switch (t.kind) {
    case CKind::Vec2: return {"x", "y"};
    case CKind::Vec4: return {"x", "y", "z", "w"};
    default: return {};
    }
}

bool EditorTypeEnv::Walk(const CType& root, const std::string& rest, CType& out, std::string* err) const {
    if (db) return db->WalkPath(root, rest, out, err);
    if (rest.empty()) {
        out = root;
        return true;
    }
    if (rest.size() == 2 && rest[0] == '.' && (root.kind == CKind::Vec2 || root.kind == CKind::Vec4)) {
        int comp = rest[1] == 'x' ? 0 : rest[1] == 'y' ? 1 : rest[1] == 'z' ? 2 : rest[1] == 'w' ? 3 : -1;
        if (comp >= 0 && comp < (root.kind == CKind::Vec2 ? 2 : 4)) {
            out = Simple(CKind::Float, "float", 4);
            return true;
        }
    }
    return SetErr(err, "no type information for '" + rest + "'");
}

bool EditorTypeEnv::StatePath(const std::string& path, CType& out, std::string* err) const {
    size_t pos = 6;
    size_t start = pos;
    while (pos < path.size() && IsIdentChar(path[pos])) ++pos;
    std::string var = path.substr(start, pos - start);
    if (var.empty()) return SetErr(err, "expected a state variable name after 'state.'");
    const StateVar* sv = nullptr;
    for (const StateVar& s : state)
        if (s.name == var) sv = &s;
    if (!sv) return SetErr(err, "unknown state variable '" + var + "'");
    CType root;
    if (!StateCType(sv->type, root))
        return SetErr(err, "state variable '" + var + "' has unsupported type '" + sv->type + "'");
    return Walk(root, path.substr(pos), out, err);
}

bool EditorTypeEnv::ComponentParamPath(const std::string& path, CType& out, std::string* err) {
    size_t pos = RootEnd(path);
    const Param* p = ComponentParam(path.substr(0, pos));
    if (!p) return SetErr(err, "unknown parameter '" + path.substr(0, pos) + "'");
    CType root;
    if (!ComponentParamCType(*p, root))
        return SetErr(err, "parameter '" + p->name + "' has unsupported type '" + p->type + "'");
    if (root.kind == CKind::Unknown) {
        out = root;
        return pos == path.size() ? true : SetErr(err, "parameter '" + p->name + "' is untyped (binding<any>)");
    }
    return Walk(root, path.substr(pos), out, err);
}

bool EditorTypeEnv::TypeOfPathErr(const std::string& path, CType& out, std::string* err) {
    if (path.empty()) return SetErr(err, "empty path");
    if (path == "state") return SetErr(err, "'state' is not a value; use state.<variable>");
    if (StartsWith(path, "state.")) return StatePath(path, out, err);
    std::string root = path.substr(0, RootEnd(path));
    if (ComponentParam(root)) return ComponentParamPath(path, out, err);
    for (const Param& p : params) {
        if (p.name == root) {
            if (db) return db->TypeOfPath(params, path, out, err);
            return SetErr(err, "no type database loaded (binding.typesJson)");
        }
    }
    return SetErr(err, "unknown path root '" + root + "'");
}

bool EditorTypeEnv::TypeOfPath(const std::string& path, CType& out) {
    last_error.clear();
    return TypeOfPathErr(path, out, &last_error);
}

const EnumInfo* EditorTypeEnv::FindEnum(const std::string& enum_name) const {
    for (const EnumInfo& e : enums_)
        if (e.name_str == enum_name) return &e;
    if (!db) return nullptr;
    if (const EnumInfo* e = db->Enum(enum_name)) return e;
    const EnumInfo* found = nullptr;
    for (const std::string& n : db->EnumNames()) {
        if (n.size() > enum_name.size() + 2 && n.compare(n.size() - enum_name.size(), enum_name.size(), enum_name) == 0 &&
            n.compare(n.size() - enum_name.size() - 2, 2, "::") == 0) {
            if (found) return nullptr;
            found = db->Enum(n);
        }
    }
    return found;
}

bool EditorTypeEnv::EnumValue(const std::string& enum_name, const std::string& member, long long& out) {
    last_error.clear();
    const EnumInfo* e = FindEnum(enum_name);
    if (!e) {
        last_error = "unknown enum '" + enum_name + "'";
        return false;
    }
    if (!e->ValueOf(member.c_str(), out)) {
        last_error = "enum '" + enum_name + "' has no member '" + member + "'";
        return false;
    }
    return true;
}

std::vector<std::string> EditorTypeEnv::Roots() const {
    std::vector<std::string> out;
    for (const Param& p : params) out.push_back(p.name);
    for (const Param& p : component_params) out.push_back(p.name);
    for (const StateVar& s : state) out.push_back("state." + s.name);
    return out;
}

void EditorTypeEnv::CompleteMembers(const std::string& prefix, std::vector<std::string>& out) const {
    std::string base, partial;
    if (!SplitPrefix(prefix, base, partial)) return;
    std::string head = base.substr(0, base.size() - 1);
    CType t;
    std::string err;
    bool ok = false;
    if (StartsWith(head, "state.")) ok = StatePath(head, t, &err);
    else if (ComponentParam(head.substr(0, RootEnd(head)))) ok = const_cast<EditorTypeEnv*>(this)->ComponentParamPath(head, t, &err);
    if (!ok) return;
    for (const std::string& m : MembersOf(t))
        if (StartsWith(m, partial)) out.push_back(base + m);
}

std::vector<std::string> EditorTypeEnv::Complete(const std::string& prefix) const {
    std::vector<std::string> out;
    if (db) out = db->Complete(params, prefix);
    bool simple = prefix.find('.') == std::string::npos && prefix.find('[') == std::string::npos &&
                  prefix.find("::") == std::string::npos;
    if (simple) {
        if (!db)
            for (const Param& p : params)
                if (StartsWith(p.name, prefix)) out.push_back(p.name);
        for (const Param& p : component_params)
            if (StartsWith(p.name, prefix)) out.push_back(p.name);
        if (StartsWith(std::string("state"), prefix) && !state.empty()) out.push_back("state");
    } else if (StartsWith(prefix, "state.")) {
        std::string rest = prefix.substr(6);
        bool first_segment = std::all_of(rest.begin(), rest.end(), IsIdentChar);
        if (first_segment) {
            for (const StateVar& s : state)
                if (StartsWith(s.name, rest)) out.push_back("state." + s.name);
        } else {
            CompleteMembers(prefix, out);
        }
    } else if (prefix.find("::") == std::string::npos) {
        std::string root = prefix.substr(0, RootEnd(prefix));
        if (ComponentParam(root)) CompleteMembers(prefix, out);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

}  // namespace imforge::builder
