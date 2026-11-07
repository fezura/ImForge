#include <filesystem>
#include <string>
#include <vector>

#include "imgui.h"

#include "builder_style.h"
#include "editor_context.h"
#include "export.h"
#include "os_util.h"
#include "panels/editor_panel.h"
#include "project_io.h"
#include "ui/ui_helpers.h"

namespace imforge::builder {

namespace {

struct ExportDialogState {
    bool open_requested = false, open = false;
    bool confirm_requested = false, confirm_open = false;
    int last_frame = -1;
    std::string out_dir_edit, error;
    std::vector<ExportPreviewEntry> preview;
    std::vector<Diagnostic> preview_diags;
    std::vector<std::string> unmarked;
    bool preview_dirty = true;
};

ExportDialogState& State() {
    static ExportDialogState s;
    return s;
}

void RefreshPreview(EditorContext& ctx) {
    ExportDialogState& s = State();
    s.preview = PreviewExport(ctx, &s.preview_diags);
    s.preview_dirty = false;
}

void DrawPreviewTable(EditorContext& ctx) {
    ExportDialogState& s = State();
    const BuilderColors& c = Colors();
    int new_count = 0, update_count = 0, same_count = 0, unmarked_count = 0;
    for (const ExportPreviewEntry& e : s.preview) {
        if (e.unmarked) ++unmarked_count;
        else if (!e.exists) ++new_count;
        else if (e.changed) ++update_count;
        else ++same_count;
    }
    ui::TextDim("%d file(s): %d new, %d updated, %d unchanged%s", (int)s.preview.size(), new_count, update_count, same_count,
                unmarked_count ? ", some unmarked" : "");
    float h = ImGui::GetTextLineHeightWithSpacing() * 9.0f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, c.bg_frame);
    if (ImGui::BeginChild("export_preview", ImVec2(0, h), IMFORGE_CHILD_BORDER)) {
        if (s.preview.empty()) ui::TextDim(CodegenAvailable() ? "Nothing to export." : "Code generation is not available in this build.");
        for (const ExportPreviewEntry& e : s.preview) {
            ImVec4 col = c.text_dim;
            const char* tag = "same";
            if (e.unmarked) {
                col = c.error;
                tag = "no marker";
            } else if (!e.exists) {
                col = c.success;
                tag = "new";
            } else if (e.changed) {
                col = c.info;
                tag = "update";
            }
            ui::Badge(tag, col);
            ImGui::SameLine(ImGui::GetFontSize() * 6.0f);
            ImGui::TextUnformatted(e.rel.c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", e.abs.c_str());
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    int errors = 0, warnings = 0;
    for (const Diagnostic& d : s.preview_diags) {
        if (d.level == Diagnostic::Level::Error) ++errors;
        else if (d.level == Diagnostic::Level::Warning) ++warnings;
    }
    if (errors || warnings) {
        ImGui::PushStyleColor(ImGuiCol_Text, errors ? c.error : c.warning);
        ImGui::Text("%d error(s), %d warning(s) from code generation", errors, warnings);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::SmallButton("Show")) ctx.OpenPanel("Diagnostics");
    }
}

void DrawExportModal(EditorContext& ctx) {
    ExportDialogState& s = State();
    const char* kTitle = "Export code###imforge_export_dialog";
    if (s.open_requested) {
        ImGui::OpenPopup(kTitle);
        s.open_requested = false;
        s.open = true;
    }
    if (!s.open) return;
    ImGui::SetNextWindowSize(ImVec2(620 * ctx.dpi_scale, 0), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(kTitle, &s.open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        if (!ImGui::IsPopupOpen(kTitle)) s.open = false;
        return;
    }
    if (!ctx.has_project) {
        s.open = false;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    if (s.preview_dirty) RefreshPreview(ctx);
    ui::HeadingText(ctx.Font(FontSlot::Heading), "Export generated code");
    ui::TextDim("Project: %s", ctx.project.name.c_str());
    ImGui::Spacing();
    if (ui::BeginPropertyTable("export_props", ImGui::GetFontSize() * 7.0f)) {
        ui::PropertyLabel("Output dir", "Relative to the project folder (codegen.outputDir).");
        ImGui::SetNextItemWidth(-ImGui::GetFontSize() * 4.5f);
        ui::InputTextStd("##outdir", s.out_dir_edit);
        bool changed = s.out_dir_edit != ctx.project.codegen.output_dir;
        ImGui::SameLine();
        if (!changed) ImGui::BeginDisabled();
        if (ImGui::Button("Apply")) {
            std::string v = s.out_dir_edit.empty() ? std::string("generated") : s.out_dir_edit;
            ctx.EditProject("Codegen: output dir", [&] { ctx.project.codegen.output_dir = v; });
            s.out_dir_edit = v;
            s.preview_dirty = true;
        }
        if (!changed) ImGui::EndDisabled();
        ui::PropertyLabel("Full path");
        std::string out_dir = ExportOutputDir(ctx.project);
        ImGui::TextWrapped("%s", out_dir.c_str());
        if (!OutputDirAllowed(ctx.project, out_dir)) {
            ImGui::PushStyleColor(ImGuiCol_Text, Colors().error);
            ImGui::TextWrapped("This folder is outside the project folder and comes from the project file. Allow it only if you trust this project.");
            ImGui::PopStyleColor();
            if (ImGui::Button("Allow this folder")) {
                ApproveExternalOutputDir(out_dir);
                s.preview_dirty = true;
            }
        }
        ui::PropertyLabel("Layout");
        bool split = ctx.project.codegen.split_header;
        if (ImGui::RadioButton("Header + source", split)) {
            ctx.EditProject("Codegen: split header", [&] { ctx.project.codegen.split_header = true; });
            s.preview_dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Single header", !split)) {
            ctx.EditProject("Codegen: single header", [&] { ctx.project.codegen.split_header = false; });
            s.preview_dirty = true;
        }
        ui::PropertyLabel("Markers", "Round-trip markers (@imb:) in the generated files. Without them hand edits cannot be merged back.");
        bool markers = ctx.project.codegen.markers;
        if (ImGui::Checkbox("##markers", &markers)) {
            ctx.EditProject("Codegen: markers", [&] { ctx.project.codegen.markers = markers; });
            s.preview_dirty = true;
        }
        ui::EndPropertyTable();
    }
    ImGui::Spacing();
    DrawPreviewTable(ctx);
    if (!s.error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, Colors().error);
        ImGui::TextWrapped("%s", s.error.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    float bw = ImGui::GetFontSize() * 7.0f;
    if (ImGui::Button("Open folder", ImVec2(bw, 0))) {
        std::string out_dir = ExportOutputDir(ctx.project);
        if (!OutputDirAllowed(ctx.project, out_dir)) {
            ctx.SetStatus("Output folder is outside the project; allow it first");
        } else {
            std::error_code ec;
            std::filesystem::create_directories(out_dir, ec);
            if (!OpenExternal(out_dir)) ctx.SetStatus("Cannot open " + out_dir);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh", ImVec2(bw, 0))) s.preview_dirty = true;
    ImGui::SameLine();
    float right = ImGui::GetContentRegionAvail().x - bw * 2 - ImGui::GetStyle().ItemSpacing.x;
    if (right > 0) ImGui::Dummy(ImVec2(right, 0));
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(bw, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        s.open = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (!CodegenAvailable()) ImGui::BeginDisabled();
    if (ui::AccentButton("Export", ImVec2(bw, 0))) {
        std::string err;
        if (ExportProject(ctx, &err)) {
            s.error.clear();
            s.open = false;
            ImGui::CloseCurrentPopup();
        } else {
            s.error = err;
        }
    }
    if (!CodegenAvailable()) ImGui::EndDisabled();
    ImGui::EndPopup();
}

void DrawConfirmModal(EditorContext& ctx) {
    ExportDialogState& s = State();
    const char* kTitle = "Overwrite files without markers?###imforge_export_confirm";
    if (s.confirm_requested) {
        ImGui::OpenPopup(kTitle);
        s.confirm_requested = false;
        s.confirm_open = true;
    }
    if (!s.confirm_open) return;
    ImGui::SetNextWindowSize(ImVec2(520 * ctx.dpi_scale, 0), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(kTitle, &s.confirm_open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        if (!ImGui::IsPopupOpen(kTitle)) s.confirm_open = false;
        return;
    }
    ImGui::TextWrapped("These files exist in the output folder but carry no \"@imb:file\" marker, so they were not "
                       "generated by ImForge (or markers were stripped). Their content cannot be merged and will be replaced:");
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Colors().bg_frame);
    if (ImGui::BeginChild("unmarked_list", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 6.0f), IMFORGE_CHILD_BORDER)) {
        for (const std::string& f : s.unmarked) ImGui::BulletText("%s", f.c_str());
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::Spacing();
    float bw = ImGui::GetFontSize() * 8.0f;
    if (ImGui::Button("Cancel", ImVec2(bw, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        s.confirm_open = false;
        ctx.SetStatus("Export cancelled");
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, Colors().error);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Colors().error);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, Colors().error);
    bool overwrite = ImGui::Button("Overwrite and export", ImVec2(bw * 1.5f, 0));
    ImGui::PopStyleColor(3);
    if (overwrite) {
        std::string err;
        if (!ExportProject(ctx, &err, true)) ctx.SetStatus("Export failed: " + err, 8.0);
        s.confirm_open = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

class ExportDialogHost final : public EditorPanel {
public:
    ExportDialogHost() { show_in_view_menu = false; }
    const char* Name() const override { return "ExportDialogHost"; }
    bool BeginWindow(EditorContext&) override {
        open = true;
        return true;
    }
    void EndWindow(EditorContext&) override {}
    void Draw(EditorContext& ctx) override { ShowExportDialog(ctx); }
};

}  // namespace

void OpenExportDialog(EditorContext& ctx) {
    ExportDialogState& s = State();
    if (!ctx.has_project) {
        ctx.SetStatus("No project open");
        return;
    }
    s.open_requested = true;
    s.error.clear();
    s.out_dir_edit = ctx.project.codegen.output_dir;
    s.preview_dirty = true;
}

void RequestOverwriteConfirm(EditorContext& ctx, const std::vector<std::string>& files) {
    (void)ctx;
    ExportDialogState& s = State();
    s.unmarked = files;
    s.confirm_requested = true;
}

void ShowExportDialog(EditorContext& ctx) {
    ExportDialogState& s = State();
    int frame = ImGui::GetFrameCount();
    if (s.last_frame == frame) return;
    s.last_frame = frame;
    DrawExportModal(ctx);
    DrawConfirmModal(ctx);
}

IMFORGE_REGISTER_PANEL(ExportDialogHost, 995);

}  // namespace imforge::builder
