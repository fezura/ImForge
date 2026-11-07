#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "imgui.h"

#include "../builder_fonts.h"
#include "../builder_style.h"
#include "../editor_context.h"
#include "../export.h"
#include "../ui/code_text.h"
#include "../ui/icon_table.h"
#include "../ui/ui_helpers.h"
#include "editor_panel.h"

namespace imforge::builder {

namespace {

constexpr double kRegenDelaySeconds = 0.25;
const char* const kDiagTag = "[codegen] ";

struct FileTab {
    std::string path, panel, content;
    ui::CodeText text;
};

class CodeViewPanel final : public EditorPanel {
public:
    const char* Name() const override { return "CodeView"; }
    const char* DefaultDock() const override { return "center"; }

    void OnProjectChanged(EditorContext& ctx) override {
        (void)ctx;
        change_pending_ = true;
        files_.clear();
        diags_.clear();
        last_selected_.clear();
    }

    void Draw(EditorContext& ctx) override {
        Subscribe(ctx);
        if (!ctx.has_project) {
            ui::EmptyState(IconUtf8("fa-code"), "No project open", "The generated C++ for every panel appears here and updates as you edit.");
            return;
        }
        if (change_pending_) {
            change_pending_ = false;
            dirty_ = true;
            last_change_ = ctx.time;
        }
        if (!CodegenAvailable()) {
            ImGui::Spacing();
            ui::TextDim("Code generation is not available in this build (src/codegen not linked).");
            return;
        }
        DrawToolbar(ctx);
        if (dirty_ && (auto_regen_ && ctx.time - last_change_ >= kRegenDelaySeconds)) Regenerate(ctx);
        EnsureDiagnosticsForwarded(ctx);
        if (files_.empty()) {
            if (dirty_) ui::TextDim("Generating...");
            else ui::TextDim("No files generated.");
            DrawFooter(ctx);
            return;
        }
        DrawTabs();
        FileTab& tab = files_[(size_t)std::min(active_, (int)files_.size() - 1)];
        HandleShortcuts();
        SyncSelection(ctx, tab);
        if (ui::DrawCodeFindBar("code_find", find_, tab.text)) scroll_to_ = find_.current.line;
        float footer_h = FooterHeight();
        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImVec2 size(avail.x, std::max(avail.y - footer_h, ImGui::GetTextLineHeight() * 3.0f));
        ui::CodeTextDrawOptions opts;
        opts.font = GetFonts().mono ? GetFonts().mono : ctx.Font(FontSlot::Mono);
        opts.highlight_begin = hl_begin_;
        opts.highlight_end = hl_end_;
        opts.find = find_.open && !find_.query.empty() ? find_.query.c_str() : nullptr;
        opts.find_case_sensitive = find_.case_sensitive;
        opts.find_current = find_.current;
        opts.scroll_to_line = scroll_to_;
        scroll_to_ = -1;
        ui::CodeTextDrawResult r = tab.text.Draw("code_view", size, opts);
        if (r.hovered_line >= 0) {
            if (const ui::CodeBlock* b = tab.text.BlockAtLine(r.hovered_line)) hover_block_ = b->id;
            else hover_block_.clear();
        } else {
            hover_block_.clear();
        }
        if (r.clicked_line >= 0) SelectBlockAt(ctx, tab, r.clicked_line);
        DrawFooter(ctx);
    }

private:
    void Subscribe(EditorContext& ctx) {
        if (subscribed_) return;
        subscribed_ = true;
        ctx.on_document_changed.push_back([this](DocKind, const std::string&) { change_pending_ = true; });
        ctx.on_project_changed.push_back([this](const char*) { change_pending_ = true; });
        change_pending_ = true;
    }

    void DrawToolbar(EditorContext& ctx) {
        if (ImGui::Button("Regenerate")) Regenerate(ctx);
        ui::Tooltip("Regenerate now (automatic 250 ms after the last edit)");
        ImGui::SameLine();
        ImGui::Checkbox("Auto", &auto_regen_);
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        bool split = ctx.project.codegen.split_header;
        if (ImGui::RadioButton("Split .h/.cpp", split))
            ctx.EditProject("Codegen: split header", [&] { ctx.project.codegen.split_header = true; });
        ImGui::SameLine();
        if (ImGui::RadioButton("Single header", !split))
            ctx.EditProject("Codegen: single header", [&] { ctx.project.codegen.split_header = false; });
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::TextUnformatted("namespace");
        ImGui::SameLine();
        if (!ns_editing_) ns_edit_ = ctx.project.codegen.nmspace;
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
        ui::InputTextStd("##ns", ns_edit_, ImGuiInputTextFlags_EnterReturnsTrue);
        ns_editing_ = ImGui::IsItemActive();
        if (ImGui::IsItemDeactivatedAfterEdit() && ns_edit_ != ctx.project.codegen.nmspace) {
            std::string v = ns_edit_;
            ctx.EditProject("Codegen: namespace", [&] { ctx.project.codegen.nmspace = v; });
        }
        ImGui::SameLine();
        bool markers = ctx.project.codegen.markers;
        if (ImGui::Checkbox("Markers", &markers))
            ctx.EditProject("Codegen: markers", [&] { ctx.project.codegen.markers = markers; });
        ui::Tooltip("Emit @imb: round-trip markers");
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        bool have = !files_.empty();
        if (!have) ImGui::BeginDisabled();
        if (ImGui::Button("Copy file")) {
            const FileTab& t = files_[(size_t)std::min(active_, (int)files_.size() - 1)];
            ImGui::SetClipboardText(t.content.c_str());
            ctx.SetStatus("Copied " + t.path);
        }
        ImGui::SameLine();
        bool has_block = have && hl_begin_ >= 0;
        if (!has_block && have) ImGui::BeginDisabled();
        if (ImGui::Button("Copy selection block")) {
            const FileTab& t = files_[(size_t)std::min(active_, (int)files_.size() - 1)];
            ImGui::SetClipboardText(t.text.SourceLines(hl_begin_, hl_end_).c_str());
            ctx.SetStatus("Copied block " + ctx.SelectedId());
        }
        if (!has_block && have) ImGui::EndDisabled();
        if (!have) ImGui::EndDisabled();
        ui::Tooltip("Copy the generated block of the selected node");
        ImGui::SameLine();
        if (ui::AccentButton("Export...")) OpenExportDialog(ctx);
        ui::Tooltip("Write the generated files to the output folder (Ctrl+E)");
        if (dirty_) {
            ImGui::SameLine();
            ui::TextDim("(pending)");
        }
    }

    void DrawTabs() {
        if (!ImGui::BeginTabBar("code_files", ImGuiTabBarFlags_FittingPolicyScroll | ImGuiTabBarFlags_TabListPopupButton))
            return;
        const bool programmatic = !want_tab_.empty();
        for (size_t i = 0; i < files_.size(); ++i) {
            ImGuiTabItemFlags f = 0;
            if (programmatic && files_[i].path == want_tab_) f |= ImGuiTabItemFlags_SetSelected;
            if (ImGui::BeginTabItem(files_[i].path.c_str(), nullptr, f)) {
                if (active_ != (int)i) {
                    active_ = (int)i;
                    find_.current = ui::CodeMatch();
                    find_.match_count = files_[i].text.CountMatches(find_.query, find_.case_sensitive);
                    last_selected_.clear();
                    if (!programmatic) user_switched_tab_ = true;
                }
                ImGui::EndTabItem();
            }
        }
        want_tab_.clear();
        ImGui::EndTabBar();
    }

    void HandleShortcuts() {
        if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) return;
        ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) {
            find_.open = true;
            find_.focus = true;
        }
    }

    void SyncSelection(EditorContext& ctx, FileTab& tab) {
        const std::string& sel = ctx.SelectedId();
        const bool changed = sel != last_selected_;
        if (!changed && !force_sync_) return;
        force_sync_ = false;
        last_selected_ = sel;
        hl_begin_ = hl_end_ = -1;
        if (sel.empty()) return;
        const ui::CodeBlock* b = tab.text.FindBlock(sel);
        if (!b && !user_switched_tab_ && changed) {
            for (size_t i = 0; i < files_.size(); ++i) {
                if (&files_[i] == &tab) continue;
                if (!files_[i].panel.empty() && files_[i].panel != ctx.active_panel) continue;
                if (const ui::CodeBlock* other = files_[i].text.FindBlock(sel)) {
                    want_tab_ = files_[i].path;
                    hl_begin_ = other->line_begin;
                    hl_end_ = other->line_end;
                    scroll_to_ = other->line_begin;
                    last_selected_.clear();
                    return;
                }
            }
        }
        user_switched_tab_ = false;
        if (!b) return;
        hl_begin_ = b->line_begin;
        hl_end_ = b->line_end;
        if (changed) scroll_to_ = b->line_begin;
    }

    void SelectBlockAt(EditorContext& ctx, FileTab& tab, int line) {
        const ui::CodeBlock* b = tab.text.BlockAtLine(line);
        if (!b) return;
        std::string id = ui::StripEventSuffix(b->id);
        if (!tab.panel.empty() && tab.panel != ctx.active_panel) {
            if (ctx.FindPanelDoc(tab.panel)) ctx.SetActivePanel(tab.panel);
            else if (ctx.FindComponentDoc(tab.panel)) ctx.SetActiveComponent(tab.panel);
        }
        if (!ctx.FindActiveNode(id)) return;
        user_switched_tab_ = true;
        ctx.Select(id);
        hl_begin_ = b->line_begin;
        hl_end_ = b->line_end;
    }

    float FooterHeight() const {
        int rows = std::min((int)diags_.size(), 4);
        return ImGui::GetTextLineHeightWithSpacing() * (float)(rows + 1) + ImGui::GetStyle().ItemSpacing.y * 2;
    }

    void DrawFooter(EditorContext& ctx) {
        int errors = 0, warnings = 0;
        for (const Diagnostic& d : diags_) {
            if (d.level == Diagnostic::Level::Error) ++errors;
            else if (d.level == Diagnostic::Level::Warning) ++warnings;
        }
        if (errors || warnings) {
            ImGui::PushStyleColor(ImGuiCol_Text, ui::LevelColor(errors ? 3 : 2));
            ImGui::Text("%s %d error(s), %d warning(s)", ui::LevelIcon(errors ? 3 : 2), errors, warnings);
            ImGui::PopStyleColor();
        } else {
            ui::TextDim(files_.empty() ? "codegen idle" : "%d file(s), no diagnostics", (int)files_.size());
        }
        if (!hover_block_.empty()) {
            ImGui::SameLine();
            ui::TextDim("  block: %s", hover_block_.c_str());
        }
        if (diags_.empty()) return;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Colors().bg_panel);
        float h = ImGui::GetTextLineHeightWithSpacing() * (float)std::min((int)diags_.size(), 4);
        if (ImGui::BeginChild("codegen_diags", ImVec2(0, h), ImGuiChildFlags_None)) {
            for (size_t i = 0; i < diags_.size(); ++i) {
                const Diagnostic& d = diags_[i];
                int lvl = d.level == Diagnostic::Level::Error ? 3 : d.level == Diagnostic::Level::Warning ? 2 : 1;
                ImGui::PushStyleColor(ImGuiCol_Text, ui::LevelColor(lvl));
                ImGui::TextUnformatted(ui::LevelIcon(lvl));
                ImGui::PopStyleColor();
                ImGui::SameLine();
                char id[32];
                std::snprintf(id, sizeof(id), "##cgdiag_%d", (int)i);
                std::string label = (d.panel.empty() ? "" : d.panel + ": ") + (d.node_id.empty() ? "" : d.node_id + ": ") + d.message;
                if (ImGui::Selectable(id, false, ImGuiSelectableFlags_AllowOverlap)) {
                    if (!d.panel.empty() && ctx.FindPanelDoc(d.panel)) ctx.SetActivePanel(d.panel);
                    if (!d.node_id.empty()) ctx.Select(d.node_id);
                }
                ImGui::SameLine(0, 0);
                ImGui::TextUnformatted(label.c_str());
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    void Regenerate(EditorContext& ctx) {
        dirty_ = false;
#if IMFORGE_EXPORT_HAS_CODEGEN
        codegen::Result r = codegen::GenerateAll(ctx.project, MakeCodegenOptions(ctx.project));
        std::string keep = files_.empty() ? std::string() : files_[(size_t)std::min(active_, (int)files_.size() - 1)].path;
        std::vector<FileTab> next;
        next.reserve(r.files.size());
        for (const codegen::FileOut& f : r.files) {
            FileTab t;
            t.path = f.path;
            t.panel = FileMarkerValue(f.content, "panel");
            t.content = f.content;
            next.push_back(std::move(t));
        }
        for (FileTab& t : next) {
            const FileTab* old = nullptr;
            for (const FileTab& o : files_)
                if (o.path == t.path) old = &o;
            if (old && old->content == t.content) t.text = old->text;
            else t.text.SetText(t.content, ctx.project.codegen.use_tabs ? ctx.project.codegen.indent : 4);
        }
        files_ = std::move(next);
        active_ = 0;
        for (size_t i = 0; i < files_.size(); ++i)
            if (files_[i].path == keep) active_ = (int)i;
        if (keep.empty() && !ctx.active_panel.empty()) {
            for (size_t i = 0; i < files_.size(); ++i) {
                if (files_[i].panel != ctx.active_panel) continue;
                active_ = (int)i;
                if (files_[i].path.size() > 4 && files_[i].path.compare(files_[i].path.size() - 4, 4, ".cpp") == 0) break;
            }
        }
        diags_ = r.diagnostics;
        forwarded_ = false;
        force_sync_ = true;
        user_switched_tab_ = true;
        if (!files_.empty()) find_.match_count = files_[(size_t)active_].text.CountMatches(find_.query, find_.case_sensitive);
#else
        (void)ctx;
#endif
    }

    void EnsureDiagnosticsForwarded(EditorContext& ctx) {
        bool present = false;
        for (const Diagnostic& d : ctx.diagnostics)
            if (d.message.compare(0, 10, kDiagTag) == 0) present = true;
        if (forwarded_ && (present || diags_.empty())) return;
        ctx.diagnostics.erase(std::remove_if(ctx.diagnostics.begin(), ctx.diagnostics.end(),
                                             [](const Diagnostic& d) { return d.message.compare(0, 10, kDiagTag) == 0; }),
                              ctx.diagnostics.end());
        for (Diagnostic d : diags_) {
            d.message = kDiagTag + d.message;
            ctx.diagnostics.push_back(std::move(d));
        }
        forwarded_ = true;
    }

    std::vector<FileTab> files_;
    std::vector<Diagnostic> diags_;
    ui::CodeFindState find_;
    std::string want_tab_, last_selected_, hover_block_, ns_edit_;
    int active_ = 0, hl_begin_ = -1, hl_end_ = -1, scroll_to_ = -1;
    double last_change_ = 0;
    bool dirty_ = true, auto_regen_ = true, subscribed_ = false, change_pending_ = false, forwarded_ = false;
    bool force_sync_ = false, user_switched_tab_ = false, ns_editing_ = false;
};

}  // namespace

IMFORGE_REGISTER_PANEL(CodeViewPanel, 500);

}  // namespace imforge::builder
