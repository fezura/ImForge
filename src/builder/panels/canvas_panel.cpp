#include "canvas_panel.h"

#if IMFORGE_HAS_INTERP

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "imgui_internal.h"

#include "../builder_fonts.h"
#include "../builder_style.h"
#include "../project_io.h"
#include "../ui/anim_preview_driver.h"
#include "../ui/anim_preview_state.h"
#include "../ui/canvas_overlay.h"
#include "../ui/icon_table.h"
#include "../ui/project_fonts.h"
#include "../ui/tree_actions.h"
#include "../ui/ui_helpers.h"
#include "theme_state.h"

namespace imforge::builder {

namespace {

const float kDpiPresets[] = {1.0f, 1.25f, 1.5f, 2.0f};
const char* const kDpiLabels[] = {"100%", "125%", "150%", "200%"};
const float kSpeedPresets[] = {0.25f, 0.5f, 1.0f, 2.0f};
const char* const kSpeedLabels[] = {"0.25x", "0.5x", "1x", "2x"};
const float kZoomPresets[] = {0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f};

bool ToolToggle(const char* label, bool* v, const char* tip) {
    const BuilderColors& c = Colors();
    if (*v) {
        ImGui::PushStyleColor(ImGuiCol_Button, c.accent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, c.accent_hover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, c.accent_active);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, c.bg_hover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, c.bg_active);
        ImGui::PushStyleColor(ImGuiCol_Text, c.text_dim);
    }
    bool pressed = ImGui::Button(label);
    ImGui::PopStyleColor(4);
    if (pressed) *v = !*v;
    ui::Tooltip(tip);
    return pressed;
}

void ToolSeparator() {
    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();
}

const char* Ico(const char* icon, const char* fallback) {
#if IMFORGE_HAS_ICON_HEADER
    (void)fallback;
    return icon;
#else
    (void)icon;
    return fallback;
#endif
}

bool RootHasFlag(const Node& root, const char* flag) {
    const PropValue* pv = root.Prop("flags");
    if (!pv || !pv->raw.IsArray()) return false;
    for (const Json& f : pv->raw.arr)
        if (f.IsString() && f.AsString() == flag) return true;
    return false;
}

const std::string& ExampleProjectDir() {
    static const std::string dir = [] {
        std::vector<std::string> candidates;
#ifdef IMFORGE_SOURCE_DIR
        candidates.push_back(std::string(IMFORGE_SOURCE_DIR) + "/examples/projects/CheatMenu");
#endif
        candidates.push_back(ExeDir() + "/examples/projects/CheatMenu");
        candidates.push_back(ExeDir() + "/../share/imforge/examples/projects/CheatMenu");
        for (const std::string& d : candidates)
            if (IsProjectDir(d)) return d;
        return std::string();
    }();
    return dir;
}

void DrawWelcome(EditorContext& ctx) {
    const BuilderColors& c = Colors();
    const ImGuiStyle& st = ImGui::GetStyle();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float fs = ImGui::GetFontSize();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 bg_min(origin.x - st.WindowPadding.x, origin.y - st.ItemSpacing.y * 0.5f);
    const ImVec2 bg_max(origin.x + avail.x + st.WindowPadding.x, origin.y + avail.y + st.WindowPadding.y);
    dl->AddRectFilled(bg_min, bg_max, ToU32(c.bg_window));
    overlay::DottedGrid(dl, bg_min, bg_max, bg_min, 16.0f * (ctx.dpi_scale > 0 ? ctx.dpi_scale : 1.0f), ToU32(c.text, 0.13f), 1.0f);

    const std::string& example = ExampleProjectDir();
    const std::vector<std::string>& recent = ctx.prefs.recent_projects;
    const int recent_n = (int)std::min<size_t>(recent.size(), 4);
    ImFont* heading = ctx.Font(FontSlot::Heading);
    ImFont* big = IconFontLarge();
    const float radius = big ? std::floor(big->FontSize * 1.35f) : 0.0f;
    const float wrap = ImMin(avail.x - fs * 2.0f, fs * 26.0f);
    const char* hint = "Pick a template and get a live, themed Dear ImGui panel in seconds, open an existing project, "
                       "or explore the CheatMenu example.";
    const float row_h = ImGui::GetFrameHeight() + 4.0f;
    const float heading_h = heading ? heading->FontSize : fs;
    const float total = radius * 2.0f + fs * 1.2f + heading_h + fs * 0.6f + ui::WrappedTextHeight(hint, wrap) + fs * 1.4f +
                        ImGui::GetFrameHeight() + (recent_n ? fs * 2.0f + ImGui::GetTextLineHeight() + recent_n * (row_h + 2.0f) : 0.0f) +
                        fs * 1.5f + ImGui::GetTextLineHeight();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImMax(fs, (avail.y - total) * 0.42f));

    if (big) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const ImVec2 center(std::floor(p.x + avail.x * 0.5f), std::floor(p.y + radius));
        for (int i = 3; i >= 1; --i) dl->AddCircleFilled(center, radius + i * fs * 0.45f, ToU32(c.accent, 0.035f), 48);
        dl->AddCircleFilled(center, radius, ToU32(c.accent, 0.95f), 48);
        const char* icon = tree_actions::Icon("fa-wand-magic-sparkles", "+");
        const ImVec2 is = big->CalcTextSizeA(big->FontSize, FLT_MAX, 0.0f, icon);
        dl->AddText(big, big->FontSize, ImVec2(std::floor(center.x - is.x * 0.5f), std::floor(center.y - is.y * 0.5f)),
                    IM_COL32(255, 255, 255, 255), icon);
        ImGui::Dummy(ImVec2(avail.x, radius * 2.0f + fs * 1.2f - st.ItemSpacing.y));
    }
    if (heading) ImGui::PushFont(heading);
    ui::CenteredTextWrapped("Start building your UI", avail.x, ToU32(c.text));
    if (heading) ImGui::PopFont();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + fs * 0.6f - st.ItemSpacing.y);
    ui::CenteredTextWrapped(hint, wrap, ToU32(c.text_dim));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + fs * 1.4f - st.ItemSpacing.y);

    const char* new_label = tree_actions::Icon("fa-plus", nullptr) ? ICON_FA_PLUS "  New Project" : "New Project";
    const char* open_label = tree_actions::Icon("fa-folder-open", nullptr) ? ICON_FA_FOLDER_OPEN "  Open..." : "Open...";
    const char* example_label = tree_actions::Icon("fa-gamepad", nullptr) ? ICON_FA_GAMEPAD "  Open Example" : "Open Example";
    const float pad_x = fs * 1.1f;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(pad_x, st.FramePadding.y + 2.0f));
    const float bw_new = ui::ButtonWidth(new_label), bw_open = ui::ButtonWidth(open_label);
    const float bw_ex = example.empty() ? 0.0f : ui::ButtonWidth(example_label) + st.ItemSpacing.x;
    ui::CenteredRowStart(bw_new + st.ItemSpacing.x + bw_open + bw_ex);
    if (ui::AccentButton(new_label)) ctx.commands.new_project = true;
    ui::Tooltip("Create a project from a template (Ctrl+N)");
    ImGui::SameLine();
    if (ImGui::Button(open_label)) ctx.commands.open_project = true;
    ui::Tooltip("Open an existing project folder (Ctrl+O)");
    if (!example.empty()) {
        ImGui::SameLine();
        if (ImGui::Button(example_label)) {
            ctx.commands.open_project = true;
            ctx.commands.open_dir = example;
        }
        ui::Tooltip(example.c_str());
    }
    ImGui::PopStyleVar();

    if (recent_n > 0) {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + fs * 2.0f - st.ItemSpacing.y);
        const float list_w = ImMin(avail.x - fs * 2.0f, fs * 26.0f);
        ui::CenteredRowStart(list_w);
        ui::TextDim("RECENT");
        const char* folder = tree_actions::Icon("fa-folder", nullptr);
        for (int i = 0; i < recent_n; ++i) {
            const std::string& dir = recent[(size_t)i];
            size_t slash = dir.find_last_of("/\\");
            const char* name = slash == std::string::npos ? dir.c_str() : dir.c_str() + slash + 1;
            ImGui::PushID(i);
            ui::CenteredRowStart(list_w);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("##recent", ImVec2(list_w, row_h))) {
                ctx.commands.open_project = true;
                ctx.commands.open_dir = dir;
            }
            const bool hov = ImGui::IsItemHovered();
            ui::Tooltip(dir.c_str());
            dl->AddRectFilled(p, ImVec2(p.x + list_w, p.y + row_h), ToU32(hov ? c.bg_hover : c.bg_panel), st.FrameRounding + 2.0f);
            if (hov) dl->AddRect(p, ImVec2(p.x + list_w, p.y + row_h), ToU32(c.accent, 0.5f), st.FrameRounding + 2.0f);
            const float ty = std::floor(p.y + (row_h - ImGui::GetTextLineHeight()) * 0.5f);
            float x = p.x + fs * 0.75f;
            if (folder) {
                dl->AddText(ImVec2(x, ty), ToU32(c.accent), folder);
                x += fs * 1.6f;
            }
            dl->AddText(ImVec2(x, ty), ToU32(c.text), name);
            const float nx = x + ImGui::CalcTextSize(name).x + fs * 0.8f;
            const float max_w = p.x + list_w - fs * 0.75f - nx;
            char parent[512];
            std::snprintf(parent, sizeof(parent), "%.*s", (int)(slash == std::string::npos ? 0 : slash), dir.c_str());
            char buf[512];
            if (const char* shown = max_w > fs * 2.0f ? ui::EllipsizeLeft(parent, max_w, buf, sizeof(buf)) : nullptr) {
                const float pw = ImGui::CalcTextSize(shown).x;
                dl->AddText(ImVec2(p.x + list_w - fs * 0.75f - pw, ty), ToU32(c.text_disabled), shown);
            }
            ImGui::PopID();
        }
    }
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + fs * 1.5f - st.ItemSpacing.y);
    ui::CenteredTextWrapped("Ctrl+N new project   \xc2\xb7   Ctrl+O open   \xc2\xb7   Ctrl+E export code", avail.x,
                            ToU32(c.text_disabled));
}

ImVec2 RootInitialSize(const Node& root) {
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
    if (size.x < 32) size.x = 32;
    if (size.y < 32) size.y = 32;
    return size;
}

}  // namespace

const char* CanvasPanel::Icon() const {
#if IMFORGE_HAS_ICON_HEADER
    return ICON_FA_VECTOR_SQUARE;
#else
    return nullptr;
#endif
}

void CanvasPanel::OnProjectChanged(EditorContext&) {
    mock_params_.clear();
    moving_ = false;
    move_origins_.clear();
    context_id_.clear();
}

float CanvasPanel::DpiMultiplier() const {
    int i = dpi_index_ < 0 ? 0 : (dpi_index_ > 3 ? 3 : dpi_index_);
    return kDpiPresets[i];
}

void CanvasPanel::ResetFrameData() {
    items_.clear();
    item_index_.clear();
    parents_.clear();
    depths_.clear();
    hovered_id_.clear();
    canvas_hovered_ = false;
    click_on_item_ = false;
    deferred_instance_click_ = false;
    tree_changed_ = false;
}

void CanvasPanel::Draw(EditorContext& ctx) {
    ResetFrameData();
    const Component* comp = ctx.ActiveComponent();
    DrawToolbar(ctx, comp);
    ImGui::Separator();

    if (!ctx.has_project) {
        DrawWelcome(ctx);
        return;
    }
    std::string key;
    const Panel* panel = ResolvePanel(ctx, key);
    Node* root = ctx.ActiveRoot();
    if (!panel || !root) {
        ui::EmptyState(tree_actions::Icon("fa-window-maximize", nullptr), "No document open",
                       "Pick a panel or component in the document menu above, or create one in the Panels tab.");
        return;
    }
    UpdateAnimationClock();
    PanelInstance& inst = ctx.instances[key];
    IndexTree(*root, nullptr, 0);
    DrawViewport(ctx, *panel, *root, inst);
    if (!tree_changed_) {
        DrawContextMenu(ctx, *root);
        HandleKeyboard(ctx, *root);
    }
    if (comp) DrawMockParamsPopup(ctx, *comp);
    ForwardDiagnostics(ctx);
}

void CanvasPanel::DrawToolbar(EditorContext& ctx, const Component* comp) {
    const float item_h = ImGui::GetFrameHeight();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, ImGui::GetStyle().ItemSpacing.y));

    std::string current = ctx.active_kind == DocKind::Panel        ? ctx.active_panel
                          : ctx.active_kind == DocKind::Component ? "Component: " + ctx.active_panel
                                                                  : std::string("(none)");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 11.0f);
    if (ImGui::BeginCombo("##canvas_doc", current.c_str())) {
        for (const Panel& p : ctx.project.panels) {
            bool sel = ctx.active_kind == DocKind::Panel && ctx.active_panel == p.name;
            if (ImGui::Selectable(p.name.c_str(), sel)) ctx.SetActivePanel(p.name);
        }
        if (!ctx.project.components.empty()) {
            ImGui::Separator();
            for (const Component& c : ctx.project.components) {
                bool sel = ctx.active_kind == DocKind::Component && ctx.active_panel == c.name;
                std::string label = "Component: " + c.name;
                if (ImGui::Selectable(label.c_str(), sel)) ctx.SetActiveComponent(c.name);
            }
        }
        ImGui::EndCombo();
    }
    ui::Tooltip("Document shown on the canvas");
    if (comp) {
        ImGui::SameLine();
        if (ImGui::Button("Params")) ImGui::OpenPopup("##canvas_mock_params");
        ui::Tooltip("Mock parameter values used to preview this component");
    }

    ToolSeparator();
    if (ui::IconButton(Ico(ICON_FA_MAGNIFYING_GLASS_MINUS, "-"), "Zoom out")) zoom_ = ImMax(kMinZoom, zoom_ / 1.25f);
    ImGui::SameLine();
    char zl[16];
    std::snprintf(zl, sizeof(zl), "%d%%", (int)std::lround(zoom_ * 100.0f));
    if (ImGui::Button(zl, ImVec2(ImGui::GetFontSize() * 3.6f, 0))) ImGui::OpenPopup("##canvas_zoom");
    ui::Tooltip("Zoom (Ctrl+wheel over the canvas)");
    if (ImGui::BeginPopup("##canvas_zoom")) {
        for (float z : kZoomPresets) {
            char l[16];
            std::snprintf(l, sizeof(l), "%d%%", (int)std::lround(z * 100.0f));
            if (ImGui::Selectable(l, std::fabs(z - zoom_) < 0.01f)) zoom_ = z;
        }
        ImGui::Separator();
        if (ImGui::Selectable("Fit")) fit_requested_ = true;
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ui::IconButton(Ico(ICON_FA_MAGNIFYING_GLASS_PLUS, "+"), "Zoom in")) zoom_ = ImMin(kMaxZoom, zoom_ * 1.25f);

    ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x * 2.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", Ico(ICON_FA_DISPLAY, "DPI"));
    ui::Tooltip("DPI preview");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 4.6f);
    ImGui::Combo("##canvas_dpi", &dpi_index_, kDpiLabels, 4);
    ui::Tooltip("DPI preview: multiplies the panel's scale factor");

    ToolSeparator();
    ToolToggle(Ico(ICON_FA_BORDER_ALL, "Grid"), &show_grid_, "Show 8px grid");
    ImGui::SameLine();
    ToolToggle(Ico(ICON_FA_EXPAND, "Bounds"), &show_bounds_, "Show item bounds");
    ImGui::SameLine();
    ToolToggle(Ico(ICON_FA_HAND_POINTER, "Interactive"), &interactive_,
               "Interactive: clicks reach the widgets instead of selecting");

    ToolSeparator();
    if (ui::IconButton(anim_playing_ ? Ico(ICON_FA_PAUSE, "Pause") : Ico(ICON_FA_PLAY, "Play"),
                       anim_playing_ ? "Pause animations" : "Play animations"))
        anim_playing_ = !anim_playing_;
    ImGui::SameLine();
    if (ui::IconButton(Ico(ICON_FA_ROTATE_RIGHT, "Replay"), "Replay enter animations")) {
        imb::Reset();
        anim_playing_ = true;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 4.2f);
    ImGui::Combo("##canvas_speed", &anim_speed_index_, kSpeedLabels, 4);
    ui::Tooltip("Animation speed");

    ToolSeparator();
    if (ui::IconButton(Ico(ICON_FA_ERASER "  Reset", "Reset state"), nullptr)) {
        std::string key;
        ResolvePanel(ctx, key);
        if (!key.empty()) ctx.instances.erase(key);
        ctx.interp.node_rt.clear();
    }
    ui::Tooltip("Clear panel state, mock bindings and open popups");

    float dot_w = ImGui::GetFontSize() * 1.2f;
    float right = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x;
    ImGui::SameLine(ImMax(ImGui::GetCursorPosX(), right - dot_w));
    bool connected = ctx.live.IsConnected();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(dot_w, item_h));
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + dot_w * 0.5f, p.y + item_h * 0.5f), 4.0f,
                                                ToU32(connected ? Colors().success : Colors().text_disabled));
    if (ImGui::IsItemHovered()) {
        if (connected) ImGui::SetTooltip("Live: %s connected", ctx.live.Client().app.c_str());
        else ImGui::SetTooltip("Live: %s", ctx.live.IsRunning() ? "waiting for a client" : "server stopped");
    }
    ImGui::PopStyleVar();
}

void CanvasPanel::UpdateAnimationClock() {
    AnimPreviewState& ap = GetAnimPreviewState();
    if (ap.Pending(last_anim_gen_)) {
        last_anim_gen_ = ap.generation;
        imb::Reset();
        anim_playing_ = true;
    }
    float speed = kSpeedPresets[anim_speed_index_ < 0 ? 0 : (anim_speed_index_ > 3 ? 3 : anim_speed_index_)];
    float dt = AnimPreviewDeltaTime(anim_playing_ ? ImGui::GetIO().DeltaTime * speed : 0.0f);
    imb::NewFrame(dt);
}

void CanvasPanel::HandleZoom(const ImVec2& region_min, const ImVec2& region_max) {
    ImGuiIO& io = ImGui::GetIO();
    if (!io.KeyCtrl || io.MouseWheel == 0.0f) return;
    if (!ImGui::IsMouseHoveringRect(region_min, region_max) || !ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
        return;
    float old_zoom = zoom_;
    float factor = io.MouseWheel > 0 ? 1.1f : 1.0f / 1.1f;
    zoom_ = ImClamp(zoom_ * factor, kMinZoom, kMaxZoom);
    if (zoom_ == old_zoom) return;
    float k = zoom_ / old_zoom;
    ImVec2 rel(io.MousePos.x - region_min.x, io.MousePos.y - region_min.y);
    ImGui::SetScrollX((ImGui::GetScrollX() + rel.x) * k - rel.x);
    ImGui::SetScrollY((ImGui::GetScrollY() + rel.y) * k - rel.y);
}

void CanvasPanel::DrawFrameChrome(ImDrawList* dl, const ImVec2& frame_min, const ImVec2& frame_max, bool title_bar) {
    const ImGuiStyle& st = ImGui::GetStyle();
    float rounding = st.WindowRounding;
    imb::Shadow(dl, frame_min, frame_max, rounding, IM_COL32(0, 0, 0, 90), 18.0f * scale_, ImVec2(0, 4.0f * scale_));
    dl->AddRectFilled(frame_min, frame_max, ImGui::GetColorU32(ImGuiCol_WindowBg), rounding);
    if (title_bar) {
        float h = ImGui::GetFrameHeight();
        ImVec2 tmax(frame_max.x, frame_min.y + h);
        dl->AddRectFilled(frame_min, tmax, ImGui::GetColorU32(ImGuiCol_TitleBgActive), rounding,
                          ImDrawFlags_RoundCornersTop);
    }
    dl->AddRect(frame_min, frame_max, ImGui::GetColorU32(ImGuiCol_Border), rounding, 0,
                ImMax(1.0f, st.WindowBorderSize));
}

void CanvasPanel::DrawViewport(EditorContext& ctx, const Panel& panel, Node& root, PanelInstance& inst) {
    const float S = zoom_ * DpiMultiplier() * (ctx.dpi_scale > 0 ? ctx.dpi_scale : 1.0f);
    scale_ = S;
    ImFont* base = BaseFont(ctx);
    ImFont* font = PickFont(base, base ? base->FontSize * zoom_ * DpiMultiplier() : 0.0f);
    if (font) {
#if IMGUI_VERSION_NUM >= 19200
        ImGui::PushFont(font, base->FontSize * zoom_ * DpiMultiplier());
#else
        ImGui::PushFont(font);
#endif
    }
    StyleScope theme_scope = PushCanvasStyle(ctx, S);

    const bool is_window = root.type == "Window";
    const bool title_bar = is_window && !RootHasFlag(root, "NoTitleBar");
    const bool menu_bar = is_window && root.Prop("menuBar") && root.Prop("menuBar")->raw.AsBool(false);
    ImVec2 frame_size = RootInitialSize(root);
    frame_size = ImVec2(std::floor(frame_size.x * S), std::floor(frame_size.y * S));
    title_bar_h_ = title_bar ? ImGui::GetFrameHeight() : 0.0f;
    title_fg_ = ImGui::GetColorU32(ImGuiCol_Text);
    title_btn_ = ImGui::GetColorU32(ImGuiCol_ButtonHovered);

    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 16) avail.x = 16;
    if (avail.y < 16) avail.y = 16;
    viewport_size_ = avail;
    if (fit_requested_) {
        fit_requested_ = false;
        ImVec2 base_size = RootInitialSize(root);
        float k = DpiMultiplier() * (ctx.dpi_scale > 0 ? ctx.dpi_scale : 1.0f);
        float fit = ImMin((avail.x - kMargin * 2) / (base_size.x * k), (avail.y - kMargin * 2) / (base_size.y * k));
        zoom_ = ImClamp(fit, kMinZoom, kMaxZoom);
    }

    ImGuiWindowFlags vflags = ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoMove;
    if (ImGui::GetIO().KeyCtrl) vflags |= ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Colors().bg_window);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
#if IMGUI_VERSION_NUM >= 19000
    bool vis = ImGui::BeginChild("##canvas_viewport", avail, ImGuiChildFlags_None, vflags);
#else
    bool vis = ImGui::BeginChild("##canvas_viewport", avail, false, vflags);
#endif
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
    if (vis) {
        const ImVec2 region_min = ImGui::GetWindowPos();
        const ImVec2 region_max(region_min.x + ImGui::GetWindowSize().x, region_min.y + ImGui::GetWindowSize().y);
        clip_min_ = region_min;
        clip_max_ = region_max;
        HandleZoom(region_min, region_max);

        const float margin = kMargin * (ctx.dpi_scale > 0 ? ctx.dpi_scale : 1.0f);
        ImVec2 content(frame_size.x + margin * 2, frame_size.y + margin * 2);
        ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::Dummy(content);
        ImVec2 offset(ImMax(margin, (avail.x - frame_size.x) * 0.5f), ImMax(margin, (avail.y - frame_size.y) * 0.5f));
        frame_min_ = ImVec2(std::floor(origin.x + offset.x), std::floor(origin.y + offset.y));
        frame_max_ = ImVec2(frame_min_.x + frame_size.x, frame_min_.y + frame_size.y);

        ImDrawList* bg = ImGui::GetWindowDrawList();
        if (show_grid_)
            overlay::DottedGrid(bg, region_min, region_max, frame_min_, kGridStep * S, ToU32(Colors().text, 0.12f),
                                ImMax(1.0f, std::floor(S)));
        DrawFrameChrome(bg, frame_min_, frame_max_, title_bar);

        ImGui::SetCursorScreenPos(frame_min_);
        ImGuiWindowFlags cflags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
        if (menu_bar) cflags |= ImGuiWindowFlags_MenuBar;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, ImGui::GetStyle().WindowRounding);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
#if IMGUI_VERSION_NUM >= 19000
        bool shown = ImGui::BeginChild("canvas", frame_size, ImGuiChildFlags_None, cflags);
#else
        bool shown = ImGui::BeginChild("canvas", frame_size, false, cflags);
#endif
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
        if (shown) {
            canvas_hovered_ = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows |
                                                     ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
            canvas_pad_ = ImGui::GetStyle().WindowPadding;
            Interpreter& in = ctx.interp;
            in.hooks.on_item = [this](const Node& n, const ImVec2& mn, const ImVec2& mx, bool hovered) {
                auto d = depths_.find(n.id);
                if (d == depths_.end()) return;
                CanvasItem ci;
                ci.node = &n;
                ci.id = n.id;
                ci.min = ci.emin = mn;
                ci.max = ci.emax = mx;
                ci.hovered = hovered && canvas_hovered_;
                ci.depth = d->second;
                auto it = item_index_.find(n.id);
                if (it != item_index_.end()) items_[it->second] = ci;
                else {
                    item_index_[n.id] = items_.size();
                    items_.push_back(ci);
                }
            };
            if (!interactive_) {
                in.hooks.on_click = [this, &ctx](const Node& n) {
                    if (!canvas_hovered_ || ImGui::GetIO().MouseDownDuration[0] > 0.0f) return;
                    click_on_item_ = true;
                    if (!depths_.count(n.id)) {
                        deferred_instance_click_ = true;
                        return;
                    }
                    if (ImGui::GetIO().KeyCtrl) {
                        if (ctx.IsSelected(n.id)) ctx.Deselect(n.id);
                        else ctx.Select(n.id, true);
                    } else {
                        ctx.Select(n.id);
                    }
                };
            } else {
                in.hooks.on_click = nullptr;
            }
            in.hooks.font = [](const std::string& name) -> ImFont* { return FindProjectFont(name); };
            in.hooks.icon = [](const std::string& name) -> const char* { return IconUtf8(name.c_str()); };

            InterpOptions opts;
            opts.design_mode = true;
            opts.dpi_scale = S;
            opts.run_handlers = false;
            opts.animations = AnimPreviewAnimationsEnabled();
            opts.fx = true;
            opts.selected_id = nullptr;
            static const std::vector<BindingRoot> kNoRoots;
            in.DrawPanel(panel, inst, kNoRoots, opts);
            in.hooks.on_item = nullptr;
            in.hooks.on_click = nullptr;

            if (!item_index_.count(root.id) && depths_.count(root.id)) {
                CanvasItem ci;
                ci.node = &root;
                ci.id = root.id;
                ci.min = ci.emin = frame_min_;
                ci.max = ci.emax = frame_max_;
                ci.hovered = canvas_hovered_ && ImGui::IsMouseHoveringRect(frame_min_, frame_max_, false);
                ci.depth = 0;
                item_index_[root.id] = items_.size();
                items_.push_back(ci);
            }
            FinalizeItems(root);
            hovered_id_ = ResolveHover();

            if (!interactive_) {
                if (deferred_instance_click_) {
                    std::string inst_id = EnclosingInstance(ImGui::GetIO().MousePos);
                    if (!inst_id.empty()) {
                        if (ImGui::GetIO().KeyCtrl) {
                            if (ctx.IsSelected(inst_id)) ctx.Deselect(inst_id);
                            else ctx.Select(inst_id, true);
                        } else {
                            ctx.Select(inst_id);
                        }
                    }
                }
                DrawSelectionHandles(ctx, root);
                if (canvas_hovered_ && ImGui::IsMouseClicked(0) && !click_on_item_ && hovered_id_.empty() &&
                    !ImGui::IsAnyItemHovered() && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
                    ctx.ClearSelection();
                if (canvas_hovered_ && ImGui::IsMouseClicked(1) && !hovered_id_.empty() && !ImGui::IsAnyItemHovered()) {
                    if (!ctx.IsSelected(hovered_id_)) ctx.Select(hovered_id_);
                    context_id_ = hovered_id_;
                    open_context_ = true;
                }
                if (!hovered_id_.empty()) ctx.hovered_id = hovered_id_;
            }
        }
        ImGui::EndChild();

        DrawOverlays(ctx, ImGui::GetForegroundDrawList());
        HandleDragDrop(ctx, root, region_min, region_max);
    }
    ImGui::EndChild();

    PopStyleScope(theme_scope);
    if (font) ImGui::PopFont();
}

void CanvasPanel::HandleKeyboard(EditorContext& ctx, Node& root) {
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) return;
    if (ImGui::GetIO().WantTextInput) return;
    auto chord = [](ImGuiKeyChord c, bool repeat = false) {
#if IMGUI_VERSION_NUM >= 19070
        ImGuiInputFlags f = ImGuiInputFlags_RouteFocused;
        if (repeat) f |= ImGuiInputFlags_Repeat;
        return ImGui::Shortcut(c, f);
#else
        return ImGui::IsKeyChordPressed(c, repeat ? ImGuiInputFlags_Repeat : 0);
#endif
    };
    const bool has_sel = !ctx.selection.empty();
    if (has_sel && chord(ImGuiKey_Escape)) ctx.ClearSelection();
    if (has_sel && chord(ImGuiKey_Delete)) {
        ctx.DeleteSelection();
        return;
    }
    if (has_sel && chord(ImGuiMod_Ctrl | ImGuiKey_D)) {
        ctx.DuplicateSelection();
        return;
    }
    if (chord(ImGuiMod_Ctrl | ImGuiKey_Z)) {
        ctx.Undo();
        return;
    }
    if (chord(ImGuiMod_Ctrl | ImGuiKey_Y) || chord(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z)) {
        ctx.Redo();
        return;
    }
    if (!has_sel || interactive_) return;
    const bool big = ImGui::GetIO().KeyShift;
    if (chord(ImGuiKey_LeftArrow, true)) NudgeOrStep(ctx, root, -1, 0, big);
    else if (chord(ImGuiKey_RightArrow, true)) NudgeOrStep(ctx, root, 1, 0, big);
    else if (chord(ImGuiKey_UpArrow, true)) NudgeOrStep(ctx, root, 0, -1, big);
    else if (chord(ImGuiKey_DownArrow, true)) NudgeOrStep(ctx, root, 0, 1, big);
}

void CanvasPanel::ForwardDiagnostics(EditorContext& ctx) {
    for (const Diagnostic& d : ctx.interp.last_diagnostics) {
        bool dup = false;
        for (const Diagnostic& e : ctx.diagnostics) {
            if (e.node_id == d.node_id && e.message == d.message && e.prop == d.prop && e.panel == d.panel) {
                dup = true;
                break;
            }
        }
        if (!dup) ctx.diagnostics.push_back(d);
    }
}

}  // namespace imforge::builder

IMFORGE_REGISTER_PANEL(imforge::builder::CanvasPanel, 30);

#endif
