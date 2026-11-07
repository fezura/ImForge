#pragma once

// helpers shared by emit_widgets*.cpp and emit_custom.cpp

#include <string>
#include <vector>

#include "emit_ctx.h"
#include "imforge/imforge_core.h"

namespace imforge {
namespace codegen {
namespace wdg {

inline const Json* RawProp(const Node& n, const char* key) {
    const PropValue* p = n.Prop(key);
    return p && !p->raw.IsNull() ? &p->raw : nullptr;
}

inline int PropInt(const Node& n, const char* key, int def) {
    const Json* j = RawProp(n, key);
    return j && j->IsNumber() ? j->AsInt(def) : def;
}

inline bool PropBool(const Node& n, const char* key, bool def) {
    const Json* j = RawProp(n, key);
    return j && j->IsBool() ? j->AsBool(def) : def;
}

inline bool PropIsExpr(const Node& n, const char* key) {
    const PropValue* p = n.Prop(key);
    return p && p->IsExpr();
}

inline bool JsonEqualsCI(const Json& a, const Json& b) {
    if (a.IsString() && b.IsString()) {
        const std::string& x = a.AsString();
        const std::string& y = b.AsString();
        if (x.size() != y.size()) return false;
        for (size_t i = 0; i < x.size(); ++i) {
            char p = x[i], q = y[i];
            if (p >= 'A' && p <= 'Z') p = static_cast<char>(p - 'A' + 'a');
            if (q >= 'A' && q <= 'Z') q = static_cast<char>(q - 'A' + 'a');
            if (p != q) return false;
        }
        return true;
    }
    return a == b;
}

// absent/null or equal to the widget default, so the argument can be dropped
inline bool IsDefault(const Node& n, const char* prop, const Json& def) {
    const Json* j = RawProp(n, prop);
    return !j || JsonEqualsCI(*j, def);
}
inline bool IsDefaultNum(const Node& n, const char* prop, double def) {
    const Json* j = RawProp(n, prop);
    return !j || (j->IsNumber() && j->AsDouble() == def);
}
inline bool IsDefaultStr(const Node& n, const char* prop, const char* def) {
    return IsDefault(n, prop, Json(def));
}
inline Json Vec2Json(double x, double y) {
    Json o = Json::Object();
    o.Set("x", Json(x));
    o.Set("y", Json(y));
    return o;
}

inline std::string Join(const std::vector<std::string>& parts, const char* sep = ", ") {
    std::string r;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) r += sep;
        r += parts[i];
    }
    return r;
}

// drops trailing args that match the ImGui default
struct Args {
    std::vector<std::string> v;
    size_t keep = 0;
    Args& Add(const std::string& s, bool is_default = false) {
        v.push_back(s);
        if (!is_default) keep = v.size();
        return *this;
    }
    std::string Str() const {
        std::string r;
        for (size_t i = 0; i < keep; ++i) {
            if (i) r += ", ";
            r += v[i];
        }
        return r;
    }
};

inline std::string Call(const char* fn, const Args& a) { return std::string(fn) + "(" + a.Str() + ")"; }

// EmitCtx adapters

inline void Line(EmitCtx& ctx, const std::string& s) { ctx.w.Line(s); }
inline void Indent(EmitCtx& ctx) { ++ctx.w.indent; }
inline void Unindent(EmitCtx& ctx) { --ctx.w.indent; }
inline void PreLine(EmitCtx& ctx, const std::string& s) { ctx.PreLine(s); }
inline void Mark(EmitCtx& ctx, const Node& n, const char* prop) { ctx.MarkLiteral(n, prop); }
inline bool HasEvent(EmitCtx& ctx, const Node& n, const char* ev) { return ctx.HasEvent(n, ev); }
inline void Fire(EmitCtx& ctx, const Node& n, const char* ev) { ctx.FireEvent(n, ev); }
inline void Children(EmitCtx& ctx, const Node& n) { codegen::EmitChildren(ctx, n); }
inline void Info(EmitCtx& ctx, const Node& n, const std::string& msg, const char* prop = nullptr) {
    ctx.Diag(Diagnostic::Level::Info, &n, prop, msg);
}
inline void Warn(EmitCtx& ctx, const Node& n, const std::string& msg, const char* prop = nullptr) {
    ctx.Diag(Diagnostic::Level::Warning, &n, prop, msg);
}

inline std::string Quote(EmitCtx& ctx, const std::string& s) { return ctx.StrLit(s); }
inline std::string IntLit(EmitCtx& ctx, long long v) { return ctx.IntLit(v); }
inline std::string Local(EmitCtx& ctx, const Node& n, const char* suffix) { return ctx.Local(n, suffix); }
inline std::string IdLit(EmitCtx& ctx, const Node& n) { return ctx.Id(n); }

inline std::string Label(EmitCtx& ctx, const Node& n, const char* prop = "label") {
    return ctx.Label(n, prop);
}
inline std::string Str(EmitCtx& ctx, const Node& n, const char* prop, const char* def) {
    return ctx.Str(n, prop, def);
}
inline std::string Num(EmitCtx& ctx, const Node& n, const char* prop, double def, bool is_float) {
    return ctx.Num(n, prop, def, is_float);
}
inline std::string FloatP(EmitCtx& ctx, const Node& n, const char* prop, double def) {
    return ctx.Num(n, prop, def, true);
}
inline std::string IntP(EmitCtx& ctx, const Node& n, const char* prop, double def) {
    return ctx.Num(n, prop, def, false);
}
inline bool Bool(EmitCtx& ctx, const Node& n, const char* prop, bool def) { return ctx.Bool(n, prop, def); }
inline std::string BoolExpr(EmitCtx& ctx, const Node& n, const char* prop, bool def) {
    return ctx.BoolLit(n, prop, def);
}
inline std::string Color(EmitCtx& ctx, const Node& n, const char* prop, const char* def_hex) {
    return ctx.Color(n, prop, def_hex);
}
inline std::string Vec2P(EmitCtx& ctx, const Node& n, const char* prop, const char* def_json) {
    return ctx.Vec2(n, prop, def_json);
}
inline std::string Px(EmitCtx& ctx, double v) { return ctx.Px(v); }
inline std::string EnumName(EmitCtx& ctx, const Node& n, const char* prop, const char* prefix,
                            const char* def) {
    return ctx.EnumValue(n, prop, prefix, def);
}
inline std::string Expr(EmitCtx& ctx, const std::string& e) { return ctx.Expr(e); }
inline void NeedStdlib(EmitCtx& ctx) { ctx.needs_stdlib = true; }
inline void NeedInclude(EmitCtx& ctx, const char* header) { ctx.Include(header); }
inline int Version(const EmitCtx& ctx) { return ctx.Version(); }

// Helper results carry invisible literal markers (\x01N\x02); strip them for comparisons only.
inline std::string Bare(const std::string& s) {
    std::string r;
    for (size_t i = 0; i < s.size();) {
        if (s[i] == '\x01') {
            size_t e = s.find('\x02', i);
            if (e == std::string::npos) break;
            i = e + 1;
            continue;
        }
        r += s[i++];
    }
    return r;
}
// Literal written inline in a statement, tagged for round-trip.
inline std::string Marked(EmitCtx& ctx, const Node& n, const char* prop, const std::string& text) {
    return ctx.w.MarkToken(n.id, prop) + text;
}
inline bool NoFlags(const std::string& f) {
    std::string b = Bare(f);
    return b.empty() || b == "0";
}
inline std::string Flags(EmitCtx& ctx, const Node& n, const char* prop, const char* enum_name) {
    std::string f = ctx.Flags(n, prop, enum_name);
    return f.empty() ? std::string("0") : f;
}
inline std::string OrFlags(const std::string& flags, const char* extra) {
    if (flags.find(extra) != std::string::npos) return flags;
    return NoFlags(flags) ? std::string(extra) : flags + " | " + extra;
}

inline void SizeAuto(const Node& n, bool& w_auto, bool& h_auto) {
    w_auto = h_auto = true;
    const Json* j = RawProp(n, "size");
    if (!j || !j->IsObject()) return;
    SizeSpec w, h;
    if (SizeSpec::Parse((*j)["w"], w)) w_auto = w.mode == SizeSpec::Mode::Auto;
    if (SizeSpec::Parse((*j)["h"], h)) h_auto = h.mode == SizeSpec::Mode::Auto;
}
inline bool IsAutoSize(const Node& n) {
    bool w, h;
    SizeAuto(n, w, h);
    return w && h;
}
// Size argument; empty when both axes are auto so callers can drop it.
inline std::string Size(EmitCtx& ctx, const Node& n) {
    return IsAutoSize(n) ? std::string() : ctx.Size(n);
}
inline std::string SizeOr0(EmitCtx& ctx, const Node& n) {
    return IsAutoSize(n) ? std::string("ImVec2(0, 0)") : ctx.Size(n);
}

// Size for widgets whose "auto" means a fixed design size (interpreter: `if (size.x <= 0) ...`).
// Writes a local when only one axis needs the fallback.
inline std::string SizeWithDefaults(EmitCtx& ctx, const Node& n, const std::string& def_w,
                                    const std::string& def_h) {
    bool w_auto, h_auto;
    SizeAuto(n, w_auto, h_auto);
    if (w_auto && h_auto) return "ImVec2(" + def_w + ", " + def_h + ")";
    if (!w_auto && !h_auto) return ctx.Size(n);
    std::string var = Local(ctx, n, "size");
    Line(ctx, "ImVec2 " + var + " = " + ctx.Size(n) + ";");
    if (w_auto) Line(ctx, "if (" + var + ".x <= 0.0f) " + var + ".x = " + def_w + ";");
    if (h_auto) Line(ctx, "if (" + var + ".y <= 0.0f) " + var + ".y = " + def_h + ";");
    return var;
}

// bindings

struct Slot {
    bool ok = false;
    std::string cpp, pointer, count;
    CType type;
    CKind Kind() const { return ok ? type.kind : CKind::Unknown; }
    bool IsEnum() const { return Kind() == CKind::Enum; }
    bool IsFloatArray() const {
        return Kind() == CKind::FloatArray ||
               (Kind() == CKind::Array && type.elem && type.elem->kind == CKind::Float);
    }
    bool IsIntArray() const {
        return Kind() == CKind::IntArray ||
               (Kind() == CKind::Array && type.elem &&
                (type.elem->kind == CKind::I32 || type.elem->kind == CKind::U32));
    }
    bool IsVector() const { return Kind() == CKind::Vector; }
    bool IsVec() const { return Kind() == CKind::Vec2 || Kind() == CKind::Vec4; }
    bool IsArrayLike() const {
        return Kind() == CKind::FloatArray || Kind() == CKind::IntArray || Kind() == CKind::CharArray ||
               Kind() == CKind::Array;
    }
    int Count() const { return ok ? type.count : 0; }
};

inline Slot GetSlot(EmitCtx& ctx, const Node& n, const char* slot) {
    Slot s;
    if (!n.Bind(slot)) return s;
    BindExpr b = ctx.Bind(n, slot);
    s.ok = b.ok && !b.cpp.empty();
    s.cpp = b.cpp;
    s.pointer = b.pointer;
    s.count = b.count;
    s.type = b.type;
    return s;
}

// Pointer to the first scalar component, as the ImGui N-component widgets expect.
inline std::string ScalarPtr(const Slot& s) {
    if (s.IsVec()) return "&" + s.cpp + ".x";
    if (s.IsVector()) return s.cpp + ".data()";
    if (s.IsArrayLike()) return s.cpp;
    if (!s.pointer.empty() && !s.IsEnum()) return s.pointer;
    return "&" + s.cpp;
}

inline std::string CountExpr(const Slot& s) {
    if (!s.count.empty()) return s.count;
    if (s.IsVector()) return "(int)" + s.cpp + ".size()";
    return "IM_ARRAYSIZE(" + s.cpp + ")";
}

inline std::string EnumCast(const Slot& s) {
    std::string name = s.type.name;
    if (name.empty() && s.type.enm && s.type.enm->name) name = s.type.enm->name;
    if (name.empty()) name = "int";
    return "(" + name + ")";
}

inline std::string TextureExpr(const Slot& tex) {
    if (tex.type.name == "ImTextureID" || tex.type.name == "ImTextureRef") return tex.cpp;
    return "(ImTextureID)" + tex.cpp;
}

// Unbound required slot: a static local keeps the generated code compiling. Returns the name.
inline std::string FallbackStorage(EmitCtx& ctx, const Node& n, const char* slot, const char* ctype,
                                   int count, const char* init) {
    std::string name = Local(ctx, n, "value");
    std::string decl = std::string("static ") + ctype + " " + name;
    if (count > 1) decl += "[" + IntLit(ctx, count) + "]";
    Line(ctx, decl + " = " + init + ";");
    Warn(ctx, n, std::string("'") + slot + "' is not bound; using a static local", slot);
    return name;
}

// statements

// `if (call) { events }` or the bare call.
inline void EmitCall(EmitCtx& ctx, const Node& n, const std::string& call, const char* event) {
    if (event && HasEvent(ctx, n, event)) {
        Line(ctx, "if (" + call + ") {");
        Indent(ctx);
        Fire(ctx, n, event);
        Unindent(ctx);
        Line(ctx, "}");
    } else {
        Line(ctx, call + ";");
    }
}

// `if (call) { assign; events }` (assign always runs; enum temporaries and copy-back buffers).
inline void EmitCallAssign(EmitCtx& ctx, const Node& n, const std::string& call,
                           const std::string& assign, const char* event) {
    if (assign.empty()) {
        EmitCall(ctx, n, call, event);
        return;
    }
    Line(ctx, "if (" + call + ") {");
    Indent(ctx);
    Line(ctx, assign);
    if (event && HasEvent(ctx, n, event)) Fire(ctx, n, event);
    Unindent(ctx);
    Line(ctx, "}");
}

bool EmitTextWidgets(EmitCtx& ctx, const Node& node);
bool EmitButtonWidgets(EmitCtx& ctx, const Node& node);
bool EmitListWidgets(EmitCtx& ctx, const Node& node);
bool EmitNumberWidgets(EmitCtx& ctx, const Node& node);
bool EmitInputWidgets(EmitCtx& ctx, const Node& node);
bool EmitDisplayWidgets(EmitCtx& ctx, const Node& node);

}  // namespace wdg
}  // namespace codegen
}  // namespace imforge
