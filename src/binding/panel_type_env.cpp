#include "panel_type_env.h"

#include <algorithm>

namespace imforge {
namespace binding {

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

}  // namespace

PanelTypeEnv::PanelTypeEnv(const TypeDatabase& db, std::vector<Param> params,
                           std::vector<StateVar> state)
    : db_(db), params_(std::move(params)), state_(std::move(state)) {}

bool PanelTypeEnv::StateType(const std::string& type_name, CType& out) {
    struct Entry {
        const char* name;
        CKind kind;
        const char* spelling;
        size_t size;
    };
    static const Entry kEntries[] = {
        {"bool", CKind::Bool, "bool", 1},       {"int", CKind::I32, "int", 4},
        {"float", CKind::Float, "float", 4},    {"double", CKind::Double, "double", 8},
        {"string", CKind::StdString, "std::string", sizeof(std::string)},
        {"vec2", CKind::Vec2, "ImVec2", 8},     {"vec4", CKind::Vec4, "ImVec4", 16},
        {"color", CKind::Vec4, "ImVec4", 16},
    };
    for (const Entry& e : kEntries) {
        if (type_name == e.name) {
            out = CType();
            out.kind = e.kind;
            out.name = e.spelling;
            out.size = e.size;
            return true;
        }
    }
    return false;
}

bool PanelTypeEnv::StatePath(const std::string& path, CType& out, std::string* err) const {
    size_t pos = 6;
    size_t start = pos;
    while (pos < path.size() && IsIdentChar(path[pos])) ++pos;
    std::string var = path.substr(start, pos - start);
    if (var.empty()) return SetErr(err, "expected a state variable name after 'state.'");
    const StateVar* sv = nullptr;
    for (const StateVar& s : state_)
        if (s.name == var) sv = &s;
    if (!sv) return SetErr(err, "unknown state variable '" + var + "'");
    CType root;
    if (!StateType(sv->type, root))
        return SetErr(err, "state variable '" + var + "' has unsupported type '" + sv->type + "'");
    return db_.WalkPath(root, path.substr(pos), out, err);
}

bool PanelTypeEnv::TypeOfPath(const std::string& path, CType& out) {
    last_error.clear();
    if (path == "state") {
        last_error = "'state' is not a value; use state.<variable>";
        return false;
    }
    if (StartsWith(path, "state.")) return StatePath(path, out, &last_error);
    return db_.TypeOfPath(params_, path, out, &last_error);
}

const EnumInfo* PanelTypeEnv::FindEnum(const std::string& enum_name) const {
    if (const EnumInfo* e = db_.Enum(enum_name)) return e;
    const EnumInfo* found = nullptr;
    for (const std::string& n : db_.EnumNames()) {
        if (n.size() > enum_name.size() + 2 &&
            n.compare(n.size() - enum_name.size(), enum_name.size(), enum_name) == 0 &&
            n.compare(n.size() - enum_name.size() - 2, 2, "::") == 0) {
            if (found) return nullptr;
            found = db_.Enum(n);
        }
    }
    return found;
}

bool PanelTypeEnv::EnumValue(const std::string& enum_name, const std::string& member,
                             long long& out) {
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

std::vector<std::string> PanelTypeEnv::Complete(const std::string& prefix) const {
    std::vector<std::string> out = db_.Complete(params_, prefix);
    bool simple = prefix.find('.') == std::string::npos && prefix.find('[') == std::string::npos &&
                  prefix.find("::") == std::string::npos;
    if (simple) {
        if (StartsWith(std::string("state"), prefix)) out.push_back("state");
    } else if (StartsWith(prefix, "state.")) {
        std::string rest = prefix.substr(6);
        bool first_segment = true;
        for (char c : rest)
            if (!IsIdentChar(c)) first_segment = false;
        if (first_segment) {
            for (const StateVar& s : state_)
                if (StartsWith(s.name, rest)) out.push_back("state." + s.name);
        } else {
            int depth = 0;
            size_t last_dot = std::string::npos;
            bool ok = true;
            for (size_t i = 0; i < prefix.size() && ok; ++i) {
                char c = prefix[i];
                if (c == '[') ++depth;
                else if (c == ']') ok = depth-- > 0;
                else if (c == '.' && depth == 0) last_dot = i;
            }
            if (ok && depth == 0) {
                std::string base = prefix.back() == ']' ? prefix + "." : prefix.substr(0, last_dot + 1);
                std::string partial = prefix.back() == ']' ? std::string() : prefix.substr(last_dot + 1);
                CType t;
                if (StatePath(base.substr(0, base.size() - 1), t, nullptr))
                    for (const std::string& m : db_.Members(t))
                        if (StartsWith(m, partial)) out.push_back(base + m);
            }
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

}  // namespace binding
}  // namespace imforge
