#include <filesystem>

#include "imgui.h"

#include "imforge/imforge_version.h"

#include "../builder_fonts.h"
#include "../builder_style.h"
#include "../editor_context.h"
#include "../os_util.h"
#include "../ui/ui_helpers.h"
#include "dialogs.h"

namespace imforge::builder {

std::string HomeDirOrCwd() {
    std::string h = HomeDir();
    std::error_code ec;
    if (!h.empty() && std::filesystem::is_directory(h, ec)) return h;
    return std::filesystem::current_path(ec).string();
}

void DrawAboutDialog(EditorContext& ctx, bool& open, const char* backend_name) {
    const char* kTitle = "About ImForge###imforge_about";
    if (open && !ImGui::IsPopupOpen(kTitle)) ImGui::OpenPopup(kTitle);
    if (!open) return;
    ImGui::SetNextWindowSize(ImVec2(460 * ctx.dpi_scale, 0), ImGuiCond_Appearing);
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(kTitle, &open, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) return;

    ui::HeadingText(ctx.Font(FontSlot::Heading), "ImForge");
    ImGui::SameLine();
    ui::TextDim("v%s", IMFORGE_VERSION);
    ImGui::TextWrapped("A visual UI builder for Dear ImGui: design natively, see edits live inside your running app, "
                       "bind widgets to real C++ variables and export plain ImGui code that round-trips.");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    if (ui::BeginPropertyTable("about_props", ImGui::GetFontSize() * 8.0f)) {
        ui::PropertyLabel("Dear ImGui");
        ImGui::Text("%s (%d)%s", IMGUI_VERSION, IMGUI_VERSION_NUM,
#ifdef IMGUI_HAS_DOCK
                    ", docking"
#else
                    ""
#endif
        );
        ui::PropertyLabel("Backend");
        ImGui::TextUnformatted(backend_name ? backend_name : "unknown");
        ui::PropertyLabel("Fonts");
        const Fonts& f = GetFonts();
        ImGui::TextWrapped("%s%s", f.dir.c_str(), f.loaded_from_files ? "" : " (fallback to ImGui default)");
        ui::PropertyLabel("Config");
        ImGui::TextWrapped("%s", ConfigDir().c_str());
        ui::PropertyLabel("Live transport");
        ImGui::TextUnformatted(ctx.live.TransportAvailable() ? "available" : "not compiled in");
        ui::PropertyLabel("Interpreter");
        ImGui::TextUnformatted(IMFORGE_HAS_INTERP ? "available" : "not compiled in");
        ui::EndPropertyTable();
    }
    ImGui::Spacing();
    ui::TextDim("MIT License. Fonts: Inter and JetBrains Mono (OFL), Font Awesome 6 Free (OFL/CC BY 4.0).");
    ImGui::Spacing();
    if (ImGui::Button("Documentation")) OpenExternal("https://github.com/fezura/ImForge/tree/main/docs");
    ImGui::SameLine();
    if (ImGui::Button("Open config folder")) OpenExternal(ConfigDir());
    ImGui::SameLine();
    float w = ImGui::CalcTextSize("Close").x + ImGui::GetStyle().FramePadding.x * 2 + 20 * ctx.dpi_scale;
    ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - w);
    if (ui::AccentButton("Close", ImVec2(w, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        open = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

}  // namespace imforge::builder
