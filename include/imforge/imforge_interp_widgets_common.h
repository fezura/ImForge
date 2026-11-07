#ifndef IMFORGE_INTERP_PARTS
#include "imforge_interp.h"
#else
#pragma once

// helpers for imforge_interp_widgets*.h, needs DrawCtx declared already

#include "imgui.h"

#include "imforge_core.h"
#include "imforge_value.h"

#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

namespace imforge {

struct DrawCtx;

namespace interp_widgets {

inline std::string Label(const DrawCtx& ctx, const char* prop = "label") {
    std::string s = ctx.GetString(prop);
    if (s.empty()) s = "##" + ctx.node.id;
    return s;
}

inline bool HasProp(const DrawCtx& ctx, const char* name) {
    const PropValue* pv = ctx.node.Prop(name);
    return pv && !pv->raw.IsNull();
}

inline bool HasBind(const DrawCtx& ctx, const char* slot) { return ctx.node.Bind(slot) != nullptr; }

inline int Components(const DrawCtx& ctx) {
    int n = ctx.GetInt("components", 1);
    return n < 1 ? 1 : n > 4 ? 4 : n;
}

inline const char* SafeFormat(const std::string& fmt, bool is_float, const char* def) {
    int conversions = 0;
    for (const char* p = fmt.c_str(); *p;) {
        if (*p != '%') {
            ++p;
            continue;
        }
        ++p;
        if (*p == '%') {
            ++p;
            continue;
        }
        while (*p && std::strchr("-+ #0", *p)) ++p;
        while (*p >= '0' && *p <= '9') ++p;
        if (*p == '.') {
            ++p;
            while (*p >= '0' && *p <= '9') ++p;
        }
        if (!*p || !std::strchr(is_float ? "fFeEgGaA" : "diouxX", *p)) return def;
        if (++conversions > 1) return def;
        ++p;
    }
    return fmt.c_str();
}

struct Scratch {
    float f[4] = {0, 0, 0, 0};
    int i[4] = {0, 0, 0, 0};

    static ImGuiID Key(const char* prefix, int k) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "##imf_%s%d", prefix, k);
        return ImGui::GetID(buf);
    }
    void LoadF(int n) {
        ImGuiStorage* s = ImGui::GetStateStorage();
        for (int k = 0; k < n; ++k) f[k] = s->GetFloat(Key("f", k), 0.0f);
    }
    void StoreF(int n) {
        ImGuiStorage* s = ImGui::GetStateStorage();
        for (int k = 0; k < n; ++k) s->SetFloat(Key("f", k), f[k]);
    }
    void LoadI(int n) {
        ImGuiStorage* s = ImGui::GetStateStorage();
        for (int k = 0; k < n; ++k) i[k] = s->GetInt(Key("i", k), 0);
    }
    void StoreI(int n) {
        ImGuiStorage* s = ImGui::GetStateStorage();
        for (int k = 0; k < n; ++k) s->SetInt(Key("i", k), i[k]);
    }
    static bool* Bool() { return ImGui::GetStateStorage()->GetBoolRef(Key("b", 0), false); }
};

inline float* FloatStorage(const DrawCtx& ctx, const char* slot, int n) {
    void* p = nullptr;
    CType t;
    if (!ctx.Binding(slot, &p, &t) || !p) return nullptr;
    switch (t.kind) {
    case CKind::Float:
        if (n == 1) return static_cast<float*>(p);
        break;
    case CKind::Vec2:
        if (n <= 2) return static_cast<float*>(p);
        break;
    case CKind::Vec4:
        if (n <= 4) return static_cast<float*>(p);
        break;
    case CKind::FloatArray:
        if (t.count >= n) return static_cast<float*>(p);
        break;
    case CKind::Vector:
        if (t.elem && t.elem->kind == CKind::Float && t.vec_data && t.vec_size &&
            t.vec_size(p) >= static_cast<size_t>(n))
            return static_cast<float*>(t.vec_data(p));
        break;
    default: break;
    }
    return ctx.BindFloat(slot, n);
}

inline int* IntStorage(const DrawCtx& ctx, const char* slot, int n) {
    void* p = nullptr;
    CType t;
    if (!ctx.Binding(slot, &p, &t) || !p) return nullptr;
    switch (t.kind) {
    case CKind::I32:
    case CKind::U32:
        if (n == 1) return static_cast<int*>(p);
        break;
    case CKind::Enum:
        if (n == 1 && t.size == sizeof(int)) return static_cast<int*>(p);
        break;
    case CKind::IntArray:
        if (t.count >= n) return static_cast<int*>(p);
        break;
    case CKind::Vector:
        if (t.elem && (t.elem->kind == CKind::I32 || t.elem->kind == CKind::U32) && t.vec_data &&
            t.vec_size && t.vec_size(p) >= static_cast<size_t>(n))
            return static_cast<int*>(t.vec_data(p));
        break;
    default: break;
    }
    return n == 1 ? ctx.BindInt(slot) : nullptr;
}

inline ImVec2 ClampFill(ImVec2 size) {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (size.x < 0) size.x = avail.x + size.x > 1.0f ? avail.x + size.x : 1.0f;
    if (size.y < 0) size.y = avail.y + size.y > 1.0f ? avail.y + size.y : 1.0f;
    return size;
}

inline bool ReadNumber(const DrawCtx& ctx, const char* slot, double& out) {
    void* p = nullptr;
    CType t;
    Value v;
    if (!ctx.Binding(slot, &p, &t) || !ReadValue(p, t, v)) return false;
    out = v.ToDouble();
    return true;
}

inline bool Fire(DrawCtx& ctx, const char* event, bool triggered) {
    if (triggered) ctx.FireEvent(event);
    return triggered;
}

}  // namespace interp_widgets
}  // namespace imforge

#endif
