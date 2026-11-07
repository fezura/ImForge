#include <cfloat>
#include <cstdio>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"

#include "../builder_fonts.h"
#include "../builder_style.h"
#include "../editor_context.h"
#include "../ui/ui_helpers.h"
#include "editor_panel.h"

namespace imforge::builder {

namespace {

class LogPanel final : public EditorPanel {
public:
    const char* Name() const override { return "Log"; }
    const char* DefaultDock() const override { return "bottom"; }

    void Draw(EditorContext& ctx) override {
        const LogBuffer& log = ctx.log;
        const BuilderColors& c = Colors();
        if (ImGui::Button("Clear")) ctx.log.Clear();
        ImGui::SameLine();
        if (ImGui::Button("Copy")) CopyVisible(ctx);
        ui::Tooltip("Copy the visible log lines to the clipboard");
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &auto_scroll_);
        ImGui::SameLine();
        ImGui::Checkbox("Mono", &mono_);
        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        static const char* names[4] = {"Debug", "Info", "Warn", "Error"};
        for (int i = 0; i < 4; ++i) {
            ImGui::SameLine();
            ImVec4 col = ui::LevelColor(i);
            ImGui::PushStyleColor(ImGuiCol_Text, show_[i] ? col : c.text_disabled);
            ImGui::PushStyleColor(ImGuiCol_Button, show_[i] ? WithAlpha(col, 0.15f) : ImVec4(0, 0, 0, 0));
            char label[64];
            std::snprintf(label, sizeof(label), "%s %u###lvl%d", names[i], log.Count((LogLevel)i), i);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, WithAlpha(col, 0.25f));
            if (ImGui::Button(label)) show_[i] = !show_[i];
            ImGui::PopStyleColor(3);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputTextWithHint("##filter", "Filter messages", filter_.InputBuf, IM_ARRAYSIZE(filter_.InputBuf)))
            filter_.Build();
        ImGui::Separator();

        ImFont* font = mono_ ? MonoFont() : nullptr;
        if (ImGui::BeginChild("log_scroll", ImVec2(0, 0), 0,
                              ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
            if (font) ImGui::PushFont(font);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 2));
            visible_.clear();
            for (size_t i = 0; i < log.Size(); ++i) {
                const LogEntry& e = log.At(i);
                if (!show_[(int)e.level]) continue;
                if (filter_.IsActive() && !filter_.PassFilter(e.message.c_str()) && !filter_.PassFilter(e.source.c_str())) continue;
                visible_.push_back((int)i);
            }
            ImGuiListClipper clipper;
            clipper.Begin((int)visible_.size());
            while (clipper.Step()) {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                    const LogEntry& e = log.At((size_t)visible_[(size_t)row]);
                    char ts[32];
                    int total_ms = (int)(e.time * 1000.0);
                    std::snprintf(ts, sizeof(ts), "%02d:%02d.%03d", (total_ms / 60000) % 100, (total_ms / 1000) % 60, total_ms % 1000);
                    ImGui::PushStyleColor(ImGuiCol_Text, c.text_disabled);
                    ImGui::TextUnformatted(ts);
                    ImGui::PopStyleColor();
                    ImGui::SameLine();
                    ImGui::PushStyleColor(ImGuiCol_Text, ui::LevelColor((int)e.level));
                    ImGui::TextUnformatted(ui::LevelIcon((int)e.level));
                    ImGui::PopStyleColor();
                    ImGui::SameLine();
                    if (!e.source.empty() && e.source != "builder") {
                        ImGui::PushStyleColor(ImGuiCol_Text, c.text_dim);
                        ImGui::Text("[%s]", e.source.c_str());
                        ImGui::PopStyleColor();
                        ImGui::SameLine();
                    }
                    if (e.level == LogLevel::Error) ImGui::PushStyleColor(ImGuiCol_Text, c.error);
                    else if (e.level == LogLevel::Warning) ImGui::PushStyleColor(ImGuiCol_Text, c.warning);
                    else if (e.level == LogLevel::Debug) ImGui::PushStyleColor(ImGuiCol_Text, c.text_dim);
                    else ImGui::PushStyleColor(ImGuiCol_Text, c.text);
                    ImGui::TextUnformatted(e.message.c_str());
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) ImGui::SetClipboardText(e.message.c_str());
                }
            }
            ImGui::PopStyleVar();
            if (font) ImGui::PopFont();
            if (auto_scroll_ && (log.Generation() != last_generation_ || ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f))
                ImGui::SetScrollHereY(1.0f);
            if (ImGui::GetIO().MouseWheel != 0.0f && ImGui::IsWindowHovered() && ImGui::GetScrollY() < ImGui::GetScrollMaxY() - 1.0f)
                auto_scroll_ = false;
            last_generation_ = log.Generation();
        }
        ImGui::EndChild();
    }

private:
    void CopyVisible(EditorContext& ctx) {
        std::string out;
        for (int idx : visible_) {
            const LogEntry& e = ctx.log.At((size_t)idx);
            static const char* lv[4] = {"DEBUG", "INFO", "WARN", "ERROR"};
            char ts[32];
            std::snprintf(ts, sizeof(ts), "%8.3f", e.time);
            out += ts;
            out += " ";
            out += lv[(int)e.level];
            out += " ";
            if (!e.source.empty()) out += "[" + e.source + "] ";
            out += e.message;
            out += "\n";
        }
        ImGui::SetClipboardText(out.c_str());
        ctx.SetStatus("Copied " + std::to_string(visible_.size()) + " log lines");
    }

    bool show_[4] = {false, true, true, true};
    bool auto_scroll_ = true;
    bool mono_ = false;
    unsigned last_generation_ = 0;
    ImGuiTextFilter filter_;
    std::vector<int> visible_;
};

}  // namespace

IMFORGE_REGISTER_PANEL(LogPanel, 900);

}  // namespace imforge::builder
