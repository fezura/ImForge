#include "flags_editor.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "imgui.h"
#include "imgui_internal.h"

#include "imforge/imforge_core.h"

namespace imforge::builder {

namespace {

bool Contains(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

void Remove(std::vector<std::string>& v, const std::string& s) {
    v.erase(std::remove(v.begin(), v.end(), s), v.end());
}

bool OpenSummaryButton(const char* label, const std::string& summary) {
    const char* vis_end = ImGui::FindRenderedTextEnd(label);
    float w = ImGui::CalcItemWidth();
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s##btn", summary.c_str());
    bool pressed = ImGui::Button(buf, ImVec2(w, 0));
    if (vis_end != label) {
        ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::TextUnformatted(label, vis_end);
    }
    return pressed;
}

}  // namespace

std::string JoinNames(const std::vector<std::string>& names, size_t max_shown) {
    if (names.empty()) return "(none)";
    std::string s;
    for (size_t i = 0; i < names.size() && i < max_shown; ++i) {
        if (i) s += ", ";
        s += names[i];
    }
    if (names.size() > max_shown) s += " +" + std::to_string(names.size() - max_shown);
    return s;
}

bool FlagsEditor(const char* label, std::vector<std::string>& flags, const char* enum_name, int imgui_version) {
    ImGui::PushID(label);
    size_t n = 0;
    const FlagDef* defs = FlagsFor(enum_name, n);
    bool changed = false;
    if (OpenSummaryButton(label, JoinNames(flags))) ImGui::OpenPopup("##flags_popup");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort) && !flags.empty())
        ImGui::SetTooltip("%s", JoinNames(flags, 64).c_str());

    if (ImGui::BeginPopup("##flags_popup")) {
        ImGui::TextDisabled("%s", enum_name ? enum_name : "flags");
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear") && !flags.empty()) {
            flags.clear();
            changed = true;
        }
        ImGui::Separator();
        if (!defs) ImGui::TextDisabled("unknown flags enum");
        std::vector<std::string> unknown = flags;
        for (size_t i = 0; i < n; ++i) {
            const FlagDef& d = defs[i];
            Remove(unknown, d.name);
            bool on = Contains(flags, d.name);
            bool unavailable = d.min_version > imgui_version;
            if (unavailable) ImGui::BeginDisabled();
            if (ImGui::Checkbox(d.name, &on)) {
                if (on) {
                    std::vector<std::string> ordered;
                    for (size_t k = 0; k < n; ++k)
                        if (Contains(flags, defs[k].name) || k == i) ordered.push_back(defs[k].name);
                    for (const std::string& u : flags)
                        if (!Contains(ordered, u)) ordered.push_back(u);
                    flags = std::move(ordered);
                } else {
                    Remove(flags, d.name);
                }
                changed = true;
            }
            if (unavailable) {
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("Requires Dear ImGui >= %s (project targets %s)",
                                      Project::VersionString(d.min_version).c_str(),
                                      Project::VersionString(imgui_version).c_str());
            }
        }
        if (!unknown.empty()) {
            ImGui::Separator();
            ImGui::TextDisabled("Unknown in this catalog");
            for (const std::string& u : unknown) {
                ImGui::PushID(u.c_str());
                if (ImGui::SmallButton("x")) {
                    Remove(flags, u);
                    changed = true;
                }
                ImGui::SameLine();
                ImGui::TextUnformatted(u.c_str());
                ImGui::PopID();
            }
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}

bool MultiSelectEditor(const char* label, std::vector<std::string>& selected, const std::vector<std::string>& options,
                       const char* empty_text) {
    ImGui::PushID(label);
    bool changed = false;
    std::string summary = selected.empty() ? std::string(empty_text) : JoinNames(selected);
    if (OpenSummaryButton(label, summary)) ImGui::OpenPopup("##multi_popup");
    if (ImGui::BeginPopup("##multi_popup")) {
        if (options.empty()) ImGui::TextDisabled("nothing to choose from");
        std::vector<std::string> unknown = selected;
        for (const std::string& o : options) {
            Remove(unknown, o);
            bool on = Contains(selected, o);
            if (ImGui::Checkbox(o.c_str(), &on)) {
                if (on) selected.push_back(o);
                else Remove(selected, o);
                changed = true;
            }
        }
        if (!unknown.empty()) {
            ImGui::Separator();
            ImGui::TextDisabled("Not defined");
            for (const std::string& u : unknown) {
                ImGui::PushID(u.c_str());
                if (ImGui::SmallButton("x")) {
                    Remove(selected, u);
                    changed = true;
                }
                ImGui::SameLine();
                ImGui::TextUnformatted(u.c_str());
                ImGui::PopID();
            }
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}

}  // namespace imforge::builder
