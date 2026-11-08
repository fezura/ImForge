#include <cctype>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <system_error>

#include "imgui_internal.h"

#include "imforge/imb_runtime.h"

#include "../builder_fonts.h"
#include "../ui/tree_actions.h"
#include "library_shared.h"

namespace fs = std::filesystem;

namespace imforge::builder::library {

namespace {

ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}

std::string Capitalized(const std::string& s) {
    std::string out = s;
    if (!out.empty()) out[0] = (char)std::toupper((unsigned char)out[0]);
    return out;
}

}  // namespace

float& ScrollRequest() {
    static float y = -1.0f;
    return y;
}

bool& SearchFocusRequest() {
    static bool req = false;
    return req;
}

float Dpi(EditorContext& ctx) { return ctx.dpi_scale > 0.0f ? ctx.dpi_scale : 1.0f; }

bool ContainsCI(const std::string& hay, const std::string& needle) {
    if (needle.empty()) return true;
    auto it = std::search(hay.begin(), hay.end(), needle.begin(), needle.end(), [](char a, char b) {
        return std::tolower((unsigned char)a) == std::tolower((unsigned char)b);
    });
    return it != hay.end();
}

std::string FindDataDir(const char* rel) {
    std::vector<std::string> candidates;
    const std::string exe = ExeDir();
    candidates.push_back(exe + "/" + rel);
    candidates.push_back(exe + "/../share/imforge/" + rel);
#ifdef IMFORGE_SOURCE_DIR
    candidates.push_back(std::string(IMFORGE_SOURCE_DIR) + "/" + rel);
#endif
    std::error_code ec;
    for (const std::string& c : candidates)
        if (fs::is_directory(fs::u8path(c), ec)) return fs::u8path(c).lexically_normal().generic_u8string();
    return std::string();
}

const char* Icon(const char* name) { return name ? tree_actions::Icon(name, nullptr) : nullptr; }

void AddCount(Counts& counts, const std::string& key) {
    if (key.empty()) return;
    for (auto& kv : counts)
        if (kv.first == key) {
            ++kv.second;
            return;
        }
    counts.emplace_back(key, 1);
}

bool SearchField(const char* id, const char* hint, std::string& text, float width) {
    const ImGuiStyle& st = ImGui::GetStyle();
    const char* icon = Icon("fa-magnifying-glass");
    const float icon_w = icon ? ImGui::CalcTextSize(icon).x + st.ItemInnerSpacing.x : 0.0f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(st.FramePadding.x + icon_w, st.FramePadding.y));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, std::floor(ImGui::GetFrameHeight() * 0.5f));
    ImGui::SetNextItemWidth(width);
    if (SearchFocusRequest()) {
        ImGui::SetKeyboardFocusHere();
        SearchFocusRequest() = false;
    }
    bool changed = ui::InputTextWithHintStd(id, hint, text);
    if (ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape) && !text.empty()) {
        text.clear();
        changed = true;
    }
    ImGui::PopStyleVar(2);
    if (icon)
        ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + st.FramePadding.x + 2.0f, p.y + st.FramePadding.y),
                                            ToU32(Colors().text_dim), icon);
    return changed;
}

bool ChipRow(const char* id, const Counts& chips, std::string& current, int total, const char* all_label) {
    const BuilderColors& c = Colors();
    const float fs = ImGui::GetFontSize();
    const float h = std::floor(ImGui::GetFrameHeight() * 0.88f);
    const float pad = std::floor(fs * 0.7f), gap = std::floor(fs * 0.4f), inner = std::floor(fs * 0.4f);
    const float avail = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    bool changed = false;
    float x = 0.0f;
    ImGui::PushID(id);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, gap));
    for (int i = -1; i < (int)chips.size(); ++i) {
        const std::string label = i < 0 ? std::string(all_label) : Capitalized(chips[(size_t)i].first);
        const int count = i < 0 ? total : chips[(size_t)i].second;
        const bool active = i < 0 ? current.empty() : current == chips[(size_t)i].first;
        char num[16];
        std::snprintf(num, sizeof(num), "%d", count);
        const float lw = ImGui::CalcTextSize(label.c_str()).x, nw = ImGui::CalcTextSize(num).x;
        const float w = pad * 2.0f + lw + inner + nw;
        if (x > 0.0f) {
            if (x + gap + w > avail) x = 0.0f;
            else ImGui::SameLine();
        }
        ImGui::PushID(i);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##chip", ImVec2(w, h))) {
            current = i < 0 ? std::string() : chips[(size_t)i].first;
            changed = true;
        }
        const bool hov = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
        ImVec4 bg = active ? c.accent : held ? c.bg_active : hov ? c.bg_hover : c.bg_frame;
        if (active && hov) bg = c.accent_hover;
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ToU32(bg), h * 0.5f);
        const float ty = std::floor(p.y + (h - ImGui::GetTextLineHeight()) * 0.5f);
        dl->AddText(ImVec2(p.x + pad, ty), active ? IM_COL32(255, 255, 255, 255) : ToU32(hov ? c.text : c.text_dim), label.c_str());
        dl->AddText(ImVec2(p.x + pad + lw + inner, ty), active ? IM_COL32(255, 255, 255, 170) : ToU32(c.text_disabled), num);
        ImGui::PopID();
        x += (x > 0.0f ? gap : 0.0f) + w;
    }
    ImGui::PopStyleVar();
    ImGui::PopID();
    return changed;
}

bool TagFilter(const char* id, const Counts& tags, std::string& current, float width) {
    std::string preview = current.empty() ? std::string("Any tag") : "#" + current;
    const char* icon = Icon("fa-tag");
    if (icon) preview = std::string(icon) + "  " + preview;
    bool changed = false;
    ImGui::SetNextItemWidth(width);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, std::floor(ImGui::GetFrameHeight() * 0.5f));
    const bool open = ImGui::BeginCombo(id, preview.c_str(), ImGuiComboFlags_HeightLarge);
    ImGui::PopStyleVar();
    if (open) {
        if (ImGui::Selectable("Any tag", current.empty())) {
            current.clear();
            changed = true;
        }
        ImGui::Separator();
        for (const auto& kv : tags) {
            char label[160];
            std::snprintf(label, sizeof(label), "#%s", kv.first.c_str());
            if (ImGui::Selectable(label, current == kv.first)) {
                current = kv.first;
                changed = true;
            }
            ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize("000").x);
            ui::TextDim("%d", kv.second);
        }
        ImGui::EndCombo();
    }
    return changed;
}

void FilterBar(EditorContext& ctx, const char* hint, std::string& search, const Counts& tags, std::string& tag) {
    const float sp = ImGui::GetStyle().ItemSpacing.x;
    const float avail = ImGui::GetContentRegionAvail().x;
    const float tag_w = tags.empty() ? 0.0f : std::floor(std::min(ImGui::GetFontSize() * 10.0f, avail * 0.4f));
    SearchField("##lib_search", hint, search, tags.empty() ? avail : avail - tag_w - sp);
    if (!tags.empty()) {
        ImGui::SameLine();
        TagFilter("##lib_tag", tags, tag, tag_w);
    }
    (void)ctx;
}

void CardBackground(ImDrawList* dl, EditorContext& ctx, const Card& card, bool selected) {
    const BuilderColors& c = Colors();
    const float s = Dpi(ctx), r = std::floor(8.0f * s);
    if (card.hovered || selected)
        imb::Shadow(dl, card.min, card.max, r, IM_COL32(0, 0, 0, card.hovered ? 100 : 60), 14.0f * s, ImVec2(0, 4.0f * s));
    ImVec4 bg = card.held ? c.bg_hover : card.hovered ? c.bg_frame : c.bg_panel;
    if (selected) bg = Mix(bg, c.accent, 0.10f);
    dl->AddRectFilled(card.min, card.max, ToU32(bg), r);
}

void PreviewInset(ImDrawList* dl, EditorContext& ctx, const Card& card, ImU32 bg) {
    const float s = Dpi(ctx), r = std::floor(8.0f * s);
    dl->AddRectFilled(ImVec2(card.pmin.x + 1.0f, card.pmin.y + 1.0f), ImVec2(card.pmax.x - 1.0f, card.pmax.y), bg, r - 1.0f,
                      ImDrawFlags_RoundCornersTop);
    dl->AddLine(ImVec2(card.pmin.x + 1.0f, card.pmax.y), ImVec2(card.pmax.x - 1.0f, card.pmax.y), ToU32(Colors().separator, 0.8f));
}

void CardFooter(ImDrawList* dl, EditorContext& ctx, const Card& card, const char* title, const char* subtitle, bool selected,
                ImU32 dot, const char* right) {
    const BuilderColors& c = Colors();
    const float s = Dpi(ctx), r = std::floor(8.0f * s), pad = std::floor(10.0f * s);
    const float line = ImGui::GetTextLineHeight();
    float w = card.max.x - card.min.x - pad * 2.0f;
    float y = card.pmax.y + std::floor(7.0f * s);
    if (right && *right) {
        const float rw = ImGui::CalcTextSize(right).x;
        if (rw < w * 0.6f) {
            dl->AddText(ImVec2(card.max.x - pad - rw, y), ToU32(c.text_disabled), right);
            w -= rw + pad;
        }
    }
    TextEllipsis(dl, ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(card.min.x + pad, y), w,
                 ToU32(selected ? c.accent_hover : c.text), title);
    y += line + std::floor(1.0f * s);
    float x = card.min.x + pad;
    if (dot) {
        const float dr = std::floor(3.0f * s + 0.5f);
        dl->AddCircleFilled(ImVec2(x + dr, std::floor(y + line * 0.5f) + 0.5f), dr, dot, 12);
        x += dr * 2.0f + std::floor(6.0f * s);
    }
    if (subtitle && *subtitle)
        TextEllipsis(dl, ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(x, y), card.max.x - pad - x, ToU32(c.text_dim), subtitle);
    const ImU32 border = selected ? ToU32(c.accent) : card.hovered ? ToU32(c.accent, 0.55f) : ToU32(c.separator, 0.9f);
    dl->AddRect(card.min, card.max, border, r, 0, selected ? std::floor(1.5f * s + 0.5f) : 1.0f);
}

void CornerBadge(ImDrawList* dl, EditorContext& ctx, const Card& card, const char* text, ImU32 col) {
    if (!text || !*text) return;
    const float s = Dpi(ctx);
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const float px = std::floor(6.0f * s), py = std::floor(1.0f * s), m = std::floor(7.0f * s);
    const ImVec2 mx(card.pmax.x - m, card.pmin.y + m + ts.y + py * 2.0f);
    const ImVec2 mn(mx.x - ts.x - px * 2.0f, card.pmin.y + m);
    ImVec4 cv = ImGui::ColorConvertU32ToFloat4(col);
    dl->AddRectFilled(mn, mx, ImGui::GetColorU32(ImVec4(cv.x, cv.y, cv.z, 0.18f)), (mx.y - mn.y) * 0.5f);
    dl->AddText(ImVec2(mn.x + px, mn.y + py), col, text);
}

void TextEllipsis(ImDrawList* dl, ImFont* font, float size, const ImVec2& pos, float max_w, ImU32 col, const char* text,
                  const char* text_end) {
    if (!text) return;
    if (!text_end) text_end = text + std::strlen(text);
    if (font->CalcTextSizeA(size, FLT_MAX, 0.0f, text, text_end).x <= max_w) {
        dl->AddText(font, size, pos, col, text, text_end);
        return;
    }
    const float dots = font->CalcTextSizeA(size, FLT_MAX, 0.0f, "...").x;
    const char* e = text_end;
    while (e > text && font->CalcTextSizeA(size, FLT_MAX, 0.0f, text, e).x + dots > max_w) --e;
    while (e > text && ((unsigned char)*e & 0xC0) == 0x80) --e;
    dl->AddText(font, size, pos, col, text, e);
    dl->AddText(font, size, ImVec2(pos.x + font->CalcTextSizeA(size, FLT_MAX, 0.0f, text, e).x, pos.y), col, "...");
}

void WrappedTooltip(const char* title, const char* text) {
    if (!ImGui::BeginTooltip()) return;
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
    ImGui::TextUnformatted(title);
    if (text && *text) ui::TextDim("%s", text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

float ActionBarHeight() {
    const ImGuiStyle& st = ImGui::GetStyle();
    return std::floor(ImGui::GetFrameHeight() + st.ItemSpacing.y * 2.0f + 1.0f);
}

void BeginActionBar() {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float pad = ImGui::GetStyle().WindowPadding.x;
    ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x - pad, p.y - 1.0f), ImVec2(p.x + ImGui::GetContentRegionAvail().x + pad, p.y - 1.0f),
                                        ToU32(Colors().separator));
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + ImGui::GetStyle().ItemSpacing.y * 0.5f));
    ImGui::AlignTextToFramePadding();
}

void EndActionBar() {}

StyleScope PushScaledStyle(const Theme* theme, float scale) {
    StyleScope scope;
    if (theme) {
        for (const auto& c : theme->colors) {
            int idx = ImGuiColIndexByName(c.first.c_str());
            if (idx < 0) continue;
            ImGui::PushStyleColor(idx, ImVec4(c.second.x, c.second.y, c.second.z, c.second.w));
            ++scope.colors;
        }
    }
    size_t n = 0;
    const char* const* names = ImGuiStyleVarNames(n);
    ImGuiStyle& style = ImGui::GetStyle();
    for (size_t i = 0; i < n; ++i) {
        interp_detail::StyleVarRef r = interp_detail::StyleVar(style, names[i]);
        if (r.var < 0) continue;
        const Json* tv = nullptr;
        if (theme)
            for (const auto& v : theme->vars)
                if (v.first == names[i]) tv = &v.second;
        const float k = r.scaled ? scale : 1.0f;
        if (r.v) {
            ImVec2 v = *r.v;
            float f = 0.0f;
            ImVec2 v2;
            if (tv && StyleVarValue(*tv, true, f, v2)) v = v2;
            ImGui::PushStyleVar(r.var, ImVec2(v.x * k, v.y * k));
        } else {
            float f = *r.f;
            float tf = 0.0f;
            ImVec2 v2;
            if (tv && StyleVarValue(*tv, false, tf, v2)) f = tf;
            ImGui::PushStyleVar(r.var, f * k);
        }
        ++scope.vars;
    }
    return scope;
}

}  // namespace imforge::builder::library
