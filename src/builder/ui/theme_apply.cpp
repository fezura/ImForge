#include "theme_apply.h"

#include <cstring>

#include "imgui_internal.h"

namespace imforge::builder {

namespace {

struct VarDef {
    const char* name;
    ImGuiStyleVar var;
    bool vec2;
};

const VarDef kVars[] = {
    {"Alpha", ImGuiStyleVar_Alpha, false},
    {"DisabledAlpha", ImGuiStyleVar_DisabledAlpha, false},
    {"WindowPadding", ImGuiStyleVar_WindowPadding, true},
    {"WindowRounding", ImGuiStyleVar_WindowRounding, false},
    {"WindowBorderSize", ImGuiStyleVar_WindowBorderSize, false},
    {"WindowMinSize", ImGuiStyleVar_WindowMinSize, true},
    {"WindowTitleAlign", ImGuiStyleVar_WindowTitleAlign, true},
    {"ChildRounding", ImGuiStyleVar_ChildRounding, false},
    {"ChildBorderSize", ImGuiStyleVar_ChildBorderSize, false},
    {"PopupRounding", ImGuiStyleVar_PopupRounding, false},
    {"PopupBorderSize", ImGuiStyleVar_PopupBorderSize, false},
    {"FramePadding", ImGuiStyleVar_FramePadding, true},
    {"FrameRounding", ImGuiStyleVar_FrameRounding, false},
    {"FrameBorderSize", ImGuiStyleVar_FrameBorderSize, false},
    {"ItemSpacing", ImGuiStyleVar_ItemSpacing, true},
    {"ItemInnerSpacing", ImGuiStyleVar_ItemInnerSpacing, true},
    {"IndentSpacing", ImGuiStyleVar_IndentSpacing, false},
    {"CellPadding", ImGuiStyleVar_CellPadding, true},
    {"ScrollbarSize", ImGuiStyleVar_ScrollbarSize, false},
    {"ScrollbarRounding", ImGuiStyleVar_ScrollbarRounding, false},
    {"GrabMinSize", ImGuiStyleVar_GrabMinSize, false},
    {"GrabRounding", ImGuiStyleVar_GrabRounding, false},
#if IMGUI_VERSION_NUM >= 19190
    {"ImageBorderSize", ImGuiStyleVar_ImageBorderSize, false},
#endif
    {"TabRounding", ImGuiStyleVar_TabRounding, false},
#if IMGUI_VERSION_NUM >= 19050
    {"TabBorderSize", ImGuiStyleVar_TabBorderSize, false},
#endif
#if IMGUI_VERSION_NUM >= 19000
    {"TabBarBorderSize", ImGuiStyleVar_TabBarBorderSize, false},
#endif
#if IMGUI_VERSION_NUM >= 19100
    {"TabBarOverlineSize", ImGuiStyleVar_TabBarOverlineSize, false},
#endif
#if IMGUI_VERSION_NUM >= 19000
    {"TableAngledHeadersAngle", ImGuiStyleVar_TableAngledHeadersAngle, false},
#endif
#if IMGUI_VERSION_NUM >= 19060
    {"TableAngledHeadersTextAlign", ImGuiStyleVar_TableAngledHeadersTextAlign, true},
#endif
    {"ButtonTextAlign", ImGuiStyleVar_ButtonTextAlign, true},
    {"SelectableTextAlign", ImGuiStyleVar_SelectableTextAlign, true},
#if IMGUI_VERSION_NUM >= 18950
    {"SeparatorTextBorderSize", ImGuiStyleVar_SeparatorTextBorderSize, false},
    {"SeparatorTextAlign", ImGuiStyleVar_SeparatorTextAlign, true},
    {"SeparatorTextPadding", ImGuiStyleVar_SeparatorTextPadding, true},
#endif
#if defined(IMGUI_HAS_DOCK) && IMGUI_VERSION_NUM >= 19000
    {"DockingSeparatorSize", ImGuiStyleVar_DockingSeparatorSize, false},
#endif
};

struct Alias {
    const char* old_name;
    const char* new_name;
};

const Alias kAliases[] = {
    {"TabActive", "TabSelected"},
    {"TabUnfocused", "TabDimmed"},
    {"TabUnfocusedActive", "TabDimmedSelected"},
    {"NavHighlight", "NavCursor"},
    {"ModalWindowDarkening", "ModalWindowDimBg"},
    {"ChildWindowBg", "ChildBg"},
    {"Column", "Separator"},
    {"ColumnHovered", "SeparatorHovered"},
    {"ColumnActive", "SeparatorActive"},
    {"ComboBg", "PopupBg"},
};

struct Group {
    const char* name;
    const char* prefixes;
};

const Group kGroups[] = {
    {"Text", "Text|TextDisabled|TextLink|TextSelectedBg"},
    {"Window", "WindowBg|ChildBg|PopupBg|Border|BorderShadow|MenuBarBg|ModalWindowDimBg|NavWindowingDimBg"},
    {"Title", "TitleBg|TitleBgActive|TitleBgCollapsed"},
    {"Frame", "FrameBg|FrameBgHovered|FrameBgActive|CheckMark|SliderGrab|SliderGrabActive"},
    {"Button", "Button|ButtonHovered|ButtonActive"},
    {"Header", "Header|HeaderHovered|HeaderActive"},
    {"Tab", "Tab|TabHovered|TabSelected|TabSelectedOverline|TabDimmed|TabDimmedSelected|TabDimmedSelectedOverline"},
    {"Scrollbar", "ScrollbarBg|ScrollbarGrab|ScrollbarGrabHovered|ScrollbarGrabActive"},
    {"Separator", "Separator|SeparatorHovered|SeparatorActive|ResizeGrip|ResizeGripHovered|ResizeGripActive"},
    {"Table", "TableHeaderBg|TableBorderStrong|TableBorderLight|TableRowBg|TableRowBgAlt"},
    {"Plot", "PlotLines|PlotLinesHovered|PlotHistogram|PlotHistogramHovered"},
    {"Nav", "NavCursor|NavWindowingHighlight|DragDropTarget"},
    {"Docking", "DockingPreview|DockingEmptyBg"},
};

bool InPipeList(const char* list, const char* name) {
    size_t n = std::strlen(name);
    const char* p = list;
    while (*p) {
        const char* end = std::strchr(p, '|');
        size_t len = end ? (size_t)(end - p) : std::strlen(p);
        if (len == n && std::strncmp(p, name, n) == 0) return true;
        if (!end) break;
        p = end + 1;
    }
    return false;
}

const VarDef* FindVar(const char* name) {
    if (!name) return nullptr;
    for (const VarDef& v : kVars)
        if (std::strcmp(v.name, name) == 0) return &v;
    return nullptr;
}

}  // namespace

const char* ResolveColorAlias(const char* name) {
    if (!name) return "";
    for (const Alias& a : kAliases)
        if (std::strcmp(a.old_name, name) == 0) return a.new_name;
    return name;
}

int ImGuiColIndexByName(const char* name) {
    if (!name || !*name) return -1;
    name = ResolveColorAlias(name);
    for (int i = 0; i < ImGuiCol_COUNT; ++i)
        if (std::strcmp(ImGui::GetStyleColorName(i), name) == 0) return i;
    return -1;
}

int ImGuiStyleVarIndexByName(const char* name, bool* is_vec2) {
    const VarDef* v = FindVar(name);
    if (!v) return -1;
    if (is_vec2) *is_vec2 = v->vec2;
    return (int)v->var;
}

const char* ColorGroupOf(const char* name) {
    name = ResolveColorAlias(name);
    for (const Group& g : kGroups)
        if (InPipeList(g.prefixes, name)) return g.name;
    return "Other";
}

const char* const* ColorGroupNames(size_t& count) {
    static const char* names[sizeof(kGroups) / sizeof(kGroups[0]) + 1];
    static bool init = false;
    if (!init) {
        for (size_t i = 0; i < sizeof(kGroups) / sizeof(kGroups[0]); ++i) names[i] = kGroups[i].name;
        names[sizeof(kGroups) / sizeof(kGroups[0])] = "Other";
        init = true;
    }
    count = sizeof(names) / sizeof(names[0]);
    return names;
}

bool StyleVarValue(const Json& v, bool vec2, float& f, ImVec2& v2) {
    if (vec2) {
        if (v.IsObject()) {
            v2 = ImVec2((float)v["x"].AsDouble(), (float)v["y"].AsDouble());
            return true;
        }
        if (v.IsArray() && v.Size() >= 2) {
            v2 = ImVec2((float)v[0].AsDouble(), (float)v[1].AsDouble());
            return true;
        }
        if (v.IsNumber()) {
            v2 = ImVec2((float)v.AsDouble(), (float)v.AsDouble());
            return true;
        }
        return false;
    }
    if (v.IsNumber()) {
        f = (float)v.AsDouble();
        return true;
    }
    if (v.IsObject() && v["x"].IsNumber()) {
        f = (float)v["x"].AsDouble();
        return true;
    }
    return false;
}

Json StyleVarJson(bool vec2, float f, ImVec2 v2) {
    if (!vec2) return Json(core_detail::FloatToNum(f));
    Json o = Json::Object();
    o.Set("x", Json(core_detail::FloatToNum(v2.x)));
    o.Set("y", Json(core_detail::FloatToNum(v2.y)));
    return o;
}

Json DefaultStyleVarJson(const char* name) {
    const VarDef* v = FindVar(name);
    if (!v) return Json(0.0);
    ImGuiStyle def;
    const void* p = ImGui::GetStyleVarInfo(v->var)->GetVarPtr(&def);
    if (v->vec2) return StyleVarJson(true, 0, *static_cast<const ImVec2*>(p));
    return StyleVarJson(false, *static_cast<const float*>(p), ImVec2());
}

StyleScope PushStyleOverride(const StyleOverride& style) {
    StyleScope scope;
    for (const auto& c : style.colors) {
        int idx = ImGuiColIndexByName(c.first.c_str());
        if (idx < 0) continue;
        ImGui::PushStyleColor(idx, ToImVec4(c.second));
        ++scope.colors;
    }
    for (const auto& v : style.vars) {
        bool vec2 = false;
        int idx = ImGuiStyleVarIndexByName(v.first.c_str(), &vec2);
        if (idx < 0) continue;
        float f = 0;
        ImVec2 v2;
        if (!StyleVarValue(v.second, vec2, f, v2)) continue;
        if (vec2) ImGui::PushStyleVar(idx, v2);
        else ImGui::PushStyleVar(idx, f);
        ++scope.vars;
    }
    return scope;
}

StyleScope PushThemeStyle(const Theme& theme) {
    StyleOverride s;
    s.colors = theme.colors;
    s.vars = theme.vars;
    return PushStyleOverride(s);
}

StyleScope PushStyleClasses(const Project& project, const std::vector<std::string>& classes) {
    StyleScope total;
    for (const std::string& name : classes) {
        const StyleOverride* cls = FindStyleClass(project, name);
        if (!cls) continue;
        StyleScope s = PushStyleOverride(*cls);
        total.colors += s.colors;
        total.vars += s.vars;
    }
    return total;
}

void PopStyleScope(const StyleScope& scope) {
    if (scope.colors) ImGui::PopStyleColor(scope.colors);
    if (scope.vars) ImGui::PopStyleVar(scope.vars);
}

void ApplyThemeToStyle(const Theme& theme, ImGuiStyle& style) {
    for (const auto& c : theme.colors) {
        int idx = ImGuiColIndexByName(c.first.c_str());
        if (idx >= 0) style.Colors[idx] = ToImVec4(c.second);
    }
    for (const auto& v : theme.vars) {
        const VarDef* def = FindVar(v.first.c_str());
        if (!def) continue;
        float f = 0;
        ImVec2 v2;
        if (!StyleVarValue(v.second, def->vec2, f, v2)) continue;
        void* p = ImGui::GetStyleVarInfo(def->var)->GetVarPtr(&style);
        if (def->vec2) *static_cast<ImVec2*>(p) = v2;
        else *static_cast<float*>(p) = f;
    }
}

Theme ThemeFromStyle(const ImGuiStyle& style, const std::string& name) {
    Theme t;
    t.name = name;
    for (int i = 0; i < ImGuiCol_COUNT; ++i)
        t.colors.emplace_back(ImGui::GetStyleColorName(i), ToVec4(style.Colors[i]));
    for (const VarDef& v : kVars) {
        const void* p = ImGui::GetStyleVarInfo(v.var)->GetVarPtr(const_cast<ImGuiStyle*>(&style));
        if (v.vec2) t.vars.emplace_back(v.name, StyleVarJson(true, 0, *static_cast<const ImVec2*>(p)));
        else t.vars.emplace_back(v.name, StyleVarJson(false, *static_cast<const float*>(p), ImVec2()));
    }
    return t;
}

const Theme* FindThemeByName(const Project& project, const std::string& name) {
    for (const Theme& t : project.themes)
        if (t.name == name) return &t;
    return nullptr;
}

Theme* FindThemeByName(Project& project, const std::string& name) {
    for (Theme& t : project.themes)
        if (t.name == name) return &t;
    return nullptr;
}

const StyleOverride* FindStyleClass(const Project& project, const std::string& name) {
    for (const auto& c : project.style_classes)
        if (c.first == name) return &c.second;
    return nullptr;
}

}  // namespace imforge::builder
