#include <cctype>
#include <cfloat>
#include <cstdio>
#include <filesystem>
#include <map>
#include <system_error>

#include "imforge/imb_runtime.h"

#include "../os_util.h"
#include "../project_io.h"
#include "../ui/file_dialog.h"
#include "dialogs.h"
#include "library_shared.h"
#include "wizard.h"
#include "wizard_state.h"

namespace fs = std::filesystem;

namespace imforge::builder::library {

namespace {

struct TemplateItem {
    std::string id, name, description, dir, group;
    std::vector<std::string> tags, features;
};

struct LoadedTemplate {
    std::unique_ptr<Project> project;
    std::string error;
    bool tried = false;
#if IMFORGE_HAS_INTERP
    PanelInstance inst;
#endif
};

ImVec2 InitialSize(const Node& root) {
    ImVec2 size(480, 320);
    if (const PropValue* pv = root.Prop("initialSize")) {
        if (pv->raw.IsObject()) {
            size.x = (float)pv->raw["x"].AsDouble(size.x);
            size.y = (float)pv->raw["y"].AsDouble(size.y);
        } else if (pv->raw.IsArray() && pv->raw.Size() == 2) {
            size.x = (float)pv->raw[0].AsDouble(size.x);
            size.y = (float)pv->raw[1].AsDouble(size.y);
        }
    }
    return ImVec2(std::max(size.x, 64.0f), std::max(size.y, 48.0f));
}

bool RootFlag(const Node& root, const char* flag) {
    const PropValue* pv = root.Prop("flags");
    if (!pv || !pv->raw.IsArray()) return false;
    for (const Json& f : pv->raw.arr)
        if (f.IsString() && f.AsString() == flag) return true;
    return false;
}

// templates spell the project name as {{PROJECT_NAME}}, the wizard fills it in on creation. previews do
// the same in memory, and any other {{WORD}} is replaced or dropped so no braces reach the screen
std::string ExpandPlaceholders(const std::string& in, const std::string& name) {
    std::string out;
    for (size_t i = 0; i < in.size();) {
        size_t end = in.compare(i, 2, "{{") == 0 ? in.find("}}", i + 2) : std::string::npos;
        bool word = end != std::string::npos && end > i + 2;
        for (size_t k = i + 2; word && k < end; ++k) word = std::isupper((unsigned char)in[k]) || in[k] == '_';
        if (!word) {
            out += in[i++];
            continue;
        }
        const std::string key = in.substr(i + 2, end - i - 2);
        if (key == "PROJECT_NAME") out += name;
        else if (key == "NAMESPACE") out += "ui";
        i = end + 2;
    }
    return out;
}

void FillPlaceholders(Json& j, const std::string& name) {
    if (j.IsString()) j.str = ExpandPlaceholders(j.str, name);
    for (Json& e : j.arr) FillPlaceholders(e, name);
    for (auto& kv : j.obj) FillPlaceholders(kv.second, name);
}

const char* FeatureLabel(const std::string& key) {
    if (key == "animations") return "anim";
    if (key == "drawlistFx") return "fx";
    if (key == "shaderFx") return "blur";
    if (key == "iconFonts") return "icons";
    if (key == "docking") return "dock";
    return nullptr;
}

class TemplatesTab final : public Tab {
public:
    const char* Title() const override { return "Templates"; }
    const char* IconName() const override { return "fa-layer-group"; }
    int Count(EditorContext& ctx) override {
        Load(ctx);
        return (int)items_.size();
    }

    bool Command(EditorContext& ctx, const std::string& verb, const std::string& arg) override {
        Load(ctx);
        if (verb == "search") {
            search_ = arg;
            return true;
        }
        if (verb != "select") return false;
        for (const TemplateItem& t : items_)
            if (t.id == arg) {
                selected_ = t.id;
                return true;
            }
        return false;
    }

    void Draw(EditorContext& ctx) override {
        Load(ctx);
        loads_this_frame_ = 0;
        FilterBar(ctx, "Search templates", search_, tags_, tag_);
        if (!groups_.empty()) ChipRow("##tpl_chips", groups_, group_, (int)items_.size());
        std::vector<const TemplateItem*> shown;
        for (const TemplateItem& t : items_)
            if (Matches(t)) shown.push_back(&t);
        const float s = Dpi(ctx);
        const float footer = std::floor(14.0f * s) + ImGui::GetTextLineHeight() * 2.0f + std::floor(1.0f * s);
        int budget = 0;
        CardGrid(ctx, "##tpl_grid", (int)shown.size(), 230.0f, 0.62f, footer, ActionBarHeight(), "fa-layer-group",
                 items_.empty() ? "No templates found" : "No template matches",
                 items_.empty() ? "The templates folder (templates/templates.json) was not found." : "Try another search or tag.",
                 [&](const Card& card) {
                     const TemplateItem& t = *shown[(size_t)card.index];
                     const bool selected = t.id == selected_;
                     if (card.tooltip) WrappedTooltip(t.name.c_str(), t.description.c_str());
                     if (card.clicked) selected_ = t.id;
                     if (card.double_clicked) OpenCreate(ctx, t);
                     DrawCard(ctx, card, t, selected, budget);
                 });
        DrawActionBar(ctx);
        DrawCreatePopup(ctx);
    }

private:
    std::vector<TemplateItem> items_;
    std::map<std::string, LoadedTemplate> loaded_;
    Counts groups_, tags_;
    bool scanned_ = false;
    int loads_this_frame_ = 0;
    std::string dir_, search_, group_, tag_, selected_;
    std::string create_id_, create_name_ = "MyProject", create_location_, create_error_;
    bool open_create_ = false;

    bool Matches(const TemplateItem& t) const {
        if (!group_.empty() && t.group != group_) return false;
        if (!tag_.empty() && std::find(t.tags.begin(), t.tags.end(), tag_) == t.tags.end()) return false;
        if (search_.empty()) return true;
        if (ContainsCI(t.name, search_) || ContainsCI(t.description, search_) || ContainsCI(t.id, search_)) return true;
        for (const std::string& tag : t.tags)
            if (ContainsCI(tag, search_)) return true;
        return false;
    }

    void Load(EditorContext& ctx) {
        if (scanned_) return;
        scanned_ = true;
        std::error_code ec;
        dir_ = ctx.prefs.templates_dir;
        if (dir_.empty() || !fs::is_directory(fs::u8path(dir_), ec)) dir_ = DefaultTemplatesDir();
        if (!fs::is_directory(fs::u8path(dir_), ec)) dir_ = FindDataDir("templates");
        Json index;
        if (dir_.empty() || !ReadJsonFile(JoinPath(dir_, "templates.json"), index)) return;
        for (const Json& j : index["templates"].arr) {
            TemplateItem t;
            t.id = j["id"].AsString(j["dir"].AsString());
            t.name = j["name"].AsString(t.id);
            t.description = j["description"].AsString();
            t.dir = JoinPath(dir_, j["dir"].AsString(t.id));
            t.group = j["category"].AsString();
            for (const Json& tag : j["tags"].arr)
                if (tag.IsString()) t.tags.push_back(tag.AsString());
            if (t.group.empty() && !t.tags.empty()) t.group = t.tags.front();
            for (const auto& kv : j["features"].obj)
                if (kv.second.AsBool() && FeatureLabel(kv.first)) t.features.push_back(FeatureLabel(kv.first));
            if (!fs::is_directory(fs::u8path(t.dir), ec)) continue;
            items_.push_back(std::move(t));
        }
        for (const TemplateItem& t : items_) {
            AddCount(groups_, t.group);
            for (const std::string& tag : t.tags) AddCount(tags_, tag);
        }
        std::sort(tags_.begin(), tags_.end(), [](const std::pair<std::string, int>& a, const std::pair<std::string, int>& b) {
            return a.second != b.second ? a.second > b.second : a.first < b.first;
        });
    }

    LoadedTemplate* Loaded(EditorContext& ctx, const TemplateItem& t) {
        LoadedTemplate& lt = loaded_[t.dir];
        if (lt.tried) return &lt;
        // loading parses every panel file, spread it over frames
        if (loads_this_frame_++ > 0) return nullptr;
        lt.tried = true;
        std::unique_ptr<Project> p(new Project());
        p->widget_catalog = &ctx.catalog;
        Error err;
        if (LoadProject(t.dir, *p, &err) && !p->panels.empty()) {
            const std::string shown = t.name.empty() ? std::string("My Project") : t.name;
            for (Panel& panel : p->panels) {
                Json pj = PanelToJson(panel);
                FillPlaceholders(pj, shown);
                Panel fixed;
                fixed.file = panel.file;
                if (PanelFromJson(pj, fixed, &err)) panel = std::move(fixed);
            }
            for (Component& c : p->components) {
                Json cj = ComponentToJson(c);
                FillPlaceholders(cj, shown);
                Component fixed;
                fixed.file = c.file;
                if (ComponentFromJson(cj, fixed, &err)) c = std::move(fixed);
            }
            lt.project = std::move(p);
        } else {
            lt.error = err.message.empty() ? "no panels" : err.message;
        }
        return &lt;
    }

    const TemplateItem* Selected() const {
        for (const TemplateItem& t : items_)
            if (t.id == selected_) return &t;
        return nullptr;
    }

    void DrawCard(EditorContext& ctx, const Card& card, const TemplateItem& t, bool selected, int& budget) {
        const BuilderColors& c = Colors();
        const float s = Dpi(ctx);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        CardBackground(dl, ctx, card, selected);
        PreviewInset(dl, ctx, card, ToU32(c.bg_window));
        const ImVec2 pmn(card.pmin.x + 1.0f, card.pmin.y + 1.0f), pmx(card.pmax.x - 1.0f, card.pmax.y);
        LoadedTemplate* lt = Loaded(ctx, t);
        if (lt && lt->project && budget < kMaxAnimatedPreviews) {
            ++budget;
            DrawPreview(ctx, *lt, pmn, pmx);
        } else {
            const char* icon = Icon(lt && !lt->error.empty() ? "fa-triangle-exclamation" : "fa-layer-group");
            ui::IconDisc(icon, ImVec2((pmn.x + pmx.x) * 0.5f, (pmn.y + pmx.y) * 0.5f), std::floor(18.0f * s),
                         lt && !lt->error.empty() ? c.warning : c.accent);
        }
        std::string feats;
        for (const std::string& f : t.features) feats += (feats.empty() ? "" : "  ") + f;
        CardFooter(dl, ctx, card, t.name.c_str(), t.description.c_str(), selected, 0, feats.c_str());
    }

    void DrawPreview(EditorContext& ctx, LoadedTemplate& lt, const ImVec2& mn, const ImVec2& mx) {
#if IMFORGE_HAS_INTERP
        const Project& proj = *lt.project;
        const Panel& panel = proj.panels.front();
        const Theme* theme = FindThemeByName(proj, proj.active_theme);
        if (!theme && !proj.themes.empty()) theme = &proj.themes.front();
        const float s = Dpi(ctx);
        const ImVec2 area(mx.x - mn.x, mx.y - mn.y);
        const ImVec2 logical = InitialSize(panel.root);
        const float m = std::floor(10.0f * s);
        const float k = std::min((area.x - m * 2.0f) / logical.x, (area.y - m * 2.0f) / logical.y);
        const ImVec2 fsize(std::floor(logical.x * k), std::floor(logical.y * k));
        const ImVec2 fmin(std::floor(mn.x + (area.x - fsize.x) * 0.5f), std::floor(mn.y + (area.y - fsize.y) * 0.5f));
        const ImVec2 fmax(fmin.x + fsize.x, fmin.y + fsize.y);
        const bool title = panel.root.type == "Window" && !RootFlag(panel.root, "NoTitleBar");
        const bool menu = panel.root.type == "Window" && panel.root.Prop("menuBar") && panel.root.Prop("menuBar")->raw.AsBool(false);
        const float base_px = proj.fonts.empty() ? 16.0f : proj.fonts.front().size;
        const float font_scale = std::max(0.2f, base_px * k / ImGui::GetFontSize());

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(mn, mx, true);
        StyleScope colors = PushScaledStyle(theme, 1.0f);
        const float wr = ImGui::GetStyle().WindowRounding * k;
        imb::Shadow(dl, fmin, fmax, wr, IM_COL32(0, 0, 0, 110), 12.0f * s, ImVec2(0, 3.0f * s));
        dl->AddRectFilled(fmin, fmax, ImGui::GetColorU32(ImGuiCol_WindowBg), wr);
        if (title) {
            const float th = std::floor((base_px + ImGui::GetStyle().FramePadding.y * 2.0f) * k);
            dl->AddRectFilled(fmin, ImVec2(fmax.x, fmin.y + th), ImGui::GetColorU32(ImGuiCol_TitleBgActive), wr, ImDrawFlags_RoundCornersTop);
        }
        dl->AddRect(fmin, fmax, ImGui::GetColorU32(ImGuiCol_Border), wr);
        PopStyleScope(colors);
        dl->PopClipRect();

        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(fmin);
        ImGuiWindowFlags wf = ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                              ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground;
        if (menu) wf |= ImGuiWindowFlags_MenuBar;
        StyleScope scope = PushScaledStyle(theme, k);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, wr);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
        const bool open = ImGui::BeginChild("##tpl", fsize, ImGuiChildFlags_None, wf);
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
        if (open) {
#if IMGUI_VERSION_NUM < 19200
            ImGui::SetWindowFontScale(font_scale);
#endif
            Interpreter& in = ctx.interp;
            InterpScope keep(in);
            in.project = &proj;
            in.SyncAnimations();
            InterpOptions opts;
            opts.design_mode = true;
            opts.dpi_scale = k;
            opts.run_handlers = false;
            opts.animations = true;
            opts.fx = true;
            static const std::vector<BindingRoot> kNoRoots;
            in.DrawPanel(panel, lt.inst, kNoRoots, opts);
        }
        ImGui::EndChild();
        PopStyleScope(scope);
        ImGui::SetCursorScreenPos(cursor);
        (void)font_scale;
#else
        (void)ctx;
        (void)lt;
        (void)mn;
        (void)mx;
#endif
    }

    void OpenCreate(EditorContext& ctx, const TemplateItem& t) {
        selected_ = t.id;
        create_id_ = t.id;
        create_error_.clear();
        if (create_location_.empty()) {
            std::error_code ec;
            if (!ctx.prefs.recent_projects.empty())
                create_location_ = fs::u8path(ctx.prefs.recent_projects.front()).parent_path().generic_u8string();
            if (create_location_.empty() || !fs::is_directory(fs::u8path(create_location_), ec)) create_location_ = HomeDirOrCwd();
        }
        open_create_ = true;
    }

    bool Create(EditorContext& ctx, const TemplateItem& t, bool save_first) {
        if (!wizard::IsIdentifier(create_name_)) {
            create_error_ = "The name must be a C++ identifier (letters, digits, underscore; no leading digit)";
            return false;
        }
        const std::string dest = JoinPath(create_location_, create_name_);
        if (IsProjectDir(dest)) {
            create_error_ = dest + " already contains an ImForge project";
            return false;
        }
        std::string err;
        if (save_first && ctx.has_project && !ctx.SaveProject(&err)) {
            create_error_ = "Could not save the current project: " + err;
            return false;
        }
        Error e;
        Project settings = SettingsFromTemplate(t.dir, &e);
        settings.name = create_name_;
        if (!ctx.NewProject(t.dir, dest, create_name_, settings, &err)) {
            create_error_ = err;
            return false;
        }
        ctx.SetStatus("Created project " + create_name_ + " from " + t.name + " in " + dest, 6.0);
        return true;
    }

    void DrawCreatePopup(EditorContext& ctx) {
        const char* kTitle = "New project from template###lib_tpl_create";
        if (open_create_) {
            ImGui::OpenPopup(kTitle);
            open_create_ = false;
        }
        const float s = Dpi(ctx);
        ImGui::SetNextWindowSize(ImVec2(520.0f * s, 0.0f), ImGuiCond_Appearing);
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (!ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_NoSavedSettings)) return;
        const TemplateItem* t = nullptr;
        for (const TemplateItem& it : items_)
            if (it.id == create_id_) t = &it;
        if (!t) {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }
        ui::HeadingText(ctx.Font(FontSlot::Heading), t->name.c_str());
        ImGui::PushTextWrapPos(0.0f);
        ui::TextDim("%s", t->description.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ui::BeginPropertyTable("##tpl_create_props")) {
            ui::PropertyLabel("Name", "Used as the project folder and the generated namespace default");
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            if (ui::InputTextWithHintStd("##name", "MyProject", create_name_)) create_error_.clear();
            ui::PropertyLabel("Location", "A new folder named after the project is created here");
            const float bw = ui::ButtonWidth("Browse...") + ImGui::GetStyle().ItemSpacing.x;
            ImGui::SetNextItemWidth(-bw);
            if (ui::InputTextStd("##location", create_location_)) create_error_.clear();
            ImGui::SameLine();
            if (ImGui::Button("Browse...")) {
                std::string picked;
                if (NativeDialogsAvailable()) {
                    if (NativePickFolder(picked, "Project location", create_location_) && !picked.empty()) create_location_ = picked;
                } else {
                    FileDialogOptions o;
                    o.title = "Project location";
                    o.pick_directory = true;
                    OpenFileDialog("lib_tpl_location", create_location_, o);
                }
            }
            std::string picked;
            if (DrawFileDialog("lib_tpl_location", picked) && !picked.empty()) create_location_ = picked;
            ui::PropertyLabel("Creates");
            ImGui::TextWrapped("%s", JoinPath(create_location_, create_name_).c_str());
            ui::EndPropertyTable();
        }
        const bool unsaved = ctx.has_project && ctx.dirty;
        if (unsaved) {
            ImGui::PushStyleColor(ImGuiCol_Text, Colors().warning);
            ImGui::TextWrapped("%s has unsaved changes. Save them first or they are lost when the new project opens.",
                               ctx.project.name.c_str());
            ImGui::PopStyleColor();
        }
        if (!create_error_.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, Colors().error);
            ImGui::TextWrapped("%s", create_error_.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::Spacing();
        const ImGuiStyle& st = ImGui::GetStyle();
        const char* primary = unsaved ? "Save and create" : "Create";
        float w = ui::ButtonWidth("Cancel") + ui::ButtonWidth(primary) + st.ItemSpacing.x;
        if (unsaved) w += ui::ButtonWidth("Discard and create") + st.ItemSpacing.x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - w));
        if (ImGui::Button("Cancel") || (ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::IsAnyItemActive())) ImGui::CloseCurrentPopup();
        if (unsaved) {
            ImGui::SameLine();
            if (ImGui::Button("Discard and create") && Create(ctx, *t, false)) ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if ((ui::AccentButton(primary) || (ImGui::IsKeyPressed(ImGuiKey_Enter) && !unsaved)) && Create(ctx, *t, unsaved))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    void DrawActionBar(EditorContext& ctx) {
        const BuilderColors& c = Colors();
        const ImGuiStyle& st = ImGui::GetStyle();
        BeginActionBar();
        const TemplateItem* sel = Selected();
        const char* create = "New project from template...";
        const char* wiz = "Open wizard";
        const float right_w = sel ? ui::ButtonWidth(create) + ui::ButtonWidth(wiz) + st.ItemSpacing.x * 2.0f : 0.0f;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float left_w = ImGui::GetContentRegionAvail().x - right_w - st.ItemSpacing.x;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float ty = p.y + st.FramePadding.y;
        if (sel) {
            TextEllipsis(dl, ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(p.x, ty), left_w, ToU32(c.text), sel->name.c_str());
            const float dx = p.x + ImGui::CalcTextSize(sel->name.c_str()).x + st.ItemSpacing.x * 1.5f;
            if (dx < p.x + left_w - 20.0f)
                TextEllipsis(dl, ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(dx, ty), p.x + left_w - dx, ToU32(c.text_dim),
                             sel->description.c_str());
        } else {
            TextEllipsis(dl, ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(p.x, ty), left_w, ToU32(c.text_dim),
                         "Click a template to pick it, double-click to start a project from it.");
            ImGui::Dummy(ImVec2(1, ImGui::GetFrameHeight()));
            EndActionBar();
            return;
        }
        ImGui::SetCursorScreenPos(ImVec2(p.x + ImGui::GetContentRegionAvail().x - right_w + st.ItemSpacing.x, p.y));
        if (ImGui::Button(wiz)) OpenWizard();
        ui::Tooltip("The project wizard, for the ImGui target, platform, bindings and theme");
        ImGui::SameLine();
        if (ui::AccentButton(create)) OpenCreate(ctx, *sel);
        EndActionBar();
    }
};

}  // namespace

std::unique_ptr<Tab> MakeTemplatesTab() { return std::unique_ptr<Tab>(new TemplatesTab()); }

}  // namespace imforge::builder::library
