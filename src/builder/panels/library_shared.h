#pragma once

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"

#include "../builder_style.h"
#include "../editor_context.h"
#include "../ui/theme_apply.h"
#include "../ui/ui_helpers.h"

namespace imforge::builder::library {

struct Card {
    int index = 0;
    ImVec2 min, max;    // whole card
    ImVec2 pmin, pmax;  // preview area on top
    bool hovered = false, held = false, clicked = false, double_clicked = false, tooltip = false;
};

struct Tab {
    virtual ~Tab() {}
    virtual const char* Title() const = 0;
    virtual const char* IconName() const = 0;
    virtual int Count(EditorContext& ctx) = 0;
    virtual void Draw(EditorContext& ctx) = 0;
    virtual void OnProjectChanged(EditorContext&) {}
    // scripted use (automation "library_tab <tab> <verb> <arg>"), false when the verb is unknown or fails
    virtual bool Command(EditorContext&, const std::string& verb, const std::string& arg) {
        (void)verb;
        (void)arg;
        return false;
    }
};

std::unique_ptr<Tab> MakeAnimationsTab();
std::unique_ptr<Tab> MakeDesignsTab();
std::unique_ptr<Tab> MakeWidgetsTab();
std::unique_ptr<Tab> MakeTemplatesTab();

// at most this many animated previews per frame, the rest draw still
constexpr int kMaxAnimatedPreviews = 48;

// automation: scroll the visible card grid to this offset on the next frame (< 0 means nothing pending)
float& ScrollRequest();
// Ctrl+F in the Library focuses the search field on the next frame
bool& SearchFocusRequest();

#if IMFORGE_HAS_INTERP
// previews borrow the canvas interpreter; this puts back what they touch so the canvas draw
// (diagnostics, registered custom animations, on_item hook) doesn't see any of it
struct InterpScope {
    Interpreter& in;
    const Project* project;
    std::function<void(const Node&, const ImVec2&, const ImVec2&, bool)> on_item;
    std::vector<Diagnostic> diagnostics;
    std::vector<std::string> anim_errors;
    int diag_frame;
    const Project* anim_project;
    unsigned long long anim_hash;
    explicit InterpScope(Interpreter& i)
        : in(i), project(i.project), on_item(i.hooks.on_item), diagnostics(i.last_diagnostics), anim_errors(i.anim_errors),
          diag_frame(i.diag_frame), anim_project(i.anim_project), anim_hash(i.anim_hash) {}
    ~InterpScope() {
        const bool resync = in.anim_project != anim_project || in.anim_hash != anim_hash;
        in.hooks.on_item = std::move(on_item);
        in.last_diagnostics = std::move(diagnostics);
        in.anim_errors = std::move(anim_errors);
        in.diag_frame = diag_frame;
        in.project = project;
        in.anim_project = resync ? nullptr : anim_project;
        in.anim_hash = anim_hash;
    }
    InterpScope(const InterpScope&) = delete;
    InterpScope& operator=(const InterpScope&) = delete;
};
#endif

float Dpi(EditorContext& ctx);
bool ContainsCI(const std::string& hay, const std::string& needle);
std::string FindDataDir(const char* rel);
const char* Icon(const char* name);

using Counts = std::vector<std::pair<std::string, int>>;
void AddCount(Counts& counts, const std::string& key);

bool SearchField(const char* id, const char* hint, std::string& text, float width);
bool ChipRow(const char* id, const Counts& chips, std::string& current, int total, const char* all_label = "All");
bool TagFilter(const char* id, const Counts& tags, std::string& current, float width);
void FilterBar(EditorContext& ctx, const char* hint, std::string& search, const Counts& tags, std::string& tag);

void CardBackground(ImDrawList* dl, EditorContext& ctx, const Card& c, bool selected);
void PreviewInset(ImDrawList* dl, EditorContext& ctx, const Card& c, ImU32 bg);
void CardFooter(ImDrawList* dl, EditorContext& ctx, const Card& c, const char* title, const char* subtitle, bool selected,
                ImU32 dot = 0, const char* right = nullptr);
void CornerBadge(ImDrawList* dl, EditorContext& ctx, const Card& c, const char* text, ImU32 col);
void TextEllipsis(ImDrawList* dl, ImFont* font, float size, const ImVec2& pos, float max_w, ImU32 col, const char* text,
                  const char* text_end = nullptr);
void WrappedTooltip(const char* title, const char* text);

float ActionBarHeight();
void BeginActionBar();
void EndActionBar();

StyleScope PushScaledStyle(const Theme* theme, float scale);

// Card grid in a scrolling child, only visible rows are drawn (ImGuiListClipper). fn(const Card&) runs
// right after the card's InvisibleButton, so it can open a drag source before drawing anything.
template <class Fn>
void CardGrid(EditorContext& ctx, const char* id, int count, float min_w, float aspect, float footer_h, float reserve,
              const char* empty_icon, const char* empty_title, const char* empty_hint, Fn&& fn) {
    const float s = Dpi(ctx);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(std::floor(10.0f * s), std::floor(8.0f * s)));
    const bool open = ImGui::BeginChild(id, ImVec2(0, -reserve), ImGuiChildFlags_AlwaysUseWindowPadding, 0);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    if (open) {
        if (ScrollRequest() >= 0.0f) {
            ImGui::SetScrollY(ScrollRequest());
            ScrollRequest() = -1.0f;
        }
        if (count <= 0) {
            ui::EmptyState(Icon(empty_icon), empty_title, empty_hint);
        } else {
            const float gap = std::floor(10.0f * s);
            const float avail = ImGui::GetContentRegionAvail().x;
            const int cols = std::max(1, (int)((avail + gap) / (std::floor(min_w * s) + gap)));
            const float cw = std::floor((avail - gap * (float)(cols - 1)) / (float)cols);
            const float ph = std::floor(cw * aspect);
            const float ch = ph + footer_h;
            const int rows = (count + cols - 1) / cols;
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, gap));
            ImGuiListClipper clipper;
            clipper.Begin(rows, ch + gap);
            while (clipper.Step()) {
                for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
                    const ImVec2 row = ImGui::GetCursorScreenPos();
                    for (int c = 0; c < cols; ++c) {
                        const int i = r * cols + c;
                        if (i >= count) break;
                        Card card;
                        card.index = i;
                        card.min = ImVec2(row.x + (cw + gap) * (float)c, row.y);
                        card.max = ImVec2(card.min.x + cw, card.min.y + ch);
                        card.pmin = card.min;
                        card.pmax = ImVec2(card.max.x, card.min.y + ph);
                        ImGui::SetCursorScreenPos(card.min);
                        ImGui::PushID(i);
                        card.clicked = ImGui::InvisibleButton("##card", ImVec2(cw, ch));
                        card.hovered = ImGui::IsItemHovered();
                        card.held = ImGui::IsItemActive();
                        card.double_clicked = card.hovered && ImGui::IsMouseDoubleClicked(0);
                        card.tooltip = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay);
                        fn(card);
                        ImGui::PopID();
                    }
                    ImGui::SetCursorScreenPos(row);
                    ImGui::Dummy(ImVec2(avail, ch));
                }
            }
            ImGui::PopStyleVar();
        }
    }
    ImGui::EndChild();
}

}  // namespace imforge::builder::library
