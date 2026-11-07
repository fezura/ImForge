#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "imgui.h"

#include "../builder_fonts.h"
#include "../builder_style.h"
#include "../editor_context.h"
#include "../project_io.h"
#include "../ui/icon_table.h"
#include "../ui/ui_helpers.h"
#include "editor_panel.h"
#include "panels_state.h"

namespace imforge::builder {

namespace {

constexpr const char* kNewDialog = "New panel###imforge_panel_new";
constexpr const char* kRenameDialog = "Rename panel###imforge_panel_rename";
constexpr const char* kDeleteDialog = "Delete panel###imforge_panel_delete";
constexpr const char* kPanelSuffix = ".imb.json";

bool NameTaken(const Project& p, const std::string& name, const std::string& except) {
    for (const Panel& pn : p.panels)
        if (pn.name == name && pn.name != except) return true;
    for (const Component& c : p.components)
        if (c.name == name) return true;
    return false;
}

std::string NameError(const Project& p, const std::string& name, const std::string& except) {
    if (name.empty()) return "Enter a name.";
    if (!core_detail::IsIdentifier(name)) return "Use letters, digits and '_' (not starting with a digit).";
    if (NameTaken(p, name, except)) return "A panel or component named \"" + name + "\" already exists.";
    return std::string();
}

std::string UniquePanelName(const Project& p, const std::string& base) {
    if (!NameTaken(p, base, std::string())) return base;
    for (int n = 2;; ++n) {
        std::string cand = base + std::to_string(n);
        if (!NameTaken(p, cand, std::string())) return cand;
    }
}

std::string FileOf(const Panel& p) { return p.file.empty() ? DefaultPanelFile(p.name) : p.file; }

std::string RenamedFile(const std::string& file, const std::string& old_name, const std::string& new_name) {
    std::string suffix = old_name + kPanelSuffix;
    if (file.size() >= suffix.size() && file.compare(file.size() - suffix.size(), suffix.size(), suffix) == 0) {
        size_t at = file.size() - suffix.size();
        if (at == 0 || file[at - 1] == '/' || file[at - 1] == '\\') return file.substr(0, at) + new_name + kPanelSuffix;
    }
    return DefaultPanelFile(new_name);
}

void SyncPanelFiles(Project& p) {
    p.panel_files.clear();
    for (const Panel& pn : p.panels) p.panel_files.push_back(FileOf(pn));
}

bool OrphanFileExists(const EditorContext& ctx, const std::string& rel) {
    if (ctx.project.dir.empty()) return false;
    std::string dir = NormalizePath(ctx.project.dir);
    for (const PendingPanelDelete& d : GetPanelFileOps().pending_deletes)
        if (d.dir == dir && d.file == rel) return false;
    std::error_code ec;
    return std::filesystem::exists(std::filesystem::path(JoinPath(ctx.project.dir, rel)), ec);
}

int IndexOf(const Project& p, const std::string& name) {
    for (size_t i = 0; i < p.panels.size(); ++i)
        if (p.panels[i].name == name) return (int)i;
    return -1;
}

void RetitleIfNamed(Node& root, const std::string& old_name, const std::string& new_name) {
    PropValue* t = root.Prop("title");
    if (t && t->raw.IsString() && t->raw.AsString() == old_name) t->raw = Json(new_name);
}

void MoveInstance(EditorContext& ctx, const std::string& old_name, const std::string& new_name) {
#if IMFORGE_HAS_INTERP
    auto it = ctx.instances.find("panel:" + old_name);
    if (it == ctx.instances.end()) return;
    PanelInstance inst = std::move(it->second);
    ctx.instances.erase(it);
    if (!new_name.empty()) ctx.instances["panel:" + new_name] = std::move(inst);
#else
    (void)ctx;
    (void)old_name;
    (void)new_name;
#endif
}

void PillRight(ImDrawList* dl, float& right, float y0, float y1, const char* text, const ImVec4& color) {
    ImVec2 ts = ImGui::CalcTextSize(text);
    float pad = 5.0f;
    float h = y1 - y0;
    ImVec2 a(right - ts.x - pad * 2, y0 + 1.0f), b(right, y1 - 1.0f);
    dl->AddRectFilled(a, b, ToU32(color, 0.25f), h * 0.5f);
    dl->AddText(ImVec2(a.x + pad, y0 + (h - ts.y) * 0.5f), ToU32(color), text);
    right = a.x - 4.0f;
}

class PanelsPanel final : public EditorPanel {
public:
    const char* Name() const override { return "Panels"; }
    const char* DefaultDock() const override { return "left-bottom"; }
    const char* Icon() const override {
#ifdef ICON_FA_WINDOW_RESTORE
        return ICON_FA_WINDOW_RESTORE;
#else
        return nullptr;
#endif
    }
    void Draw(EditorContext& ctx) override;
    void EndWindow(EditorContext& ctx) override {
        ImGui::End();
        DrawDialogs(ctx);
    }
    void OnProjectChanged(EditorContext& ctx) override {
        Hook(ctx);
        target_.clear();
    }

private:
    std::string target_, name_buf_, open_popup_;
    bool focus_input_ = false;
    bool hooked_ = false;

    void Hook(EditorContext& ctx);
    void DrawToolbar(EditorContext& ctx, const std::string& active);
    void DrawList(EditorContext& ctx);
    void DrawTrash(EditorContext& ctx);
    void DrawItemMenu(EditorContext& ctx, const std::string& name, int index, int count);
    void DrawDialogs(EditorContext& ctx);
    void DrawNameDialog(EditorContext& ctx, bool rename);
    void DrawDeleteDialog(EditorContext& ctx);

    void RequestNew(EditorContext& ctx);
    void RequestRename(const std::string& name);
    void RequestDelete(const std::string& name);

    void Create(EditorContext& ctx, const std::string& name);
    void Duplicate(EditorContext& ctx, const std::string& name);
    void Rename(EditorContext& ctx, const std::string& old_name, const std::string& new_name);
    void Delete(EditorContext& ctx, const std::string& name);
    void Move(EditorContext& ctx, const std::string& name, int delta);
    void Restore(EditorContext& ctx, size_t trash_index);
};

void PanelsPanel::Hook(EditorContext& ctx) {
    if (hooked_) return;
    hooked_ = true;
    EditorContext* c = &ctx;
    ctx.on_project_changed.push_back([c](const char* what) {
        std::string w = what ? what : "";
        if (w == "saved") FlushPendingDeletes(c->project);
        else if (w == "loaded" || w == "closed") ClearPanelFileOps();
    });
}

void PanelsPanel::Draw(EditorContext& ctx) {
    Hook(ctx);
    if (!ctx.has_project) {
        ui::EmptyState(IconUtf8("fa-layer-group"), "No project open", "Panels are the windows your app shows. Open a project to manage them.");
        return;
    }
    std::string active = ctx.active_kind == DocKind::Panel ? ctx.active_panel : std::string();
    DrawToolbar(ctx, active);
    ImGui::Separator();
    if (ImGui::BeginChild("##panels_list", ImVec2(0, 0))) {
        DrawList(ctx);
        DrawTrash(ctx);
    }
    ImGui::EndChild();
}

void PanelsPanel::DrawToolbar(EditorContext& ctx, const std::string& active) {
    if (ui::AccentButton("+ New")) RequestNew(ctx);
    ui::Tooltip("Create a new panel (panels/<Name>.imb.json)");
    bool has = !active.empty();
    int idx = IndexOf(ctx.project, active);
    int count = (int)ctx.project.panels.size();
    ImGui::SameLine();
    ImGui::BeginDisabled(!has);
    if (ImGui::Button("Duplicate")) Duplicate(ctx, active);
    ImGui::SameLine();
    if (ImGui::Button("Rename")) RequestRename(active);
    ImGui::SameLine();
    if (ImGui::Button("Delete")) RequestDelete(active);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!has || idx <= 0);
    if (ImGui::ArrowButton("##panel_up", ImGuiDir_Up)) Move(ctx, active, -1);
    ImGui::EndDisabled();
    ui::Tooltip("Move the active panel up");
    ImGui::SameLine();
    ImGui::BeginDisabled(!has || idx < 0 || idx + 1 >= count);
    if (ImGui::ArrowButton("##panel_down", ImGuiDir_Down)) Move(ctx, active, 1);
    ImGui::EndDisabled();
    ui::Tooltip("Move the active panel down");
}

void PanelsPanel::DrawList(EditorContext& ctx) {
    const BuilderColors& col = Colors();
    std::string pick, menu_target;
    bool focus_canvas = false;
    int count = (int)ctx.project.panels.size();
    for (int i = 0; i < count; ++i) {
        const Panel& p = ctx.project.panels[(size_t)i];
        bool active = ctx.active_kind == DocKind::Panel && ctx.active_panel == p.name;
        int errors = 0, warnings = 0;
        for (const Diagnostic& d : ctx.diagnostics) {
            bool mine = d.panel == p.name || (d.panel.empty() && active);
            if (!mine) continue;
            if (d.level == Diagnostic::Level::Error) ++errors;
            else if (d.level == Diagnostic::Level::Warning) ++warnings;
        }
        ImGui::PushID(i);
        if (ImGui::Selectable(p.name.c_str(), active, ImGuiSelectableFlags_AllowDoubleClick)) {
            pick = p.name;
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) focus_canvas = true;
        }
        ImVec2 rmin = ImGui::GetItemRectMin(), rmax = ImGui::GetItemRectMax();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("%s\n%d params, %d state vars\nDouble-click to open in the Canvas", FileOf(p).c_str(),
                              (int)p.params.size(), (int)p.state.size());
        if (ImGui::BeginPopupContextItem("##panel_ctx")) {
            DrawItemMenu(ctx, std::string(p.name), i, count);
            ImGui::EndPopup();
        }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (active) dl->AddRectFilled(rmin, ImVec2(rmin.x + 3.0f, rmax.y), ToU32(col.accent));
        float right = rmax.x - 2.0f;
        char buf[32];
        if (warnings > 0) {
            std::snprintf(buf, sizeof(buf), "%d", warnings);
            PillRight(dl, right, rmin.y, rmax.y, buf, col.warning);
        }
        if (errors > 0) {
            std::snprintf(buf, sizeof(buf), "%d", errors);
            PillRight(dl, right, rmin.y, rmax.y, buf, col.error);
        }
        ImGui::PopID();
        if ((int)ctx.project.panels.size() != count) break;
    }
    if (count == 0) {
        ImGui::Spacing();
        ui::TextDim("No panels yet.");
        if (ImGui::SmallButton("Create the first panel")) RequestNew(ctx);
    }
    if (!pick.empty()) {
        ctx.SetActivePanel(pick);
        if (focus_canvas) ctx.OpenPanel("Canvas", true);
    }
}

void PanelsPanel::DrawItemMenu(EditorContext& ctx, const std::string& name, int index, int count) {
    if (ImGui::MenuItem("Open in Canvas")) {
        ctx.SetActivePanel(name);
        ctx.OpenPanel("Canvas", true);
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Duplicate")) Duplicate(ctx, name);
    if (ImGui::MenuItem("Rename...")) RequestRename(name);
    if (ImGui::MenuItem("Delete...")) RequestDelete(name);
    ImGui::Separator();
    if (ImGui::MenuItem("Move Up", nullptr, false, index > 0)) Move(ctx, name, -1);
    if (ImGui::MenuItem("Move Down", nullptr, false, index + 1 < count)) Move(ctx, name, 1);
    ImGui::Separator();
    if (const Panel* p = ctx.FindPanelDoc(name))
        if (ImGui::MenuItem("Copy file path")) ImGui::SetClipboardText(JoinPath(ctx.project.dir, FileOf(*p)).c_str());
}

void PanelsPanel::DrawTrash(EditorContext& ctx) {
    PanelFileOps& ops = GetPanelFileOps();
    std::string dir = NormalizePath(ctx.project.dir);
    bool any = false;
    for (const DeletedPanel& d : ops.trash) any = any || d.dir == dir;
    if (!any) return;
    ImGui::Spacing();
    ImGui::SeparatorText("Deleted (until saved)");
    for (size_t i = 0; i < ops.trash.size(); ++i) {
        const DeletedPanel& d = ops.trash[i];
        if (d.dir != dir) continue;
        ImGui::PushID((int)i);
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, Colors().text_dim);
        ImGui::TextUnformatted(d.panel.name.c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine();
        bool taken = NameTaken(ctx.project, d.panel.name, std::string());
        ImGui::BeginDisabled(taken);
        bool restore = ImGui::SmallButton("Restore");
        ImGui::EndDisabled();
        if (taken) ui::Tooltip("A panel with this name exists again");
        ImGui::PopID();
        if (restore) {
            Restore(ctx, i);
            break;
        }
    }
}

void PanelsPanel::RequestNew(EditorContext& ctx) {
    name_buf_ = UniquePanelName(ctx.project, "NewPanel");
    focus_input_ = true;
    open_popup_ = kNewDialog;
}

void PanelsPanel::RequestRename(const std::string& name) {
    target_ = name;
    name_buf_ = name;
    focus_input_ = true;
    open_popup_ = kRenameDialog;
}

void PanelsPanel::RequestDelete(const std::string& name) {
    target_ = name;
    open_popup_ = kDeleteDialog;
}

void PanelsPanel::DrawDialogs(EditorContext& ctx) {
    PanelFileOps& ops = GetPanelFileOps();
    if (ops.request_new) {
        ops.request_new = false;
        if (ctx.has_project) RequestNew(ctx);
    }
    if (!open_popup_.empty()) {
        ImGui::OpenPopup(open_popup_.c_str());
        open_popup_.clear();
    }
    DrawNameDialog(ctx, false);
    DrawNameDialog(ctx, true);
    DrawDeleteDialog(ctx);
}

void PanelsPanel::DrawNameDialog(EditorContext& ctx, bool rename) {
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 26.0f, 0), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(rename ? kRenameDialog : kNewDialog, nullptr, ImGuiWindowFlags_NoSavedSettings)) return;
    if (rename && !ctx.FindPanelDoc(target_)) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    const BuilderColors& col = Colors();
    if (rename) ui::TextDim("Rename \"%s\". Bindings and the live app follow the new name.", target_.c_str());
    else ui::TextDim("Creates an empty window panel in panels/<Name>.imb.json.");
    ImGui::Spacing();
    if (focus_input_) {
        ImGui::SetKeyboardFocusHere();
        focus_input_ = false;
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    bool enter = ui::InputTextWithHintStd("##panel_name", "PanelName", name_buf_,
                                          ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsNoBlank);
    bool unchanged = rename && name_buf_ == target_;
    std::string err = unchanged ? std::string() : NameError(ctx.project, name_buf_, rename ? target_ : std::string());
    std::string file;
    if (rename) {
        const Panel* p = ctx.FindPanelDoc(target_);
        file = RenamedFile(FileOf(*p), target_, name_buf_);
    } else {
        file = DefaultPanelFile(name_buf_);
    }
    if (!err.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, col.error);
        ImGui::TextWrapped("%s", err.c_str());
        ImGui::PopStyleColor();
    } else if (!unchanged) {
        ui::TextDim("File: %s", file.c_str());
        if (OrphanFileExists(ctx, file)) {
            ImGui::PushStyleColor(ImGuiCol_Text, col.warning);
            ImGui::TextWrapped("%s already exists on disk and will be overwritten on save.", file.c_str());
            ImGui::PopStyleColor();
        }
    }
    ImGui::Spacing();
    bool can = err.empty() && !unchanged;
    ImGui::BeginDisabled(!can);
    bool ok = ui::AccentButton(rename ? "Rename" : "Create", ImVec2(ImGui::GetFontSize() * 6.0f, 0));
    ImGui::EndDisabled();
    ImGui::SameLine();
    bool cancel = ImGui::Button("Cancel", ImVec2(ImGui::GetFontSize() * 6.0f, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape);
    if ((ok || enter) && can) {
        if (rename) Rename(ctx, target_, name_buf_);
        else Create(ctx, name_buf_);
        ImGui::CloseCurrentPopup();
    } else if (cancel || (enter && unchanged)) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void PanelsPanel::DrawDeleteDialog(EditorContext& ctx) {
    if (!ImGui::BeginPopupModal(kDeleteDialog, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) return;
    const Panel* p = ctx.FindPanelDoc(target_);
    if (!p) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    ImGui::Text("Delete panel \"%s\"?", p->name.c_str());
    ui::TextDim("%s is removed from the project now and deleted from disk", FileOf(*p).c_str());
    ui::TextDim("when the project is saved. Until then it can be restored from this list.");
    ImGui::Spacing();
    const BuilderColors& col = Colors();
    ImGui::PushStyleColor(ImGuiCol_Button, col.error);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, WithAlpha(col.error, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, WithAlpha(col.error, 0.7f));
    bool del = ImGui::Button("Delete", ImVec2(ImGui::GetFontSize() * 6.0f, 0));
    ImGui::PopStyleColor(3);
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(ImGui::GetFontSize() * 6.0f, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        ImGui::CloseCurrentPopup();
    if (del) {
        Delete(ctx, target_);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void PanelsPanel::Create(EditorContext& ctx, const std::string& name) {
    Panel p = MakeDefaultPanel(name);
    p.root.children.clear();
    ctx.BeginEdit("New panel", DocKind::Project, std::string());
    ctx.project.panels.push_back(std::move(p));
    SyncPanelFiles(ctx.project);
    ctx.EndEdit();
    ctx.SetActivePanel(name);
    ctx.Log(LogLevel::Info, "created panel %s (%s)", name.c_str(), DefaultPanelFile(name).c_str());
    ctx.SetStatus("Created panel " + name);
}

void PanelsPanel::Duplicate(EditorContext& ctx, const std::string& name) {
    int idx = IndexOf(ctx.project, name);
    if (idx < 0) return;
    Panel copy = ctx.project.panels[(size_t)idx];
    std::string fresh = UniquePanelName(ctx.project, name + "Copy");
    copy.file = RenamedFile(FileOf(copy), name, fresh);
    copy.name = fresh;
    RetitleIfNamed(copy.root, name, fresh);
    ctx.BeginEdit("Duplicate panel", DocKind::Project, std::string());
    ctx.project.panels.insert(ctx.project.panels.begin() + idx + 1, std::move(copy));
    SyncPanelFiles(ctx.project);
    ctx.EndEdit();
    ctx.SetActivePanel(fresh);
    ctx.SetStatus("Duplicated " + name + " as " + fresh);
}

void PanelsPanel::Rename(EditorContext& ctx, const std::string& old_name, const std::string& new_name) {
    Panel* p = ctx.FindPanelDoc(old_name);
    if (!p || old_name == new_name) return;
    std::string old_file = FileOf(*p);
    std::string new_file = RenamedFile(old_file, old_name, new_name);
    ctx.SetActivePanel(old_name);
    ctx.BeginEdit("Rename panel", DocKind::Panel, old_name);
    p->name = new_name;
    p->file = new_file;
    RetitleIfNamed(p->root, old_name, new_name);
    ctx.active_panel = new_name;
    ctx.EndEdit();
    if (new_file != old_file) QueuePanelFileDelete(ctx.project.dir, old_file);
    SyncPanelFiles(ctx.project);
    MoveInstance(ctx, old_name, new_name);
    if (ctx.live.IsConnected()) ctx.live.RemovePanel(old_name);
    ctx.Log(LogLevel::Info, "renamed panel %s -> %s (%s)", old_name.c_str(), new_name.c_str(), new_file.c_str());
    ctx.SetStatus("Renamed " + old_name + " to " + new_name);
}

void PanelsPanel::Delete(EditorContext& ctx, const std::string& name) {
    int idx = IndexOf(ctx.project, name);
    if (idx < 0) return;
    std::vector<Panel>& panels = ctx.project.panels;
    bool was_active = ctx.active_kind == DocKind::Panel && ctx.active_panel == name;
    DeletedPanel d;
    d.dir = NormalizePath(ctx.project.dir);
    d.panel = panels[(size_t)idx];
    d.index = (size_t)idx;
    std::string file = FileOf(d.panel);
    GetPanelFileOps().trash.push_back(std::move(d));
    ctx.BeginEdit("Delete panel", DocKind::Project, std::string());
    panels.erase(panels.begin() + idx);
    SyncPanelFiles(ctx.project);
    ctx.EndEdit();
    QueuePanelFileDelete(ctx.project.dir, file);
    MoveInstance(ctx, name, std::string());
    if (ctx.live.IsConnected()) ctx.live.RemovePanel(name);
    if (was_active) {
        if (!panels.empty()) ctx.SetActivePanel(panels[std::min((size_t)idx, panels.size() - 1)].name);
        else ctx.CloseActiveDocument();
    }
    ctx.Log(LogLevel::Info, "deleted panel %s (%s is removed on save)", name.c_str(), file.c_str());
    ctx.SetStatus("Deleted panel " + name);
}

void PanelsPanel::Move(EditorContext& ctx, const std::string& name, int delta) {
    int idx = IndexOf(ctx.project, name);
    int to = idx + delta;
    if (idx < 0 || to < 0 || to >= (int)ctx.project.panels.size()) return;
    ctx.BeginEdit("Reorder panels", DocKind::Project, std::string());
    std::swap(ctx.project.panels[(size_t)idx], ctx.project.panels[(size_t)to]);
    SyncPanelFiles(ctx.project);
    ctx.EndEdit();
}

void PanelsPanel::Restore(EditorContext& ctx, size_t trash_index) {
    PanelFileOps& ops = GetPanelFileOps();
    if (trash_index >= ops.trash.size()) return;
    DeletedPanel d = std::move(ops.trash[trash_index]);
    ops.trash.erase(ops.trash.begin() + (long)trash_index);
    std::string file = FileOf(d.panel);
    for (size_t i = 0; i < ops.pending_deletes.size(); ++i) {
        if (ops.pending_deletes[i].dir == d.dir && ops.pending_deletes[i].file == file) {
            ops.pending_deletes.erase(ops.pending_deletes.begin() + (long)i);
            break;
        }
    }
    std::vector<Panel>& panels = ctx.project.panels;
    size_t at = std::min(d.index, panels.size());
    std::string name = d.panel.name;
    ctx.BeginEdit("Restore panel", DocKind::Project, std::string());
    panels.insert(panels.begin() + (long)at, std::move(d.panel));
    SyncPanelFiles(ctx.project);
    ctx.EndEdit();
    ctx.SetActivePanel(name);
    ctx.SetStatus("Restored panel " + name);
}

}  // namespace

}  // namespace imforge::builder

IMFORGE_REGISTER_PANEL(imforge::builder::PanelsPanel, 25);
