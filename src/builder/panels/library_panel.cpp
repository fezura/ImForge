#include <cstdio>
#include <cstring>

#include "../ui/tree_actions.h"
#include "editor_panel.h"
#include "library_actions.h"
#include "library_shared.h"

namespace imforge::builder {

namespace {

class LibraryPanel;
LibraryPanel* g_library = nullptr;

class LibraryPanel final : public EditorPanel {
public:
    LibraryPanel() {
        tabs_.push_back(library::MakeAnimationsTab());
        tabs_.push_back(library::MakeDesignsTab());
        tabs_.push_back(library::MakeWidgetsTab());
        tabs_.push_back(library::MakeTemplatesTab());
        g_library = this;
    }
    ~LibraryPanel() override {
        if (g_library == this) g_library = nullptr;
    }
    const char* Name() const override { return "Library"; }
    const char* DefaultDock() const override { return "center"; }
    const char* Icon() const override { return tree_actions::Icon("fa-book-open", nullptr); }
    void OnProjectChanged(EditorContext& ctx) override {
        for (auto& t : tabs_) t->OnProjectChanged(ctx);
    }

    void Draw(EditorContext& ctx) override {
        ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false) && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            library::SearchFocusRequest() = true;
        DrawTabs(ctx);
        ImGui::Spacing();
        tabs_[(size_t)active_]->Draw(ctx);
    }

    bool SetTab(const std::string& name) {
        for (size_t i = 0; i < tabs_.size(); ++i)
            if (library::ContainsCI(tabs_[i]->Title(), name) && name.size() >= 3) {
                active_ = (int)i;
                return true;
            }
        return false;
    }

    bool Command(EditorContext& ctx, const std::string& verb, const std::string& arg) {
        return tabs_[(size_t)active_]->Command(ctx, verb, arg);
    }

private:
    std::vector<std::unique_ptr<library::Tab>> tabs_;
    int active_ = 0;

    void DrawTabs(EditorContext& ctx) {
        const BuilderColors& c = Colors();
        const float s = library::Dpi(ctx);
        const float fs = ImGui::GetFontSize();
        const float h = std::floor(ImGui::GetFrameHeight() * 1.3f);
        const float inset = std::floor(3.0f * s);
        const float w = ImGui::GetContentRegionAvail().x;
        const int n = (int)tabs_.size();
        const float seg = std::floor((w - inset * 2.0f) / (float)n);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float r = std::floor(h * 0.5f);
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ToU32(c.bg_frame, 0.65f), r);
        ImGui::PushID("##lib_tabs");
        for (int i = 0; i < n; ++i) {
            library::Tab& tab = *tabs_[(size_t)i];
            const ImVec2 a(p.x + inset + seg * (float)i, p.y + inset);
            const ImVec2 b(i == n - 1 ? p.x + w - inset : a.x + seg, p.y + h - inset);
            ImGui::SetCursorScreenPos(a);
            ImGui::PushID(i);
            if (ImGui::InvisibleButton("##tab", ImVec2(b.x - a.x, b.y - a.y))) active_ = i;
            const bool hov = ImGui::IsItemHovered(), on = i == active_;
            ImGui::PopID();
            if (on) {
                dl->AddRectFilled(ImVec2(a.x, a.y + 1.0f), ImVec2(b.x, b.y + 1.0f), IM_COL32(0, 0, 0, 50), r - inset);
                dl->AddRectFilled(a, b, ToU32(c.accent), r - inset);
            } else if (hov) {
                dl->AddRectFilled(a, b, ToU32(c.bg_hover), r - inset);
            }
            char count[16];
            std::snprintf(count, sizeof(count), "%d", tab.Count(ctx));
            const char* icon = library::Icon(tab.IconName());
            const float iw = icon ? ImGui::CalcTextSize(icon).x + std::floor(fs * 0.45f) : 0.0f;
            const float tw = ImGui::CalcTextSize(tab.Title()).x, cw = ImGui::CalcTextSize(count).x + std::floor(fs * 0.45f);
            const float avail = b.x - a.x - fs;
            const bool show_count = iw + tw + cw <= avail, show_title = iw + tw <= avail || !icon;
            const float total = (icon ? iw : 0.0f) + (show_title ? tw : 0.0f) + (show_count ? cw : 0.0f) - (show_title ? 0.0f : fs * 0.45f);
            float x = std::floor((a.x + b.x - total) * 0.5f);
            const float ty = std::floor((a.y + b.y - ImGui::GetTextLineHeight()) * 0.5f);
            const ImU32 fg = on ? IM_COL32(255, 255, 255, 255) : ToU32(hov ? c.text : c.text_dim);
            if (icon) {
                dl->AddText(ImVec2(x, ty), on ? IM_COL32(255, 255, 255, 230) : ToU32(hov ? c.accent_hover : c.accent), icon);
                x += iw;
            }
            if (show_title) {
                dl->AddText(ImVec2(x, ty), fg, tab.Title());
                x += tw;
            }
            if (show_count)
                dl->AddText(ImVec2(x + std::floor(fs * 0.45f), ty), on ? IM_COL32(255, 255, 255, 160) : ToU32(c.text_disabled), count);
            if (!show_title && hov) ImGui::SetTooltip("%s", tab.Title());
        }
        ImGui::PopID();
        ImGui::SetCursorScreenPos(p);
        ImGui::Dummy(ImVec2(w, h));
    }
};

}  // namespace

void ScrollLibrary(float y) { library::ScrollRequest() = y < 0.0f ? 0.0f : y; }

bool ShowLibraryTab(EditorContext& ctx, const std::string& command) {
    if (!g_library) return false;
    std::string words[3];
    size_t pos = 0;
    for (int i = 0; i < 3; ++i) {
        while (pos < command.size() && command[pos] == ' ') ++pos;
        size_t end = i == 2 ? command.size() : command.find(' ', pos);
        if (end == std::string::npos) end = command.size();
        words[i] = command.substr(pos, end - pos);
        pos = end;
    }
    if (!words[0].empty() && !g_library->SetTab(words[0])) return false;
    if (!ctx.OpenPanel("Library", true)) return false;
    return words[1].empty() || g_library->Command(ctx, words[1], words[2]);
}

IMFORGE_REGISTER_PANEL(LibraryPanel, 31);

}  // namespace imforge::builder
