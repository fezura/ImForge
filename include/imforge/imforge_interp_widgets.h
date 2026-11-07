#ifndef IMFORGE_INTERP_PARTS
#include "imforge_interp.h"
#else
#pragma once

// stock leaf widgets, part of imforge_interp.h (including this directly pulls that in)

#include "imforge_interp_widgets_common.h"
#include "imforge_interp_widgets_input.h"

namespace imforge {
namespace interp_widgets {

inline bool DrawText(DrawCtx& ctx) {
    std::string text = ctx.GetString("text");
    bool has_col = HasProp(ctx, "color");
    ImVec4 col = has_col ? ctx.GetColor("color", ImVec4(1, 1, 1, 1)) : ImVec4();
    if (ctx.GetBool("disabled", false)) {
        col = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
        has_col = true;
    }
    if (has_col) ImGui::PushStyleColor(ImGuiCol_Text, col);
    if (ctx.GetBool("bullet", false)) ImGui::BulletText("%s", text.c_str());
    else if (ctx.GetBool("wrapped", false)) ImGui::TextWrapped("%s", text.c_str());
    else ImGui::TextUnformatted(text.c_str());
    if (has_col) ImGui::PopStyleColor();
    return false;
}

inline bool DrawSeparatorText(DrawCtx& ctx) {
    std::string text = ctx.GetString("text");
#if IMGUI_VERSION_NUM >= 18950
    ImGui::SeparatorText(text.c_str());
#else
    ImGui::Separator();
    ImGui::TextUnformatted(text.c_str());
#endif
    return false;
}

inline bool DrawSeparator(DrawCtx&) {
    ImGui::Separator();
    return false;
}

inline bool DrawSpacing(DrawCtx& ctx) {
    int n = ctx.GetInt("count", 1);
    for (int k = 0; k < n && k < 256; ++k) ImGui::Spacing();
    return false;
}

inline bool DrawNewLine(DrawCtx&) {
    ImGui::NewLine();
    return false;
}

inline bool DrawDummy(DrawCtx& ctx) {
    ImGui::Dummy(ClampFill(ctx.GetSize()));
    return false;
}

inline bool DrawButton(DrawCtx& ctx) {
    bool repeat = ctx.GetBool("repeat", false);
#if IMGUI_VERSION_NUM >= 19100
    if (repeat) ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
#else
    if (repeat) ImGui::PushButtonRepeat(true);
#endif
    bool clicked = ImGui::Button(Label(ctx).c_str(), ctx.GetSize());
#if IMGUI_VERSION_NUM >= 19100
    if (repeat) ImGui::PopItemFlag();
#else
    if (repeat) ImGui::PopButtonRepeat();
#endif
    return Fire(ctx, "onClick", clicked);
}

inline bool DrawSmallButton(DrawCtx& ctx) {
    return Fire(ctx, "onClick", ImGui::SmallButton(Label(ctx).c_str()));
}

inline bool DrawArrowButton(DrawCtx& ctx) {
    int dir = ctx.GetEnum("dir", "Left|Right|Up|Down", 0);
    if (dir < 0 || dir > 3) dir = 0;
    return Fire(ctx, "onClick", ImGui::ArrowButton(ctx.node.id.c_str(), static_cast<ImGuiDir>(dir)));
}

inline bool DrawColorButton(DrawCtx& ctx) {
    std::string desc = Label(ctx, "desc");
    ImVec4 col = ctx.GetColor("color", ImVec4(1, 1, 1, 1));
    int flags = ctx.GetFlags("flags", "ColorEditFlags");
    return Fire(ctx, "onClick", ImGui::ColorButton(desc.c_str(), col, flags, ClampFill(ctx.GetSize())));
}

inline bool DrawCheckbox(DrawCtx& ctx) {
    bool* v = ctx.BindBool("value");
    if (!v) v = Scratch::Bool();
    return Fire(ctx, "onChange", ImGui::Checkbox(Label(ctx).c_str(), v));
}

inline bool DrawRadioButton(DrawCtx& ctx) {
    int button_value = ctx.GetInt("value", 0);
    int* v = IntStorage(ctx, "value", 1);
    Scratch sc;
    if (!v) {
        sc.LoadI(1);
        v = sc.i;
    }
    bool changed = ImGui::RadioButton(Label(ctx).c_str(), v, button_value);
    if (v == sc.i) sc.StoreI(1);
    return Fire(ctx, "onChange", changed);
}

struct ListState {
    std::vector<std::string> items;
    void* ptr = nullptr;
    CType type;
    bool bound = false;
    const EnumInfo* enm = nullptr;
    bool map_enum = false;
    int index = -1;
    Scratch sc;

    void Load(const DrawCtx& ctx) {
        items = ctx.GetStringList("items");
        bound = ctx.Binding("value", &ptr, &type) && ptr;
        enm = bound && type.kind == CKind::Enum ? type.enm : nullptr;
        map_enum = enm && items.empty();
        if (map_enum)
            for (const auto& kv : enm->values) items.push_back(kv.first);
        long long cur = 0;
        if (bound) {
            Value v;
            if (ReadValue(ptr, type, v)) cur = v.ToInt();
        } else {
            sc.LoadI(1);
            cur = sc.i[0];
        }
        index = -1;
        if (map_enum) {
            for (size_t k = 0; k < enm->values.size(); ++k)
                if (enm->values[k].second == cur) index = static_cast<int>(k);
        } else if (cur >= 0 && cur < static_cast<long long>(items.size())) {
            index = static_cast<int>(cur);
        }
    }
    const char* Preview() const {
        return index >= 0 && index < static_cast<int>(items.size()) ? items[index].c_str() : "";
    }
    bool DrawItems() {
        bool changed = false;
        for (int k = 0; k < static_cast<int>(items.size()); ++k) {
            ImGui::PushID(k);
            bool selected = k == index;
            if (ImGui::Selectable(items[k].c_str(), selected)) {
                index = k;
                changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        return changed;
    }
    void Store() {
        long long nv = map_enum && index >= 0 ? enm->values[index].second : index;
        if (bound) {
            WriteValue(ptr, type, Value(nv));
        } else {
            sc.i[0] = static_cast<int>(nv);
            sc.StoreI(1);
        }
    }
};

inline bool DrawCombo(DrawCtx& ctx) {
    ListState st;
    st.Load(ctx);
    int flags = ctx.GetFlags("flags", "ComboFlags");
    bool changed = false;
    if (ImGui::BeginCombo(Label(ctx).c_str(), st.Preview(), flags)) {
        changed = st.DrawItems();
        ImGui::EndCombo();
    }
    if (changed) st.Store();
    return Fire(ctx, "onChange", changed);
}

inline bool DrawListBox(DrawCtx& ctx) {
    ListState st;
    st.Load(ctx);
    int n = ctx.GetInt("heightItems", -1);
    ImVec2 size(0, n > 0 ? ImGui::GetTextLineHeightWithSpacing() * (static_cast<float>(n) + 0.25f) +
                               ImGui::GetStyle().FramePadding.y * 2.0f
                         : 0.0f);
    bool changed = false;
    if (ImGui::BeginListBox(Label(ctx).c_str(), size)) {
        changed = st.DrawItems();
        ImGui::EndListBox();
    }
    if (changed) st.Store();
    return Fire(ctx, "onChange", changed);
}

inline bool DrawProgressBar(DrawCtx& ctx) {
    double bound = 0;
    float fraction = HasBind(ctx, "value") && ReadNumber(ctx, "value", bound)
                         ? static_cast<float>(bound)
                         : ctx.GetFloat("fraction", 0.0f);
    std::string overlay = ctx.GetString("overlay");
    ImGui::ProgressBar(fraction, ctx.GetSize(), overlay.empty() ? nullptr : overlay.c_str());
    return false;
}

template <class T>
inline T TexFromInt(long long v, std::true_type) {
    return reinterpret_cast<T>(static_cast<intptr_t>(v));
}
template <class T>
inline T TexFromInt(long long v, std::false_type) {
    return static_cast<T>(v);
}

inline bool ResolveTexture(const DrawCtx& ctx, ImTextureID& out) {
    if (!HasBind(ctx, "texture")) return false;
    out = ctx.GetTexture("texture");
    if (out != ImTextureID()) return true;
    std::string path = ctx.BindingPath("texture");
    void* p = nullptr;
    CType t;
    if (path.empty() || !ctx.ResolveStorage(path, &p, &t) || !p) return false;
    bool int_like = t.kind == CKind::TexID || t.kind == CKind::U64 || t.kind == CKind::I64 ||
                    t.kind == CKind::U32 || t.kind == CKind::I32;
    Value v;
    if (!int_like || !ReadValue(p, t, v) || v.ToInt() == 0) return false;
    out = TexFromInt<ImTextureID>(v.ToInt(), std::is_pointer<ImTextureID>());
    return true;
}

inline ImVec2 ImageSize(const DrawCtx& ctx) {
    ImVec2 size = ClampFill(ctx.GetSize());
    if (size.x <= 0) size.x = 64.0f * ctx.S();
    if (size.y <= 0) size.y = 64.0f * ctx.S();
    return size;
}

inline void DrawChecker(const ImVec2& p, const ImVec2& size, float scale) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float cell = 8.0f * scale;
    float min_cell = (size.x > size.y ? size.x : size.y) / 32.0f;
    if (cell < min_cell) cell = min_cell;
    if (cell < 1.0f) cell = 1.0f;
    const ImU32 c0 = IM_COL32(96, 96, 96, 255), c1 = IM_COL32(150, 150, 150, 255);
    int rows = static_cast<int>(size.y / cell) + 1, cols = static_cast<int>(size.x / cell) + 1;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            ImVec2 a(p.x + static_cast<float>(c) * cell, p.y + static_cast<float>(r) * cell);
            ImVec2 b(a.x + cell < p.x + size.x ? a.x + cell : p.x + size.x,
                     a.y + cell < p.y + size.y ? a.y + cell : p.y + size.y);
            if (b.x <= a.x || b.y <= a.y) continue;
            dl->AddRectFilled(a, b, ((r + c) & 1) ? c1 : c0);
        }
    }
    dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), IM_COL32(210, 210, 210, 255));
}

inline bool DrawImage(DrawCtx& ctx) {
    ImVec2 size = ImageSize(ctx);
    ImVec2 uv0 = ctx.GetVec2("uv0", ImVec2(0, 0)), uv1 = ctx.GetVec2("uv1", ImVec2(1, 1));
    ImVec4 tint = ctx.GetColor("tint", ImVec4(1, 1, 1, 1));
    ImVec4 border = ctx.GetColor("border", ImVec4(0, 0, 0, 0));
    ImTextureID tex = ImTextureID();
    if (!ResolveTexture(ctx, tex)) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(size);
        DrawChecker(p, size, ctx.S());
        return false;
    }
#if IMGUI_VERSION_NUM >= 19190
    bool has_border = border.w > 0;
    if (has_border) {
        ImGui::PushStyleVar(ImGuiStyleVar_ImageBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Border, border);
    }
    ImGui::ImageWithBg(tex, size, uv0, uv1, ImVec4(0, 0, 0, 0), tint);
    if (has_border) {
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }
#else
    ImGui::Image(tex, size, uv0, uv1, tint, border);
#endif
    return false;
}

inline bool DrawImageButton(DrawCtx& ctx) {
    ImVec2 size = ImageSize(ctx);
    ImVec2 uv0 = ctx.GetVec2("uv0", ImVec2(0, 0)), uv1 = ctx.GetVec2("uv1", ImVec2(1, 1));
    ImVec4 bg = ctx.GetColor("bg", ImVec4(0, 0, 0, 0));
    ImVec4 tint = ctx.GetColor("tint", ImVec4(1, 1, 1, 1));
    ImTextureID tex = ImTextureID();
    bool clicked;
    if (!ResolveTexture(ctx, tex)) {
        const ImVec2 pad = ImGui::GetStyle().FramePadding;
        std::string id = "##" + ctx.node.id;
        clicked = ImGui::Button(id.c_str(), ImVec2(size.x + pad.x * 2, size.y + pad.y * 2));
        ImVec2 p = ImGui::GetItemRectMin();
        DrawChecker(ImVec2(p.x + pad.x, p.y + pad.y), size, ctx.S());
    } else {
#if IMGUI_VERSION_NUM >= 18900
        clicked = ImGui::ImageButton(ctx.node.id.c_str(), tex, size, uv0, uv1, bg, tint);
#else
        clicked = ImGui::ImageButton(tex, size, uv0, uv1, -1, bg, tint);
#endif
    }
    return Fire(ctx, "onClick", clicked);
}

inline bool DrawSelectable(DrawCtx& ctx) {
    int flags = ctx.GetFlags("flags", "SelectableFlags");
    bool* sel = HasBind(ctx, "selected") ? ctx.BindBool("selected") : nullptr;
    if (!sel) sel = Scratch::Bool();
    return Fire(ctx, "onClick", ImGui::Selectable(Label(ctx).c_str(), sel, flags, ClampFill(ctx.GetSize())));
}

inline bool DrawMenuItem(DrawCtx& ctx) {
    std::string label = Label(ctx), shortcut = ctx.GetString("shortcut");
    bool enabled = ctx.GetBool("enabled", true);
    const char* sc = shortcut.empty() ? nullptr : shortcut.c_str();
    bool* sel = HasBind(ctx, "selected") ? ctx.BindBool("selected") : nullptr;
    bool clicked = sel ? ImGui::MenuItem(label.c_str(), sc, sel, enabled)
                       : ImGui::MenuItem(label.c_str(), sc, false, enabled);
    return Fire(ctx, "onClick", clicked);
}

inline const float* PlotValues(const DrawCtx& ctx, int& count) {
    static const float kDemo[] = {0.1f, 0.4f, 0.3f, 0.8f, 0.6f, 0.9f, 0.5f, 0.7f,
                                  0.2f, 0.5f, 0.9f, 0.4f, 0.6f, 0.3f, 0.8f, 0.1f};
    count = 0;
    void* p = nullptr;
    CType t;
    if (ctx.Binding("values", &p, &t) && p) {
        if (t.kind == CKind::FloatArray && t.count > 0) {
            count = t.count;
            return static_cast<const float*>(p);
        }
        if (t.kind == CKind::Vector && t.elem && t.elem->kind == CKind::Float && t.vec_data &&
            t.vec_size) {
            count = static_cast<int>(t.vec_size(p));
            return count > 0 ? static_cast<const float*>(t.vec_data(p)) : nullptr;
        }
        if (t.kind == CKind::Array && t.elem && t.elem->kind == CKind::Float && t.count > 0) {
            count = t.count;
            return static_cast<const float*>(p);
        }
    }
    if (ctx.Design() && !HasBind(ctx, "values")) {
        count = static_cast<int>(sizeof(kDemo) / sizeof(kDemo[0]));
        return kDemo;
    }
    return nullptr;
}

inline bool DrawPlot(DrawCtx& ctx, bool histogram) {
    std::string label = Label(ctx), overlay = ctx.GetString("overlay");
    float smin = ctx.GetFloat("scaleMin", 0.0f), smax = ctx.GetFloat("scaleMax", 0.0f);
    if (smin == 0.0f && smax == 0.0f) smin = smax = FLT_MAX;
    int count = 0;
    const float* values = PlotValues(ctx, count);
    const char* ov = overlay.empty() ? nullptr : overlay.c_str();
    ImVec2 size = ClampFill(ctx.GetSize());
    if (histogram) ImGui::PlotHistogram(label.c_str(), values, count, 0, ov, smin, smax, size);
    else ImGui::PlotLines(label.c_str(), values, count, 0, ov, smin, smax, size);
    return false;
}

inline bool DrawPlotLines(DrawCtx& ctx) { return DrawPlot(ctx, false); }
inline bool DrawPlotHistogram(DrawCtx& ctx) { return DrawPlot(ctx, true); }

using DrawFn = bool (*)(DrawCtx&);

struct Entry {
    const char* type;
    DrawFn fn;
};

inline const Entry* Table(size_t& count) {
    static const Entry table[] = {
        {"Text", &DrawText},
        {"SeparatorText", &DrawSeparatorText},
        {"Separator", &DrawSeparator},
        {"Spacing", &DrawSpacing},
        {"NewLine", &DrawNewLine},
        {"Dummy", &DrawDummy},
        {"Button", &DrawButton},
        {"SmallButton", &DrawSmallButton},
        {"ArrowButton", &DrawArrowButton},
        {"Checkbox", &DrawCheckbox},
        {"RadioButton", &DrawRadioButton},
        {"Combo", &DrawCombo},
        {"ListBox", &DrawListBox},
        {"SliderFloat", &DrawSliderFloat},
        {"SliderInt", &DrawSliderInt},
        {"VSliderFloat", &DrawVSliderFloat},
        {"VSliderInt", &DrawVSliderInt},
        {"DragFloat", &DrawDragFloat},
        {"DragInt", &DrawDragInt},
        {"InputText", &DrawInputText},
        {"InputTextMultiline", &DrawInputTextMultiline},
        {"InputInt", &DrawInputInt},
        {"InputFloat", &DrawInputFloat},
        {"ColorEdit", &DrawColorEdit},
        {"ColorPicker", &DrawColorPicker},
        {"ColorButton", &DrawColorButton},
        {"ProgressBar", &DrawProgressBar},
        {"Image", &DrawImage},
        {"ImageButton", &DrawImageButton},
        {"Selectable", &DrawSelectable},
        {"MenuItem", &DrawMenuItem},
        {"PlotLines", &DrawPlotLines},
        {"PlotHistogram", &DrawPlotHistogram},
    };
    count = sizeof(table) / sizeof(table[0]);
    return table;
}

inline DrawFn Find(const char* type) {
    size_t n = 0;
    const Entry* table = Table(n);
    for (size_t k = 0; k < n; ++k)
        if (std::strcmp(table[k].type, type) == 0) return table[k].fn;
    return nullptr;
}

}  // namespace interp_widgets

inline bool IsStockLeafWidget(const char* type) { return type && interp_widgets::Find(type) != nullptr; }

inline bool DrawStockWidget(DrawCtx& ctx) {
    interp_widgets::DrawFn fn = interp_widgets::Find(ctx.node.type.c_str());
    return fn ? fn(ctx) : false;
}

}  // namespace imforge

#endif
