#pragma once

// small helpers for packs/<pack>/imforge_adapters_<pack>.h, see packs/ADAPTERS.md

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

// imgui.h first: imforge_value.h only exposes its ImVec4 support when IMGUI_VERSION is already defined
#include "imgui.h"
#include "imforge_interp.h"

namespace imforge {
namespace pack {

struct Entry {
    const char* type;
    WidgetDrawFn draw;
    WidgetEndFn end;
};

template <size_t N> inline void RegisterAll(Interpreter& in, const Entry (&entries)[N]) {
    for (const Entry& e : entries) in.RegisterWidget(e.type, e.draw, e.end);
}

inline bool Fire(DrawCtx& ctx, const char* event, bool triggered) {
    if (triggered) ctx.FireEvent(event);
    return triggered;
}

// "text###node_id": visible text can change (interpolation) without resetting the widget's state
inline std::string Label(const DrawCtx& ctx, const char* prop = "label") {
    return ctx.GetString(prop) + "###" + ctx.node.id;
}

inline std::string Text(const DrawCtx& ctx, const char* prop) { return ctx.GetString(prop); }

inline float Float(const DrawCtx& ctx, const char* prop, float def = 0.0f) { return ctx.GetFloat(prop, def); }
inline int Int(const DrawCtx& ctx, const char* prop, int def = 0) { return ctx.GetInt(prop, def); }
inline bool Bool(const DrawCtx& ctx, const char* prop, bool def = false) { return ctx.GetBool(prop, def); }

inline int IntClamped(const DrawCtx& ctx, const char* prop, int lo, int hi, int def = 0) {
    int v = ctx.GetInt(prop, def);
    return v < lo ? lo : v > hi ? hi : v;
}

// pixel props, multiplied by the dpi scale like the stock widgets do
inline float Px(const DrawCtx& ctx, const char* prop, float def = 0.0f) { return ctx.GetFloat(prop, def) * ctx.S(); }

// #00000000 comes back as 0, which most pack widgets read as "use the style default"
inline ImU32 Color(const DrawCtx& ctx, const char* prop, ImU32 def = 0) {
    return ImGui::ColorConvertFloat4ToU32(ctx.GetColor(prop, ImGui::ColorConvertU32ToFloat4(def)));
}

inline ImVec4 ColorVec(const DrawCtx& ctx, const char* prop, const ImVec4& def = ImVec4(0, 0, 0, 0)) {
    return ctx.GetColor(prop, def);
}

inline ImVec2 Vec2(const DrawCtx& ctx, const char* prop, const ImVec2& def = ImVec2(0, 0)) { return ctx.GetVec2(prop, def); }

// ImGui-style size: 0 auto, negative fills (like BeginChild / InvisibleButton take it)
inline ImVec2 Size(const DrawCtx& ctx, const char* prop = "size") { return ctx.GetSize(prop); }

// same, with fill turned into the actual available width/height (for widgets that need positive sizes)
inline ImVec2 SizeResolved(const DrawCtx& ctx, const char* prop = "size") {
    ImVec2 s = ctx.GetSize(prop);
    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (s.x < 0) s.x = avail.x + s.x > 1.0f ? avail.x + s.x : 1.0f;
    if (s.y < 0) s.y = avail.y + s.y > 1.0f ? avail.y + s.y : 1.0f;
    return s;
}

// index into the manifest's "values" of an enum prop
inline int Enum(const DrawCtx& ctx, const char* prop, int def = 0) {
    const WidgetDef* d = ctx.Def();
    const PropDef* p = d ? FindProp(*d, prop) : nullptr;
    return ctx.GetEnum(prop, p && p->enum_values ? p->enum_values : "", def);
}

inline std::vector<std::string> List(const DrawCtx& ctx, const char* prop) { return ctx.GetStringList(prop); }

// "a\0b\0c\0\0", the way ImGui::Combo and the pack overloads take a stringList.
// Empty entries become a space so they don't end the list early.
inline std::string ZeroList(const std::vector<std::string>& items) {
    std::string out;
    for (const std::string& s : items) {
        out += s.empty() ? std::string(" ") : s;
        out.push_back('\0');
    }
    out.push_back('\0');
    return out;
}

inline std::string ZeroList(const DrawCtx& ctx, const char* prop) { return ZeroList(ctx.GetStringList(prop)); }

// icon names ("fa-gear") through the host's icon hook, anything else unchanged
inline std::string Icon(const DrawCtx& ctx, const std::string& name) {
    if (!name.empty() && ctx.interp.hooks.icon)
        if (const char* s = ctx.interp.hooks.icon(name)) return s;
    return name;
}

inline std::string IconZeroList(const DrawCtx& ctx, const char* prop) {
    std::vector<std::string> items = ctx.GetStringList(prop);
    for (std::string& s : items) s = Icon(ctx, s);
    return ZeroList(items);
}

// `expr` props: a bare string is an expression ("cfg.menu_key"), numbers and bools are literals
inline bool ExprValue(const DrawCtx& ctx, const char* prop, Value& out) {
    const PropValue* pv = ctx.node.Prop(prop);
    if (pv && pv->raw.IsString() && !pv->raw.AsString().empty()) return ctx.Eval(pv->raw.AsString(), out);
    if (pv && !pv->raw.IsNull()) return ctx.GetValue(prop, out);
    const Json* d = ctx.interp.PropDefault(ctx.node.type, prop);
    if (d && d->IsString() && !d->AsString().empty()) return ctx.Eval(d->AsString(), out);
    if (d) {
        out = Value::FromJson(*d);
        return true;
    }
    return false;
}

inline int ExprInt(const DrawCtx& ctx, const char* prop, int def = 0) {
    Value v;
    return ExprValue(ctx, prop, v) ? static_cast<int>(v.ToInt()) : def;
}

inline float ExprFloat(const DrawCtx& ctx, const char* prop, float def = 0.0f) {
    Value v;
    return ExprValue(ctx, prop, v) ? static_cast<float>(v.ToDouble()) : def;
}

// Fallback storage for unbound slots, kept per panel instance + node. std::map nodes never move,
// so pointers stay valid while other slots of the same widget are created.
inline std::string& LocalBytes(const DrawCtx& ctx, const char* slot, size_t bytes, const void* init) {
    static std::map<std::string, std::string> store;
    char inst[32];
    std::snprintf(inst, sizeof(inst), "%p/", static_cast<const void*>(&ctx.inst));
    std::string& s = store[std::string(inst) + ctx.ScopeKey() + ctx.node.id + "." + slot];
    if (s.size() != bytes) {
        s.assign(bytes, '\0');
        if (init && bytes) std::memcpy(&s[0], init, bytes);
    }
    return s;
}

template <class T> inline T* Local(const DrawCtx& ctx, const char* slot, const T& init = T()) {
    return reinterpret_cast<T*>(&LocalBytes(ctx, slot, sizeof(T), &init)[0]);
}

// design mode: give an unresolved binding a nicer mock start value than false/0. Call it before
// taking any other pointer for this widget (it may add a mock entry).
inline void SeedMock(const DrawCtx& ctx, const char* slot, const Value& init) {
    if (!ctx.Design()) return;
    const PropValue* b = ctx.node.Bind(slot);
    if (b && b->IsParam()) return;
    std::string path = b && b->raw.IsString() ? b->raw.AsString() : std::string();
    if (!path.empty()) {
        void* p = nullptr;
        CType t;
        if (ctx.ResolveStorage(path, &p, &t)) return;
    }
    std::string key = path.empty() ? ctx.ScopeKey() + ctx.node.id + "." + slot : path;
    if (!ctx.inst.Mock(key)) ctx.inst.mock.emplace_back(key, init);
}

// Required slots: the bound variable, the builder's mock in design mode, or local storage when the
// live panel has no binding, so the widget always works.
inline bool* BindBool(const DrawCtx& ctx, const char* slot, bool init = false) {
    if (ctx.HasBinding(slot) || ctx.Design())
        if (bool* v = ctx.BindBool(slot)) return v;
    return Local<bool>(ctx, slot, init);
}

inline int* BindInt(const DrawCtx& ctx, const char* slot, int init = 0) {
    if (ctx.HasBinding(slot) || ctx.Design())
        if (int* v = interp_widgets::IntStorage(ctx, slot, 1)) return v;
    return Local<int>(ctx, slot, init);
}

inline float* BindFloat(const DrawCtx& ctx, const char* slot, float init = 0.0f) {
    if (ctx.HasBinding(slot) || ctx.Design())
        if (float* v = interp_widgets::FloatStorage(ctx, slot, 1)) return v;
    return Local<float>(ctx, slot, init);
}

// n floats (vec2/vec3/vec4/float[N]); `init` may be null
inline float* BindFloats(const DrawCtx& ctx, const char* slot, int n, const float* init = nullptr) {
    if (ctx.HasBinding(slot) || ctx.Design())
        if (float* v = interp_widgets::FloatStorage(ctx, slot, n)) return v;
    float zero[16] = {};
    if (n > 16) n = 16;
    return reinterpret_cast<float*>(&LocalBytes(ctx, slot, sizeof(float) * static_cast<size_t>(n), init ? init : zero)[0]);
}

inline std::string* BindString(const DrawCtx& ctx, const char* slot, const char* init = "") {
    if (ctx.HasBinding(slot) || ctx.Design())
        if (std::string* v = ctx.BindStdString(slot)) return v;
    static std::map<std::string, std::string> store;
    char inst[32];
    std::snprintf(inst, sizeof(inst), "%p/", static_cast<const void*>(&ctx.inst));
    auto it = store.find(std::string(inst) + ctx.ScopeKey() + ctx.node.id + "." + slot);
    if (it == store.end()) it = store.emplace(std::string(inst) + ctx.ScopeKey() + ctx.node.id + "." + slot, init ? init : "").first;
    return &it->second;
}

// Optional slots: null when the node doesn't bind them, same as generated code passing nullptr
inline bool* OptBool(const DrawCtx& ctx, const char* slot) { return ctx.HasBinding(slot) ? BindBool(ctx, slot) : nullptr; }
inline int* OptInt(const DrawCtx& ctx, const char* slot) { return ctx.HasBinding(slot) ? BindInt(ctx, slot) : nullptr; }
inline float* OptFloat(const DrawCtx& ctx, const char* slot) { return ctx.HasBinding(slot) ? BindFloat(ctx, slot) : nullptr; }

// Array slots ("vec4[N]", "float[N]", "int[N]", std::vector of those). `count` is how many elements
// the widget wants; it comes back clamped to what the bound storage holds. Unresolved bindings and
// design mode without data get local storage of `count` elements filled from `seed` (cycled).
inline ImVec4* BindVec4Array(const DrawCtx& ctx, const char* slot, int& count, const ImVec4* seed = nullptr,
                             int seed_count = 0) {
    if (count < 1) count = 1;
    const PropValue* b = ctx.node.Bind(slot);
    void* p = nullptr;
    CType t;
    bool found = false;
    if (b && b->IsParam()) found = ctx.ResolveParamStorage(b->Param(), &p, &t);
    else if (b && b->raw.IsString() && !b->raw.AsString().empty()) found = ctx.ResolveStorage(b->raw.AsString(), &p, &t);
    if (found && p) {
        int have = 0;
        ImVec4* data = nullptr;
        if (t.kind == CKind::Vec4) {
            have = 1;
            data = static_cast<ImVec4*>(p);
        } else if (t.kind == CKind::Array && t.elem && t.elem->kind == CKind::Vec4) {
            have = t.count;
            data = static_cast<ImVec4*>(p);
        } else if (t.kind == CKind::Vector && t.elem && t.elem->kind == CKind::Vec4 && t.vec_data && t.vec_size) {
            have = static_cast<int>(t.vec_size(p));
            data = static_cast<ImVec4*>(t.vec_data(p));
        } else if (t.kind == CKind::FloatArray && t.count >= 4) {
            have = t.count / 4;
            data = static_cast<ImVec4*>(p);
        }
        if (data && have > 0) {
            if (count > have) count = have;
            return data;
        }
        ctx.Diag(Diagnostic::Level::Error, "binding '" + ctx.BindingPath(slot) + "' is not an ImVec4 array (" + t.name + ")", slot);
    } else if (b && !ctx.Design()) {
        ctx.Diag(Diagnostic::Level::Error, "cannot resolve binding '" + ctx.BindingPath(slot) + "'", slot);
    }
    std::vector<ImVec4> init(static_cast<size_t>(count), ImVec4(1, 1, 1, 1));
    if (seed && seed_count > 0)
        for (int k = 0; k < count; ++k) init[static_cast<size_t>(k)] = seed[k % seed_count];
    std::string& bytes = LocalBytes(ctx, slot, sizeof(ImVec4) * static_cast<size_t>(count), init.data());
    return reinterpret_cast<ImVec4*>(&bytes[0]);
}

// for widgets that submit no item of their own (overlays, hosts): gives the node a small
// selectable box in the builder canvas, a zero-size rect in the app
inline void DesignPlaceholder(DrawCtx& ctx, const char* text) {
    if (!ctx.Design()) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        ctx.has_rect = true;
        ctx.rect_min = ctx.rect_max = p;
        return;
    }
    ImVec2 ts = ImGui::CalcTextSize(text);
    const ImGuiStyle& st = ImGui::GetStyle();
    ImVec2 sz(ts.x + st.FramePadding.x * 2.0f, ts.y + st.FramePadding.y * 2.0f);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(sz);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 col = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    dl->AddRect(p, ImVec2(p.x + sz.x, p.y + sz.y), col, st.FrameRounding);
    dl->AddText(ImVec2(p.x + st.FramePadding.x, p.y + st.FramePadding.y), col, text);
}

}  // namespace pack
}  // namespace imforge
