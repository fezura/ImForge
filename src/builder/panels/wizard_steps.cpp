#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <system_error>

#include "imgui.h"

#include "../builder_fonts.h"
#include "../builder_style.h"
#include "../os_util.h"
#include "../project_io.h"
#include "../ui/client_snippet.h"
#include "../ui/file_dialog.h"
#include "../ui/theme_presets.h"
#include "../ui/ui_helpers.h"
#include "wizard_state.h"

namespace fs = std::filesystem;

namespace imforge::builder::wizard {

namespace {

const char* const kVersions[] = {"1.85", "1.86", "1.87", "1.88", "1.89", "1.89.9", "1.90",
                                 "1.90.9", "1.91", "1.91.9", "1.92", "1.93", "Custom..."};
const char* const kWindows[] = {"win32", "sdl2", "sdl3", "glfw"};
const char* const kWindowLabels[] = {"Win32", "SDL2", "SDL3", "GLFW"};
const char* const kRenderers[] = {"dx9", "dx11", "dx12", "gl3", "vulkan"};
const char* const kRendererLabels[] = {"DirectX 9", "DirectX 11", "DirectX 12", "OpenGL 3", "Vulkan"};

struct FeatureDef {
    const char* key;
    const char* label;
    const char* description;
};

const FeatureDef kFeatures[] = {
    {"animations", "Animations", "Transitions, enter/exit presets and springs through imb_runtime.h."},
    {"drawlistFx", "DrawList effects", "Shadows, glows and gradients drawn with ImDrawList; no shader required."},
    {"shaderFx", "Shader effects", "Background blur through imb::fx; needs the PrepareFrame hook in your renderer."},
    {"iconFonts", "Icon fonts", "Merges Font Awesome 6 into your fonts and enables the icon picker."},
    {"dpiScaling", "DPI scaling", "Sizes scale with the font size or an imb_scale parameter (see Codegen)."},
    {"docking", "Docking", "DockSpace and dockable windows; requires the docking branch."},
};

struct Capability {
    const char* feature;
    int needs;
    const char* fallback;
};

const Capability kCapabilities[] = {
    {"BeginDisabled / EndDisabled (enabledIf)", 18600, "PushItemFlag(Disabled) + Alpha"},
    {"ImGuiKey keyboard IO (keybind picker)", 18700, "legacy KeyMap indices"},
    {"SeparatorText", 18950, "Separator() + Text()"},
    {"BeginChild with ImGuiChildFlags", 19000, "bool border overload"},
    {"TabSelected / TabDimmed color names", 19090, "TabActive / TabUnfocused"},
    {"TabBarOverlineSize style var", 19100, "ignored"},
    {"InputText ElideLeft flag", 19190, "ignored"},
    {"Dynamic fonts (PushFont with size)", 19200, "LoadFonts bakes each size"},
    {"ImTextureRef for Image widgets", 19200, "ImTextureID"},
};

int IndexOf(const char* const* list, int count, const std::string& v) {
    for (int i = 0; i < count; ++i)
        if (v == list[i]) return i;
    return -1;
}

bool& FeatureRef(Project& p, const char* key) {
    for (auto& f : p.features)
        if (f.first == key) return f.second;
    p.features.emplace_back(key, false);
    return p.features.back().second;
}

std::string JoinList(const std::vector<std::string>& v, const char* sep) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) out += sep;
        out += v[i];
    }
    return out;
}

bool BrowseButton(const char* key, std::string& path, bool directory, const char* ext, EditorContext& ctx) {
    bool changed = false;
    std::string start = path;
    if (start.empty()) start = ctx.has_project ? ctx.project.dir : HomeDir();
    std::error_code ec;
    if (!directory && !start.empty() && !fs::is_directory(start, ec)) start = fs::path(start).parent_path().string();
    if (ImGui::Button("Browse...")) {
        std::string picked;
        if (NativeDialogsAvailable()) {
            bool ok = directory ? NativePickFolder(picked, "Select folder", start) : NativePickFile(picked, "Select file", ext, start);
            if (ok && !picked.empty()) {
                path = picked;
                changed = true;
            }
        } else {
            FileDialogOptions opts;
            opts.title = directory ? "Select folder" : "Select file";
            opts.pick_directory = directory;
            if (ext && *ext) opts.extensions.push_back(ext);
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

void StepHeading(EditorContext& ctx, const char* title, const char* subtitle) {
    ui::HeadingText(ctx.Font(FontSlot::Heading), title);
    ImGui::PushTextWrapPos(0.0f);
    ui::TextDim("%s", subtitle);
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
}

void Notice(const ImVec4& color, const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

const char* RendererFxNote(const std::string& r) {
    if (r == "dx9") return "imb_fx_dx9.h: Init(Dx9InitDesc{device}); PrepareFrame(FrameDesc{back buffer surface}) after your scene, before ImGui_ImplDX9_RenderDrawData.";
    if (r == "dx11") return "imb_fx_dx11.h: Init(Dx11InitDesc{device, context}); PrepareFrame(FrameDesc{back buffer texture}) after your scene, before ImGui_ImplDX11_RenderDrawData.";
    if (r == "dx12") return "imb_fx_dx12.h: Init(Dx12InitDesc{device, rtv_format, frames_in_flight, srv heap + descriptor range}). Every frame PrepareFrame(FrameDesc{cmd_list, back_buffer, width, height, frame_index, back_buffer_rtv}) with the back buffer in RENDER_TARGET state, before ImGui_ImplDX12_RenderDrawData on the same command list.";
    if (r == "gl3") return "imb_fx_gl3.h: Init(Gl3InitDesc{}); PrepareFrame(FrameDesc{framebuffer, width, height}) after your scene, before ImGui_ImplOpenGL3_RenderDrawData. Uses the GL loader imgui_impl_opengl3 uses.";
    if (r == "vulkan") return "imb_fx_vulkan.h: Init(VulkanInitDesc{instance, physical_device, device, queue_family, queue, descriptor_pool, render_pass, color_format, image_count}). Every frame PrepareFrame(FrameDesc{cmd, back_buffer image, layout, width, height, frame_index}) recorded on the frame command buffer outside the render pass, before ImGui_ImplVulkan_RenderDrawData.";
    return "Unknown renderer: shader effects unavailable.";
}

void AddImblibs(const fs::path& dir, std::vector<LibraryEntry>& out) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec) || e.path().extension() != ".imblib") continue;
        std::string name = e.path().stem().string();
        bool dup = false;
        for (const LibraryEntry& l : out) dup = dup || l.name == name;
        if (dup) continue;
        LibraryEntry le;
        le.name = name;
        le.path = e.path().generic_string();
        le.importable = true;
        out.push_back(std::move(le));
    }
}

void AddPacks(const fs::path& dir, std::vector<LibraryEntry>& out) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_directory(ec)) continue;
        fs::path manifest = e.path() / "manifest.json";
        Json j;
        if (!fs::is_regular_file(manifest, ec) || !ReadJsonFile(manifest.generic_string(), j)) continue;
        std::string name = j["name"].AsString(e.path().filename().string());
        LibraryEntry* existing = nullptr;
        for (LibraryEntry& l : out)
            if (l.name == name) existing = &l;
        if (existing) {
            if (existing->description.empty()) existing->description = j["description"].AsString();
            existing->min_version = j["minImGuiVersion"].AsInt(existing->min_version);
            continue;
        }
        LibraryEntry le;
        le.name = name;
        le.path = manifest.generic_string();
        le.description = j["description"].AsString();
        le.min_version = j["minImGuiVersion"].AsInt(0);
        le.importable = false;
        le.reason = "no packaged .imblib found in dist/ (run tools/pack_imblib.py)";
        out.push_back(std::move(le));
    }
}

}  // namespace

void SettingsForm::Attach(Project& p) {
    project = &p;
    if (VersionChoiceIndex(p.imgui_version) < 0) custom_version = p.imgui_version;
    SyncTexts();
}

void SettingsForm::SyncTexts() {
    if (!project) return;
    types_text = JoinList(project->binding_types, ", ");
    include_paths_text = JoinList(project->include_paths, "\n");
}

void SettingsForm::ApplyTexts() {
    if (!project) return;
    project->binding_types = SplitList(types_text, ",;\n");
    project->include_paths = SplitList(include_paths_text, "\n;");
}

Theme SettingsForm::ChosenTheme() const {
    if (theme_choice >= 0 && theme_choice < (int)ThemePresets().size()) return MakeThemePreset(ThemePresets()[(size_t)theme_choice].name);
    if (has_imported) return imported_theme;
    return Theme();
}

const char* const* ImGuiVersionChoices(int& count) {
    count = (int)(sizeof(kVersions) / sizeof(kVersions[0]));
    return kVersions;
}

int VersionChoiceIndex(const std::string& version) {
    int n = (int)(sizeof(kVersions) / sizeof(kVersions[0])) - 1;
    return IndexOf(kVersions, n, version);
}

bool IsIdentifier(const std::string& s) {
    if (s.empty() || !(std::isalpha((unsigned char)s[0]) || s[0] == '_')) return false;
    for (unsigned char c : s)
        if (!(std::isalnum(c) || c == '_')) return false;
    return true;
}

bool WindowRendererCompatible(const std::string& window, const std::string& renderer) {
    if (IndexOf(kWindows, 4, window) < 0 || IndexOf(kRenderers, 5, renderer) < 0) return false;
    if (window == "win32") return true;
    return renderer == "gl3" || renderer == "vulkan" || renderer == "dx11";
}

const char* IncompatibilityReason(const std::string& window, const std::string& renderer) {
    if (WindowRendererCompatible(window, renderer)) return "";
    if (IndexOf(kWindows, 4, window) < 0) return "unknown window backend";
    if (IndexOf(kRenderers, 5, renderer) < 0) return "unknown renderer";
    return "Dear ImGui has no SDL/GLFW platform binding paired with this renderer; use Win32";
}

std::vector<std::string> SplitList(const std::string& text, const char* separators) {
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&]() {
        size_t a = 0, b = cur.size();
        while (a < b && std::isspace((unsigned char)cur[a])) ++a;
        while (b > a && std::isspace((unsigned char)cur[b - 1])) --b;
        if (b > a) out.push_back(cur.substr(a, b - a));
        cur.clear();
    };
    for (char c : text) {
        if (std::strchr(separators, c)) flush();
        else cur.push_back(c);
    }
    flush();
    return out;
}

bool DetectImGuiHeader(const std::string& path_in, std::string& version, bool& docking, std::string& message) {
    std::error_code ec;
    fs::path path = path_in;
    if (fs::is_directory(path, ec)) path /= "imgui.h";
    std::string text;
    Error err;
    if (!ReadTextFile(path.generic_string(), text, &err)) {
        message = "cannot read " + path.generic_string();
        return false;
    }
    size_t pos = text.find("#define IMGUI_VERSION_NUM");
    if (pos == std::string::npos) {
        message = "no IMGUI_VERSION_NUM in " + path.filename().string();
        return false;
    }
    int num = std::atoi(text.c_str() + pos + std::strlen("#define IMGUI_VERSION_NUM"));
    if (num < 18500) {
        message = "IMGUI_VERSION_NUM " + std::to_string(num) + " is older than 1.85, which ImForge does not support";
        return false;
    }
    version = Project::VersionString(num);
    docking = text.find("#define IMGUI_HAS_DOCK") != std::string::npos;
    std::string label;
    size_t vpos = text.find("#define IMGUI_VERSION ");
    if (vpos != std::string::npos) {
        size_t q1 = text.find('"', vpos), q2 = q1 == std::string::npos ? q1 : text.find('"', q1 + 1);
        if (q2 != std::string::npos) label = text.substr(q1 + 1, q2 - q1 - 1);
    }
    message = "Detected Dear ImGui " + (label.empty() ? version : label) + " (" + std::to_string(num) + "), " +
              (docking ? "docking" : "master") + " branch";
    return true;
}

void ScanLibraries(SettingsForm& form) {
    std::vector<LibraryEntry> found;
    std::vector<fs::path> bases;
    fs::path exe = ExeDir();
    bases.push_back(exe);
    bases.push_back(exe / "..");
    bases.push_back(exe / ".." / "..");
    bases.push_back(exe / ".." / ".." / "..");
#ifdef IMFORGE_SOURCE_DIR
    bases.push_back(fs::path(IMFORGE_SOURCE_DIR));
#endif
    for (const fs::path& b : bases) AddImblibs(b / "dist", found);
    for (const fs::path& b : bases) AddPacks(b / "packs", found);
    std::sort(found.begin(), found.end(), [](const LibraryEntry& a, const LibraryEntry& b) { return a.name < b.name; });
    for (LibraryEntry& f : found)
        for (const LibraryEntry& old : form.libraries)
            if (old.name == f.name) f.selected = old.selected;
    form.libraries = std::move(found);
    form.libraries_scanned = true;
}

bool DrawTargetStep(SettingsForm& form, EditorContext& ctx) {
    Project& p = *form.project;
    bool changed = false;
    StepHeading(ctx, "Dear ImGui target",
                "The version and branch your application compiles against. The builder hides what the target cannot do and the generated code gates the rest with IMGUI_VERSION_NUM checks.");
    int count = 0;
    const char* const* choices = ImGuiVersionChoices(count);
    int idx = VersionChoiceIndex(p.imgui_version);
    bool custom = idx < 0;
    if (ui::BeginPropertyTable("target_props")) {
        ui::PropertyLabel("Version");
        if (ImGui::BeginCombo("##version", custom ? "Custom..." : p.imgui_version.c_str())) {
            for (int i = 0; i < count; ++i) {
                bool is_custom_item = i == count - 1;
                bool selected = is_custom_item ? custom : i == idx;
                if (ImGui::Selectable(choices[i], selected)) {
                    if (is_custom_item) {
                        if (form.custom_version.empty()) form.custom_version = p.imgui_version + ".x";
                        p.imgui_version = form.custom_version;
                    } else {
                        p.imgui_version = choices[i];
                    }
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        if (custom) {
            ui::PropertyLabel("Custom version", "Form: major.minor[.patch], e.g. 1.91.5");
            if (ui::InputTextStd("##custom_version", form.custom_version)) {
                p.imgui_version = form.custom_version;
                changed = true;
            }
        }
        ui::PropertyLabel("Branch");
        if (ImGui::RadioButton("master", p.imgui_branch != "docking")) {
            p.imgui_branch = "master";
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("docking", p.imgui_branch == "docking")) {
            p.imgui_branch = "docking";
            changed = true;
        }
        ui::PropertyLabel("Detect from imgui.h", "Scans imgui.h for IMGUI_VERSION_NUM and IMGUI_HAS_DOCK");
        float bw = ImGui::CalcTextSize("Browse...").x + ImGui::CalcTextSize("Detect").x + ImGui::GetStyle().FramePadding.x * 4 +
                   ImGui::GetStyle().ItemSpacing.x * 2;
        ImGui::SetNextItemWidth(-bw);
        ui::InputTextWithHintStd("##detect_path", "path to imgui.h or its folder", form.detect_path);
        ImGui::SameLine();
        BrowseButton("wizard_detect_imgui", form.detect_path, false, ".h", ctx);
        ImGui::SameLine();
        if (ImGui::Button("Detect")) {
            std::string v;
            bool dock = false;
            form.detect_ok = DetectImGuiHeader(form.detect_path, v, dock, form.detect_message);
            if (form.detect_ok) {
                p.imgui_version = v;
                p.imgui_branch = dock ? "docking" : "master";
                if (VersionChoiceIndex(v) < 0) form.custom_version = v;
                changed = true;
            }
        }
        if (!form.detect_message.empty()) {
            ui::PropertyLabel("");
            Notice(form.detect_ok ? Colors().success : Colors().warning, form.detect_message.c_str());
        }
        ui::EndPropertyTable();
    }
    ImGui::Spacing();
    int num = p.ImGuiVersionNum();
    if (num < 18500) {
        Notice(Colors().error, "Enter a version of the form 1.xx[.y] that is 1.85 or newer.");
        return changed;
    }
    ImGui::SeparatorText("Capabilities at this target");
    if (ImGui::BeginTable("caps", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Feature", ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableSetupColumn("Needs", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch, 2.5f);
        ImGui::TableHeadersRow();
        for (const Capability& c : kCapabilities) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(c.feature);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(Project::VersionString(c.needs).c_str());
            ImGui::TableSetColumnIndex(2);
            bool ok = num >= c.needs;
            ImGui::PushStyleColor(ImGuiCol_Text, ok ? Colors().success : Colors().warning);
            ImGui::TextUnformatted(ok ? "available" : c.fallback);
            ImGui::PopStyleColor();
        }
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Docking, DockSpace, viewports");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("docking branch");
        ImGui::TableSetColumnIndex(2);
        bool dock = p.imgui_branch == "docking";
        ImGui::PushStyleColor(ImGuiCol_Text, dock ? Colors().success : Colors().warning);
        ImGui::TextUnformatted(dock ? "available" : "docking feature hidden");
        ImGui::PopStyleColor();
        ImGui::EndTable();
    }
    return changed;
}

bool DrawPlatformStep(SettingsForm& form, EditorContext& ctx) {
    Project& p = *form.project;
    bool changed = false;
    StepHeading(ctx, "Platform & renderer", "Which Dear ImGui platform and renderer bindings your application uses. This selects the shader-fx backend and the example client.");
    if (ui::BeginPropertyTable("platform_props")) {
        ui::PropertyLabel("Window");
        int wi = IndexOf(kWindows, 4, p.window);
        if (ImGui::BeginCombo("##window", wi >= 0 ? kWindowLabels[wi] : p.window.c_str())) {
            for (int i = 0; i < 4; ++i)
                if (ImGui::Selectable(kWindowLabels[i], i == wi)) {
                    p.window = kWindows[i];
                    changed = true;
                }
            ImGui::EndCombo();
        }
        ui::PropertyLabel("Renderer");
        int ri = IndexOf(kRenderers, 5, p.renderer);
        if (ImGui::BeginCombo("##renderer", ri >= 0 ? kRendererLabels[ri] : p.renderer.c_str())) {
            for (int i = 0; i < 5; ++i) {
                bool ok = WindowRendererCompatible(p.window, kRenderers[i]);
                if (ImGui::Selectable(kRendererLabels[i], i == ri, ok ? 0 : ImGuiSelectableFlags_Disabled)) {
                    p.renderer = kRenderers[i];
                    changed = true;
                }
                if (!ok) ui::Tooltip(IncompatibilityReason(p.window, kRenderers[i]));
            }
            ImGui::EndCombo();
        }
        ui::EndPropertyTable();
    }
    ImGui::Spacing();
    ImGui::SeparatorText("Supported combinations");
    if (ImGui::BeginTable("combos", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableSetupColumn("");
        for (int r = 0; r < 5; ++r) ImGui::TableSetupColumn(kRendererLabels[r]);
        ImGui::TableHeadersRow();
        for (int w = 0; w < 4; ++w) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(kWindowLabels[w]);
            for (int r = 0; r < 5; ++r) {
                ImGui::TableSetColumnIndex(r + 1);
                bool ok = WindowRendererCompatible(kWindows[w], kRenderers[r]);
                bool current = p.window == kWindows[w] && p.renderer == kRenderers[r];
                ImGui::PushID(w * 8 + r);
                ImGui::PushStyleColor(ImGuiCol_Text, ok ? (current ? Colors().accent : Colors().success) : Colors().text_disabled);
                if (ImGui::Selectable(ok ? (current ? "selected" : "ok") : "-", current, ok ? 0 : ImGuiSelectableFlags_Disabled)) {
                    p.window = kWindows[w];
                    p.renderer = kRenderers[r];
                    changed = true;
                }
                ImGui::PopStyleColor();
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    if (!WindowRendererCompatible(p.window, p.renderer)) Notice(Colors().error, IncompatibilityReason(p.window, p.renderer));
    ImGui::Spacing();
    ImGui::SeparatorText("Shader effects on this renderer");
    std::string hdr = ShaderFxHeader(p.renderer);
    if (hdr.empty()) {
        Notice(Colors().warning, "No imb::fx backend for this renderer; the Shader effects feature will be disabled.");
    } else {
        ImGui::TextWrapped("Available: imb::fx::Blur (rounded background blur with tint), via \"%s\".", hdr.c_str());
        ui::TextDim("%s", RendererFxNote(p.renderer));
    }
    return changed;
}

bool DrawFeaturesStep(SettingsForm& form, EditorContext& ctx) {
    Project& p = *form.project;
    bool changed = false;
    StepHeading(ctx, "Features", "Toggle the optional parts of the runtime and the builder. Everything stays editable in Project Settings.");
    bool fx_ok = !ShaderFxHeader(p.renderer).empty();
    bool dock_ok = p.imgui_branch == "docking";
    for (const FeatureDef& f : kFeatures) {
        bool& v = FeatureRef(p, f.key);
        bool enabled = true;
        const char* reason = nullptr;
        if (std::strcmp(f.key, "shaderFx") == 0 && !fx_ok) {
            enabled = false;
            reason = "renderer has no imb::fx backend";
        }
        if (std::strcmp(f.key, "docking") == 0 && !dock_ok) {
            enabled = false;
            reason = "requires the docking branch (Target step)";
        }
        if (!enabled && v) {
            v = false;
            changed = true;
        }
        ImGui::PushID(f.key);
        if (!enabled) ImGui::BeginDisabled();
        if (ui::ToggleButton("##toggle", &v)) changed = true;
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextUnformatted(f.label);
        ui::TextDim("%s", f.description);
        if (reason) Notice(Colors().warning, reason);
        ImGui::EndGroup();
        if (!enabled) ImGui::EndDisabled();
        ImGui::PopID();
        ImGui::Spacing();
    }
    ImGui::SeparatorText("Component libraries to import");
    if (!form.libraries_scanned) ScanLibraries(form);
    if (ImGui::SmallButton("Rescan")) ScanLibraries(form);
    ImGui::SameLine();
    ui::TextDim("packs/*/manifest.json and dist/*.imblib next to the builder");
    if (form.libraries.empty()) ui::TextDim("No libraries found.");
    int target = p.ImGuiVersionNum();
    for (LibraryEntry& lib : form.libraries) {
        ImGui::PushID(lib.name.c_str());
        bool enabled = lib.importable && (lib.min_version == 0 || target >= lib.min_version);
        bool already = std::find_if(p.libraries.begin(), p.libraries.end(), [&](const std::string& l) {
                           return l.find(lib.name) != std::string::npos;
                       }) != p.libraries.end();
        if (!enabled || already) ImGui::BeginDisabled();
        bool sel = already ? true : lib.selected;
        if (ImGui::Checkbox(lib.name.c_str(), &sel)) {
            lib.selected = sel;
            changed = true;
        }
        if (!enabled || already) ImGui::EndDisabled();
        if (already) {
            ImGui::SameLine();
            ui::Badge("imported", Colors().success);
        } else if (!lib.importable) {
            ImGui::SameLine();
            ui::Badge("source only", Colors().warning);
            ui::Tooltip(lib.reason.c_str());
        } else if (!enabled) {
            ImGui::SameLine();
            ui::Badge(("needs ImGui " + Project::VersionString(lib.min_version)).c_str(), Colors().warning);
        }
        if (!lib.description.empty()) {
            ImGui::Indent();
            ui::TextDim("%s", lib.description.c_str());
            ImGui::Unindent();
        }
        ImGui::PopID();
    }
    return changed;
}

bool DrawBindingsStep(SettingsForm& form, EditorContext& ctx) {
    Project& p = *form.project;
    bool changed = false;
    StepHeading(ctx, "Bindings", "imforge-reflect reads your compile_commands.json (or include paths) and writes the fields of the listed structs so widgets can bind to real C++ variables.");
    if (ImGui::Checkbox("Skip for now", &form.skip_bindings)) changed = true;
    ui::Tooltip("Leave bindings empty; you can configure them later in Project Settings");
    if (form.skip_bindings) ImGui::BeginDisabled();
    if (ui::BeginPropertyTable("binding_props")) {
        ui::PropertyLabel("compile_commands", "Exported by CMake with CMAKE_EXPORT_COMPILE_COMMANDS=ON");
        float bw = ImGui::CalcTextSize("Browse...").x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetNextItemWidth(-bw);
        if (ui::InputTextWithHintStd("##cc", "build/compile_commands.json", p.compile_commands)) changed = true;
        ImGui::SameLine();
        if (BrowseButton("wizard_compile_commands", p.compile_commands, false, ".json", ctx)) changed = true;
        ui::PropertyLabel("Include paths", "One per line; used when no compile_commands.json is given");
        if (ui::InputTextMultilineStd("##inc", form.include_paths_text, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 4))) {
            form.ApplyTexts();
            changed = true;
        }
        ui::PropertyLabel("Types", "Comma separated struct names, e.g. Player, Settings");
        if (ui::InputTextWithHintStd("##types", "Player, Settings", form.types_text)) {
            form.ApplyTexts();
            changed = true;
        }
        ui::PropertyLabel("types.json", "Where imforge-reflect writes the reflected fields (relative to the project)");
        if (ui::InputTextWithHintStd("##types_json", "generated/types.json", p.types_json)) changed = true;
        ui::EndPropertyTable();
    }
    if (form.skip_bindings) ImGui::EndDisabled();
    std::error_code ec;
    if (!form.skip_bindings && !p.compile_commands.empty()) {
        fs::path cc = p.compile_commands;
        if (cc.is_relative() && !p.dir.empty()) cc = fs::path(p.dir) / cc;
        if (!fs::is_regular_file(cc, ec)) Notice(Colors().warning, "compile_commands.json does not exist yet; reflection runs once it does.");
    }
    if (!form.skip_bindings && p.binding_types.empty()) ui::TextDim("No types listed: panels will only bind to their own state until you add some.");
    return changed;
}

bool ValidateTarget(const SettingsForm& form, std::string& msg) {
    const Project& p = *form.project;
    if (p.ImGuiVersionNum() < 18500) {
        msg = "ImGui version must be 1.85 or newer";
        return false;
    }
    if (p.imgui_branch != "master" && p.imgui_branch != "docking") {
        msg = "branch must be master or docking";
        return false;
    }
    return true;
}

bool ValidatePlatform(const SettingsForm& form, std::string& msg) {
    const Project& p = *form.project;
    if (!WindowRendererCompatible(p.window, p.renderer)) {
        msg = IncompatibilityReason(p.window, p.renderer);
        return false;
    }
    return true;
}

bool ValidateBindings(const SettingsForm& form, std::string& msg) {
    if (form.skip_bindings) return true;
    for (const std::string& t : form.project->binding_types)
        if (!IsIdentifier(t)) {
            msg = "\"" + t + "\" is not a valid type name";
            return false;
        }
    return true;
}

}  // namespace imforge::builder::wizard
