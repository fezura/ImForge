#include <cmath>
#include <cstdio>
#include <cstring>

#include "imgui.h"
#include "imgui_internal.h"

#include "app.h"
#include "builder_style.h"
#include "ui/tree_actions.h"
#include "ui/ui_helpers.h"

namespace imforge::builder {

namespace {

constexpr int kLayoutVersion = 3;

void VSep() {
    ImGui::SameLine();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float y = p.y + ImGui::GetStyle().FramePadding.y, h = ImGui::GetTextLineHeight();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, y + h * 0.1f), ImVec2(p.x, y + h * 0.9f), ToU32(Colors().separator), 1.0f);
    ImGui::Dummy(ImVec2(1.0f, h));
    ImGui::SameLine();
}

void BarDot(const ImVec4& col, float radius) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    float cy = p.y + ImGui::GetStyle().FramePadding.y + ImGui::GetTextLineHeight() * 0.5f;
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + radius, cy), radius, ToU32(col), 12);
    ImGui::Dummy(ImVec2(radius * 2.0f, ImGui::GetTextLineHeight()));
}

bool StatusPill(const char* id, const char* text, const ImVec4& col, bool pulse) {
    const BuilderColors& c = Colors();
    const float fs = ImGui::GetFontSize();
    const float pad_x = std::floor(fs * 0.55f), dot = std::floor(fs * 0.42f), gap = std::floor(fs * 0.4f);
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const ImVec2 cur = ImGui::GetCursorScreenPos();
    const ImVec2 p(cur.x, cur.y + ImGui::GetStyle().FramePadding.y);
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(pad_x * 2.0f + dot + gap + ts.x, ts.y + ImGui::GetStyle().FramePadding.y * 2.0f));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a(p.x, p.y - 3.0f), b(p.x + pad_x * 2.0f + dot + gap + ts.x, p.y + ts.y + 3.0f);
    const float r = (b.y - a.y) * 0.5f;
    dl->AddRectFilled(a, b, ToU32(col, hovered ? 0.26f : 0.13f), r);
    if (hovered) dl->AddRect(a, b, ToU32(col, 0.55f), r);
    const ImVec2 dc(std::floor(p.x + pad_x + dot * 0.5f) + 0.5f, std::floor(p.y + ts.y * 0.5f) + 0.5f);
    if (pulse) {
        float t = (float)std::fmod(ImGui::GetTime(), 1.8) / 1.8f;
        dl->AddCircle(dc, dot * 0.5f + t * dot * 0.9f, ToU32(col, (1.0f - t) * 0.7f), 16, 1.5f);
    }
    dl->AddCircleFilled(dc, dot * 0.5f, ToU32(col), 12);
    dl->AddText(ImVec2(p.x + pad_x + dot + gap, p.y), ToU32(hovered ? c.text : WithAlpha(c.text, 0.85f)), text);
    return clicked;
}

short TabOrder(const char* name) {
    static const char* const kOrder[] = {"Hierarchy", "Panels",           "Palette", "Components",  "Canvas",        "Library",       "CodeView",
                                         "Panel Settings", "Project Settings", "Fonts",   "Properties",  "Theme Editor", "Style Classes",
                                         "Log",       "Diagnostics",      "Conflicts", "Live",       "Anim Timeline"};
    for (short i = 0; i < (short)(sizeof(kOrder) / sizeof(kOrder[0])); ++i)
        if (std::strcmp(kOrder[i], name) == 0) return i;
    return 100;
}

float StatusPillWidth(const char* text) {
    const float fs = ImGui::GetFontSize();
    return std::floor(fs * 0.55f) * 2.0f + std::floor(fs * 0.42f) + std::floor(fs * 0.4f) + ImGui::CalcTextSize(text).x;
}

}  // namespace

const char* App::DockAreaFor(const EditorPanel& panel) const {
    struct Entry {
        const char* name;
        const char* area;
    };
    static const Entry kMap[] = {
        {"Hierarchy", "left"},        {"Palette", "left-bottom"},   {"Components", "left-bottom"},
        {"Panels", "left"},           {"Panel Settings", "center"},
        {"Canvas", "center"},         {"CodeView", "center"},       {"Project Settings", "center"},
        {"Properties", "right"},      {"Theme Editor", "right"},    {"Style Classes", "right"},
        {"Fonts", "center"},          {"Anim Timeline", "bottom"},  {"Log", "bottom"},
        {"Diagnostics", "bottom"},    {"Conflicts", "bottom"},      {"Live", "bottom"},
        {"Wizard", "float"},          {"ExportDialogHost", "none"},
    };
    for (const Entry& e : kMap)
        if (std::strcmp(e.name, panel.Name()) == 0) return e.area;
    if (const char* d = panel.DefaultDock()) return d;
    return "center";
}

static ImGuiID WindowTabId(const char* name) { return ImHashStr("#TAB", 0, ImHashStr(name)); }

void App::BuildDefaultLayout(unsigned dockspace_id, const ImVec2& size) {
#ifdef IMGUI_HAS_DOCK
    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id, size);
    const float left_frac = 0.18f, right_frac = 0.25f;
    ImGuiID center = dockspace_id;
    ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, left_frac, nullptr, &center);
    ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, right_frac / (1.0f - left_frac), nullptr, &center);
    ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.24f, nullptr, &center);
    ImGuiID left_bottom = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.48f, nullptr, &left);
    if (ImGuiDockNode* n = ImGui::DockBuilderGetNode(center)) n->LocalFlags |= ImGuiDockNodeFlags_CentralNode;
    for (ImGuiID id : {left, left_bottom, center, right, bottom})
        if (ImGuiDockNode* n = ImGui::DockBuilderGetNode(id)) n->LocalFlags |= ImGuiDockNodeFlags_NoCloseButton;
    for (auto& p : ctx_.panels) {
        const char* area = DockAreaFor(*p);
        ImGuiID node = 0;
        if (std::strcmp(area, "left") == 0) node = left;
        else if (std::strcmp(area, "left-bottom") == 0) node = left_bottom;
        else if (std::strcmp(area, "right") == 0) node = right;
        else if (std::strcmp(area, "bottom") == 0) node = bottom;
        else if (std::strcmp(area, "center") == 0) node = center;
        if (!node) continue;
        ImGui::DockBuilderDockWindow(p->Name(), node);
        const short order = TabOrder(p->Name());
        if (ImGuiWindow* w = ImGui::FindWindowByName(p->Name())) w->DockOrder = order;
        else if (ImGuiWindowSettings* ws = ImGui::FindWindowSettingsByID(ImHashStr(p->Name()))) ws->DockOrder = order;
    }
    struct Preferred {
        ImGuiID node;
        const char* window;
    };
    const Preferred preferred[] = {{left, "Hierarchy"}, {left_bottom, "Palette"}, {center, "Canvas"}, {right, "Properties"}, {bottom, "Log"}};
    for (const Preferred& pf : preferred)
        if (ImGuiDockNode* n = ImGui::DockBuilderGetNode(pf.node))
            if (ctx_.FindPanel(pf.window)) n->SelectedTabId = WindowTabId(pf.window);
    ImGui::DockBuilderFinish(dockspace_id);
    layout_focus_frames_ = 2;
    ctx_.Log(LogLevel::Debug, "dock layout rebuilt");
#else
    (void)dockspace_id;
    (void)size;
#endif
}

void App::DrawDockHost() {
#ifdef IMGUI_HAS_DOCK
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                             ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##ImForgeDockHost", nullptr, flags);
    ImGui::PopStyleVar(3);
    ImGuiID dockspace_id = ImGui::GetID("ImForgeDockSpace");
    static bool version_checked = false;
    if (!version_checked) {
        version_checked = true;
        const Json& v = ctx_.prefs.extra["layoutVersion"];
        if (!v.IsNumber() || v.AsInt() != kLayoutVersion) {
            layout_dirty_ = true;
            ctx_.prefs.extra.Set("layoutVersion", Json(kLayoutVersion));
        }
    }
    if (layout_dirty_ || ImGui::DockBuilderGetNode(dockspace_id) == nullptr) {
        BuildDefaultLayout(dockspace_id, vp->WorkSize);
        layout_dirty_ = false;
    }
    ImGui::DockSpace(dockspace_id, ImVec2(0, 0), ImGuiDockNodeFlags_None);
    ImGui::End();
#else
    if (layout_dirty_) {
        ImGuiViewport* vp = ImGui::GetMainViewport();
        float x = vp->WorkPos.x + 10, y = vp->WorkPos.y + 10;
        for (auto& p : ctx_.panels) {
            ImGui::SetWindowPos(p->Name(), ImVec2(x, y), ImGuiCond_Always);
            ImGui::SetWindowSize(p->Name(), ImVec2(420, 320), ImGuiCond_Always);
            x += 40;
            y += 40;
        }
        layout_dirty_ = false;
    }
#endif
}

void App::DrawPanels() {
    auto draw = [&](EditorPanel& p) {
        bool visible = p.BeginWindow(ctx_);
        if (visible) p.Draw(ctx_);
        p.EndWindow(ctx_);
    };
    for (auto& p : ctx_.panels)
        if (p->open && std::strcmp(DockAreaFor(*p), "float") != 0) draw(*p);
    for (auto& p : ctx_.panels)
        if (p->open && std::strcmp(DockAreaFor(*p), "float") == 0) draw(*p);
    if (layout_focus_frames_ > 0) {
        const char* name = layout_focus_frames_ == 2 ? "Log" : "Canvas";
        if (ctx_.FindPanel(name) && !ImGui::GetTopMostPopupModal()) ImGui::SetWindowFocus(name);
        --layout_focus_frames_;
    }
}

void App::DrawStatusBar() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const float height = std::floor(ImGui::GetFrameHeight() * 1.15f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, std::floor((height - ImGui::GetFontSize()) * 0.5f)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, Colors().bg_window);
    if (ImGui::BeginViewportSideBar("##ImForgeStatusBar", vp, ImGuiDir_Down, height, flags)) {
        {
            ImVec2 wp = ImGui::GetWindowPos();
            ImGui::GetWindowDrawList()->AddLine(wp, ImVec2(wp.x + ImGui::GetWindowWidth(), wp.y), ToU32(Colors().separator), 1.0f);
        }
        if (ImGui::BeginMenuBar()) {
            const BuilderColors& c = Colors();
            const ImGuiStyle& st = ImGui::GetStyle();
            const LiveServer& ls = ctx_.live;

            int errors = 0, warnings = 0;
            for (const Diagnostic& d : ctx_.diagnostics) {
                if (d.level == Diagnostic::Level::Error) ++errors;
                else if (d.level == Diagnostic::Level::Warning) ++warnings;
            }
            char diag[64];
            if (errors || warnings)
                std::snprintf(diag, sizeof(diag), "%d error%s, %d warning%s", errors, errors == 1 ? "" : "s", warnings,
                              warnings == 1 ? "" : "s");
            else
                std::snprintf(diag, sizeof(diag), "No problems");
            char live[160];
            const bool connected = ls.IsConnected();
            if (connected) {
                const LiveClientInfo& ci = ls.Client();
                std::snprintf(live, sizeof(live), "Live: %s%s", ci.app.empty() ? "connected" : ci.app.c_str(),
                              ctx_.PickerMode() ? " (picker)" : "");
            } else if (ls.IsRunning()) {
                const std::string& url = ls.Url();
                size_t colon = url.rfind(':');
                std::snprintf(live, sizeof(live), "Live: listening %s",
                              colon != std::string::npos && colon + 1 < url.size() ? url.c_str() + colon : url.c_str());
            } else {
                std::snprintf(live, sizeof(live), "Live: %s", ls.StatusText());
            }
            char fps[32];
            std::snprintf(fps, sizeof(fps), "%.0f fps", ImGui::GetIO().Framerate);

            const float sp = st.ItemSpacing.x;
            const float fps_w = ImGui::CalcTextSize("0000 fps").x;
            const float right_w = StatusPillWidth("Library") + sp + StatusPillWidth(diag) + sp + StatusPillWidth(live) + sp * 1.5f + fps_w + st.WindowPadding.x + sp;
            const float right_x = ImGui::GetWindowWidth() - right_w;
            const float limit_x = ImGui::GetWindowPos().x + right_x - sp * 2.0f;

            if (ctx_.has_project) {
                if (const char* ico = tree_actions::Icon("fa-folder-open", nullptr)) {
                    ImGui::PushStyleColor(ImGuiCol_Text, c.accent);
                    ImGui::TextUnformatted(ico);
                    ImGui::PopStyleColor();
                    ImGui::SameLine(0, sp * 0.75f);
                }
                ImGui::TextUnformatted(ctx_.project.name.c_str());
                if (ctx_.dirty) {
                    ImGui::SameLine(0, sp * 0.6f);
                    BarDot(c.warning, 3.5f);
                    ui::Tooltip("Unsaved changes (Ctrl+S)");
                }
                char doc[160] = "";
                if (ctx_.active_kind != DocKind::None) {
                    const char* ico = tree_actions::Icon(ctx_.active_kind == DocKind::Panel ? "fa-window-maximize" : "fa-cube", nullptr);
                    std::snprintf(doc, sizeof(doc), "%s%s%s", ico ? ico : (ctx_.active_kind == DocKind::Panel ? "panel " : "component "),
                                  ico ? "  " : "", ctx_.active_panel.c_str());
                }
                char sel[32] = "";
                if (!ctx_.selection.empty()) std::snprintf(sel, sizeof(sel), "%d selected", (int)ctx_.selection.size());
                float rest = (doc[0] ? ImGui::CalcTextSize(doc).x + sp * 3.0f : 0.0f) + (sel[0] ? ImGui::CalcTextSize(sel).x + sp : 0.0f);
                ImGui::SameLine();
                char path_buf[512];
                float path_w = limit_x - ImGui::GetCursorScreenPos().x - rest - sp;
                if (const char* path = path_w > 40.0f ? ui::EllipsizeLeft(ctx_.project.dir.c_str(), path_w, path_buf, sizeof(path_buf)) : nullptr) {
                    ui::TextDim("%s", path);
                    if (path == path_buf) ui::Tooltip(ctx_.project.dir.c_str());
                } else {
                    ImGui::Dummy(ImVec2(0, 0));
                }
                if (doc[0]) {
                    VSep();
                    ImGui::TextUnformatted(doc);
                }
                if (sel[0]) {
                    ImGui::SameLine();
                    ui::TextDim("%s", sel);
                }
            } else {
                ui::TextDim("No project open");
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, c.text_disabled);
                ImGui::TextUnformatted("Ctrl+N new project, Ctrl+O open");
                ImGui::PopStyleColor();
            }
            if (!ctx_.status_message.empty() && ImGui::GetCursorScreenPos().x < limit_x) {
                VSep();
                float max_w = limit_x - ImGui::GetCursorScreenPos().x;
                ImVec2 p = ImGui::GetCursorScreenPos();
                ImVec4 clip(p.x, p.y - 4.0f, p.x + ImMax(0.0f, max_w), p.y + ImGui::GetTextLineHeight() + 4.0f);
                ImGui::GetWindowDrawList()->AddText(nullptr, 0.0f, p, ToU32(c.info), ctx_.status_message.c_str(), nullptr, 0.0f, &clip);
                ImGui::Dummy(ImVec2(ImMax(1.0f, ImMin(max_w, ImGui::CalcTextSize(ctx_.status_message.c_str()).x)), ImGui::GetTextLineHeight()));
                ui::Tooltip(ctx_.status_message.c_str());
            }

            ImGui::SameLine(right_x);
            if (StatusPill("##status_lib", "Library", c.accent, false)) ctx_.OpenPanel("Library");
            ui::Tooltip("Animations, designs, widgets and templates (Ctrl+L)");
            ImGui::SameLine(0, sp);
            const ImVec4 diag_col = errors ? c.error : warnings ? c.warning : c.success;
            if (StatusPill("##status_diag", diag, diag_col, false)) ctx_.OpenPanel("Diagnostics");
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("%d errors, %d warnings, %d diagnostics in total\nClick to open Diagnostics", errors, warnings,
                                  (int)ctx_.diagnostics.size());
            ImGui::SameLine(0, sp);
            const ImVec4 live_col = connected ? c.success : ls.IsRunning() ? c.info : c.text_disabled;
            if (StatusPill("##status_live", live, live_col, connected)) ctx_.OpenPanel("Live");
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                if (connected) {
                    const LiveClientInfo& ci = ls.Client();
                    ImGui::SetTooltip("Connected to %s (pid %d, %s %s)\n%s\nClick to open the Live panel", ci.app.empty() ? "app" : ci.app.c_str(),
                                      ci.pid, ci.backend.c_str(), ci.platform.c_str(), ls.Url().c_str());
                } else if (ls.IsRunning()) {
                    ImGui::SetTooltip("Waiting for an app on %s\nClick to open the Live panel", ls.Url().c_str());
                } else {
                    ImGui::SetTooltip("Live server: %s\nClick to open the Live panel", ls.StatusText());
                }
            }
            ImGui::SameLine(0, sp * 1.5f);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + fps_w - ImGui::CalcTextSize(fps).x);
            ImGui::PushStyleColor(ImGuiCol_Text, c.text_disabled);
            ImGui::TextUnformatted(fps);
            ImGui::PopStyleColor();
            ImGui::EndMenuBar();
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

}  // namespace imforge::builder
