#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <system_error>

#include "imgui.h"

#include "../builder_fonts.h"
#include "../builder_style.h"
#include "../os_util.h"
#include "../project_io.h"
#include "../ui/file_dialog.h"
#include "../ui/ui_helpers.h"
#include "dialogs.h"
#include "editor_panel.h"
#include "wizard.h"
#include "wizard_state.h"

#ifndef ICON_FA_FILE
#define ICON_FA_FILE "[ ]"
#endif
#ifndef ICON_FA_GAMEPAD
#define ICON_FA_GAMEPAD "[G]"
#endif
#ifndef ICON_FA_TOOLBOX
#define ICON_FA_TOOLBOX "[T]"
#endif
#ifndef ICON_FA_SLIDERS
#define ICON_FA_SLIDERS "[S]"
#endif
#ifndef ICON_FA_CROSSHAIRS
#define ICON_FA_CROSSHAIRS "[+]"
#endif
#ifndef ICON_FA_CUBE
#define ICON_FA_CUBE "[#]"
#endif
#ifndef ICON_FA_CHECK
#define ICON_FA_CHECK "v"
#endif
#ifndef ICON_FA_TRIANGLE_EXCLAMATION
#define ICON_FA_TRIANGLE_EXCLAMATION "!"
#endif

namespace fs = std::filesystem;

namespace imforge::builder {

namespace {

struct TemplateInfo {
    std::string id, name, description, dir;
    std::vector<std::pair<std::string, bool>> features;
    Json project_json;
};

enum Step { StepProject, StepTarget, StepPlatform, StepFeatures, StepBindings, StepLive, StepCodegen, StepTheme, StepCount };

const char* const kStepNames[StepCount] = {"Project", "ImGui target", "Platform", "Features", "Bindings", "Live attach", "Code style", "Theme"};

struct BadgeDef {
    const char* key;
    const char* label;
};
const BadgeDef kBadges[] = {{"animations", "anim"}, {"drawlistFx", "fx"},  {"shaderFx", "blur"},
                            {"iconFonts", "icons"},  {"dpiScaling", "dpi"}, {"docking", "dock"}};

const char* TemplateIcon(const std::string& id) {
    if (id == "blank") return ICON_FA_FILE;
    if (id == "cheat-menu") return ICON_FA_GAMEPAD;
    if (id == "tool-dock") return ICON_FA_TOOLBOX;
    if (id == "settings-dialog") return ICON_FA_SLIDERS;
    if (id == "overlay-hud") return ICON_FA_CROSSHAIRS;
    return ICON_FA_CUBE;
}

class WizardPanel;
WizardPanel* g_instance = nullptr;

class WizardPanel final : public EditorPanel {
public:
    WizardPanel() {
        g_instance = this;
        open = false;
        show_in_view_menu = false;
    }
    ~WizardPanel() override {
        if (g_instance == this) g_instance = nullptr;
    }
    const char* Name() const override { return "Wizard"; }
    bool BeginWindow(EditorContext&) override { return true; }
    void EndWindow(EditorContext&) override {}
    void Draw(EditorContext& ctx) override;

private:
    void Reset(EditorContext& ctx);
    void LoadTemplates(EditorContext& ctx);
    void SelectTemplate(int index, EditorContext& ctx);
    bool StepValid(int step, std::string& msg);
    void DrawStepList(EditorContext& ctx);
    void DrawProjectStep(EditorContext& ctx);
    void DrawTemplateCards(EditorContext& ctx);
    void DrawFooter(EditorContext& ctx, bool focused);
    bool Finish(EditorContext& ctx, std::string& err);

    Project draft_;
    wizard::SettingsForm form_;
    std::string name_ = "MyProject", location_, error_;
    std::vector<TemplateInfo> templates_;
    std::string templates_dir_;
    int template_index_ = 0;
    int step_ = 0, max_step_ = 0;
    bool visited_[StepCount] = {};
    bool was_open_ = false;
};

void WizardPanel::Reset(EditorContext& ctx) {
    step_ = max_step_ = 0;
    for (bool& v : visited_) v = false;
    error_.clear();
    if (location_.empty()) {
        std::error_code ec;
        if (!ctx.prefs.recent_projects.empty()) location_ = fs::path(ctx.prefs.recent_projects.front()).parent_path().generic_string();
        if (location_.empty() || !fs::is_directory(location_, ec)) location_ = HomeDirOrCwd();
    }
    form_ = wizard::SettingsForm();
    LoadTemplates(ctx);
    SelectTemplate(template_index_ < (int)templates_.size() ? template_index_ : 0, ctx);
}

void WizardPanel::LoadTemplates(EditorContext& ctx) {
    templates_.clear();
    std::error_code ec;
    templates_dir_ = ctx.prefs.templates_dir;
    if (templates_dir_.empty() || !fs::is_directory(templates_dir_, ec)) templates_dir_ = DefaultTemplatesDir();
#ifdef IMFORGE_SOURCE_DIR
    if (!fs::is_directory(templates_dir_, ec)) templates_dir_ = std::string(IMFORGE_SOURCE_DIR) + "/templates";
#endif
    Json index;
    if (ReadJsonFile(JoinPath(templates_dir_, "templates.json"), index) && index["templates"].IsArray()) {
        for (const Json& t : index["templates"].arr) {
            TemplateInfo info;
            info.id = t["id"].AsString(t["dir"].AsString());
            info.name = t["name"].AsString(info.id);
            info.description = t["description"].AsString();
            info.dir = JoinPath(templates_dir_, t["dir"].AsString(info.id));
            for (const auto& kv : t["features"].obj) info.features.emplace_back(kv.first, kv.second.AsBool());
            ReadJsonFile(JoinPath(info.dir, "imforge.json"), info.project_json);
            if (fs::is_directory(info.dir, ec)) templates_.push_back(std::move(info));
        }
    }
    if (templates_.empty()) {
        for (const std::string& d : ListTemplates(templates_dir_)) {
            TemplateInfo info;
            info.id = info.name = d;
            info.dir = JoinPath(templates_dir_, d);
            if (ReadJsonFile(JoinPath(info.dir, "imforge.json"), info.project_json))
                for (const auto& kv : info.project_json["features"].obj) info.features.emplace_back(kv.first, kv.second.AsBool());
            templates_.push_back(std::move(info));
        }
    }
    if (templates_.empty()) {
        TemplateInfo info;
        info.id = "blank";
        info.name = "Blank";
        info.description = "Empty window with a text node (built-in fallback; no templates folder found).";
        info.features = {{"animations", true}, {"drawlistFx", true}, {"shaderFx", false}, {"iconFonts", true}, {"dpiScaling", true}, {"docking", false}};
        templates_.push_back(std::move(info));
        ctx.Log(LogLevel::Warning, "no templates found in %s", templates_dir_.c_str());
    }
}

void WizardPanel::SelectTemplate(int index, EditorContext& ctx) {
    if (index < 0 || index >= (int)templates_.size()) index = 0;
    template_index_ = index;
    const TemplateInfo& t = templates_[(size_t)index];
    Project seed;
    seed.widget_catalog = &ctx.catalog;
    Error err;
    if (!t.project_json.IsObject() || !ProjectFromJson(t.project_json, seed, &err)) {
        seed = Project();
        seed.widget_catalog = &ctx.catalog;
        seed.features = t.features.empty() ? std::vector<std::pair<std::string, bool>>{{"animations", true}, {"drawlistFx", true}, {"shaderFx", false}, {"iconFonts", true}, {"dpiScaling", true}, {"docking", false}}
                                           : t.features;
        seed.imgui_branch = seed.Feature("docking") ? "docking" : "master";
    }
    draft_ = std::move(seed);
    draft_.name = name_;
    draft_.dir.clear();
    bool skip = form_.skip_bindings;
    std::vector<wizard::LibraryEntry> libs = form_.libraries;
    bool scanned = form_.libraries_scanned;
    form_ = wizard::SettingsForm();
    form_.skip_bindings = skip;
    form_.libraries = std::move(libs);
    form_.libraries_scanned = scanned;
    form_.Attach(draft_);
}

bool WizardPanel::StepValid(int step, std::string& msg) {
    switch (step) {
    case StepProject: {
        if (name_.empty()) {
            msg = "Enter a project name";
            return false;
        }
        if (!wizard::IsIdentifier(name_)) {
            msg = "The project name must be a C++ identifier (letters, digits, underscore; no leading digit)";
            return false;
        }
        if (location_.empty()) {
            msg = "Choose a location";
            return false;
        }
        std::string dest = JoinPath(location_, name_);
        if (IsProjectDir(dest)) {
            msg = dest + " already contains an ImForge project";
            return false;
        }
        return template_index_ >= 0 && template_index_ < (int)templates_.size();
    }
    case StepTarget: return wizard::ValidateTarget(form_, msg);
    case StepPlatform: return wizard::ValidatePlatform(form_, msg);
    case StepBindings: return wizard::ValidateBindings(form_, msg);
    case StepLive: return wizard::ValidateLive(form_, msg);
    case StepCodegen: return wizard::ValidateCodegen(form_, msg);
    default: return true;
    }
}

void WizardPanel::DrawStepList(EditorContext& ctx) {
    const BuilderColors& c = Colors();
    float h = ImGui::GetFrameHeight() * 1.25f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ui::HeadingText(ctx.Font(FontSlot::Heading), "New project");
    ImGui::Spacing();
    for (int i = 0; i < StepCount; ++i) {
        ImGui::PushID(i);
        bool reachable = i <= max_step_;
        std::string msg;
        bool valid = StepValid(i, msg);
        bool done = visited_[i] && valid;
        bool bad = visited_[i] && !valid;
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;
        if (ImGui::Selectable("##step", i == step_, reachable ? 0 : ImGuiSelectableFlags_Disabled, ImVec2(w, h))) {
            visited_[step_] = true;
            step_ = i;
            error_.clear();
        }
        ImVec2 center(p.x + h * 0.5f, p.y + h * 0.5f);
        float r = h * 0.33f;
        ImVec4 fill = i == step_ ? c.accent : (bad ? c.warning : (done ? c.success : c.bg_frame));
        dl->AddCircleFilled(center, r, ToU32(fill));
        char num[8];
        std::snprintf(num, sizeof(num), "%d", i + 1);
        const char* glyph = bad ? ICON_FA_TRIANGLE_EXCLAMATION : (done && i != step_ ? ICON_FA_CHECK : num);
        ImVec2 ts = ImGui::CalcTextSize(glyph);
        ImVec4 glyph_col = (i == step_ || done || bad) ? ImVec4(1, 1, 1, 1) : c.text_dim;
        dl->AddText(ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f), ToU32(glyph_col), glyph);
        ImVec4 name_col = i == step_ ? c.text : (reachable ? c.text_dim : c.text_disabled);
        ImVec2 ns = ImGui::CalcTextSize(kStepNames[i]);
        dl->AddText(ImVec2(p.x + h + ImGui::GetStyle().ItemInnerSpacing.x, center.y - ns.y * 0.5f), ToU32(name_col), kStepNames[i]);
        if (i + 1 < StepCount) {
            float next_center = p.y + h + ImGui::GetStyle().ItemSpacing.y + h * 0.5f;
            dl->AddLine(ImVec2(center.x, center.y + r + 3.0f), ImVec2(center.x, next_center - r - 3.0f),
                        ToU32(done ? WithAlpha(c.success, 0.6f) : c.separator), 1.5f);
        }
        ImGui::PopID();
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::PushTextWrapPos(0.0f);
    ui::TextDim("Every choice can be changed later in Project Settings.");
    ImGui::PopTextWrapPos();
}

void WizardPanel::DrawTemplateCards(EditorContext& ctx) {
    const BuilderColors& c = Colors();
    const float s = ctx.dpi_scale > 0 ? ctx.dpi_scale : 1.0f;
    const float gap = ImGui::GetStyle().ItemSpacing.x + 2.0f * s;
    const float avail = ImGui::GetContentRegionAvail().x;
    const int per_row = std::max(1, (int)((avail + gap) / (200.0f * s + gap)));
    const float card_w = std::floor((avail - gap * (float)(per_row - 1)) / (float)per_row);
    const float pad = 12.0f * s;
    ImFont* font = ImGui::GetFont();
    const float fsz = ImGui::GetFontSize();
    const float line = ImGui::GetTextLineHeight();
    ImFont* heading = ctx.Font(FontSlot::Heading);
    if (!heading) heading = font;
    const float hsz = heading->FontSize;
    const float tile = std::floor(hsz * 1.6f);
    const float desc_w = card_w - pad * 2.0f;
    float desc_h = line;
    for (const TemplateInfo& t : templates_)
        desc_h = std::max(desc_h, font->CalcTextSizeA(fsz, FLT_MAX, desc_w, t.description.c_str()).y);
    desc_h = std::min(desc_h, line * 5.0f);
    const float badge_h = std::floor(line + 4.0f * s);
    const float card_h = std::ceil(pad + tile + 8.0f * s + desc_h + 12.0f * s + badge_h + pad);
    const float r = 8.0f * s;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int i = 0; i < (int)templates_.size(); ++i) {
        const TemplateInfo& t = templates_[(size_t)i];
        if (i % per_row) ImGui::SameLine(0.0f, gap);
        ImGui::PushID(i);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImVec2 end(pos.x + card_w, pos.y + card_h);
        if (ImGui::InvisibleButton("card", ImVec2(card_w, card_h))) {
            SelectTemplate(i, ctx);
            error_.clear();
        }
        const bool hovered = ImGui::IsItemHovered();
        const bool held = ImGui::IsItemActive();
        const bool selected = i == template_index_;
        ImVec4 bg = held ? c.bg_active : hovered ? c.bg_hover : c.bg_frame;
        if (selected) bg = ImVec4(bg.x + (c.accent.x - bg.x) * 0.12f, bg.y + (c.accent.y - bg.y) * 0.12f, bg.z + (c.accent.z - bg.z) * 0.12f, 1.0f);
        dl->AddRectFilled(pos, end, ToU32(bg), r);
        dl->AddRect(pos, end, ToU32(selected ? c.accent : hovered ? WithAlpha(c.accent, 0.5f) : c.separator), r, 0,
                    selected ? 2.0f : 1.0f);

        const ImVec2 tmin(pos.x + pad, pos.y + pad), tmax(tmin.x + tile, tmin.y + tile);
        dl->AddRectFilled(tmin, tmax, ToU32(c.accent, selected ? 1.0f : 0.16f), 6.0f * s);
        const char* icon = TemplateIcon(t.id);
        const ImVec2 is = heading->CalcTextSizeA(hsz, FLT_MAX, 0.0f, icon);
        dl->AddText(heading, hsz, ImVec2(std::floor(tmin.x + (tile - is.x) * 0.5f), std::floor(tmin.y + (tile - is.y) * 0.5f)),
                    selected ? IM_COL32(255, 255, 255, 255) : ToU32(c.accent), icon);

        if (selected) {
            const float cr = std::floor(fsz * 0.5f);
            const ImVec2 cc(tmax.x - 1.0f, tmax.y - 1.0f);
            dl->AddCircleFilled(cc, cr + 2.0f * s, ToU32(bg), 20);
            dl->AddCircleFilled(cc, cr, ToU32(c.success), 20);
            const ImVec2 cs = font->CalcTextSizeA(fsz * 0.7f, FLT_MAX, 0.0f, ICON_FA_CHECK);
            dl->AddText(font, fsz * 0.7f, ImVec2(std::floor(cc.x - cs.x * 0.5f), std::floor(cc.y - cs.y * 0.5f)),
                        IM_COL32(255, 255, 255, 255), ICON_FA_CHECK);
        }
        const float name_x = tmax.x + 10.0f * s, name_max = end.x - pad - name_x;
        const char* name = t.name.c_str();
        const char* name_end = name + t.name.size();
        const float dots = heading->CalcTextSizeA(hsz, FLT_MAX, 0.0f, "...").x;
        bool trimmed = false;
        while (name_end > name + 1 && heading->CalcTextSizeA(hsz, FLT_MAX, 0.0f, name, name_end).x + (trimmed ? dots : 0.0f) > name_max) {
            --name_end;
            trimmed = true;
        }
        const ImVec2 name_pos(name_x, std::floor(tmin.y + (tile - hsz) * 0.5f));
        dl->AddText(heading, hsz, name_pos, ToU32(c.text), name, name_end);
        if (trimmed)
            dl->AddText(heading, hsz, ImVec2(name_pos.x + heading->CalcTextSizeA(hsz, FLT_MAX, 0.0f, name, name_end).x, name_pos.y),
                        ToU32(c.text), "...");

        const float dy = tmax.y + 8.0f * s;
        const ImVec4 desc_clip(pos.x, dy, end.x, dy + desc_h);
        dl->AddText(font, fsz, ImVec2(pos.x + pad, dy), ToU32(c.text_dim), t.description.c_str(), nullptr, desc_w, &desc_clip);

        float bx = pos.x + pad;
        const float by = end.y - pad - badge_h;
        bool any = false;
        for (const BadgeDef& b : kBadges) {
            bool on = false;
            for (const auto& f : t.features) on = on || (f.first == b.key && f.second);
            if (!on) continue;
            const float bw = font->CalcTextSizeA(fsz, FLT_MAX, 0.0f, b.label).x + 14.0f * s;
            if (bx + bw > end.x - pad) break;
            dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + badge_h), ToU32(c.accent, selected ? 0.26f : 0.16f), badge_h * 0.5f);
            dl->AddText(font, fsz, ImVec2(bx + 7.0f * s, std::floor(by + (badge_h - fsz) * 0.5f)), ToU32(c.accent_hover), b.label);
            bx += bw + 5.0f * s;
            any = true;
        }
        if (!any) dl->AddText(font, fsz, ImVec2(bx, std::floor(by + (badge_h - fsz) * 0.5f)), ToU32(c.text_disabled), "no extras");
        ImGui::PopID();
    }
    ImGui::Spacing();
    ui::TextDim("Templates: %s", templates_dir_.c_str());
}

void WizardPanel::DrawProjectStep(EditorContext& ctx) {
    ui::HeadingText(ctx.Font(FontSlot::Heading), "Project");
    ui::TextDim("Name the project, choose where it lives and pick a template to start from.");
    ImGui::Spacing();
    if (ui::BeginPropertyTable("project_props")) {
        ui::PropertyLabel("Name", "Used as the project folder, the generated namespace default and in code markers");
        if (ui::InputTextWithHintStd("##name", "MyProject", name_)) {
            draft_.name = name_;
            error_.clear();
        }
        ui::PropertyLabel("Location", "A new folder named after the project is created here");
        float bw = ImGui::CalcTextSize("Browse...").x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetNextItemWidth(-bw);
        if (ui::InputTextStd("##location", location_)) error_.clear();
        ImGui::SameLine();
        if (ImGui::Button("Browse...")) {
            std::string picked;
            if (NativeDialogsAvailable()) {
                if (NativePickFolder(picked, "Project location", location_) && !picked.empty()) location_ = picked;
            } else {
                FileDialogOptions opts;
                opts.title = "Project location";
                opts.pick_directory = true;
                OpenFileDialog("wizard_location", location_, opts);
            }
        }
        std::string picked;
        if (DrawFileDialog("wizard_location", picked) && !picked.empty()) location_ = picked;
        ui::PropertyLabel("Creates");
        std::string dest = JoinPath(location_.empty() ? "<location>" : location_, name_.empty() ? "<name>" : name_);
        ImGui::TextWrapped("%s", dest.c_str());
        ui::EndPropertyTable();
    }
    std::string msg;
    if (!StepValid(StepProject, msg) && (!name_.empty() || visited_[StepProject])) {
        ImGui::PushStyleColor(ImGuiCol_Text, Colors().warning);
        ImGui::TextWrapped("%s", msg.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::Spacing();
    ImGui::SeparatorText("Template");
    DrawTemplateCards(ctx);
}

void WizardPanel::DrawFooter(EditorContext& ctx, bool focused) {
    float s = ctx.dpi_scale;
    ImVec2 bsize(96.0f * s, 0);
    const ImGuiStyle& st = ImGui::GetStyle();
    float total = bsize.x * 3 + st.ItemSpacing.x * 2;
    if (!error_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, Colors().error);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - total - st.ItemSpacing.x);
        ImGui::TextUnformatted(error_.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::SameLine();
    }
    ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - total);
    if (ImGui::Button("Cancel", bsize)) {
        open = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (step_ == 0) ImGui::BeginDisabled();
    if (ImGui::Button("Back", bsize)) {
        visited_[step_] = true;
        --step_;
        error_.clear();
    }
    if (step_ == 0) ImGui::EndDisabled();
    ImGui::SameLine();
    bool last = step_ == StepCount - 1;
    if (ui::AccentButton(last ? "Finish" : "Next", bsize) ||
        (focused && ImGui::IsKeyPressed(ImGuiKey_Enter, false) && !ImGui::IsAnyItemActive())) {
        visited_[step_] = true;
        std::string msg;
        if (!StepValid(step_, msg)) {
            error_ = msg;
        } else if (!last) {
            ++step_;
            max_step_ = std::max(max_step_, step_);
            error_.clear();
        } else {
            int bad = -1;
            for (int i = 0; i < StepCount && bad < 0; ++i)
                if (!StepValid(i, msg)) bad = i;
            if (bad >= 0) {
                step_ = bad;
                error_ = msg;
            } else if (Finish(ctx, msg)) {
                open = false;
                ImGui::CloseCurrentPopup();
            } else {
                error_ = msg;
            }
        }
    }
}

bool WizardPanel::Finish(EditorContext& ctx, std::string& err) {
    form_.ApplyTexts();
    Project settings = draft_;
    settings.name = name_;
    if (form_.skip_bindings) {
        settings.binding_types.clear();
        settings.include_paths.clear();
        settings.compile_commands.clear();
    }
    std::string dest = JoinPath(location_, name_);
    const TemplateInfo& t = templates_[(size_t)template_index_];
    if (!ctx.NewProject(t.dir, dest, name_, settings, &err)) return false;
    Project& p = ctx.project;
    p.binding_types = settings.binding_types;
    p.include_paths = settings.include_paths;
    p.compile_commands = settings.compile_commands;
    p.types_json = settings.types_json;
    Theme chosen = form_.ChosenTheme();
    if (!chosen.name.empty()) {
        Theme* existing = nullptr;
        for (Theme& th : p.themes)
            if (th.name == chosen.name) existing = &th;
        if (existing) {
            existing->colors = chosen.colors;
            existing->vars = chosen.vars;
        } else {
            chosen.file = DefaultThemeFile(chosen.name);
            p.themes.push_back(chosen);
        }
        p.active_theme = chosen.name;
    }
    for (const wizard::LibraryEntry& lib : form_.libraries) {
        if (!lib.selected || !lib.importable) continue;
        std::string e;
        if (!ctx.ImportImblib(lib.path, &e)) ctx.Log(LogLevel::Warning, "could not import %s: %s", lib.name.c_str(), e.c_str());
    }
    if (!ctx.SaveProject(&err)) return false;
    ctx.NotifyProjectChanged("settings");
    ctx.SetStatus("Created project " + name_ + " in " + dest, 6.0);
    template_index_ = 0;
    return true;
}

void WizardPanel::Draw(EditorContext& ctx) {
    const char* kTitle = "New Project###imforge_wizard";
    if (open && !was_open_) Reset(ctx);
    was_open_ = open;
    if (!open) return;
    if (!ImGui::IsPopupOpen(kTitle)) ImGui::OpenPopup(kTitle);
    float s = ctx.dpi_scale;
    ImVec2 vp = ImGui::GetMainViewport()->WorkSize;
    ImVec2 size(std::min(960.0f * s, vp.x * 0.92f), std::min(710.0f * s, vp.y * 0.92f));
    ImGui::SetNextWindowSize(size, ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(640.0f * s, 420.0f * s), ImVec2(FLT_MAX, FLT_MAX));
    bool keep = true;
    if (!ImGui::BeginPopupModal(kTitle, &keep, ImGuiWindowFlags_NoSavedSettings)) {
        if (!ImGui::IsPopupOpen(kTitle)) open = false;
        return;
    }
    bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows | ImGuiFocusedFlags_NoPopupHierarchy);
    if (!keep || (focused && ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::IsAnyItemActive())) {
        open = false;
        ImGui::CloseCurrentPopup();
    }
    float footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    float list_w = 200.0f * s;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Colors().bg_panel);
    if (ImGui::BeginChild("wizard_steps", ImVec2(list_w, -footer), ImGuiChildFlags_AlwaysUseWindowPadding)) DrawStepList(ctx);
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (ImGui::BeginChild("wizard_content", ImVec2(0, -footer), ImGuiChildFlags_AlwaysUseWindowPadding)) {
        switch (step_) {
        case StepProject: DrawProjectStep(ctx); break;
        case StepTarget: wizard::DrawTargetStep(form_, ctx); break;
        case StepPlatform: wizard::DrawPlatformStep(form_, ctx); break;
        case StepFeatures: wizard::DrawFeaturesStep(form_, ctx); break;
        case StepBindings: wizard::DrawBindingsStep(form_, ctx); break;
        case StepLive: wizard::DrawLiveStep(form_, ctx); break;
        case StepCodegen: wizard::DrawCodegenStep(form_, ctx); break;
        default: wizard::DrawThemeStep(form_, ctx, true); break;
        }
    }
    ImGui::EndChild();
    DrawFooter(ctx, focused);
    ImGui::EndPopup();
}

}  // namespace

void OpenWizard() {
    if (g_instance) g_instance->open = true;
}

void CloseWizard() {
    if (g_instance) g_instance->open = false;
}

bool IsWizardOpen() { return g_instance && g_instance->open; }

IMFORGE_REGISTER_PANEL(WizardPanel, 90);

}  // namespace imforge::builder
