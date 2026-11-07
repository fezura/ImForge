#ifndef IMFORGE_INTERP_PARTS
#include "imforge_interp.h"
#else
#pragma once

// sliders, drags, inputs and color editors. needs DrawCtx declared

#include "imforge_interp_widgets_common.h"

namespace imforge {
namespace interp_widgets {

inline bool DrawSliderFloat(DrawCtx& ctx) {
    int n = Components(ctx);
    std::string label = Label(ctx), fmt = ctx.GetString("format", "%.3f");
    const char* format = SafeFormat(fmt, true, "%.3f");
    float lo = ctx.GetFloat("min", 0.0f), hi = ctx.GetFloat("max", 1.0f);
    int flags = ctx.GetFlags("flags", "SliderFlags");
    Scratch sc;
    float* v = FloatStorage(ctx, "value", n);
    if (!v) {
        sc.LoadF(n);
        v = sc.f;
    }
    bool changed = false;
    switch (n) {
    case 1: changed = ImGui::SliderFloat(label.c_str(), v, lo, hi, format, flags); break;
    case 2: changed = ImGui::SliderFloat2(label.c_str(), v, lo, hi, format, flags); break;
    case 3: changed = ImGui::SliderFloat3(label.c_str(), v, lo, hi, format, flags); break;
    default: changed = ImGui::SliderFloat4(label.c_str(), v, lo, hi, format, flags); break;
    }
    if (v == sc.f) sc.StoreF(n);
    return Fire(ctx, "onChange", changed);
}

inline bool DrawSliderInt(DrawCtx& ctx) {
    int n = Components(ctx);
    std::string label = Label(ctx), fmt = ctx.GetString("format", "%d");
    const char* format = SafeFormat(fmt, false, "%d");
    int lo = ctx.GetInt("min", 0), hi = ctx.GetInt("max", 100);
    int flags = ctx.GetFlags("flags", "SliderFlags");
    Scratch sc;
    int* v = IntStorage(ctx, "value", n);
    if (!v) {
        sc.LoadI(n);
        v = sc.i;
    }
    bool changed = false;
    switch (n) {
    case 1: changed = ImGui::SliderInt(label.c_str(), v, lo, hi, format, flags); break;
    case 2: changed = ImGui::SliderInt2(label.c_str(), v, lo, hi, format, flags); break;
    case 3: changed = ImGui::SliderInt3(label.c_str(), v, lo, hi, format, flags); break;
    default: changed = ImGui::SliderInt4(label.c_str(), v, lo, hi, format, flags); break;
    }
    if (v == sc.i) sc.StoreI(n);
    return Fire(ctx, "onChange", changed);
}

inline ImVec2 VSliderSize(const DrawCtx& ctx) {
    ImVec2 size = ClampFill(ctx.GetSize());
    if (size.x <= 0) size.x = ImGui::GetFrameHeight();
    if (size.y <= 0) size.y = 100.0f * ctx.S();
    return size;
}

inline bool DrawVSliderFloat(DrawCtx& ctx) {
    std::string label = Label(ctx), fmt = ctx.GetString("format", "%.3f");
    const char* format = SafeFormat(fmt, true, "%.3f");
    float lo = ctx.GetFloat("min", 0.0f), hi = ctx.GetFloat("max", 1.0f);
    int flags = ctx.GetFlags("flags", "SliderFlags");
    Scratch sc;
    float* v = FloatStorage(ctx, "value", 1);
    if (!v) {
        sc.LoadF(1);
        v = sc.f;
    }
    bool changed = ImGui::VSliderFloat(label.c_str(), VSliderSize(ctx), v, lo, hi, format, flags);
    if (v == sc.f) sc.StoreF(1);
    return Fire(ctx, "onChange", changed);
}

inline bool DrawVSliderInt(DrawCtx& ctx) {
    std::string label = Label(ctx), fmt = ctx.GetString("format", "%d");
    const char* format = SafeFormat(fmt, false, "%d");
    int lo = ctx.GetInt("min", 0), hi = ctx.GetInt("max", 100);
    int flags = ctx.GetFlags("flags", "SliderFlags");
    Scratch sc;
    int* v = IntStorage(ctx, "value", 1);
    if (!v) {
        sc.LoadI(1);
        v = sc.i;
    }
    bool changed = ImGui::VSliderInt(label.c_str(), VSliderSize(ctx), v, lo, hi, format, flags);
    if (v == sc.i) sc.StoreI(1);
    return Fire(ctx, "onChange", changed);
}

inline bool DrawDragFloat(DrawCtx& ctx) {
    int n = Components(ctx);
    std::string label = Label(ctx), fmt = ctx.GetString("format", "%.3f");
    const char* format = SafeFormat(fmt, true, "%.3f");
    float speed = ctx.GetFloat("speed", 1.0f), lo = ctx.GetFloat("min", 0.0f),
          hi = ctx.GetFloat("max", 0.0f);
    int flags = ctx.GetFlags("flags", "SliderFlags");
    Scratch sc;
    float* v = FloatStorage(ctx, "value", n);
    if (!v) {
        sc.LoadF(n);
        v = sc.f;
    }
    bool changed = false;
    switch (n) {
    case 1: changed = ImGui::DragFloat(label.c_str(), v, speed, lo, hi, format, flags); break;
    case 2: changed = ImGui::DragFloat2(label.c_str(), v, speed, lo, hi, format, flags); break;
    case 3: changed = ImGui::DragFloat3(label.c_str(), v, speed, lo, hi, format, flags); break;
    default: changed = ImGui::DragFloat4(label.c_str(), v, speed, lo, hi, format, flags); break;
    }
    if (v == sc.f) sc.StoreF(n);
    return Fire(ctx, "onChange", changed);
}

inline bool DrawDragInt(DrawCtx& ctx) {
    int n = Components(ctx);
    std::string label = Label(ctx), fmt = ctx.GetString("format", "%d");
    const char* format = SafeFormat(fmt, false, "%d");
    float speed = ctx.GetFloat("speed", 1.0f);
    int lo = ctx.GetInt("min", 0), hi = ctx.GetInt("max", 0);
    int flags = ctx.GetFlags("flags", "SliderFlags");
    Scratch sc;
    int* v = IntStorage(ctx, "value", n);
    if (!v) {
        sc.LoadI(n);
        v = sc.i;
    }
    bool changed = false;
    switch (n) {
    case 1: changed = ImGui::DragInt(label.c_str(), v, speed, lo, hi, format, flags); break;
    case 2: changed = ImGui::DragInt2(label.c_str(), v, speed, lo, hi, format, flags); break;
    case 3: changed = ImGui::DragInt3(label.c_str(), v, speed, lo, hi, format, flags); break;
    default: changed = ImGui::DragInt4(label.c_str(), v, speed, lo, hi, format, flags); break;
    }
    if (v == sc.i) sc.StoreI(n);
    return Fire(ctx, "onChange", changed);
}

inline bool DrawInputInt(DrawCtx& ctx) {
    int n = Components(ctx);
    std::string label = Label(ctx);
    int step = ctx.GetInt("step", 1), step_fast = ctx.GetInt("stepFast", 100);
    int flags = interp_detail::InputTextFlagsWithoutCallbacks(ctx.GetFlags("flags", "InputTextFlags"));
    Scratch sc;
    int* v = IntStorage(ctx, "value", n);
    if (!v) {
        sc.LoadI(n);
        v = sc.i;
    }
    bool changed = false;
    switch (n) {
    case 1: changed = ImGui::InputInt(label.c_str(), v, step, step_fast, flags); break;
    case 2: changed = ImGui::InputInt2(label.c_str(), v, flags); break;
    case 3: changed = ImGui::InputInt3(label.c_str(), v, flags); break;
    default: changed = ImGui::InputInt4(label.c_str(), v, flags); break;
    }
    if (v == sc.i) sc.StoreI(n);
    return Fire(ctx, "onChange", changed);
}

inline bool DrawInputFloat(DrawCtx& ctx) {
    int n = Components(ctx);
    std::string label = Label(ctx), fmt = ctx.GetString("format", "%.3f");
    const char* format = SafeFormat(fmt, true, "%.3f");
    float step = ctx.GetFloat("step", 0.0f), step_fast = ctx.GetFloat("stepFast", 0.0f);
    int flags = interp_detail::InputTextFlagsWithoutCallbacks(ctx.GetFlags("flags", "InputTextFlags"));
    Scratch sc;
    float* v = FloatStorage(ctx, "value", n);
    if (!v) {
        sc.LoadF(n);
        v = sc.f;
    }
    bool changed = false;
    switch (n) {
    case 1: changed = ImGui::InputFloat(label.c_str(), v, step, step_fast, format, flags); break;
    case 2: changed = ImGui::InputFloat2(label.c_str(), v, format, flags); break;
    case 3: changed = ImGui::InputFloat3(label.c_str(), v, format, flags); break;
    default: changed = ImGui::InputFloat4(label.c_str(), v, format, flags); break;
    }
    if (v == sc.f) sc.StoreF(n);
    return Fire(ctx, "onChange", changed);
}

inline int StdStringResizeCallback(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        std::string* str = static_cast<std::string*>(data->UserData);
        str->resize(static_cast<size_t>(data->BufTextLen));
        data->Buf = const_cast<char*>(str->c_str());
    }
    return 0;
}

struct TextBuffer {
    std::string* str = nullptr;
    char* buf = nullptr;
    size_t cap = 0;
    std::vector<char> local;

    void Resolve(const DrawCtx& ctx) {
        void* p = nullptr;
        CType t;
        if (ctx.Binding("value", &p, &t) && p) {
            if (t.kind == CKind::StdString) {
                str = static_cast<std::string*>(p);
                return;
            }
            if (t.kind == CKind::CharArray && t.count > 0) {
                buf = static_cast<char*>(p);
                cap = static_cast<size_t>(t.count);
                return;
            }
        }
        if ((str = ctx.BindStdString("value")) != nullptr) return;
        int c = 0;
        if ((buf = ctx.BindCharArray("value", &c)) != nullptr && c > 0) {
            cap = static_cast<size_t>(c);
            return;
        }
        if (!HasBind(ctx, "value")) {
            Value& v = ctx.inst.MockOrCreate(ctx.ScopeKey() + ctx.node.id + ".value", Value(std::string()));
            if (!v.IsString()) v = Value(std::string());
            str = &v.s;
            return;
        }
        int max_len = ctx.GetInt("maxLength", 256);
        if (max_len < 1) max_len = 1;
        if (max_len > 65536) max_len = 65536;
        local.assign(static_cast<size_t>(max_len) + 1, 0);
        buf = local.data();
        cap = local.size();
    }
    bool Input(const char* label, const char* hint, int flags) {
        if (str) {
            flags |= ImGuiInputTextFlags_CallbackResize;
            char* data = const_cast<char*>(str->c_str());
            size_t size = str->capacity() + 1;
            if (hint)
                return ImGui::InputTextWithHint(label, hint, data, size, flags, StdStringResizeCallback, str);
            return ImGui::InputText(label, data, size, flags, StdStringResizeCallback, str);
        }
        if (hint) return ImGui::InputTextWithHint(label, hint, buf, cap, flags);
        return ImGui::InputText(label, buf, cap, flags);
    }
    bool Multiline(const char* label, const ImVec2& size, int flags) {
        if (str) {
            flags |= ImGuiInputTextFlags_CallbackResize;
            char* data = const_cast<char*>(str->c_str());
            return ImGui::InputTextMultiline(label, data, str->capacity() + 1, size, flags,
                                             StdStringResizeCallback, str);
        }
        return ImGui::InputTextMultiline(label, buf, cap, size, flags);
    }
};

inline bool DrawInputText(DrawCtx& ctx) {
    std::string label = Label(ctx), hint = ctx.GetString("hint");
    int flags = interp_detail::InputTextFlagsWithoutCallbacks(ctx.GetFlags("flags", "InputTextFlags"));
    bool wants_enter = ctx.HasEvent("onEnter");
    if (wants_enter) flags |= ImGuiInputTextFlags_EnterReturnsTrue;
    bool enter_mode = (flags & ImGuiInputTextFlags_EnterReturnsTrue) != 0;
    TextBuffer tb;
    tb.Resolve(ctx);
    bool ret = tb.Input(label.c_str(), hint.empty() ? nullptr : hint.c_str(), flags);
    if (!enter_mode) return Fire(ctx, "onChange", ret);
    Fire(ctx, "onChange", ImGui::IsItemEdited());
    return Fire(ctx, "onEnter", ret);
}

inline bool DrawInputTextMultiline(DrawCtx& ctx) {
    std::string label = Label(ctx);
    int flags = interp_detail::InputTextFlagsWithoutCallbacks(ctx.GetFlags("flags", "InputTextFlags"));
    flags &= ~ImGuiInputTextFlags_EnterReturnsTrue;
    TextBuffer tb;
    tb.Resolve(ctx);
    return Fire(ctx, "onChange", tb.Multiline(label.c_str(), ctx.GetSize(), flags));
}

inline bool DrawColorWidget(DrawCtx& ctx, bool picker) {
    std::string label = Label(ctx);
    bool alpha = ctx.GetBool("alpha", true);
    int flags = ctx.GetFlags("flags", "ColorEditFlags");
    float* v = nullptr;
    int comps = 4;
    void* p = nullptr;
    CType t;
    if (ctx.Binding("value", &p, &t) && p) {
        if (t.kind == CKind::Vec4 || (t.kind == CKind::FloatArray && t.count >= 4)) {
            v = static_cast<float*>(p);
        } else if (t.kind == CKind::FloatArray && t.count == 3) {
            v = static_cast<float*>(p);
            comps = 3;
        }
    }
    Scratch sc;
    if (!v) v = ctx.BindFloat("value", 4);
    if (!v) {
        sc.LoadF(4);
        v = sc.f;
    }
    bool changed;
    if (comps == 3 || !alpha) {
        if (comps == 4) flags |= ImGuiColorEditFlags_NoAlpha;
        changed = picker ? ImGui::ColorPicker3(label.c_str(), v, flags)
                         : ImGui::ColorEdit3(label.c_str(), v, flags);
    } else {
        changed = picker ? ImGui::ColorPicker4(label.c_str(), v, flags)
                         : ImGui::ColorEdit4(label.c_str(), v, flags);
    }
    if (v == sc.f) sc.StoreF(4);
    return Fire(ctx, "onChange", changed);
}

inline bool DrawColorEdit(DrawCtx& ctx) { return DrawColorWidget(ctx, false); }
inline bool DrawColorPicker(DrawCtx& ctx) { return DrawColorWidget(ctx, true); }

}  // namespace interp_widgets
}  // namespace imforge

#endif
