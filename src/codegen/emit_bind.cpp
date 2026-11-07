#include <cstring>

#include "emit_internal.h"

namespace imforge {
namespace codegen {

namespace {

bool StartsWith(const std::string& s, const char* p) { return s.compare(0, std::strlen(p), p) == 0; }

const Param* FindParam(const EmitCtx& ctx, const std::string& name) {
    if (!ctx.component) return nullptr;
    for (const Param& p : ctx.component->params)
        if (p.name == name) return &p;
    return nullptr;
}

const StateVar* FindState(const EmitCtx& ctx, const std::string& name) {
    if (!ctx.panel) return nullptr;
    for (const StateVar& s : ctx.panel->state)
        if (s.name == name) return &s;
    return nullptr;
}

const Param* FindPanelParam(const EmitCtx& ctx, const std::string& name) {
    if (!ctx.panel) return nullptr;
    for (const Param& p : ctx.panel->params)
        if (p.name == name) return &p;
    return nullptr;
}

CType StructType(const EmitCtx& ctx, const std::string& type_name) {
    CType t;
    t.name = type_name;
    if (ctx.types) {
        if (const StructInfo* si = ctx.types->FindStruct(type_name)) {
            t.kind = CKind::Struct;
            t.strct = si;
        }
    }
    return t;
}

CType CStringType() {
    CType t;
    t.kind = CKind::CharArray;
    t.name = "const char*";
    return t;
}

struct PathResult {
    std::string cpp;
    CType type;
    bool known = false;
};

PathResult ResolvePath(EmitCtx& ctx, const std::string& path) {
    PathResult r;
    size_t cut = path.find_first_of(".[");
    std::string root = path.substr(0, cut);
    std::string rest = cut == std::string::npos ? std::string() : path.substr(cut);
    bool is_size = false;
    CType container;
    auto finish = [&](const std::string& base_cpp, const CType& base_type, bool base_known) {
        std::string body_rest = rest;
        bool size_tail = rest.size() >= 5 && rest.compare(rest.size() - 5, 5, ".size") == 0;
        CType out;
        bool ok = false;
        if (base_known && ctx.types) ok = ctx.types->Resolve(base_type, rest, out, &is_size, &container);
        else if (base_known && rest.empty()) { out = base_type; ok = true; }
        if (ok) {
            r.known = true;
            r.type = out;
        }
        if (size_tail && (is_size || !ok)) {
            std::string base = base_cpp + body_rest.substr(0, body_rest.size() - 5);
            CKind ck = container.kind;
            if (!ok) {
                bool seen = false;
                for (const std::string& s : ctx.warned_paths) seen = seen || s == path;
                if (!seen) {
                    ctx.warned_paths.push_back(path);
                    ctx.Diag(Diagnostic::Level::Info, nullptr, nullptr, "'" + path + "': container type unknown, assuming std::vector");
                }
                ck = CKind::Vector;
                r.type = TypesDb::ScalarType("int");
                r.known = true;
            }
            if (ck == CKind::Vector) r.cpp = "(int)" + base + ".size()";
            else if (ck == CKind::CharArray) { r.cpp = "(int)strlen(" + base + ")"; ctx.Include("<cstring>"); }
            else r.cpp = "IM_ARRAYSIZE(" + base + ")";
            return;
        }
        r.cpp = base_cpp + body_rest;
    };
    if (ctx.panel && root == "state") {
        size_t cut2 = rest.find_first_of(".[", 1);
        std::string var = rest.size() > 1 && rest[0] == '.' ? rest.substr(1, cut2 == std::string::npos ? std::string::npos : cut2 - 1) : std::string();
        const StateVar* sv = FindState(ctx, var);
        if (!sv) {
            ctx.Diag(Diagnostic::Level::Error, nullptr, nullptr, "unknown state variable '" + var + "'");
            r.cpp = path;
            return r;
        }
        std::string tail = cut2 == std::string::npos ? std::string() : rest.substr(cut2);
        rest = tail;
        finish("state." + VarCase(ctx.opts, sv->name), TypesDb::ScalarType(sv->type), true);
        return r;
    }
    if (const Param* pp = FindPanelParam(ctx, root)) {
        finish(SanitizeIdentifier(pp->name), StructType(ctx, pp->type), ctx.types && ctx.types->FindStruct(pp->type));
        return r;
    }
    if (const Param* cp = FindParam(ctx, root)) {
        std::string name = SanitizeIdentifier(cp->name);
        if (StartsWith(cp->type, "binding<")) {
            std::string inner = cp->type.substr(8, cp->type.size() - 9);
            CType t = TypesDb::ScalarType(inner);
            if (inner == "string" && !ctx.opts.settings.std_string) t = CStringType();
            finish("(*" + name + ")", t, inner != "any");
            return r;
        }
        CType t = cp->type == "string" ? CStringType() : TypesDb::ScalarType(StartsWith(cp->type, "enum:") ? "int" : cp->type);
        finish(name, t, true);
        return r;
    }
    bool seen = false;
    for (const std::string& s : ctx.warned_paths) seen = seen || s == path;
    if (!seen) {
        ctx.warned_paths.push_back(path);
        ctx.Diag(Diagnostic::Level::Warning, nullptr, nullptr, "'" + path + "': unknown root '" + root + "' (not a parameter or state)");
    }
    CType unknown;
    finish(root, unknown, false);
    return r;
}

struct CtxTypeEnv : expr::TypeEnv {
    EmitCtx& ctx;
    explicit CtxTypeEnv(EmitCtx& c) : ctx(c) {}
    bool TypeOfPath(const std::string& path, CType& out) override {
        PathResult r = ResolvePath(ctx, path);
        if (!r.known) return false;
        out = r.type;
        return true;
    }
    bool EnumValue(const std::string& enum_name, const std::string& member, long long& out) override {
        if (ctx.types) {
            if (const EnumInfo* e = ctx.types->FindEnum(enum_name)) return e->ValueOf(member.c_str(), out);
        }
        out = 0;
        return true;
    }
};

expr::EmitOptions MakeEmitOptions(EmitCtx& ctx) {
    expr::EmitOptions o;
    o.path_to_cpp = [&ctx](const std::string& path) { return ResolvePath(ctx, path).cpp; };
    o.path_is_char_array = [&ctx](const std::string& path) {
        PathResult r = ResolvePath(ctx, path);
        return r.known && r.type.kind == CKind::CharArray;
    };
    o.path_to_cstr = [&ctx](const std::string& path) {
        PathResult r = ResolvePath(ctx, path);
        if (r.known && r.type.kind == CKind::StdString) return r.cpp + ".c_str()";
        return r.cpp;
    };
    return o;
}

CType InferredType(const char* accepted, const EmitCtx& ctx) {
    std::string first = accepted ? accepted : "";
    size_t bar = first.find('|');
    if (bar != std::string::npos) first = first.substr(0, bar);
    if (first == "string") return ctx.opts.settings.std_string ? TypesDb::ScalarType("string") : TypesDb::FromSpelling("char[256]");
    if (first == "float[N]") return TypesDb::FromSpelling("float[4]");
    if (first == "int[N]") return TypesDb::FromSpelling("int[4]");
    if (first == "float[4]" || first == "float[3]") return TypesDb::FromSpelling(first);
    if (first == "float[]" || first == "vector<float>") {
        CType t;
        t.kind = CKind::Vector;
        t.name = "std::vector<float>";
        return t;
    }
    if (first == "enum") return TypesDb::ScalarType("int");
    return TypesDb::ScalarType(first);
}

}  // namespace

std::string EmitCtx::Path(const std::string& path, CType* type_out) {
    PathResult r = ResolvePath(*this, path);
    if (type_out) *type_out = r.known ? r.type : CType();
    return r.cpp;
}

std::string EmitCtx::Expr(const std::string& src) {
    expr::Ast ast;
    imforge::Error err;
    if (!expr::Parse(src, ast, &err)) {
        Diag(Diagnostic::Level::Error, nullptr, nullptr, "expression '" + src + "': " + err.message);
        return "true";
    }
    bool uses_strcmp = false;
    std::string out = expr::EmitCpp(ast, MakeEmitOptions(*this));
    uses_strcmp = out.find("strcmp(") != std::string::npos;
    if (uses_strcmp) Include("<cstring>");
    return out;
}

std::string EmitCtx::ExprCStr(const std::string& src) {
    expr::Ast ast;
    imforge::Error err;
    if (!expr::Parse(src, ast, &err)) {
        Diag(Diagnostic::Level::Error, nullptr, nullptr, "expression '" + src + "': " + err.message);
        return "\"\"";
    }
    expr::EmitOptions o = MakeEmitOptions(*this);
    if (ast.kind == expr::Ast::Kind::Path) {
        PathResult r = ResolvePath(*this, expr::StaticPath(ast));
        std::string cpp = expr::EmitCpp(ast, o);
        if (r.known && r.type.kind == CKind::StdString) return cpp + ".c_str()";
        if (r.known && (r.type.kind == CKind::CharArray || r.type.kind == CKind::Unknown)) return cpp;
        if (!r.known) return cpp;
        return cpp;
    }
    if (ast.kind == expr::Ast::Kind::Literal && ast.literal.kind == Value::Kind::String) return StrLit(ast.literal.s);
    std::string cpp = expr::EmitCpp(ast, o);
    if (ExprType(src) == expr::Type::String) {
        Include("<string>");
        return "std::string(" + cpp + ").c_str()";
    }
    return cpp;
}

expr::Type EmitCtx::ExprType(const std::string& src) {
    expr::Ast ast;
    if (!expr::Parse(src, ast, nullptr)) return expr::Type::Error;
    CtxTypeEnv env(*this);
    expr::Type t = expr::Type::Error;
    if (!expr::TypeCheck(ast, env, t, nullptr)) return expr::Type::Error;
    return t;
}

BindInfo EmitCtx::BindPath(const std::string& path, const char* accepted) {
    BindInfo b;
    if (path.empty()) return b;
    PathResult r = ResolvePath(*this, path);
    b.cpp = r.cpp;
    b.ok = true;
    if (r.known) {
        b.type = r.type;
    } else {
        b.type = InferredType(accepted, *this);
        b.inferred = true;
        bool seen = false;
        for (const std::string& s : warned_paths) seen = seen || s == "bind:" + path;
        if (!seen) {
            warned_paths.push_back("bind:" + path);
            Diag(Diagnostic::Level::Info, nullptr, nullptr, "'" + path + "': type unknown, assuming " + b.type.name);
        }
    }
    switch (b.type.kind) {
    case CKind::Enum: b.pointer = "(int*)&" + b.cpp; break;
    case CKind::Vector:
        b.pointer = b.cpp + ".data()";
        b.count = "(int)" + b.cpp + ".size()";
        break;
    case CKind::FloatArray: case CKind::IntArray: case CKind::CharArray: case CKind::Array:
        b.pointer = b.cpp;
        b.count = b.type.name == "const char*" ? std::string() : "IM_ARRAYSIZE(" + b.cpp + ")";
        break;
    case CKind::Vec2: b.pointer = "&" + b.cpp; b.count = "2"; break;
    case CKind::Vec4: b.pointer = "&" + b.cpp; b.count = "4"; break;
    default: b.pointer = "&" + b.cpp; break;
    }
    if (StartsWith(b.cpp, "(*") && b.cpp.back() == ')' && b.cpp.find_first_of(".[") == std::string::npos) {
        std::string name = b.cpp.substr(2, b.cpp.size() - 3);
        if (b.type.kind != CKind::Enum && b.type.kind != CKind::Vector) b.pointer = name;
    }
    return b;
}

BindInfo EmitCtx::Bind(const Node& n, const char* slot) {
    const PropValue* b = n.Bind(slot);
    if (!b || b->raw.IsNull()) return BindInfo();
    const WidgetDef* d = Def(n);
    const BindDef* bd = d ? FindBind(*d, slot) : nullptr;
    const char* accepted = bd ? bd->types : "float";
    if (b->IsParam()) {
        const Param* cp = FindParam(*this, b->Param());
        if (!cp) {
            Diag(Diagnostic::Level::Error, &n, slot, "unknown component parameter '" + b->Param() + "'");
            return BindInfo();
        }
        return BindPath(cp->name, accepted);
    }
    if (!b->raw.IsString()) {
        Diag(Diagnostic::Level::Error, &n, slot, "binding must be a path string");
        return BindInfo();
    }
    return BindPath(b->raw.AsString(), accepted);
}

// events

bool EmitCtx::HasEvent(const Node& n, const char* event) const {
    for (const auto& e : n.events)
        if (e.first == event && (e.second.handler || !e.second.actions.empty())) return true;
    return false;
}

std::string EmitCtx::HandlerName(const Node& n, const char* event) const {
    return FnCase(opts, OwnerIdent(project, panel, component)) + "_" + SanitizeIdentifier(n.id) + "_" + FnCase(opts, EventFnSuffix(event));
}

std::string EmitCtx::HandlerArgs() const {
    std::string out;
    if (panel) {
        out = "state";
        for (const Param& p : panel->params) out += ", " + SanitizeIdentifier(p.name);
    } else if (component) {
        for (const Param& p : component->params) out += (out.empty() ? "" : ", ") + SanitizeIdentifier(p.name);
    }
    return out;
}

std::string TabStateMember(const std::string& tabbar_id) { return "imb_tab_" + SanitizeIdentifier(tabbar_id); }

int TabIndex(const EmitCtx& ctx, const std::string& tabbar_id, const std::string& tab_id) {
    const Node* root = ctx.panel ? &ctx.panel->root : (ctx.component ? &ctx.component->root : nullptr);
    if (!root) return -1;
    const Node* tb = FindNode(*root, tabbar_id);
    if (!tb) return -1;
    for (size_t i = 0; i < tb->children.size(); ++i)
        if (tb->children[i].id == tab_id) return static_cast<int>(i);
    return -1;
}

void EmitCtx::FireEvent(const Node& n, const char* event) {
    const EventSpec* spec = nullptr;
    for (const auto& e : n.events)
        if (e.first == event) spec = &e.second;
    if (!spec) return;
    bool call_handler = spec->handler;
    for (const Action& a : spec->actions) {
        if (a.type == "handler") {
            call_handler = true;
        } else if (a.type == "toggle") {
            std::string t = Path(a.target);
            w.Line(t + " = !" + t + ";");
        } else if (a.type == "set") {
            CType ty;
            std::string t = Path(a.target, &ty);
            std::string v;
            if (a.value.IsObject() && a.value["expr"].IsString()) {
                v = Expr(a.value["expr"].AsString());
            } else {
                const Json& j = a.value;
                switch (ty.kind) {
                case CKind::Bool: v = Value::FromJson(j, Value::Kind::Bool).b ? "true" : "false"; break;
                case CKind::Float: case CKind::Double: v = FloatLitOf(j.AsDouble()); break;
                case CKind::StdString: v = StrLit(j.IsString() ? j.AsString() : Value::Infer(j).ToString()); break;
                case CKind::CharArray:
                    Include("<cstdio>");
                    w.Line("std::snprintf(" + t + ", sizeof(" + t + "), \"%s\", " + StrLit(j.IsString() ? j.AsString() : Value::Infer(j).ToString()) + ");");
                    continue;
                case CKind::Vec2: {
                    imforge::Vec2 p = Value::Infer(j).ToVec2();
                    v = "ImVec2(" + NumLit(p.x) + ", " + NumLit(p.y) + ")";
                    break;
                }
                case CKind::Vec4: v = ColorLit(j.IsString() ? ParseColor(j.AsString()) : Value::Infer(j).ToVec4()); break;
                case CKind::Enum:
                    if (j.IsNumber()) v = "(" + ty.name + ")" + std::to_string(j.AsInt());
                    else if (j.AsString().find("::") != std::string::npos || ty.name.empty()) v = j.AsString();
                    else v = ty.name + "::" + SanitizeIdentifier(j.AsString());
                    break;
                default:
                    if (j.IsBool()) v = j.AsBool() ? "true" : "false";
                    else if (j.IsNumber()) v = NumLit(j.AsDouble());
                    else if (j.IsString()) v = StrLit(j.AsString());
                    else v = "0";
                    break;
                }
            }
            w.Line(t + " = " + v + ";");
        } else if (a.type == "openPopup") {
            const Node* root = panel ? &panel->root : (component ? &component->root : nullptr);
            const Node* target = root ? FindNode(*root, a.target) : nullptr;
            w.Line("ImGui::OpenPopup(" + StrLit(target && target->type == "Modal" ? "###" + a.target : a.target) + ");");
        } else if (a.type == "closePopup") {
            w.Line("ImGui::CloseCurrentPopup();");
        } else if (a.type == "setTab") {
            std::string tab = a.value.IsString() ? a.value.AsString() : std::string();
            int idx = TabIndex(*this, a.target, tab);
            if (!panel || idx < 0) {
                Diag(Diagnostic::Level::Warning, &n, nullptr, "setTab: tab '" + tab + "' not found in TabBar '" + a.target + "'");
                continue;
            }
            w.Line("state." + TabStateMember(a.target) + " = " + std::to_string(idx) + ";");
        } else if (a.type == "copyClipboard") {
            std::string v;
            if (a.value.IsObject() && a.value["expr"].IsString()) {
                v = ExprCStr(a.value["expr"].AsString());
            } else {
                std::string raw = a.value.IsString() ? a.value.AsString() : Value::Infer(a.value).ToString();
                v = Writer::StripMarks(CStr(n, "events", raw, "clip"));
            }
            w.Line("ImGui::SetClipboardText(" + v + ");");
        } else {
            Diag(Diagnostic::Level::Warning, &n, nullptr, "unknown action type '" + a.type + "'");
        }
    }
    if (call_handler) w.Line(HandlerName(n, event) + "(" + HandlerArgs() + ");");
}

}  // namespace codegen
}  // namespace imforge
