#include <cctype>
#include <cstdio>
#include <cstring>

#include "imgui.h"

#include "../builder_fonts.h"
#include "../builder_style.h"
#include "../os_util.h"
#include "../project_io.h"
#include "../ui/client_snippet.h"
#include "../ui/file_dialog.h"
#include "../ui/theme_apply.h"
#include "../ui/theme_presets.h"
#include "../ui/ui_helpers.h"
#include "wizard_state.h"

#if __has_include("theme_import.h")
#include "theme_import.h"
#define IMFORGE_HAS_THEME_IMPORT 1
#else
#define IMFORGE_HAS_THEME_IMPORT 0
#endif

namespace imforge::builder::wizard {

namespace {

const char* const kCases[] = {"pascal", "camel", "snake"};
const char* const kCaseLabels[] = {"PascalCase", "camelCase", "snake_case"};
const char* const kDpiModes[] = {"none", "fontSize", "variable"};
const char* const kDpiLabels[] = {"None (fixed pixels)", "Font size (sizes scale with the font)", "Variable (float imb_scale parameter)"};

int IndexOf(const char* const* list, int count, const std::string& v) {
    for (int i = 0; i < count; ++i)
        if (v == list[i]) return i;
    return -1;
}

bool Combo(const char* id, std::string& value, const char* const* values, const char* const* labels, int count) {
    bool changed = false;
    int cur = IndexOf(values, count, value);
    if (ImGui::BeginCombo(id, cur >= 0 ? labels[cur] : value.c_str())) {
        for (int i = 0; i < count; ++i)
            if (ImGui::Selectable(labels[i], i == cur)) {
                value = values[i];
                changed = true;
            }
        ImGui::EndCombo();
    }
    return changed;
}

void Notice(const ImVec4& color, const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

void StepHeading(EditorContext& ctx, const char* title, const char* subtitle) {
    ui::HeadingText(ctx.Font(FontSlot::Heading), title);
    ImGui::PushTextWrapPos(0.0f);
    ui::TextDim("%s", subtitle);
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
}

bool BrowseDir(const char* key, std::string& path, EditorContext& ctx) {
    bool changed = false;
    std::string start = ctx.has_project ? ctx.project.dir : HomeDir();
    if (ImGui::Button("Browse...")) {
        std::string picked;
        if (NativeDialogsAvailable()) {
            if (NativePickFolder(picked, "Select folder", start) && !picked.empty()) {
                path = picked;
                changed = true;
            }
        } else {
            FileDialogOptions opts;
            opts.title = "Select folder";
            opts.pick_directory = true;
            OpenFileDialog(key, start, opts);
        }
    }
    std::string out;
    if (DrawFileDialog(key, out) && !out.empty()) {
        path = out;
        changed = true;
    }
    return changed;
}

std::string SnippetKey(const Project& p) {
    std::string k = p.live.transport + "|" + std::to_string(p.live.tcp_port) + "|" + p.live.pipe_name + "|" + p.live.shm_name +
                    "|" + (p.live.enabled ? "1" : "0") + "|" + p.codegen.output_dir + "|" + p.codegen.nmspace + "|" + p.name;
    for (const std::string& t : p.binding_types) k += "," + t;
    if (!p.panels.empty()) {
        k += "|" + p.panels.front().name;
        for (const Param& prm : p.panels.front().params) k += ":" + prm.name + "/" + prm.type;
    }
    return k;
}

void ThemeSwatches(const ThemePresetInfo& info, float size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    for (int i = 0; i < 4; ++i) {
        unsigned c = info.swatch[i];
        ImU32 col = IM_COL32((c >> 16) & 255, (c >> 8) & 255, c & 255, 255);
        ImVec2 a(p.x + i * (size + 2), p.y);
        dl->AddRectFilled(a, ImVec2(a.x + size, a.y + size), col, 3.0f);
        dl->AddRect(a, ImVec2(a.x + size, a.y + size), ToU32(Colors().border), 3.0f);
    }
    ImGui::Dummy(ImVec2(4 * (size + 2), size));
}

void ThemePreview(const char* id, const Theme& theme, const ImVec2& size) {
    StyleScope scope = PushThemeStyle(theme);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
    if (ImGui::BeginChild(id, size, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding)) {
        static float slider = 0.4f;
        static bool check = true;
        static char buf[32] = "Text input";
        static int tab = 0;
        if (ImGui::BeginTabBar("preview_tabs")) {
            for (int i = 0; i < 3; ++i) {
                const char* names[] = {"General", "Visuals", "Misc"};
                if (ImGui::BeginTabItem(names[i])) {
                    tab = i;
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
        (void)tab;
        ImGui::Text("Heading text");
        ImGui::TextDisabled("Disabled / hint text");
        ImGui::Checkbox("Enabled", &check);
        ImGui::SameLine();
        ImGui::Button("Button");
        ImGui::SameLine();
        ImGui::SmallButton("Small");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SliderFloat("##slider", &slider, 0.0f, 1.0f, "Slider %.2f");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputText("##input", buf, sizeof(buf));
        ImGui::ProgressBar(slider, ImVec2(-FLT_MIN, 0));
        if (ImGui::CollapsingHeader("Section", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Selectable("Selected row", true);
            ImGui::Selectable("Another row", false);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    PopStyleScope(scope);
}

}  // namespace

void DrawCodeBox(const char* id, const std::string& text, EditorContext& ctx, float height) {
    ImFont* mono = ctx.Font(FontSlot::Mono);
    if (!mono) mono = MonoFont();
    if (mono) ImGui::PushFont(mono);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, Colors().bg_panel);
    ImGui::InputTextMultiline(id, const_cast<char*>(text.c_str()), text.size() + 1, ImVec2(-FLT_MIN, height),
                              ImGuiInputTextFlags_ReadOnly);
    ImGui::PopStyleColor();
    if (mono) ImGui::PopFont();
}

bool DrawLiveStep(SettingsForm& form, EditorContext& ctx) {
    Project& p = *form.project;
    bool changed = false;
    StepHeading(ctx, "Live attach", "The builder listens; your application connects and renders the panel you are editing with its own renderer and fonts.");
    if (ui::BeginPropertyTable("live_props")) {
        ui::PropertyLabel("Enabled");
        if (ui::ToggleButton("##live_enabled", &p.live.enabled)) changed = true;
        ui::PropertyLabel("Transport");
        if (ImGui::RadioButton("TCP", p.live.transport == "tcp")) {
            p.live.transport = "tcp";
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Named pipe", p.live.transport == "pipe")) {
            p.live.transport = "pipe";
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Shared memory", p.live.transport == "shm")) {
            p.live.transport = "shm";
            changed = true;
        }
        if (p.live.transport == "pipe") {
            ui::PropertyLabel("Pipe name", "Windows: \\\\.\\pipe\\<name>; Linux: /tmp/imforge-<name>.sock");
            if (ui::InputTextStd("##pipe", p.live.pipe_name)) changed = true;
        } else if (p.live.transport == "shm") {
            ui::PropertyLabel("Segment name", "Shared memory ring buffer, polled every frame");
            if (ui::InputTextStd("##shm", p.live.shm_name)) changed = true;
        } else {
            ui::PropertyLabel("Port", "The builder binds 127.0.0.1 by default; change the bind host in Preferences for remote apps");
            if (ImGui::InputInt("##port", &p.live.tcp_port, 1, 100)) changed = true;
        }
        ui::PropertyLabel("URL");
        ImGui::TextUnformatted(LiveUrl(p).c_str());
        ui::EndPropertyTable();
    }
    std::string msg;
    if (!ValidateLive(form, msg)) Notice(Colors().error, msg.c_str());
    ImGui::Spacing();
    std::string key = SnippetKey(p);
    if (key != form.snippet_key) {
        form.snippet_key = key;
        form.snippet = MakeClientSnippet(p);
    }
    ImGui::SeparatorText("Client setup");
    ImGui::TextUnformatted("Add this to your application:");
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy")) {
        ImGui::SetClipboardText(form.snippet.c_str());
        ctx.SetStatus("Client snippet copied to clipboard");
    }
    float h = ImGui::GetContentRegionAvail().y;
    if (h < ImGui::GetTextLineHeight() * 8) h = ImGui::GetTextLineHeight() * 8;
    DrawCodeBox("##client_snippet", form.snippet, ctx, h);
    return changed;
}

bool DrawCodegenStep(SettingsForm& form, EditorContext& ctx) {
    Project& p = *form.project;
    CodegenSettings& c = p.codegen;
    bool changed = false;
    StepHeading(ctx, "Code generation", "How the exported C++ looks. Generated code depends only on Dear ImGui and imb_runtime.h, never on the builder.");
    if (ui::BeginPropertyTable("codegen_props", ImGui::GetFontSize() * 9.0f)) {
        ui::PropertyLabel("C++ standard");
        const char* stds[] = {"C++14", "C++17", "C++20"};
        int cur = c.cpp_standard == 14 ? 0 : (c.cpp_standard == 20 ? 2 : 1);
        if (ImGui::Combo("##std", &cur, stds, 3)) {
            c.cpp_standard = cur == 0 ? 14 : (cur == 2 ? 20 : 17);
            changed = true;
        }
        ui::PropertyLabel("Namespace", "Empty for the global namespace");
        if (ui::InputTextWithHintStd("##ns", "ui", c.nmspace)) changed = true;
        ui::PropertyLabel("Files");
        if (ImGui::RadioButton("Header + source", c.split_header)) {
            c.split_header = true;
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Single header", !c.split_header)) {
            c.split_header = false;
            changed = true;
        }
        ui::PropertyLabel("Function case");
        if (Combo("##fcase", c.function_case, kCases, kCaseLabels, 3)) changed = true;
        ui::PropertyLabel("Variable case");
        if (Combo("##vcase", c.variable_case, kCases, kCaseLabels, 3)) changed = true;
        ui::PropertyLabel("Indent");
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5.0f);
        if (ImGui::InputInt("##indent", &c.indent)) {
            c.indent = c.indent < 1 ? 1 : (c.indent > 8 ? 8 : c.indent);
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("Use tabs", &c.use_tabs)) changed = true;
        ui::PropertyLabel("DPI mode", "fontSize: sizes are multiplied by GetFontSize()/baseFontSize; variable: every Draw function takes float imb_scale");
        if (Combo("##dpi", c.dpi_mode, kDpiModes, kDpiLabels, 3)) changed = true;
        if (c.dpi_mode != "none") {
            ui::PropertyLabel("Base font size", "The font size the layout was designed at");
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
            if (ImGui::InputFloat("##basefont", &c.base_font_size, 1.0f, 2.0f, "%.0f")) {
                if (c.base_font_size < 6) c.base_font_size = 6;
                changed = true;
            }
        }
        ui::PropertyLabel("std::string", "Bind InputText to std::string (needs misc/cpp/imgui_stdlib.h); off emits char arrays");
        if (ImGui::Checkbox("##stdstring", &c.std_string)) changed = true;
        ui::PropertyLabel("Markers", "// @imforge comments around generated blocks");
        if (ImGui::Checkbox("##markers", &c.markers)) changed = true;
        ui::PropertyLabel("Runtime header");
        if (ui::InputTextWithHintStd("##rt", "imb_runtime.h", c.runtime_header)) changed = true;
        ui::PropertyLabel("Output directory", "Relative to the project folder");
        float bw = ImGui::CalcTextSize("Browse...").x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetNextItemWidth(-bw);
        if (ui::InputTextWithHintStd("##outdir", "generated", c.output_dir)) changed = true;
        ImGui::SameLine();
        if (BrowseDir("wizard_output_dir", c.output_dir, ctx)) changed = true;
        ui::EndPropertyTable();
    }
    if (!c.markers)
        Notice(Colors().warning, "Markers are off: exported code cannot be re-imported or merged with your edits (round-trip disabled). Export becomes one-way.");
    std::string msg;
    if (!ValidateCodegen(form, msg)) Notice(Colors().error, msg.c_str());
    return changed;
}

bool DrawThemeStep(SettingsForm& form, EditorContext& ctx, bool in_wizard) {
    Project& p = *form.project;
    bool changed = false;
    StepHeading(ctx, "Theme", in_wizard ? "Pick a starting theme. You can refine every color later in the Theme Editor."
                                        : "Add a preset or import a theme from ImGui style code; the active theme is used by the canvas and the generated code.");
    const std::vector<ThemePresetInfo>& presets = ThemePresets();
    float avail = ImGui::GetContentRegionAvail().x;
    float card_w = 150.0f * ctx.dpi_scale;
    int per_row = (int)(avail / (card_w + ImGui::GetStyle().ItemSpacing.x));
    if (per_row < 1) per_row = 1;
    if (in_wizard) {
        bool keep = form.theme_choice < 0 && !form.has_imported;
        if (ImGui::RadioButton("Keep the template's theme", keep)) {
            form.theme_choice = -1;
            form.has_imported = false;
            changed = true;
        }
    }
    for (int i = 0; i < (int)presets.size(); ++i) {
        const ThemePresetInfo& info = presets[(size_t)i];
        if (i % per_row) ImGui::SameLine();
        ImGui::PushID(i);
        ImVec2 pos = ImGui::GetCursorScreenPos();
        float sw = 18.0f * ctx.dpi_scale;
        ImVec2 size(card_w, sw + ImGui::GetTextLineHeightWithSpacing() * 2 + ImGui::GetStyle().FramePadding.y * 4);
        bool selected = form.theme_choice == i && !form.has_imported;
        if (ImGui::InvisibleButton("card", size)) {
            form.theme_choice = i;
            form.has_imported = false;
            changed = true;
        }
        bool hovered = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), ToU32(hovered ? Colors().bg_hover : Colors().bg_frame), 6.0f);
        dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), ToU32(selected ? Colors().accent : Colors().border), 6.0f, 0,
                    selected ? 2.0f : 1.0f);
        ImGui::SetCursorScreenPos(ImVec2(pos.x + ImGui::GetStyle().FramePadding.x * 2, pos.y + ImGui::GetStyle().FramePadding.y * 2));
        ImGui::BeginGroup();
        ThemeSwatches(info, sw);
        ImGui::TextUnformatted(info.name);
        ImGui::EndGroup();
        ui::Tooltip(info.description);
        ImGui::SetCursorScreenPos(pos);
        ImGui::Dummy(size);
        ImGui::PopID();
    }
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Import from code...")) {
        ui::TextDim("Paste C++ that assigns style.Colors[ImGuiCol_*] / ImGui::PushStyleColor and style variables.");
        ui::InputTextMultilineStd("##theme_code", form.theme_code, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 6));
        if (ImGui::Button("Parse")) {
            Theme t;
            std::string err;
            int ncol = 0, nvar = 0;
            bool ok = false;
#if IMFORGE_HAS_THEME_IMPORT
            t.name = "Imported";
            ThemeImportResult r = ImportThemeFromCode(form.theme_code, t, true);
            ncol = r.colors;
            nvar = r.vars;
            ok = ncol + nvar > 0;
            if (!ok) err = "no ImGuiCol_* color assignments or style variables were recognized";
            else if (!r.warnings.empty()) err = " (" + std::to_string(r.warnings.size()) + " warnings: " + r.warnings.front() + ")";
            if (ok && !r.base.empty()) err = " (base: " + r.base + ")" + err;
#else
            ok = ParseThemeFromCode(form.theme_code, t, &err, &ncol, &nvar);
            if (ok) err.clear();
#endif
            if (ok) {
                form.imported_theme = t;
                form.has_imported = true;
                form.theme_choice = -1;
                form.theme_message = "Imported " + std::to_string(ncol) + " colors and " + std::to_string(nvar) + " style variables" + err;
                changed = true;
            } else {
                form.theme_message = err;
            }
        }
        if (!form.theme_message.empty()) {
            ImGui::SameLine();
            Notice(form.has_imported ? Colors().success : Colors().warning, form.theme_message.c_str());
        }
    }
    Theme chosen = form.ChosenTheme();
    bool have = form.theme_choice >= 0 || form.has_imported;
    if (!have && !in_wizard) {
        const Theme* active = FindThemeByName(p, p.active_theme);
        if (active) chosen = *active;
        have = active != nullptr;
    }
    if (have) {
        ImGui::Spacing();
        ImGui::SeparatorText(chosen.name.empty() ? "Preview" : ("Preview: " + chosen.name).c_str());
        ThemePreview("theme_preview", chosen, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 13));
    }
    return changed;
}

bool ValidateLive(const SettingsForm& form, std::string& msg) {
    const LiveSettings& l = form.project->live;
    if (l.transport == "tcp") {
        if (l.tcp_port < 1 || l.tcp_port > 65535) {
            msg = "TCP port must be between 1 and 65535";
            return false;
        }
    } else if (l.transport == "pipe") {
        if (l.pipe_name.empty() || !IsIdentifier(l.pipe_name)) {
            msg = "pipe name must be a simple identifier";
            return false;
        }
    } else if (l.transport == "shm") {
        if (l.shm_name.empty() || !IsIdentifier(l.shm_name)) {
            msg = "shared memory name must be a simple identifier";
            return false;
        }
    } else {
        msg = "transport must be tcp, pipe or shm";
        return false;
    }
    return true;
}

bool ValidateCodegen(const SettingsForm& form, std::string& msg) {
    const CodegenSettings& c = form.project->codegen;
    if (!c.nmspace.empty()) {
        for (const std::string& part : SplitList(c.nmspace, ":"))
            if (!IsIdentifier(part)) {
                msg = "namespace must be an identifier (a::b allowed)";
                return false;
            }
    }
    if (c.output_dir.empty()) {
        msg = "output directory is required";
        return false;
    }
    if (c.indent < 1 || c.indent > 8) {
        msg = "indent must be between 1 and 8";
        return false;
    }
    if (c.dpi_mode != "none" && c.base_font_size <= 0) {
        msg = "base font size must be positive";
        return false;
    }
    if (c.cpp_standard != 14 && c.cpp_standard != 17 && c.cpp_standard != 20) {
        msg = "C++ standard must be 14, 17 or 20";
        return false;
    }
    return true;
}

}  // namespace imforge::builder::wizard
