#pragma once

#include <cfloat>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

#include "imgui.h"
#include "imgui_internal.h"

#include "../builder_fonts.h"
#include "../builder_style.h"

#if IMGUI_VERSION_NUM >= 19110
#define IMFORGE_CHILD_BORDER ImGuiChildFlags_Borders
#elif IMGUI_VERSION_NUM >= 19000
#define IMFORGE_CHILD_BORDER ImGuiChildFlags_Border
#else
#define IMFORGE_CHILD_BORDER true
#endif

#if __has_include("misc/cpp/imgui_stdlib.h")
#include "misc/cpp/imgui_stdlib.h"
#define IMFORGE_HAS_IMGUI_STDLIB 1
#elif __has_include("imgui_stdlib.h")
#include "imgui_stdlib.h"
#define IMFORGE_HAS_IMGUI_STDLIB 1
#else
#define IMFORGE_HAS_IMGUI_STDLIB 0
#endif

namespace imforge::builder::ui {

namespace detail {
inline int StdStringResizeCallback(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        std::string* s = static_cast<std::string*>(data->UserData);
        s->resize((size_t)data->BufTextLen);
        data->Buf = const_cast<char*>(s->c_str());
    }
    return 0;
}
}  // namespace detail

inline bool InputTextStd(const char* label, std::string& s, ImGuiInputTextFlags flags = 0) {
#if IMFORGE_HAS_IMGUI_STDLIB
    return ImGui::InputText(label, &s, flags);
#else
    flags |= ImGuiInputTextFlags_CallbackResize;
    return ImGui::InputText(label, const_cast<char*>(s.c_str()), s.capacity() + 1, flags, detail::StdStringResizeCallback, &s);
#endif
}

inline bool InputTextMultilineStd(const char* label, std::string& s, const ImVec2& size = ImVec2(0, 0),
                                  ImGuiInputTextFlags flags = 0) {
#if IMFORGE_HAS_IMGUI_STDLIB
    return ImGui::InputTextMultiline(label, &s, size, flags);
#else
    flags |= ImGuiInputTextFlags_CallbackResize;
    return ImGui::InputTextMultiline(label, const_cast<char*>(s.c_str()), s.capacity() + 1, size, flags,
                                     detail::StdStringResizeCallback, &s);
#endif
}

inline bool InputTextWithHintStd(const char* label, const char* hint, std::string& s, ImGuiInputTextFlags flags = 0) {
#if IMFORGE_HAS_IMGUI_STDLIB
    return ImGui::InputTextWithHint(label, hint, &s, flags);
#else
    flags |= ImGuiInputTextFlags_CallbackResize;
    return ImGui::InputTextWithHint(label, hint, const_cast<char*>(s.c_str()), s.capacity() + 1, flags,
                                    detail::StdStringResizeCallback, &s);
#endif
}

inline bool Section(const char* label, bool default_open = true) {
    ImGuiTreeNodeFlags f = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Framed;
    if (default_open) f |= ImGuiTreeNodeFlags_DefaultOpen;
    ImGui::PushStyleColor(ImGuiCol_Header, Colors().bg_frame);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Colors().bg_hover);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, Colors().bg_active);
    bool open = ImGui::CollapsingHeader(label, f);
    ImGui::PopStyleColor(3);
    return open;
}

inline void HelpMarker(const char* desc) {
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort) && ImGui::BeginTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

inline void Tooltip(const char* text) {
    if (text && *text && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", text);
}

inline bool ToggleButton(const char* str_id, bool* v) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float height = ImGui::GetFrameHeight() * 0.8f;
    float width = height * 1.8f;
    float radius = height * 0.5f;
    ImGui::InvisibleButton(str_id, ImVec2(width, height));
    bool changed = false;
    if (ImGui::IsItemClicked()) {
        *v = !*v;
        changed = true;
    }
    float t = *v ? 1.0f : 0.0f;
    float anim_speed = 0.08f;
    ImGuiID id = ImGui::GetItemID();
    ImGuiStorage* st = ImGui::GetStateStorage();
    float cur = st->GetFloat(id, t);
    if (cur < t) cur = cur + ImGui::GetIO().DeltaTime / anim_speed > t ? t : cur + ImGui::GetIO().DeltaTime / anim_speed;
    else if (cur > t) cur = cur - ImGui::GetIO().DeltaTime / anim_speed < t ? t : cur - ImGui::GetIO().DeltaTime / anim_speed;
    st->SetFloat(id, cur);
    const BuilderColors& c = Colors();
    ImVec4 off = ImGui::IsItemHovered() ? c.bg_active : c.bg_hover;
    ImVec4 on = ImGui::IsItemHovered() ? c.accent_hover : c.accent;
    ImVec4 bg(off.x + (on.x - off.x) * cur, off.y + (on.y - off.y) * cur, off.z + (on.z - off.z) * cur, 1.0f);
    dl->AddRectFilled(p, ImVec2(p.x + width, p.y + height), ImGui::GetColorU32(bg), radius);
    dl->AddCircleFilled(ImVec2(p.x + radius + cur * (width - 2 * radius), p.y + radius), radius - 2.0f,
                        IM_COL32(255, 255, 255, 255));
    return changed;
}

inline bool IconButton(const char* icon, const char* tooltip = nullptr, const ImVec2& size = ImVec2(0, 0),
                       bool enabled = true) {
    if (!enabled) ImGui::BeginDisabled();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool pressed = ImGui::Button(icon, size);
    ImGui::PopStyleColor();
    if (!enabled) ImGui::EndDisabled();
    if (tooltip) Tooltip(tooltip);
    return pressed;
}

inline bool AccentButton(const char* label, const ImVec2& size = ImVec2(0, 0)) {
    const BuilderColors& c = Colors();
    ImGui::PushStyleColor(ImGuiCol_Button, c.accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, c.accent_hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, c.accent_active);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return pressed;
}

inline void TextDim(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    ImGui::PushStyleColor(ImGuiCol_Text, Colors().text_dim);
    ImGui::TextUnformatted(buf);
    ImGui::PopStyleColor();
}

inline void Badge(const char* text, const ImVec4& color) {
    ImVec2 pad(6.0f, 2.0f);
    ImVec2 ts = ImGui::CalcTextSize(text);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 size(ts.x + pad.x * 2, ts.y + pad.y * 2);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), ToU32(color, 0.25f), size.y * 0.5f);
    dl->AddText(ImVec2(p.x + pad.x, p.y + pad.y), ToU32(color), text);
    ImGui::Dummy(size);
}

inline void StatusDot(const ImVec4& color, float radius = 4.0f) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    float h = ImGui::GetTextLineHeight();
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + radius, p.y + h * 0.5f), radius, ToU32(color));
    ImGui::Dummy(ImVec2(radius * 2 + 2, h));
}

inline bool BeginPropertyTable(const char* id, float label_width = 0.0f) {
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX)) return false;
    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed,
                            label_width > 0 ? label_width : ImGui::GetFontSize() * 7.0f);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
    return true;
}

inline void PropertyLabel(const char* label, const char* help = nullptr) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, Colors().text_dim);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    if (help) {
        ImGui::SameLine();
        HelpMarker(help);
    }
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-FLT_MIN);
}

inline void EndPropertyTable() { ImGui::EndTable(); }

inline void HeadingText(ImFont* font, const char* text) {
    if (font) ImGui::PushFont(font);
    ImGui::TextUnformatted(text);
    if (font) ImGui::PopFont();
}

inline const char* NextWrappedLine(ImFont* font, float size, const char* s, const char* end, float wrap_w) {
#if IMGUI_VERSION_NUM >= 19200
    const char* e = font->CalcWordWrapPosition(size, s, end, wrap_w);
#else
    const char* e = font->CalcWordWrapPositionA(size / font->FontSize, s, end, wrap_w);
#endif
    if (e <= s) e = s + 1;
    return e;
}

inline float WrappedTextHeight(const char* text, float wrap_w) {
    if (!text || !*text) return 0.0f;
    ImFont* font = ImGui::GetFont();
    const float size = ImGui::GetFontSize();
    const char* end = text + std::strlen(text);
    int lines = 0;
    for (const char* s = text; s < end;) {
        s = NextWrappedLine(font, size, s, end, wrap_w);
        while (s < end && (*s == ' ' || *s == '\n')) ++s;
        ++lines;
    }
    return lines * ImGui::GetTextLineHeight() + (lines > 1 ? (lines - 1) * 2.0f : 0.0f);
}

inline void CenteredTextWrapped(const char* text, float wrap_w, ImU32 col) {
    if (!text || !*text) return;
    ImFont* font = ImGui::GetFont();
    const float size = ImGui::GetFontSize();
    const float avail = ImGui::GetContentRegionAvail().x;
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const char* end = text + std::strlen(text);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float y = start.y;
    for (const char* s = text; s < end;) {
        const char* e = NextWrappedLine(font, size, s, end, wrap_w);
        const char* trim = e;
        while (trim > s && (trim[-1] == ' ' || trim[-1] == '\n')) --trim;
        float w = font->CalcTextSizeA(size, FLT_MAX, 0.0f, s, trim).x;
        dl->AddText(font, size, ImVec2(std::floor(start.x + (avail - w) * 0.5f), y), col, s, trim);
        y += ImGui::GetTextLineHeight() + 2.0f;
        s = e;
        while (s < end && (*s == ' ' || *s == '\n')) ++s;
    }
    ImGui::Dummy(ImVec2(avail, y - start.y - 2.0f));
}

inline void IconDisc(const char* icon, const ImVec2& center, float radius, const ImVec4& tint) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const BuilderColors& c = Colors();
    dl->AddCircleFilled(center, radius * 1.35f, ToU32(tint, 0.05f), 48);
    dl->AddCircleFilled(center, radius, ToU32(c.bg_frame), 48);
    dl->AddCircle(center, radius, ToU32(tint, 0.35f), 48, 1.0f);
    if (!icon || !*icon) return;
    ImFont* big = IconFontLarge();
    ImFont* font = big ? big : ImGui::GetFont();
    float size = big ? big->FontSize : ImGui::GetFontSize();
    ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, icon);
    dl->AddText(font, size, ImVec2(std::floor(center.x - ts.x * 0.5f), std::floor(center.y - ts.y * 0.5f)), ToU32(tint), icon);
}

inline void EmptyState(const char* icon, const char* title, const char* hint = nullptr, float extra_h = 0.0f) {
    const BuilderColors& c = Colors();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float fs = ImGui::GetFontSize();
    const float wrap = ImMax(fs * 6.0f, ImMin(avail.x - fs * 1.5f, fs * 20.0f));
    ImFont* big = IconFontLarge();
    const bool has_icon = icon && *icon && big;
    const float radius = has_icon ? big->FontSize * 1.15f : 0.0f;
    const float gap = fs * 0.75f;
    const float title_h = WrappedTextHeight(title, wrap);
    const float hint_h = hint ? WrappedTextHeight(hint, wrap) : 0.0f;
    const float total = (has_icon ? radius * 2.0f + gap * 1.4f : 0.0f) + title_h + (hint ? gap * 0.5f + hint_h : 0.0f) + extra_h;
    const float top = ImMax(fs * 0.75f, (avail.y - total) * 0.42f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + top);
    if (has_icon) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        IconDisc(icon, ImVec2(std::floor(p.x + avail.x * 0.5f), std::floor(p.y + radius)), radius, c.accent);
        ImGui::Dummy(ImVec2(avail.x, radius * 2.0f + gap * 1.4f - ImGui::GetStyle().ItemSpacing.y));
    }
    CenteredTextWrapped(title, wrap, ToU32(c.text));
    if (hint) {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + gap * 0.5f - ImGui::GetStyle().ItemSpacing.y);
        CenteredTextWrapped(hint, wrap, ToU32(c.text_dim));
    }
}

inline const char* EllipsizeLeft(const char* text, float max_w, char* buf, size_t n) {
    if (ImGui::CalcTextSize(text).x <= max_w) return text;
    const float ell = ImGui::CalcTextSize("...").x;
    for (const char* p = std::strpbrk(text + 1, "/\\"); p; p = std::strpbrk(p + 1, "/\\")) {
        if (ell + ImGui::CalcTextSize(p).x <= max_w) {
            std::snprintf(buf, n, "...%s", p);
            return buf;
        }
    }
    return nullptr;
}

inline float CenteredRowStart(float row_w) {
    float avail = ImGui::GetContentRegionAvail().x;
    float x = ImGui::GetCursorPosX() + ImMax(0.0f, (avail - row_w) * 0.5f);
    ImGui::SetCursorPosX(x);
    return x;
}

inline float ButtonWidth(const char* label) {
    return ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
}

inline const char* LevelIcon(int level) {
#ifdef ICON_FA_CIRCLE_INFO
    switch (level) {
    case 0: return ICON_FA_BUG;
    case 1: return ICON_FA_CIRCLE_INFO;
    case 2: return ICON_FA_TRIANGLE_EXCLAMATION;
    default: return ICON_FA_CIRCLE_XMARK;
    }
#else
    switch (level) {
    case 0: return "D";
    case 1: return "I";
    case 2: return "W";
    default: return "E";
    }
#endif
}

inline ImVec4 LevelColor(int level) {
    const BuilderColors& c = Colors();
    switch (level) {
    case 0: return c.text_dim;
    case 1: return c.info;
    case 2: return c.warning;
    default: return c.error;
    }
}

}  // namespace imforge::builder::ui
