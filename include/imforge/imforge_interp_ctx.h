#ifndef IMFORGE_INTERP_PARTS
#include "imforge_interp.h"
#else
#pragma once

namespace imforge {

namespace interp_detail {

// "Mode" matches "game::Mode" and the other way round, leading "::" ignored
inline bool EnumNameMatches(const std::string& registered, const std::string& wanted) {
    const char* r = registered.c_str();
    const char* w = wanted.c_str();
    if (r[0] == ':' && r[1] == ':') r += 2;
    if (w[0] == ':' && w[1] == ':') w += 2;
    size_t rn = std::strlen(r), wn = std::strlen(w);
    if (rn == wn) return std::memcmp(r, w, rn) == 0;
    const char* longer = rn > wn ? r : w;
    const char* shorter = rn > wn ? w : r;
    size_t ln = rn > wn ? rn : wn, sn = rn > wn ? wn : rn;
    return sn > 0 && ln >= sn + 2 && longer[ln - sn - 1] == ':' && longer[ln - sn - 2] == ':' &&
           std::memcmp(longer + ln - sn, shorter, sn) == 0;
}

inline bool LookupEnumValue(const std::string& enum_name, const std::string& member, long long& out) {
    const TypeRegistry& reg = TypeRegistry::Global();
    if (const EnumInfo* e = reg.FindEnum(enum_name.c_str()))
        if (e->ValueOf(member.c_str(), out)) return true;
    for (const auto& e : reg.enums)
        if (EnumNameMatches(e->name_str, enum_name) && e->ValueOf(member.c_str(), out)) return true;
    return false;
}

struct PathEnv : expr::ValueEnv {
    const DrawCtx* ctx;
    explicit PathEnv(const DrawCtx* c) : ctx(c) {}
    bool ReadPath(const std::string& path, Value& out) override { return ctx->ReadPath(path, out); }
    bool EnumValue(const std::string& enum_name, const std::string& member, long long& out) override {
        return LookupEnumValue(enum_name, member, out);
    }
};

inline const Param* FindParam(const Component* c, const char* name, size_t len) {
    if (!c) return nullptr;
    for (const Param& p : c->params)
        if (p.name.size() == len && p.name.compare(0, len, name, len) == 0) return &p;
    return nullptr;
}

inline const Param* FindParam(const Component* c, const std::string& name) {
    return FindParam(c, name.c_str(), name.size());
}

inline Value* FindState(PanelInstance& inst, const char* name, size_t len) {
    for (auto& kv : inst.state)
        if (kv.first.size() == len && kv.first.compare(0, len, name, len) == 0) return &kv.second;
    return nullptr;
}

inline size_t RootLength(const char* path) {
    size_t i = 0;
    while (path[i] && path[i] != '.' && path[i] != '[') ++i;
    return i;
}

constexpr size_t kMaxCachedExprs = 4096;

inline const BindDef* FindBindDef(const WidgetDef* d, const char* slot) {
    if (!d || !slot) return nullptr;
    for (int i = 0; i < d->bind_count; ++i)
        if (std::strcmp(d->binds[i].name, slot) == 0) return &d->binds[i];
    return nullptr;
}

// resolve through outer params so design-mode mock storage is shared by everything bound to the same path
inline std::string ParamPath(const DrawCtx& c, const std::string& param) {
    if (!c.instance_node || !c.outer) return std::string();
    const PropValue* pv = c.instance_node->Prop(param.c_str());
    if (!pv) return std::string();
    if (pv->IsParam()) return ParamPath(*c.outer, pv->Param());
    if (!pv->raw.IsString()) return std::string();
    const std::string& path = pv->raw.AsString();
    const size_t n = RootLength(path.c_str());
    if (n == 0) return std::string();
    if (FindParam(c.outer->component, path.c_str(), n)) return n == path.size() ? ParamPath(*c.outer, path) : std::string();
    return path;
}

// one Info per message per frame
inline void InfoOnce(const DrawCtx& ctx, const std::string& msg, const char* prop) {
    for (const Diagnostic& d : ctx.interp.last_diagnostics)
        if (d.message == msg) return;
    ctx.Diag(Diagnostic::Level::Info, msg, prop);
}

inline bool IsIntKind(CKind k) {
    return k == CKind::I8 || k == CKind::U8 || k == CKind::I16 || k == CKind::U16 || k == CKind::I32 ||
           k == CKind::U32 || k == CKind::I64 || k == CKind::U64 || k == CKind::Enum || k == CKind::Bool;
}

}  // namespace interp_detail

inline const Json* Interpreter::PropDefault(const std::string& type, const char* prop) {
    static thread_local std::string key;
    key.assign(type);
    key += '.';
    key += prop;
    auto it = prop_defaults.find(key);
    if (it != prop_defaults.end()) return it->second.IsNull() ? nullptr : &it->second;
    Json parsed;
    if (const WidgetDef* d = WidgetDefFor(type)) {
        for (int i = 0; i < d->prop_count; ++i) {
            if (std::strcmp(d->props[i].name, prop) == 0) {
                if (d->props[i].def_json && *d->props[i].def_json) ParseJson(d->props[i].def_json, parsed);
                break;
            }
        }
    }
    Json& slot = prop_defaults[key];
    slot = std::move(parsed);
    return slot.IsNull() ? nullptr : &slot;
}

inline void Interpreter::AddDiagnostic(Diagnostic::Level level, const std::string& msg, const std::string& panel,
                                       const std::string& node_id, const char* prop) {
    for (const Diagnostic& d : last_diagnostics)
        if (d.node_id == node_id && d.message == msg && d.prop == (prop ? prop : "")) return;
    Diagnostic d;
    d.level = level;
    d.message = msg;
    d.panel = panel;
    d.node_id = node_id;
    d.prop = prop ? prop : "";
    last_diagnostics.push_back(std::move(d));
}

inline bool DrawCtx::HasEvent(const char* event) const {
    for (const auto& e : node.events)
        if (e.first == event && (e.second.handler || !e.second.actions.empty())) return true;
    return false;
}

inline std::string DrawCtx::ScopeKey() const {
    if (!instance_node) return std::string();
    std::string k = outer ? outer->ScopeKey() : std::string();
    return k + instance_node->id + "/";
}

inline bool DrawCtx::ResolveParamValue(const std::string& param, Value& out) const {
    const Param* def = interp_detail::FindParam(component, param);
    if (!def || !instance_node) return false;
    const PropValue* pv = instance_node->Prop(param.c_str());
    Value::Kind hint = interp_detail::KindForTypeName(def->type);
    if (!pv) {
        if (def->def.IsNull()) return false;
        out = Value::FromJson(def->def, hint);
        return true;
    }
    if (!outer) return false;
    if (pv->IsParam()) return outer->ResolveParamValue(pv->Param(), out);
    if (pv->IsExpr()) return outer->Eval(pv->Expr(), out);
    if (interp_detail::IsBindingParamType(def->type)) {
        if (!pv->raw.IsString()) {
            out = Value::FromJson(pv->raw);
            return true;
        }
        return outer->ReadPath(pv->raw.AsString(), out);
    }
    if (pv->raw.IsString() && hint == Value::Kind::String) {
        out = Value(outer->Interpolate(pv->raw.AsString()));
        return true;
    }
    out = Value::FromJson(pv->raw, hint);
    return true;
}

inline bool DrawCtx::ResolveParamStorage(const std::string& param, void** ptr, CType* type) const {
    if (!instance_node || !outer) return false;
    const PropValue* pv = instance_node->Prop(param.c_str());
    if (!pv) return false;
    if (pv->IsParam()) return outer->ResolveParamStorage(pv->Param(), ptr, type);
    if (pv->raw.IsString()) return outer->ResolveStorage(pv->raw.AsString(), ptr, type);
    return false;
}

inline bool DrawCtx::ResolveStorage(const std::string& path, void** ptr, CType* type) const {
    const char* s = path.c_str();
    const size_t n = interp_detail::RootLength(s);
    if (n == 0) return false;
    const char* rest = s + n;
    if (*rest == '.') ++rest;
    if (n == 5 && std::memcmp(s, "state", 5) == 0) {
        const size_t m = interp_detail::RootLength(rest);
        Value* v = interp_detail::FindState(inst, rest, m);
        if (!v) return false;
        void* base = nullptr;
        CType t;
        if (!interp_detail::ValueStorage(*v, &base, &t)) return false;
        const char* tail = rest + m;
        if (*tail == '.') ++tail;
        if (!*tail) {
            *ptr = base;
            *type = t;
            return true;
        }
        return interp_detail::ComponentOf(base, t, tail, ptr, type);
    }
    if (component && interp_detail::FindParam(component, s, n)) {
        void* base = nullptr;
        CType t;
        if (!ResolveParamStorage(std::string(s, n), &base, &t)) return false;
        if (!*rest) {
            *ptr = base;
            *type = t;
            return true;
        }
        if (t.kind == CKind::Struct && t.strct) {
            std::string err;
            return ResolvePath(base, t.strct, rest, ptr, type, &err);
        }
        return interp_detail::ComponentOf(base, t, rest, ptr, type);
    }
    for (const BindingRoot& r : roots) {
        if (r.name.size() != n || std::memcmp(r.name.c_str(), s, n) != 0) continue;
        std::string err;
        return ResolvePath(r.ptr, r.type, rest, ptr, type, &err);
    }
    return false;
}

inline bool DrawCtx::ReadPath(const std::string& path, Value& out) const {
    const char* s = path.c_str();
    const size_t n = interp_detail::RootLength(s);
    if (n == 0) return false;
    const char* rest = s + n;
    if (*rest == '.') ++rest;
    if (n == 5 && std::memcmp(s, "state", 5) == 0) {
        const size_t m = interp_detail::RootLength(rest);
        Value* v = interp_detail::FindState(inst, rest, m);
        if (!v) {
            if (Value* mock = inst.Mock(path)) {
                out = *mock;
                return true;
            }
            return false;
        }
        if (!rest[m]) {
            out = *v;
            return true;
        }
    } else if (component && interp_detail::FindParam(component, s, n)) {
        void* p = nullptr;
        CType t;
        if (ResolveStorage(path, &p, &t)) return ReadValue(p, t, out);
        const std::string root(s, n);
        if (*rest) {
            Value base;
            if (!ResolveParamValue(root, base)) return false;
            Vec4 v = base.ToVec4();
            const char c = rest[1] ? '\0' : rest[0];
            float comp = c == 'x' ? v.x : c == 'y' ? v.y : c == 'z' ? v.z : c == 'w' ? v.w : 0.0f;
            out = Value(comp);
            return true;
        }
        return ResolveParamValue(root, out);
    }
    void* p = nullptr;
    CType t;
    if (ResolveStorage(path, &p, &t)) return ReadValue(p, t, out);
    if (Design()) {
        if (Value* m = inst.Mock(path)) {
            out = *m;
            return true;
        }
    }
    return false;
}

inline bool DrawCtx::WritePath(const std::string& path, const Value& v) const {
    void* p = nullptr;
    CType t;
    if (ResolveStorage(path, &p, &t)) return WriteValue(p, t, v);
    if (Design() || path.compare(0, 6, "state.") == 0) {
        inst.MockOrCreate(path, v) = v;
        return true;
    }
    return false;
}

inline bool DrawCtx::Eval(const std::string& src, Value& out) const {
    interp_detail::CachedExpr local;
    interp_detail::CachedExpr* cep = nullptr;
    auto it = interp.exprs.find(src);
    if (it != interp.exprs.end()) cep = &it->second;
    else if (interp.exprs.size() < interp_detail::kMaxCachedExprs) cep = &interp.exprs[src];
    else cep = &local;
    interp_detail::CachedExpr& ce = *cep;
    if (!ce.parsed) {
        ce.parsed = true;
        ce.ok = expr::Parse(src, ce.ast, &ce.err);
    }
    if (!ce.ok) {
        Diag(Diagnostic::Level::Error, "expression '" + src + "': " + ce.err.message);
        return false;
    }
    interp_detail::PathEnv env(this);
    Error err;
    if (!expr::Eval(ce.ast, env, out, &err)) {
        Diag(Design() ? Diagnostic::Level::Warning : Diagnostic::Level::Error,
             "cannot evaluate '" + src + "': " + err.message);
        return false;
    }
    return true;
}

inline std::string DrawCtx::Interpolate(const std::string& text) const {
    if (text.find('{') == std::string::npos) return text;
    interp_detail::CachedInterp local;
    interp_detail::CachedInterp* cip = nullptr;
    auto it = interp.interps.find(text);
    if (it != interp.interps.end()) cip = &it->second;
    else if (interp.interps.size() < interp_detail::kMaxCachedExprs) cip = &interp.interps[text];
    else cip = &local;
    interp_detail::CachedInterp& ci = *cip;
    if (!ci.parsed) {
        ci.parsed = true;
        ci.ok = expr::ParseInterpolation(text, ci.segs, &ci.err);
    }
    if (!ci.ok) {
        Diag(Diagnostic::Level::Warning, "interpolation '" + text + "': " + ci.err.message);
        return text;
    }
    std::string out;
    for (const expr::InterpSegment& seg : ci.segs) {
        if (seg.is_icon) {
            if (interp.hooks.icon)
                if (const char* s = interp.hooks.icon(seg.text_or_expr)) out += s;
        } else if (seg.is_expr) {
            Value v;
            out += Eval(seg.text_or_expr, v) ? expr::FormatValue(v, seg.fmt) : std::string("?");
        } else {
            out += seg.text_or_expr;
        }
    }
    return out;
}

inline bool DrawCtx::GetValue(const char* prop, Value& out, Value::Kind hint) const {
    const PropValue* pv = node.Prop(prop);
    if (!pv) {
        const Json* d = interp.PropDefault(node.type, prop);
        if (!d) return false;
        out = Value::FromJson(*d, hint);
        return true;
    }
    if (pv->IsParam()) {
        if (!component) {
            Diag(Diagnostic::Level::Error, "{\"param\"} used outside a component", prop);
            return false;
        }
        if (!ResolveParamValue(pv->Param(), out)) {
            Diag(Diagnostic::Level::Warning, "unresolved param '" + pv->Param() + "'", prop);
            return false;
        }
        if (hint != Value::Kind::Null && out.kind != hint && hint != Value::Kind::List) out = Value::FromJson(out.ToJson(), hint);
        return true;
    }
    if (pv->IsExpr()) return Eval(pv->Expr(), out);
    out = Value::FromJson(pv->raw, hint);
    return true;
}

inline bool DrawCtx::GetBool(const char* prop, bool def) const {
    Value v;
    return GetValue(prop, v, Value::Kind::Bool) ? v.ToBool() : def;
}

inline int DrawCtx::GetInt(const char* prop, int def) const {
    Value v;
    return GetValue(prop, v, Value::Kind::Int) ? static_cast<int>(v.ToInt()) : def;
}

inline float DrawCtx::GetFloat(const char* prop, float def) const {
    Value v;
    return GetValue(prop, v, Value::Kind::Float) ? static_cast<float>(v.ToDouble()) : def;
}

inline std::string DrawCtx::GetString(const char* prop, const char* def) const {
    const PropValue* pv = node.Prop(prop);
    if (pv && pv->raw.IsString()) return Interpolate(pv->raw.AsString());
    Value v;
    if (!GetValue(prop, v, Value::Kind::String)) return def ? def : "";
    if (!pv && v.IsString()) return Interpolate(v.s);
    return v.ToString();
}

inline ImVec4 DrawCtx::GetColor(const char* prop, ImVec4 def) const {
    Value v;
    if (!GetValue(prop, v)) return def;
    return interp_detail::ValueToColor(v, def);
}

inline ImVec2 DrawCtx::GetVec2(const char* prop, ImVec2 def) const {
    Value v;
    if (!GetValue(prop, v)) return def;
    if (v.IsNull() || v.IsString() || v.IsBool()) return def;
    Vec2 r = v.ToVec2();
    return ImVec2(r.x, r.y);
}

inline bool DrawCtx::GetSizeSpec(const char* prop, SizeSpec& w, SizeSpec& h) const {
    w = SizeSpec();
    h = SizeSpec();
    const PropValue* pv = node.Prop(prop);
    const Json* j = nullptr;
    if (pv && !pv->IsParam() && !pv->IsExpr()) j = &pv->raw;
    else if (!pv) j = interp.PropDefault(node.type, prop);
    if (j) return interp_detail::ParseSizeJson(*j, w, h);
    Value v;
    if (!GetValue(prop, v)) return false;
    return interp_detail::ParseSizeJson(v.ToJson(), w, h);
}

inline ImVec2 DrawCtx::GetSize(const char* prop) const {
    SizeSpec w, h;
    GetSizeSpec(prop, w, h);
    ImVec2 r = interp_detail::ResolveSize(w, h, S());
    if (w.mode == SizeSpec::Mode::Fill && fill_width >= 0.0f) r.x = interp_detail::MaxF(fill_width, 1.0f);
    if (size_scale_w != 1.0f && r.x > 0.0f) r.x *= size_scale_w;
    if (size_scale_h != 1.0f && r.y > 0.0f) r.y *= size_scale_h;
    return r;
}

inline int DrawCtx::GetFlags(const char* prop, const char* enum_name) const {
    Value v;
    if (!GetValue(prop, v, Value::Kind::List)) return 0;
    size_t n = 0;
    const FlagDef* defs = FlagsFor(enum_name, n);
    int flags = 0;
    Value single;
    const std::vector<Value>* names = &v.list;
    if (v.IsString()) {
        single = Value(std::vector<Value>{v});
        names = &single.list;
    }
    for (const Value& e : *names) {
        std::string tmp;
        const std::string& name = e.IsString() ? e.s : (tmp = e.ToString());
        if (name.empty()) continue;
        const FlagDef* def = nullptr;
        for (size_t i = 0; i < n; ++i) {
            if (name == defs[i].name) {
                def = &defs[i];
                break;
            }
        }
        int value = 0;
        switch (interp_detail::ResolveFlag(enum_name, name.c_str(), value)) {
        case interp_detail::FlagLookup::Found: flags |= value; break;
        case interp_detail::FlagLookup::NeedsVersion:
            interp_detail::InfoOnce(*this, std::string(enum_name) + "_" + name + " needs Dear ImGui " +
                                               interp_detail::ImGuiVersionText(value) + "; ignored", prop);
            break;
        case interp_detail::FlagLookup::UnknownName:
            if (!def) Diag(Diagnostic::Level::Warning, "unknown flag '" + name + "' for " + enum_name, prop);
            else interp_detail::InfoOnce(*this, std::string(enum_name) + "_" + name + " is not available in Dear ImGui " IMGUI_VERSION "; ignored", prop);
            break;
        case interp_detail::FlagLookup::UnknownEnum:
            if (!def) Diag(Diagnostic::Level::Warning, "unknown flag '" + name + "' for " + enum_name, prop);
            else if (def->min_version <= IMGUI_VERSION_NUM) flags |= def->value;
            break;
        }
    }
    return flags;
}

inline int DrawCtx::GetEnum(const char* prop, const char* values, int def) const {
    Value v;
    if (!GetValue(prop, v)) return def;
    if (v.IsInt()) return static_cast<int>(v.i);
    std::string name = v.ToString();
    const char* p = values ? values : "";
    int idx = 0;
    while (*p) {
        const char* e = std::strchr(p, '|');
        size_t len = e ? static_cast<size_t>(e - p) : std::strlen(p);
        if (name.size() == len && std::strncmp(p, name.c_str(), len) == 0) return idx;
        if (!e) break;
        p = e + 1;
        ++idx;
    }
    Diag(Diagnostic::Level::Warning, "unknown enum value '" + name + "'", prop);
    return def;
}

inline std::vector<std::string> DrawCtx::GetStringList(const char* prop) const {
    std::vector<std::string> out;
    Value v;
    if (!GetValue(prop, v, Value::Kind::List)) return out;
    for (const Value& e : v.list) out.push_back(e.IsString() ? Interpolate(e.s) : e.ToString());
    return out;
}

inline std::string DrawCtx::BindingPath(const char* slot) const {
    const PropValue* b = node.Bind(slot);
    if (!b) return std::string();
    if (b->IsParam()) {
        if (!instance_node) return std::string();
        const PropValue* pv = instance_node->Prop(b->Param().c_str());
        if (pv && pv->raw.IsString()) return pv->raw.AsString();
        return std::string();
    }
    return b->raw.IsString() ? b->raw.AsString() : std::string();
}

inline bool DrawCtx::MockStorage(const std::string& key, const char* slot, void** ptr, CType* type) const {
    const BindDef* bd = interp_detail::FindBindDef(Def(), slot);
    interp_detail::MockType mt = interp_detail::MockTypeOf(bd ? bd->types : nullptr);
    if ((mt.kind == interp_detail::MockKind::Float || mt.kind == interp_detail::MockKind::Int) && bd &&
        std::strstr(bd->types, "[N]")) {
        int n = GetInt("components", 1);
        if (n > 1) {
            mt.kind = mt.kind == interp_detail::MockKind::Float ? interp_detail::MockKind::FloatArray
                                                                : interp_detail::MockKind::IntArray;
            mt.count = n > 4 ? 4 : n;
        }
    }
    Value* v = inst.Mock(key);
    if (!v) {
        Value init = interp_detail::MakeMockValue(mt);
        const PropValue* b = node.Bind(slot);
        if (b && b->IsParam()) {
            Value pv;
            if (ResolveParamValue(b->Param(), pv) && !pv.IsNull() && !pv.IsList()) init = pv;
        }
        v = &inst.MockOrCreate(key, init);
    }
    return interp_detail::MockValueStorage(*v, mt, ptr, type);
}

inline bool DrawCtx::Binding(const char* slot, void** ptr, CType* type) const {
    const PropValue* b = node.Bind(slot);
    if (!b) {
        if (Design()) return MockStorage(ScopeKey() + node.id + "." + slot, slot, ptr, type);
        return false;
    }
    if (b->IsParam()) {
        if (!component || !instance_node) {
            Diag(Diagnostic::Level::Error, "{\"param\"} binding outside a component", slot);
            return false;
        }
        if (ResolveParamStorage(b->Param(), ptr, type)) return true;
        std::string key = Design() ? interp_detail::ParamPath(*this, b->Param()) : std::string();
        return MockStorage(key.empty() ? ScopeKey() + "@" + b->Param() : key, slot, ptr, type);
    }
    const std::string& path = b->raw.AsString();
    if (!path.empty() && ResolveStorage(path, ptr, type)) return true;
    if (Design()) return MockStorage(path.empty() ? ScopeKey() + node.id + "." + slot : path, slot, ptr, type);
    Diag(Diagnostic::Level::Error, "cannot resolve binding '" + path + "'", slot);
    return false;
}

inline void* DrawCtx::ShadowFor(void* dst, const CType& t, bool as_int, int count) const {
    if (count != 1 || shadow_count >= 4) return nullptr;
    interp_detail::Shadow& s = shadows[shadow_count++];
    s.dst = dst;
    s.type = t;
    s.as_int = as_int;
    Value v;
    ReadValue(dst, t, v);
    s.i = s.i0 = static_cast<int>(v.ToInt());
    s.f = s.f0 = static_cast<float>(v.ToDouble());
    return as_int ? static_cast<void*>(&s.i) : static_cast<void*>(&s.f);
}

inline void DrawCtx::FlushShadows() const {
    for (int k = 0; k < shadow_count; ++k) {
        interp_detail::Shadow& s = shadows[k];
        if (s.as_int && s.i != s.i0) WriteValue(s.dst, s.type, Value(static_cast<long long>(s.i)));
        else if (!s.as_int && s.f != s.f0) WriteValue(s.dst, s.type, Value(static_cast<double>(s.f)));
        s.i0 = s.i;
        s.f0 = s.f;
    }
    shadow_count = 0;
}

inline bool* DrawCtx::BindBool(const char* slot) const {
    void* p = nullptr;
    CType t;
    if (!Binding(slot, &p, &t)) return nullptr;
    if (t.kind == CKind::Bool) return static_cast<bool*>(p);
    Diag(Diagnostic::Level::Error, "binding '" + BindingPath(slot) + "' is not a bool (" + t.name + ")", slot);
    return nullptr;
}

inline int* DrawCtx::BindInt(const char* slot) const {
    void* p = nullptr;
    CType t;
    if (!Binding(slot, &p, &t)) return nullptr;
    if (t.kind == CKind::I32 || t.kind == CKind::U32) return static_cast<int*>(p);
    if (t.kind == CKind::IntArray && t.count >= 1) return static_cast<int*>(p);
    if (interp_detail::IsIntKind(t.kind) || t.kind == CKind::Float || t.kind == CKind::Double)
        return static_cast<int*>(ShadowFor(p, t, true, 1));
    Diag(Diagnostic::Level::Error, "binding '" + BindingPath(slot) + "' is not an int (" + t.name + ")", slot);
    return nullptr;
}

inline float* DrawCtx::BindFloat(const char* slot, int count) const {
    void* p = nullptr;
    CType t;
    if (!Binding(slot, &p, &t)) return nullptr;
    switch (t.kind) {
    case CKind::Float:
        if (count == 1) return static_cast<float*>(p);
        break;
    case CKind::Vec2:
        if (count <= 2) return static_cast<float*>(p);
        break;
    case CKind::Vec4:
        if (count <= 4) return static_cast<float*>(p);
        break;
    case CKind::FloatArray:
        if (count <= t.count) return static_cast<float*>(p);
        break;
    case CKind::Vector:
        if (t.elem && t.elem->kind == CKind::Float && t.vec_data && t.vec_size &&
            static_cast<int>(t.vec_size(p)) >= count)
            return static_cast<float*>(t.vec_data(p));
        break;
    default:
        if (count == 1 && (t.kind == CKind::Double || interp_detail::IsIntKind(t.kind)))
            return static_cast<float*>(ShadowFor(p, t, false, 1));
        break;
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "' does not hold %d float(s) (", count);
    Diag(Diagnostic::Level::Error, "binding '" + BindingPath(slot) + buf + t.name + ")", slot);
    return nullptr;
}

inline float* DrawCtx::BindFloatArray(const char* slot, int* count) const {
    void* p = nullptr;
    CType t;
    if (count) *count = 0;
    if (!Binding(slot, &p, &t)) return nullptr;
    int n = 0;
    float* data = nullptr;
    switch (t.kind) {
    case CKind::Float: n = 1; data = static_cast<float*>(p); break;
    case CKind::Vec2: n = 2; data = static_cast<float*>(p); break;
    case CKind::Vec4: n = 4; data = static_cast<float*>(p); break;
    case CKind::FloatArray: n = t.count; data = static_cast<float*>(p); break;
    case CKind::Vector:
        if (t.elem && t.elem->kind == CKind::Float && t.vec_data && t.vec_size) {
            n = static_cast<int>(t.vec_size(p));
            data = static_cast<float*>(t.vec_data(p));
        }
        break;
    default: break;
    }
    if (!data) {
        Diag(Diagnostic::Level::Error, "binding '" + BindingPath(slot) + "' is not a float array (" + t.name + ")", slot);
        return nullptr;
    }
    if (count) *count = n;
    return data;
}

inline std::string* DrawCtx::BindStdString(const char* slot) const {
    void* p = nullptr;
    CType t;
    if (!Binding(slot, &p, &t)) return nullptr;
    if (t.kind == CKind::StdString) return static_cast<std::string*>(p);
    if (t.kind != CKind::CharArray)
        Diag(Diagnostic::Level::Error, "binding '" + BindingPath(slot) + "' is not a string (" + t.name + ")", slot);
    return nullptr;
}

inline char* DrawCtx::BindCharArray(const char* slot, int* cap) const {
    void* p = nullptr;
    CType t;
    if (cap) *cap = 0;
    if (!Binding(slot, &p, &t)) return nullptr;
    if (t.kind != CKind::CharArray) return nullptr;
    if (cap) *cap = t.count;
    return static_cast<char*>(p);
}

inline ImTextureID DrawCtx::GetTexture(const char* slot) const {
    void* p = nullptr;
    CType t;
    const PropValue* b = node.Bind(slot);
    if (b && b->raw.IsString() && ResolveStorage(b->raw.AsString(), &p, &t) && t.kind == CKind::TexID) {
        ImTextureID tex;
        std::memcpy(&tex, p, sizeof(tex));
        return tex;
    }
    if (b && interp.hooks.texture) {
        std::string name = BindingPath(slot);
        if (!name.empty()) return interp.hooks.texture(name);
    }
    return ImTextureID();
}

inline void DrawCtx::FireEvent(const char* event) {
    FlushShadows();
    const EventSpec* ev = nullptr;
    for (const auto& e : node.events)
        if (e.first == event) ev = &e.second;
    if (!ev || Design()) return;
    bool run_handler = ev->handler;
    for (const Action& a : ev->actions) {
        if (a.type == "set") {
            Value v;
            if (a.value.IsObject() && a.value["expr"].IsString()) {
                if (!Eval(a.value["expr"].AsString(), v)) continue;
            } else {
                v = Value::FromJson(a.value);
                if (v.IsString()) v = Value(Interpolate(v.s));
            }
            if (!WritePath(a.target, v)) Diag(Diagnostic::Level::Error, "set: cannot write '" + a.target + "'", event);
        } else if (a.type == "toggle") {
            Value v;
            if (!ReadPath(a.target, v) || !WritePath(a.target, Value(!v.ToBool())))
                Diag(Diagnostic::Level::Error, "toggle: cannot access '" + a.target + "'", event);
        } else if (a.type == "openPopup") {
            inst.pending_popups.push_back(a.target);
        } else if (a.type == "closePopup") {
            ImGui::CloseCurrentPopup();
        } else if (a.type == "setTab") {
            inst.pending_tab.emplace_back(a.target, a.value.AsString(""));
        } else if (a.type == "copyClipboard") {
            Value v;
            std::string text;
            if (a.value.IsObject() && a.value["expr"].IsString()) {
                if (Eval(a.value["expr"].AsString(), v)) text = v.ToString();
            } else {
                text = a.value.IsString() ? Interpolate(a.value.AsString()) : Value::FromJson(a.value).ToString();
            }
            ImGui::SetClipboardText(text.c_str());
        } else if (a.type == "handler") {
            run_handler = true;
        } else {
            Diag(Diagnostic::Level::Warning, "unknown action type '" + a.type + "'", event);
        }
    }
    if (run_handler && opts.run_handlers) {
        if (HandlerFn fn = interp.FindHandler(panel.name + "." + node.id + "." + event)) fn(*this);
    }
}

inline void DrawCtx::DrawChildren() { interp.DrawChildrenOf(*this); }

}  // namespace imforge

#endif
