#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "imgui.h"

#include "imforge/imb_runtime.h"

#include "../builder_style.h"
#include "../editor_context.h"
#include "../ui/anim_preview_driver.h"
#include "../ui/ease_curves.h"
#include "../ui/icon_table.h"
#include "../ui/ui_helpers.h"
#include "anim_timeline_editors.h"
#include "editor_panel.h"

namespace imforge::builder {

namespace anim_timeline {

bool IsSpring(const Transition& tr) { return tr.stiffness > 0.0f || IsSpringEaseName(tr.ease.c_str()); }

float TransitionLength(const Transition& tr) {
    if (!IsSpring(tr)) return tr.duration > 0.0f ? tr.duration : 0.0f;
    ui::SpringParams sp;
    sp.stiffness = tr.stiffness > 0.0f ? tr.stiffness : 170.0f;
    sp.damping = tr.damping > 0.0f ? tr.damping : 26.0f;
    return ui::SpringSettleTime(sp);
}

void BuildRows(const Node& node, std::vector<Row>& out) {
    out.clear();
    const AnimSpec& a = node.anim;
    for (size_t i = 0; i < a.transitions.size(); ++i) {
        const Transition& tr = a.transitions[i];
        Row r;
        r.kind = RowKind::Transition;
        r.index = (int)i;
        r.duration = TransitionLength(tr);
        r.spring = IsSpring(tr);
        r.label = tr.prop;
        r.badge = tr.on;
        out.push_back(r);
    }
    if (!a.enter_preset.empty()) {
        Row r;
        r.kind = RowKind::Enter;
        r.duration = a.enter_duration;
        r.label = "enter " + a.enter_preset;
        r.badge = "visible";
        out.push_back(r);
        if (a.stagger > 0.0f && !node.children.empty()) {
            int n = std::min((int)node.children.size(), kMaxChildRows);
            for (int i = 0; i < n; ++i) {
                Row c;
                c.kind = RowKind::Child;
                c.index = i;
                c.start = a.stagger * (float)i;
                c.duration = a.enter_duration;
                c.label = "  " + node.children[(size_t)i].id;
                c.badge = "stagger";
                out.push_back(c);
            }
        }
    }
    if (!a.exit_preset.empty()) {
        Row r;
        r.kind = RowKind::Exit;
        r.duration = a.exit_duration;
        r.label = "exit " + a.exit_preset;
        r.badge = "hidden";
        out.push_back(r);
    }
}

float NodeTimelineLength(const Node& node) {
    std::vector<Row> rows;
    BuildRows(node, rows);
    float len = 1.0f;
    for (const Row& r : rows) len = std::max(len, r.start + r.duration);
    return std::ceil(len * 4.0f) / 4.0f;
}

bool HasAnimOrFx(const Node& node) { return !node.anim.Empty() || !node.fx.empty(); }

ImVec4 TriggerColor(const std::string& on) {
    const BuilderColors& c = Colors();
    if (on == "hover") return c.accent;
    if (on == "active") return c.warning;
    if (on == "open") return c.success;
    if (on == "visible") return c.info;
    if (on == "focus") return ImVec4(0.75f, 0.55f, 0.95f, 1.0f);
    if (on == "disabled") return c.text_dim;
    if (on == "hidden") return c.error;
    return c.text_dim;
}

}  // namespace anim_timeline

namespace {

using namespace anim_timeline;

const float kLabelWidth = 150.0f;
const float kBadgeWidth = 64.0f;
const float kThumbWidth = 44.0f;
const float kRowHeight = 22.0f;
const float kRulerHeight = 18.0f;
const float kHandleWidth = 8.0f;
const float kMinDuration = 0.02f;
const float kMaxDuration = 10.0f;

class AnimTimelinePanel final : public EditorPanel {
public:
    const char* Name() const override { return "Anim Timeline"; }
    const char* DefaultDock() const override { return "bottom"; }

    void Draw(EditorContext& ctx) override {
        if (!ctx.has_project) {
            ui::EmptyState(IconUtf8("fa-wave-square"), "No project open", "Preview transitions, enter/exit presets and effects of the selected widget.");
            return;
        }
        if (ImGui::BeginTabBar("anim_tabs")) {
            if (ImGui::BeginTabItem("Node")) {
                DrawNodeTab(ctx);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Panel")) {
                DrawPanelTab(ctx);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }

private:
    void DrawNodeTab(EditorContext& ctx) {
        Node* node = ctx.SelectedNode();
        if (!node) {
            ui::TextDim("Select a node to see its transitions, enter/exit presets and effects.");
            DrawTransport(ctx, nullptr);
            return;
        }
        if (node->id != last_node_id_) {
            last_node_id_ = node->id;
            selected_transition_ = node->anim.transitions.empty() ? -1 : 0;
        }
        if (selected_transition_ >= (int)node->anim.transitions.size()) selected_transition_ = -1;
        AnimPreviewResolveRequest(*node);
        GetAnimPreviewDriver().duration = NodeTimelineLength(*node);

        DrawTransport(ctx, node);
        ImGui::Separator();
        DrawQuickAdd(ctx, *node);
        ImGui::Separator();

        float avail = ImGui::GetContentRegionAvail().y;
        float timeline_h = std::max(90.0f, std::min(avail * 0.45f, 220.0f));
        if (ImGui::BeginChild("timeline", ImVec2(0, timeline_h), ImGuiChildFlags_None,
                              ImGuiWindowFlags_HorizontalScrollbar)) {
            DrawTimeline(ctx, *node);
        }
        ImGui::EndChild();
        ImGui::Separator();

        if (ImGui::BeginChild("editors", ImVec2(0, 0), ImGuiChildFlags_None)) {
            if (ImGui::BeginTable("editor_split", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
                ImGui::TableSetupColumn("curve", ImGuiTableColumnFlags_WidthStretch, 0.6f);
                ImGui::TableSetupColumn("fx", ImGuiTableColumnFlags_WidthStretch, 0.4f);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (selected_transition_ >= 0) DrawCurveEditor(ctx, *node, selected_transition_);
                else DrawPresetEditor(ctx, *node);
                ImGui::TableSetColumnIndex(1);
                DrawFxPreview(ctx, *node);
                ImGui::EndTable();
            }
        }
        ImGui::EndChild();
    }

    void DrawTransport(EditorContext& ctx, Node* node) {
        (void)ctx;
        AnimPreviewDriver& d = GetAnimPreviewDriver();
        const BuilderColors& c = Colors();
        if (d.playing) {
            if (ImGui::Button("Pause")) AnimPreviewPlay(false);
        } else {
            if (ui::AccentButton("Play")) AnimPreviewPlay(true);
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop")) AnimPreviewStop();
        ui::Tooltip("Pause and rewind the preview to 0 s (resets runtime animation state)");
        ImGui::SameLine();
        if (ImGui::Button("Replay")) AnimPreviewReplay();
        ui::Tooltip("Restart every animation in the canvas from 0 s");
        ImGui::SameLine();
        ImGui::Checkbox("Loop", &d.loop);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f);
        ImGui::SliderFloat("##speed", &d.time_scale, 0.1f, 2.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
        ui::Tooltip("Preview time scale");
        ImGui::SameLine();
        float shown = std::min(d.time, d.duration);
        ImGui::SetNextItemWidth(std::max(120.0f, ImGui::GetContentRegionAvail().x * 0.35f));
        if (ImGui::SliderFloat("##scrub", &shown, 0.0f, d.duration, "%.2f s", ImGuiSliderFlags_AlwaysClamp))
            AnimPreviewSeek(shown);
        ImGui::SameLine();
        ImGui::Checkbox("Animate canvas", &d.preview_animations);
        ui::Tooltip("Off: the canvas draws every node in its settled state (InterpOptions.animations = false)");

        if (!node) return;
        ImGui::AlignTextToFramePadding();
        ui::TextDim("Force");
        ImGui::SameLine();
        for (int i = 0; i < kTransitionTriggerCount; ++i) {
            const char* trig = TransitionTriggerName(i);
            int st = AnimPreviewForcedState(node->id, trig);
            char label[48];
            std::snprintf(label, sizeof(label), "%s: %s###force%d", trig, st < 0 ? "auto" : (st ? "on" : "off"), i);
            ImVec4 col = st < 0 ? c.bg_frame : (st ? TriggerColor(trig) : c.bg_active);
            ImGui::PushStyleColor(ImGuiCol_Button, st < 0 ? col : WithAlpha(col, 0.45f));
            if (ImGui::SmallButton(label)) AnimPreviewSetTrigger(node->id, trig, st < 0 ? 1 : (st ? 0 : -1));
            ImGui::PopStyleColor();
            ui::Tooltip("Cycle auto / on / off for this trigger on the selected node (design mode only)");
            if (i + 1 < kTransitionTriggerCount) ImGui::SameLine();
        }
        if (d.node_id == node->id && (d.override_mask != 0 || d.transition_index >= 0)) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear")) AnimPreviewClearTriggers();
        }
    }

    void DrawTimeline(EditorContext& ctx, Node& node) {
        BuildRows(node, rows_);
        AnimPreviewDriver& d = GetAnimPreviewDriver();
        const BuilderColors& c = Colors();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 origin = ImGui::GetCursorScreenPos();
        float total_w = std::max(ImGui::GetContentRegionAvail().x, kLabelWidth + kBadgeWidth + kThumbWidth + 200.0f);
        float bar_x0 = origin.x + kLabelWidth + kBadgeWidth;
        float bar_x1 = origin.x + total_w - kThumbWidth - 8.0f;
        if (!drag_active_) {
            drag_length_ = d.duration;
            drag_pps_ = (bar_x1 - bar_x0) / drag_length_;
        }
        const float length = drag_length_;
        const float pps = drag_pps_;
        float rows_h = kRulerHeight + kRowHeight * (float)std::max<size_t>(rows_.size(), 1);

        DrawRuler(dl, ImVec2(bar_x0, origin.y), ImVec2(bar_x1, origin.y + kRulerHeight), length, pps);
        ImGui::SetCursorScreenPos(ImVec2(bar_x0, origin.y));
        ImGui::InvisibleButton("##ruler", ImVec2(std::max(bar_x1 - bar_x0, 1.0f), kRulerHeight));
        if (ImGui::IsItemActive() || ImGui::IsItemClicked()) {
            float t = (ImGui::GetMousePos().x - bar_x0) / pps;
            AnimPreviewSeek(std::max(0.0f, std::min(t, length)));
        }
        ui::Tooltip("Click or drag to scrub");

        if (rows_.empty()) {
            ImGui::SetCursorScreenPos(ImVec2(origin.x + 4.0f, origin.y + kRulerHeight + 4.0f));
            ui::TextDim("No transitions or presets on this node. Use the quick-add buttons above.");
        }
        for (size_t i = 0; i < rows_.size(); ++i) DrawRow(ctx, node, rows_[i], (int)i, origin, bar_x0, bar_x1, pps);

        float ph = bar_x0 + std::min(d.time, length) * pps;
        ImU32 ph_col = ToU32(c.accent);
        dl->AddLine(ImVec2(ph, origin.y), ImVec2(ph, origin.y + rows_h), ph_col, 1.5f);
        dl->AddTriangleFilled(ImVec2(ph - 5.0f, origin.y), ImVec2(ph + 5.0f, origin.y), ImVec2(ph, origin.y + 6.0f), ph_col);
        ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + rows_h));
        ImGui::Dummy(ImVec2(total_w, 1.0f));
    }

    static void DrawRuler(ImDrawList* dl, const ImVec2& min, const ImVec2& max, float length, float pps) {
        const BuilderColors& c = Colors();
        ImU32 line = ToU32(c.separator), text = ToU32(c.text_dim);
        dl->AddLine(ImVec2(min.x, max.y - 1.0f), ImVec2(max.x, max.y - 1.0f), line);
        float step = length <= 2.0f ? 0.1f : (length <= 5.0f ? 0.25f : 0.5f);
        int major_every = length <= 2.0f ? 5 : (length <= 5.0f ? 4 : 2);
        int n = (int)std::floor(length / step + 0.5f);
        for (int i = 0; i <= n; ++i) {
            float x = min.x + (float)i * step * pps;
            bool major = (i % major_every) == 0;
            dl->AddLine(ImVec2(x, max.y - (major ? 8.0f : 4.0f)), ImVec2(x, max.y - 1.0f), line);
            if (major) {
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%.2gs", (double)((float)i * step));
                dl->AddText(ImVec2(x + 2.0f, min.y), text, buf);
            }
        }
    }

    void DrawRow(EditorContext& ctx, Node& node, const Row& row, int row_index, const ImVec2& origin, float bar_x0,
                 float bar_x1, float pps) {
        const BuilderColors& c = Colors();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float y0 = origin.y + kRulerHeight + kRowHeight * (float)row_index;
        float y1 = y0 + kRowHeight;
        bool selected = row.kind == RowKind::Transition ? row.index == selected_transition_
                                                        : (selected_transition_ < 0 && row.kind != RowKind::Child);
        ImGui::PushID(row_index);
        ImGui::SetCursorScreenPos(ImVec2(origin.x, y0));
        ImGui::InvisibleButton("##row", ImVec2(std::max(bar_x1 - origin.x, 1.0f), kRowHeight));
        bool hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked()) selected_transition_ = row.kind == RowKind::Transition ? row.index : -1;
        if (selected || hovered)
            dl->AddRectFilled(ImVec2(origin.x, y0), ImVec2(bar_x1 + kThumbWidth + 8.0f, y1),
                              ToU32(selected ? c.selection : c.bg_hover, selected ? 0.35f : 0.5f));
        dl->AddLine(ImVec2(origin.x, y1), ImVec2(bar_x1 + kThumbWidth + 8.0f, y1), ToU32(c.separator, 0.5f));

        ImVec4 text_col = row.kind == RowKind::Child ? c.text_dim : c.text;
        dl->PushClipRect(ImVec2(origin.x, y0), ImVec2(origin.x + kLabelWidth - 4.0f, y1), true);
        dl->AddText(ImVec2(origin.x + 4.0f, y0 + 4.0f), ToU32(text_col), row.label.c_str());
        dl->PopClipRect();

        ImVec4 bc = TriggerColor(row.badge);
        ImVec2 bts = ImGui::CalcTextSize(row.badge.c_str());
        ImVec2 bp(origin.x + kLabelWidth, y0 + (kRowHeight - bts.y - 4.0f) * 0.5f);
        dl->AddRectFilled(bp, ImVec2(bp.x + bts.x + 10.0f, bp.y + bts.y + 4.0f), ToU32(bc, 0.25f), (bts.y + 4.0f) * 0.5f);
        dl->AddText(ImVec2(bp.x + 5.0f, bp.y + 2.0f), ToU32(bc), row.badge.c_str());

        float bx0 = bar_x0 + row.start * pps;
        float bx1 = bx0 + std::max(row.duration, 0.0f) * pps;
        if (bx1 - bx0 < 2.0f) bx1 = bx0 + 2.0f;
        ImVec4 bar_col = row.kind == RowKind::Exit ? c.error : (row.kind == RowKind::Child ? c.accent_dim : c.accent);
        ImU32 fill = ToU32(bar_col, row.spring ? 0.45f : 0.8f);
        dl->AddRectFilled(ImVec2(bx0, y0 + 4.0f), ImVec2(bx1, y1 - 4.0f), fill, 3.0f);
        if (row.spring) dl->AddRect(ImVec2(bx0, y0 + 4.0f), ImVec2(bx1, y1 - 4.0f), ToU32(bar_col), 3.0f);
        char dur[24];
        std::snprintf(dur, sizeof(dur), row.spring ? "~%.2fs" : "%.2fs", (double)row.duration);
        ImVec2 dts = ImGui::CalcTextSize(dur);
        float dx = bx1 - bx0 > dts.x + 8.0f ? bx0 + 4.0f : bx1 + 4.0f;
        dl->AddText(ImVec2(dx, y0 + 4.0f), ToU32(c.text), dur);

        DrawDurationHandle(ctx, node, row, bx0, bx1, y0, y1, pps);

        ImVec2 tmin(bar_x1 + 6.0f, y0 + 3.0f), tmax(bar_x1 + kThumbWidth, y1 - 3.0f);
        if (row.kind == RowKind::Transition) {
            const Transition& tr = node.anim.transitions[(size_t)row.index];
            if (IsSpring(tr)) {
                ui::SpringParams sp;
                sp.stiffness = tr.stiffness > 0 ? tr.stiffness : 170.0f;
                sp.damping = tr.damping > 0 ? tr.damping : 26.0f;
                ui::DrawSpringCurve(dl, tmin, tmax, sp, ToU32(c.text), 1.0f);
            } else {
                ui::DrawEaseCurve(dl, tmin, tmax, EaseFromName(tr.ease.c_str()), ToU32(c.text), 1.0f, 24);
            }
        } else if (row.kind != RowKind::Child) {
            std::string preset = row.kind == RowKind::Enter ? node.anim.enter_preset : node.anim.exit_preset;
            imb::Ease e = PresetFromName(preset.c_str()) == imb::Preset::ScalePop ? imb::Ease::OutBack : imb::Ease::OutCubic;
            ui::DrawEaseCurve(dl, tmin, tmax, e, ToU32(c.text_dim), 1.0f, 24);
        }
        ImGui::PopID();
    }

    void DrawDurationHandle(EditorContext& ctx, Node& node, const Row& row, float bx0, float bx1, float y0, float y1,
                            float pps) {
        if (row.spring) return;
        ImGui::SetCursorScreenPos(ImVec2(bx1 - kHandleWidth * 0.5f, y0 + 2.0f));
        ImGui::InvisibleButton("##end", ImVec2(kHandleWidth, y1 - y0 - 4.0f));
        bool active = ImGui::IsItemActive();
        if (ImGui::IsItemActivated()) drag_active_ = true;
        if (ImGui::IsItemDeactivated()) drag_active_ = false;
        if (ImGui::IsItemHovered() || active) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (active) {
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(bx1 - 2.0f, y0 + 3.0f), ImVec2(bx1 + 2.0f, y1 - 3.0f),
                                                      ToU32(Colors().text));
            float new_dur = (ImGui::GetMousePos().x - bx0) / pps;
            new_dur = std::floor(new_dur * 100.0f + 0.5f) / 100.0f;
            new_dur = std::max(kMinDuration, std::min(new_dur, kMaxDuration));
            if (std::fabs(new_dur - row.duration) > 1e-4f) {
                ctx.Edit("Change animation duration", [&] {
                    if (row.kind == RowKind::Transition) node.anim.transitions[(size_t)row.index].duration = new_dur;
                    else if (row.kind == RowKind::Exit) node.anim.exit_duration = new_dur;
                    else node.anim.enter_duration = new_dur;
                });
            }
        }
    }

    void DrawPanelTab(EditorContext& ctx) {
        AnimPreviewDriver& d = GetAnimPreviewDriver();
        Node* root = ctx.ActiveRoot();
        ImGui::Checkbox("Preview animations in the canvas", &d.preview_animations);
        ImGui::SameLine();
        ui::HelpMarker("When off, the canvas passes InterpOptions.animations = false and draws every node settled.");
        if (!root) {
            ui::TextDim("No active panel or component.");
            return;
        }
        std::vector<const Node*> animated;
        Walk(*root, [&](const Node& n) {
            if (HasAnimOrFx(n)) animated.push_back(&n);
        });
        ui::TextDim("%d animated node%s in %s", (int)animated.size(), animated.size() == 1 ? "" : "s",
                    ctx.active_panel.c_str());
        if (animated.empty()) return;
        ImGuiTableFlags table_flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
                                ImGuiTableFlags_SizingStretchProp;
        if (!ImGui::BeginTable("anim_nodes", 6, table_flags)) return;
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Id");
        ImGui::TableSetupColumn("Type");
        ImGui::TableSetupColumn("Transitions");
        ImGui::TableSetupColumn("Enter");
        ImGui::TableSetupColumn("Exit");
        ImGui::TableSetupColumn("Fx");
        ImGui::TableHeadersRow();
        for (const Node* n : animated) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            bool sel = ctx.IsSelected(n->id);
            if (ImGui::Selectable(n->id.c_str(), sel, ImGuiSelectableFlags_SpanAllColumns)) ctx.Select(n->id);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(n->type.c_str());
            ImGui::TableSetColumnIndex(2);
            if (n->anim.transitions.empty()) ui::TextDim("-");
            else {
                std::string s;
                for (const Transition& tr : n->anim.transitions) s += (s.empty() ? "" : ", ") + tr.prop + "@" + tr.on;
                ImGui::TextUnformatted(s.c_str());
            }
            ImGui::TableSetColumnIndex(3);
            if (n->anim.enter_preset.empty()) ui::TextDim("-");
            else ImGui::Text("%s %.2fs%s", n->anim.enter_preset.c_str(), (double)n->anim.enter_duration, n->anim.stagger > 0 ? " +stagger" : "");
            ImGui::TableSetColumnIndex(4);
            if (n->anim.exit_preset.empty()) ui::TextDim("-");
            else ImGui::Text("%s %.2fs", n->anim.exit_preset.c_str(), (double)n->anim.exit_duration);
            ImGui::TableSetColumnIndex(5);
            if (n->fx.empty()) ui::TextDim("-");
            else {
                std::string s;
                for (const FxSpec& f : n->fx) s += (s.empty() ? "" : ", ") + f.type;
                ImGui::TextUnformatted(s.c_str());
            }
        }
        ImGui::EndTable();
    }

    std::string last_node_id_;
    int selected_transition_ = -1;
    std::vector<Row> rows_;
    bool drag_active_ = false;
    float drag_length_ = 1.0f;
    float drag_pps_ = 1.0f;
};

}  // namespace

IMFORGE_REGISTER_PANEL(AnimTimelinePanel, 700);

}  // namespace imforge::builder
