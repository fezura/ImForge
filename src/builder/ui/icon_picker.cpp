#include "icon_picker.h"

#include <cstdio>
#include <cstring>

#include "icon_table.h"
#include "imgui_internal.h"

namespace imforge {
namespace builder {

namespace {

ImFont*& PickerFont() {
    static ImFont* font = nullptr;
    return font;
}

char Lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

bool ContainsNoCase(const std::string& hay, const std::string& needle) {
    if (needle.empty()) return true;
    if (needle.size() > hay.size()) return false;
    for (size_t i = 0; i + needle.size() <= hay.size(); ++i) {
        size_t j = 0;
        while (j < needle.size() && Lower(hay[i + j]) == Lower(needle[j])) ++j;
        if (j == needle.size()) return true;
    }
    return false;
}

const char* PreviewGlyph(const std::string& name) {
    const char* utf8 = IconUtf8(name.c_str());
    return utf8 ? utf8 : "";
}

}  // namespace

void SetIconPickerFont(ImFont* font) { PickerFont() = font; }
ImFont* IconPickerFont() { return PickerFont(); }

std::string IconToken(const std::string& name) {
    if (name.empty()) return std::string();
    if (name.compare(0, 6, "{icon:") == 0) return name;
    return "{icon:" + name + "}";
}

std::string IconNameFromToken(const std::string& token) {
    if (token.compare(0, 6, "{icon:") == 0 && !token.empty() && token.back() == '}')
        return token.substr(6, token.size() - 7);
    return token;
}

bool IconPicker(const char* id, std::string& icon_name) {
    ImGui::PushID(id);
    static char filter[64] = "";
    bool changed = false;
    std::string current = IconNameFromToken(icon_name);
    ImFont* font = PickerFont();

    char label[160];
    const char* glyph = current.empty() ? nullptr : PreviewGlyph(current);
    if (current.empty()) std::snprintf(label, sizeof(label), "(no icon)");
    else std::snprintf(label, sizeof(label), "%s  %s", glyph && *glyph ? glyph : "?", current.c_str());
    if (font) ImGui::PushFont(font);
    bool open = ImGui::Button(label, ImVec2(ImGui::CalcItemWidth(), 0));
    if (font) ImGui::PopFont();
    if (open) {
        filter[0] = 0;
        ImGui::OpenPopup("##icon_picker_popup");
    }
    if (!current.empty()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            icon_name.clear();
            changed = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clear icon");
    }

    ImGui::SetNextWindowSize(ImVec2(420, 360), ImGuiCond_Appearing);
    if (ImGui::BeginPopup("##icon_picker_popup")) {
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##filter", "Search icons (e.g. play, arrow, user)", filter, sizeof(filter));
        size_t count = 0;
        const IconEntry* table = IconTable(count);
        ImGui::TextDisabled("%zu icons (%s)", count, IconTableSource() == "builtin" ? "built-in list" : "header");
        ImGui::Separator();
        if (ImGui::BeginChild("##grid", ImVec2(0, 0), ImGuiChildFlags_None)) {
            const float cell = ImGui::GetFontSize() * 2.2f;
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            int columns = (int)((ImGui::GetContentRegionAvail().x + spacing) / (cell + spacing));
            if (columns < 1) columns = 1;
            std::string needle = filter;
            int shown = 0;
            if (font) ImGui::PushFont(font);
            for (size_t i = 0; i < count; ++i) {
                const IconEntry& e = table[i];
                if (!ContainsNoCase(e.name, needle)) continue;
                if (shown % columns != 0) ImGui::SameLine();
                ImGui::PushID((int)i);
                bool selected = e.name == current;
                if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                if (ImGui::Button(e.utf8.c_str(), ImVec2(cell, cell))) {
                    icon_name = IconToken(e.name);
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }
                if (selected) ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) {
                    if (font) ImGui::PopFont();
                    ImGui::SetTooltip("%s\nU+%04X", e.name.c_str(), e.codepoint);
                    if (font) ImGui::PushFont(font);
                }
                ImGui::PopID();
                ++shown;
            }
            if (font) ImGui::PopFont();
            if (shown == 0) ImGui::TextDisabled("No icon matches \"%s\"", filter);
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}

}  // namespace builder
}  // namespace imforge
