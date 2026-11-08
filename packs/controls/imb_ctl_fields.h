#pragma once
// DragNumber (with units), VecEdit, Vec2Edit, Vec3Edit
#include <cstddef>
#include "imb_ctl_core.h"

namespace imb {
namespace ctl {

// value shown = stored / scale, dragging moves the shown value by speed per pixel
struct DragUnit {
    const char* name;
    float scale;
    float speed;
    const char* fmt;
};

struct DragNumberStyle {
    float width = 0.0f;
    bool show_label = true;
    bool show_arrows = true;
    ImU32 bg = 0;
};

namespace detail {

struct FieldEdit {
    ImGuiID id;
    int frames;
    bool was_active;
    char buf[64];
};

inline FieldEdit& EditState() {
    static FieldEdit e = {0, 0, false, {0}};
    return e;
}

// one number field: drag sideways (Shift x10, Alt x0.1), click or Enter to type a value,
// arrows step it while focused. v is the shown value
inline bool DragFieldImpl(const char* str_id, float* v, float speed, float v_min, float v_max, const char* fmt,
                          const char* suffix, ImU32 axis_raw, const char* axis, float w, bool arrows, ImU32 bg_raw) {
    const ImGuiIO& io = ImGui::GetIO();
    float dpi = Dpi();
    float h = ImGui::GetFrameHeight();
    ImGuiID id = ImGui::GetID(str_id);
    FieldEdit& es = EditState();
    float r = Rounding(h);
    bool clamp = v_min < v_max;
    bool changed = false;
    w = Max(w, h);
    if (es.id == id) {
        ImGui::PushID(str_id);
        ImGui::SetNextItemWidth(w);
        if (es.frames == 0) ImGui::SetKeyboardFocusHere();
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, r);
        bool enter = ImGui::InputText("##edit", es.buf, sizeof(es.buf),
                                      ImGuiInputTextFlags_CharsScientific | ImGuiInputTextFlags_AutoSelectAll |
                                          ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopStyleVar();
        bool act = ImGui::IsItemActive();
        ImGui::PopID();
        if (act) es.was_active = true;
        ++es.frames;
        if (enter || (es.was_active && !act) || (!es.was_active && es.frames > 4)) {
            if (es.was_active && !KeyPressed(ImGuiKey_Escape, false)) {
                char* end = nullptr;
                double d = strtod(es.buf, &end);
                if (end != es.buf) {
                    float nv = (float)d;
                    if (clamp) nv = Clamp(nv, v_min, v_max);
                    if (nv != *v) {
                        *v = nv;
                        changed = true;
                    }
                }
            }
            es.id = 0;
        }
        return changed;
    }
    ImVec2 pos = ImGui::GetCursorScreenPos(), b(pos.x + w, pos.y + h);
    bool released = ImGui::InvisibleButton(str_id, ImVec2(w, h), kNav);
    bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive(), focused = ImGui::IsItemFocused();
    ImGuiStorage* store = ImGui::GetStateStorage();
    ImGuiID k_drag = Sub(id, 1);
    if (hovered || active) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (ImGui::IsItemActivated()) store->SetBool(k_drag, false);
    bool dragged = store->GetBool(k_drag, false);
    if (active && ImGui::IsMouseDown(0)) {
        if (!dragged && ImGui::IsMouseDragging(0, 3.0f * dpi)) {
            dragged = true;
            store->SetBool(k_drag, true);
        }
        if (dragged && io.MouseDelta.x != 0.0f) {
            float mul = io.KeyShift ? 10.0f : (io.KeyAlt ? 0.1f : 1.0f);
            float nv = *v + io.MouseDelta.x * speed * mul;
            if (clamp) nv = Clamp(nv, v_min, v_max);
            if (nv != *v) {
                *v = nv;
                changed = true;
            }
        }
    }
    bool start_edit = (released && !dragged) || (focused && !active && KeyPressed(ImGuiKey_Enter, false));
    int d = FocusedArrows(true, true);
    if (d) {
        float nv = *v + speed * (io.KeyShift ? 100.0f : 10.0f) * (float)d;
        if (clamp) nv = Clamp(nv, v_min, v_max);
        if (nv != *v) {
            *v = nv;
            changed = true;
        }
    }
    if (start_edit) {
        es.id = id;
        es.frames = 0;
        es.was_active = false;
        Fmt(es.buf, (int)sizeof(es.buf), fmt, *v);
        char* p = es.buf;
        while (*p == ' ') ++p;
        if (p != es.buf) memmove(es.buf, p, strlen(p) + 1);
        for (char* q = es.buf; *q; ++q)
            if (!((*q >= '0' && *q <= '9') || *q == '.' || *q == '-' || *q == '+' || *q == 'e' || *q == 'E')) {
                *q = 0;
                break;
            }
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    float hk = AnimTo(Sub(id, 2), active ? 1.0f : (hovered ? 0.6f : 0.0f));
    ImU32 bg = bg_raw ? bg_raw : StyleRaw(ImGuiCol_FrameBg);
    ImU32 fill = Mix(bg, active ? StyleRaw(ImGuiCol_FrameBgActive) : StyleRaw(ImGuiCol_FrameBgHovered), hk);
    Frame(dl, pos, b, Final(fill), r);
    float left = pos.x;
    if (axis && *axis) {
        float aw = Max(h * 0.75f, TextW(axis) + 6.0f * dpi);
        ImVec2 ab(pos.x + aw, b.y);
        dl->AddRectFilled(pos, ab, Final(axis_raw, 0.85f + 0.15f * hk), r, kRoundLeft);
        TextIn(dl, pos, ab, axis, nullptr, Final(OnRaw(axis_raw)));
        left = ab.x;
    } else if (axis_raw) {
        float th = Max(2.0f, 2.5f * dpi);
        dl->AddRectFilled(pos, ImVec2(pos.x + th + r * 0.5f, b.y), Final(axis_raw), r, kRoundLeft);
        left = pos.x + th;
    }
    if (clamp) {
        float t = Sat((*v - v_min) / (v_max - v_min));
        float lh = Max(1.5f, 2.0f * dpi);
        if (t > 0.0f)
            dl->AddRectFilled(ImVec2(left + r * 0.3f, b.y - lh), ImVec2(Lerp(left + r * 0.3f, b.x - r * 0.3f, t), b.y),
                              Final(AccentRaw(), 0.7f), lh * 0.5f);
    }
    char vb[64];
    Fmt(vb, 64, fmt, *v);
    float vw = TextW(vb), sw = suffix && *suffix ? TextW(" ") + TextW(suffix) : 0.0f;
    float cx = (left + b.x) * 0.5f, tx = cx - (vw + sw) * 0.5f, ty = pos.y + (h - ImGui::GetFontSize()) * 0.5f;
    ImVec4 clip(left, pos.y, b.x, b.y);
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(tx, ty), Final(TextRaw()), vb, nullptr, 0.0f, &clip);
    if (sw > 0.0f)
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(tx + vw + TextW(" "), ty), Final(DimRaw()), suffix,
                    nullptr, 0.0f, &clip);
    float at = Anim(Sub(id, 3), arrows && hovered && !active);
    if (at > 0.01f && b.x - left > h * 2.5f) {
        float s = h * 0.14f, th = Max(1.2f, 1.3f * dpi);
        Chevron(dl, ImVec2(left + h * 0.32f + (1.0f - at) * 3.0f * dpi, pos.y + h * 0.5f), s, kPi * 0.5f, Final(DimRaw(), at), th);
        Chevron(dl, ImVec2(b.x - h * 0.32f - (1.0f - at) * 3.0f * dpi, pos.y + h * 0.5f), s, -kPi * 0.5f, Final(DimRaw(), at), th);
    }
    return changed;
}

}  // namespace detail

inline bool DragNumber(const char* label, float* v, float speed = 1.0f, float v_min = 0.0f, float v_max = 0.0f,
                       const char* fmt = "%.2f", const char* unit = nullptr, const DragNumberStyle& ds = DragNumberStyle()) {
    float tmp = 0.0f;
    if (!v) v = &tmp;
    const char* lend = detail::LabelEnd(label);
    float w = ds.width > 0.0f ? ds.width : ImGui::CalcItemWidth();
    ImGui::PushID(label);
    ImGui::BeginGroup();
    bool changed = detail::DragFieldImpl("##field", v, speed, v_min, v_max, fmt, unit, 0, nullptr, w, ds.show_arrows, ds.bg);
    if (ds.show_label) detail::TrailingLabel(label, lend, ImGui::GetFrameHeight());
    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}

// several units: *v stays in base units, the unit button on the right switches the display
inline bool DragNumber(const char* label, float* v, const DragUnit* units, int unit_count, int* unit_index,
                       float v_min = 0.0f, float v_max = 0.0f, const DragNumberStyle& ds = DragNumberStyle()) {
    if (!units || unit_count <= 0) return DragNumber(label, v, 1.0f, v_min, v_max, "%.2f", nullptr, ds);
    float tmp = 0.0f;
    if (!v) v = &tmp;
    const ImGuiStyle& st = ImGui::GetStyle();
    float dpi = detail::Dpi();
    float h = ImGui::GetFrameHeight();
    const char* lend = detail::LabelEnd(label);
    ImGuiID id = ImGui::GetID(label);
    ImGuiStorage* store = ImGui::GetStateStorage();
    ImGuiID k_unit = detail::Sub(id, 1);
    int local = store->GetInt(k_unit, 0);
    int* ui = unit_index ? unit_index : &local;
    *ui = detail::ClampI(*ui, 0, unit_count - 1);
    const DragUnit& u = units[*ui];
    float scale = u.scale != 0.0f ? u.scale : 1.0f;
    float name_w = 0.0f;
    for (int i = 0; i < unit_count; ++i) name_w = detail::Max(name_w, detail::TextW(units[i].name));
    float ub_w = name_w + h * 0.9f;
    float w = ds.width > 0.0f ? ds.width : ImGui::CalcItemWidth();
    ImGui::PushID(label);
    ImGui::BeginGroup();
    float shown = *v / scale;
    float lo = v_min / scale, hi = v_max / scale;
    if (lo > hi) {
        float t = lo;
        lo = hi;
        hi = t;
    }
    bool changed = false;
    if (detail::DragFieldImpl("##field", &shown, u.speed > 0.0f ? u.speed : 1.0f, lo, hi, u.fmt ? u.fmt : "%.2f", nullptr, 0,
                              nullptr, w - ub_w - st.ItemInnerSpacing.x, ds.show_arrows, ds.bg)) {
        *v = shown * scale;
        changed = true;
    }
    ImGui::SameLine(0.0f, st.ItemInnerSpacing.x);
    ImVec2 p = ImGui::GetCursorScreenPos(), q(p.x + ub_w, p.y + h);
    bool open = ImGui::InvisibleButton("##unit", ImVec2(ub_w, h), detail::kNav);
    float hk = detail::Anim(detail::Sub(id, 2), ImGui::IsItemHovered());
    bool popup_open = ImGui::IsPopupOpen("##units");
    float ot = detail::Anim(detail::Sub(id, 3), popup_open);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float r = detail::Rounding(h);
    detail::Frame(dl, p, q, detail::Final(detail::Shade(detail::StyleRaw(ImGuiCol_FrameBg), 0.06f * hk)), r);
    detail::TextIn(dl, p, ImVec2(q.x - h * 0.55f, q.y), u.name, nullptr, detail::Final(detail::TextRaw()), 0.5f, st.FramePadding.x * 0.5f);
    detail::Chevron(dl, ImVec2(q.x - h * 0.4f, p.y + h * 0.5f), h * 0.12f, detail::kPi * ot, detail::Final(detail::DimRaw()),
                    detail::Max(1.2f, 1.3f * dpi));
    if (open) ImGui::OpenPopup("##units");
    ImGui::SetNextWindowPos(ImVec2(p.x, q.y + 2.0f * dpi));
    if (ImGui::BeginPopup("##units")) {
        for (int i = 0; i < unit_count; ++i) {
            ImGui::PushID(i);
            if (ImGui::Selectable(units[i].name ? units[i].name : "", i == *ui) && i != *ui) {
                *ui = i;
                changed = true;
            }
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }
    if (!unit_index) store->SetInt(k_unit, *ui);
    if (ds.show_label) detail::TrailingLabel(label, lend, h);
    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}

// units from strings "name", "name:scale" or "name:scale:speed" (cm:0.01:0.1), for generated code
inline bool DragNumber(const char* label, float* v, const char* const* unit_specs, int unit_count, int* unit_index,
                       float speed = 1.0f, float v_min = 0.0f, float v_max = 0.0f, const char* fmt = "%.2f",
                       const DragNumberStyle& ds = DragNumberStyle()) {
    const int kMaxUnits = 16;
    if (!unit_specs || unit_count <= 0) return DragNumber(label, v, speed, v_min, v_max, fmt, nullptr, ds);
    if (unit_count > kMaxUnits) unit_count = kMaxUnits;
    DragUnit units[kMaxUnits];
    char names[kMaxUnits][24];
    for (int i = 0; i < unit_count; ++i) {
        const char* s = unit_specs[i] ? unit_specs[i] : "";
        const char* c = strchr(s, ':');
        size_t n = c ? (size_t)(c - s) : strlen(s);
        if (n >= sizeof(names[i])) n = sizeof(names[i]) - 1;
        memcpy(names[i], s, n);
        names[i][n] = 0;
        units[i].name = names[i];
        units[i].scale = c ? (float)atof(c + 1) : 1.0f;
        if (units[i].scale == 0.0f) units[i].scale = 1.0f;
        const char* c2 = c ? strchr(c + 1, ':') : nullptr;
        units[i].speed = c2 ? (float)atof(c2 + 1) : speed / units[i].scale;
        units[i].fmt = fmt;
    }
    if (unit_count > 1) return DragNumber(label, v, units, unit_count, unit_index, v_min, v_max, ds);
    // one unit: plain field with a suffix, still shown in that unit
    float tmp = 0.0f;
    if (!v) v = &tmp;
    float sc = units[0].scale, shown = *v / sc;
    float lo = detail::Min(v_min / sc, v_max / sc), hi = detail::Max(v_min / sc, v_max / sc);
    if (!DragNumber(label, &shown, units[0].speed, lo, hi, fmt, names[0][0] ? names[0] : nullptr, ds)) return false;
    *v = shown * sc;
    return true;
}

struct VecEditStyle {
    float width = 0.0f;
    bool show_label = true;
    bool letters = true;
    const char* const* axis_labels = nullptr;
};

// n fields (1..4) with colored X/Y/Z/W tags
inline bool VecEdit(const char* label, float* v, int n, float speed = 0.1f, float v_min = 0.0f, float v_max = 0.0f,
                    const char* fmt = "%.2f", const VecEditStyle& vs = VecEditStyle()) {
    if (!v || n <= 0) return false;
    if (n > 4) n = 4;
    static const char* const kAxes[4] = {"X", "Y", "Z", "W"};
    const ImGuiStyle& st = ImGui::GetStyle();
    const char* lend = detail::LabelEnd(label);
    float w = vs.width > 0.0f ? vs.width : ImGui::CalcItemWidth();
    float gap = st.ItemInnerSpacing.x;
    float fw = (w - gap * (float)(n - 1)) / (float)n;
    ImGui::PushID(label);
    ImGui::BeginGroup();
    bool changed = false;
    for (int i = 0; i < n; ++i) {
        if (i > 0) ImGui::SameLine(0.0f, gap);
        ImGui::PushID(i);
        const char* axis = vs.axis_labels ? vs.axis_labels[i] : kAxes[i];
        if (detail::DragFieldImpl("##c", &v[i], speed, v_min, v_max, fmt, nullptr, detail::AxisRaw(i), vs.letters ? axis : nullptr,
                                  fw, false, 0))
            changed = true;
        ImGui::PopID();
    }
    if (vs.show_label) detail::TrailingLabel(label, lend, ImGui::GetFrameHeight());
    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}

inline bool Vec2Edit(const char* label, float v[2], float speed = 0.1f, float v_min = 0.0f, float v_max = 0.0f,
                     const char* fmt = "%.2f", const VecEditStyle& vs = VecEditStyle()) {
    return VecEdit(label, v, 2, speed, v_min, v_max, fmt, vs);
}
inline bool Vec2Edit(const char* label, ImVec2* v, float speed = 0.1f, float v_min = 0.0f, float v_max = 0.0f,
                     const char* fmt = "%.2f", const VecEditStyle& vs = VecEditStyle()) {
    return v ? VecEdit(label, &v->x, 2, speed, v_min, v_max, fmt, vs) : false;
}
inline bool Vec3Edit(const char* label, float v[3], float speed = 0.1f, float v_min = 0.0f, float v_max = 0.0f,
                     const char* fmt = "%.2f", const VecEditStyle& vs = VecEditStyle()) {
    return VecEdit(label, v, 3, speed, v_min, v_max, fmt, vs);
}
// xyz of an ImVec4, w untouched
inline bool Vec3Edit(const char* label, ImVec4* v, float speed = 0.1f, float v_min = 0.0f, float v_max = 0.0f,
                     const char* fmt = "%.2f", const VecEditStyle& vs = VecEditStyle()) {
    return v ? VecEdit(label, &v->x, 3, speed, v_min, v_max, fmt, vs) : false;
}

inline bool Vec2Edit(const char* label, std::nullptr_t, float speed = 0.1f, float v_min = 0.0f, float v_max = 0.0f,
                     const char* fmt = "%.2f", const VecEditStyle& vs = VecEditStyle()) {
    return Vec2Edit(label, static_cast<ImVec2*>(nullptr), speed, v_min, v_max, fmt, vs);
}

inline bool Vec3Edit(const char* label, std::nullptr_t, float speed = 0.1f, float v_min = 0.0f, float v_max = 0.0f,
                     const char* fmt = "%.2f", const VecEditStyle& vs = VecEditStyle()) {
    return Vec3Edit(label, static_cast<ImVec4*>(nullptr), speed, v_min, v_max, fmt, vs);
}

}  // namespace ctl
}  // namespace imb
