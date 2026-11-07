#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "imgui.h"

#include "../builder_fonts.h"
#include "../builder_style.h"
#include "../editor_context.h"
#include "../export.h"
#include "../ui/code_text.h"
#include "../ui/file_dialog.h"
#include "../ui/ui_helpers.h"
#include "conflicts_state.h"
#include "editor_panel.h"

namespace imforge::builder {

namespace {

const char* const kImportDialogKey = "imforge_import_code";

struct ConflictView {
    ui::CodeText user, generated;
};

class ConflictsPanel final : public EditorPanel {
public:
    const char* Name() const override { return "Conflicts"; }
    const char* DefaultDock() const override { return "bottom"; }

    void Draw(EditorContext& ctx) override {
        ConflictsState& cs = GetConflictsState();
        RebuildViews(cs);
        DrawToolbar(ctx, cs);
        DrawImportDialog(ctx);
        if (cs.conflicts.empty()) {
            ImGui::Spacing();
            if (!cs.report.empty()) {
                ui::TextDim("No conflicts from the last export.");
                DrawReport(cs);
            } else {
                ui::TextDim("Round-trip conflicts from export (Ctrl+E) or an imported hand-edited file appear here.");
            }
            return;
        }
        float list_h = ImGui::GetTextLineHeightWithSpacing() * (float)std::min((int)cs.conflicts.size() + 1, 6) +
                       ImGui::GetStyle().ItemSpacing.y * 2;
        DrawList(ctx, cs, list_h);
        if (current_ >= (int)cs.conflicts.size()) current_ = (int)cs.conflicts.size() - 1;
        if (current_ < 0) current_ = 0;
        DrawDetail(ctx, cs, (size_t)current_);
    }

private:
    void RebuildViews(const ConflictsState& cs) {
        if (built_generation_ == cs.generation && views_.size() == cs.conflicts.size()) return;
        views_.clear();
        views_.resize(cs.conflicts.size());
        for (size_t i = 0; i < cs.conflicts.size(); ++i) {
            views_[i].user.SetText(cs.conflicts[i].user_code);
            views_[i].generated.SetText(cs.conflicts[i].generated_code);
        }
        built_generation_ = cs.generation;
        if (current_ >= (int)cs.conflicts.size()) current_ = 0;
    }

    void DrawToolbar(EditorContext& ctx, ConflictsState& cs) {
        int unresolved = cs.Unresolved();
        if (cs.conflicts.empty()) ui::TextDim("No conflicts");
        else if (unresolved == 0) {
            ImGui::PushStyleColor(ImGuiCol_Text, Colors().success);
            ImGui::Text("%d conflict(s), all resolved", (int)cs.conflicts.size());
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, Colors().warning);
            ImGui::Text("%d conflict(s), %d unresolved", (int)cs.conflicts.size(), unresolved);
            ImGui::PopStyleColor();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        if (unresolved == 0) ImGui::BeginDisabled();
        if (ImGui::Button("Apply all: keep my code")) ResolveAll(ctx, cs, true);
        ui::Tooltip("Lock every conflicting node and keep the hand-written code");
        ImGui::SameLine();
        if (ImGui::Button("Apply all: regenerate")) ResolveAll(ctx, cs, false);
        ui::Tooltip("Discard every hand edit and write the generated code");
        if (unresolved == 0) ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        if (ImGui::Button("Import hand-edited file...")) {
            FileDialogOptions o;
            o.title = "Import generated file";
            o.extensions = {".cpp", ".h", ".hpp"};
            std::string start = cs.output_dir.empty() ? (ctx.has_project ? ExportOutputDir(ctx.project) : std::string()) : cs.output_dir;
            OpenFileDialog(kImportDialogKey, start, o);
        }
        ui::Tooltip("Reopen a generated file you edited by hand: literal edits go back into the panel, the rest shows up here");
        ImGui::SameLine();
        bool any_resolved = false;
        for (const ConflictItem& c : cs.conflicts) any_resolved = any_resolved || c.resolved;
        if (!any_resolved) ImGui::BeginDisabled();
        if (ImGui::Button("Clear resolved")) {
            cs.conflicts.erase(std::remove_if(cs.conflicts.begin(), cs.conflicts.end(),
                                              [](const ConflictItem& c) { return c.resolved; }),
                               cs.conflicts.end());
            cs.Touch();
            current_ = 0;
        }
        if (!any_resolved) ImGui::EndDisabled();
        if (!cs.output_dir.empty()) {
            ImGui::SameLine();
            ui::TextDim("%s", cs.output_dir.c_str());
        }
        ImGui::Separator();
    }

    void DrawImportDialog(EditorContext& ctx) {
        std::string path;
        if (!DrawFileDialog(kImportDialogKey, path)) return;
        std::string err;
        if (!ImportFromCode(ctx, path, &err)) ctx.SetStatus("Import failed: " + err, 8.0);
        current_ = 0;
    }

    void DrawList(EditorContext& ctx, ConflictsState& cs, float height) {
        ImGuiTableFlags table_flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
                                      ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
        if (!ImGui::BeginTable("conflicts", 5, table_flags, ImVec2(0, height))) return;
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Panel", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 7.0f);
        ImGui::TableSetupColumn("Node", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 8.0f);
        ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 9.0f);
        ImGui::TableSetupColumn("Reason", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 6.0f);
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < cs.conflicts.size(); ++i) {
            const ConflictItem& c = cs.conflicts[i];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            char id[32];
            std::snprintf(id, sizeof(id), "##conflict_%d", (int)i);
            bool selected = (int)i == current_;
            if (ImGui::Selectable(id, selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
                current_ = (int)i;
                SelectNode(ctx, c);
            }
            ImGui::SameLine(0, 0);
            ImGui::TextUnformatted(c.panel.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(c.node_id.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(std::filesystem::path(c.file).filename().generic_string().c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", c.file.c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(c.reason.c_str());
            ImGui::TableSetColumnIndex(4);
            if (!c.resolved) ui::Badge("open", Colors().warning);
            else if (c.keep_user) ui::Badge("kept", Colors().info);
            else ui::Badge("regen", Colors().success);
        }
        ImGui::EndTable();
    }

    void DrawDetail(EditorContext& ctx, ConflictsState& cs, size_t index) {
        const ConflictItem& c = cs.conflicts[index];
        ImGui::Spacing();
        ui::HeadingText(ctx.Font(FontSlot::Heading), c.node_id.c_str());
        ImGui::SameLine();
        ui::TextDim("in %s  -  %s", c.panel.c_str(), c.reason.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Select node")) SelectNode(ctx, c);
        bool resolved = c.resolved;
        if (resolved) ImGui::BeginDisabled();
        ImGui::SameLine();
        if (ui::AccentButton("Keep my code (lock node)")) Resolve(ctx, index, true);
        ui::Tooltip("Keep the hand-written block as a locked function and mark the node locked (undoable)");
        ImGui::SameLine();
        if (ImGui::Button("Regenerate")) Resolve(ctx, index, false);
        ui::Tooltip("Replace the hand-written block with the generated code");
        if (resolved) ImGui::EndDisabled();
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float report_h = cs.report.empty() ? 0.0f : ImGui::GetFrameHeightWithSpacing();
        float h = std::max(avail.y - report_h - ImGui::GetStyle().ItemSpacing.y, ImGui::GetTextLineHeight() * 4.0f);
        float w = (avail.x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        ImFont* mono = GetFonts().mono ? GetFonts().mono : ctx.Font(FontSlot::Mono);
        ImGui::BeginGroup();
        ui::TextDim("Your code");
        ui::CodeTextDrawOptions o;
        o.font = mono;
        o.clickable = false;
        views_[index].user.Draw("user_code", ImVec2(w, h - ImGui::GetTextLineHeightWithSpacing()), o);
        ImGui::EndGroup();
        ImGui::SameLine();
        ImGui::BeginGroup();
        ui::TextDim("Generated code");
        views_[index].generated.Draw("generated_code", ImVec2(w, h - ImGui::GetTextLineHeightWithSpacing()), o);
        ImGui::EndGroup();
        if (!cs.report.empty()) DrawReport(cs);
    }

    void DrawReport(const ConflictsState& cs) {
        if (!ImGui::CollapsingHeader("Last export / import report")) return;
        for (const std::string& line : cs.report) ImGui::BulletText("%s", line.c_str());
        if (!cs.updated_nodes.empty()) {
            ui::TextDim("Nodes updated from hand-edited literals:");
            for (const std::string& n : cs.updated_nodes) ImGui::BulletText("%s", n.c_str());
        }
    }

    void SelectNode(EditorContext& ctx, const ConflictItem& c) {
        if (!c.panel.empty() && ctx.FindPanelDoc(c.panel) && ctx.active_panel != c.panel) ctx.SetActivePanel(c.panel);
        if (ctx.FindActiveNode(c.node_id)) {
            ctx.Select(c.node_id);
            ctx.OpenPanel("Properties", false);
        }
    }

    void Resolve(EditorContext& ctx, size_t index, bool keep) {
        std::string err;
        if (!ResolveConflict(ctx, index, keep, &err)) ctx.SetStatus("Resolve failed: " + err, 8.0);
    }

    void ResolveAll(EditorContext& ctx, ConflictsState& cs, bool keep) {
        int done = 0, failed = 0;
        for (size_t i = 0; i < cs.conflicts.size(); ++i) {
            if (cs.conflicts[i].resolved) continue;
            std::string err;
            if (ResolveConflict(ctx, i, keep, &err)) ++done;
            else {
                ++failed;
                ctx.Log(LogLevel::Error, "resolve %s/%s failed: %s", cs.conflicts[i].panel.c_str(),
                        cs.conflicts[i].node_id.c_str(), err.c_str());
            }
        }
        ctx.SetStatus(std::to_string(done) + " conflict(s) " + (keep ? "kept" : "regenerated") +
                      (failed ? ", " + std::to_string(failed) + " failed" : ""));
    }

    std::vector<ConflictView> views_;
    unsigned built_generation_ = 0;
    int current_ = 0;
};

}  // namespace

IMFORGE_REGISTER_PANEL(ConflictsPanel, 920);

}  // namespace imforge::builder
