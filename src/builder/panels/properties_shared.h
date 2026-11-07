#pragma once

#include <cfloat>
#include <cstring>
#include <string>
#include <vector>

#include "imgui.h"

#include "../builder_fonts.h"
#include "../builder_style.h"
#include "../editor_context.h"
#include "../ui/color_utils.h"
#include "../ui/editor_type_env.h"
#include "../ui/ui_helpers.h"

#ifdef ICON_FA_ROTATE_LEFT
#define PP_ICON_RESET ICON_FA_ROTATE_LEFT
#define PP_ICON_REMOVE ICON_FA_XMARK
#define PP_ICON_ADD ICON_FA_PLUS
#define PP_ICON_UP ICON_FA_ARROW_UP
#define PP_ICON_DOWN ICON_FA_ARROW_DOWN
#define PP_ICON_PLAY ICON_FA_PLAY
#define PP_ICON_LOCK ICON_FA_LOCK
#define PP_ICON_UNLOCK ICON_FA_LOCK_OPEN
#define PP_ICON_PARAM ICON_FA_LINK
#else
#define PP_ICON_RESET "R"
#define PP_ICON_REMOVE "x"
#define PP_ICON_ADD "+"
#define PP_ICON_UP "^"
#define PP_ICON_DOWN "v"
#define PP_ICON_PLAY ">"
#define PP_ICON_LOCK "L"
#define PP_ICON_UNLOCK "U"
#define PP_ICON_PARAM "P"
#endif

namespace imforge::builder::props {

struct PropsUI {
    EditorContext& ctx;
    Node& node;
    Node* root;
    std::vector<Node*> selected;
    EditorTypeEnv& env;
    const WidgetDef* def = nullptr;
    const CustomWidgetDef* custom = nullptr;
    int version = 0;
    bool multi = false;
    bool in_component = false;
    const Component* component_doc = nullptr;
    std::string doc_name;

    template <class F>
    void Edit(const char* label, F&& f) {
        ctx.BeginEdit(label);
        f();
        if (multi) SyncMulti();
        ctx.EndEdit();
    }
    void SyncMulti();
};

void DrawIdentitySection(PropsUI& ui);
void DrawPropertiesSection(PropsUI& ui);
void DrawBindingsSection(PropsUI& ui);
void DrawConditionsSection(PropsUI& ui);
void DrawEventsSection(PropsUI& ui);
void DrawStyleSection(PropsUI& ui);
void DrawAnimSection(PropsUI& ui);
void DrawFxSection(PropsUI& ui);
void DrawLayoutSection(PropsUI& ui);
// true when the node was replaced (Unpack), don't touch `ui.node` after that
bool DrawComponentSection(PropsUI& ui);

// shared widgets
// open state is remembered per section name, across selections
bool SectionHeader(const char* label, bool default_open);
float SmallButtonWidth();
bool SmallIconButton(const char* id, const char* icon, const char* tooltip, bool enabled = true, bool highlighted = false);
bool ResetButton(const char* tooltip = "Reset to default");
// Label cell of a property row. Sets the next item width to the cell width minus room for
// `trailing_buttons` small buttons and returns it.
float PropRow(const char* label, const char* help, int trailing_buttons);
void TrailingSameLine();
bool ColorEditHex(const char* id, ImVec4& color);
bool ComboStrings(const char* label, std::string& value, const std::vector<std::string>& items, const char* none_label);
bool ComboIndex(const char* label, int& index, const char* const* items, int count);
void UnsupportedNote(int min_version, int version);
std::string VersionText(int version_num);
std::string HandlerName(const PropsUI& ui, const char* event);
std::vector<std::string> NodeIdsOfType(const Node& root, const char* types_pipe);
std::vector<std::string> ComponentNames(const Project& project);

// json helpers, ToVec4 / ToImVec4 live in imforge_value.h
using imforge::ToImVec4;
using imforge::ToVec4;
inline Json ColorJson(const ImVec4& c) { return Json(imforge::ColorToHex(imforge::ToVec4(c))); }
inline ImVec4 JsonColor(const Json& j, const ImVec4& def) {
    bool ok = false;
    Vec4 c = j.IsString() ? ParseColor(j.AsString(), &ok) : Vec4{};
    return ok ? imforge::ToImVec4(c) : def;
}
inline Json Vec2Json(const ImVec2& v) {
    Json o = Json::Object();
    o.Set("x", Json(core_detail::FloatToNum(v.x)));
    o.Set("y", Json(core_detail::FloatToNum(v.y)));
    return o;
}
inline ImVec2 JsonVec2(const Json& j, const ImVec2& def) {
    if (!j.IsObject()) return def;
    return ImVec2((float)j["x"].AsDouble(def.x), (float)j["y"].AsDouble(def.y));
}
inline Json Vec4Json(const ImVec4& v) {
    Json o = Json::Object();
    o.Set("x", Json(core_detail::FloatToNum(v.x)));
    o.Set("y", Json(core_detail::FloatToNum(v.y)));
    o.Set("z", Json(core_detail::FloatToNum(v.z)));
    o.Set("w", Json(core_detail::FloatToNum(v.w)));
    return o;
}
inline ImVec4 JsonVec4(const Json& j, const ImVec4& def) {
    if (!j.IsObject()) return def;
    return ImVec4((float)j["x"].AsDouble(def.x), (float)j["y"].AsDouble(def.y), (float)j["z"].AsDouble(def.z),
                  (float)j["w"].AsDouble(def.w));
}
inline Json FloatJson(float f) { return Json(core_detail::FloatToNum(f)); }
inline Json ExprJson(const std::string& e) {
    Json o = Json::Object();
    o.Set("expr", Json(e));
    return o;
}
inline Json ParamJson(const std::string& p) {
    Json o = Json::Object();
    o.Set("param", Json(p));
    return o;
}
inline std::vector<std::string> JsonStringList(const Json& j) {
    std::vector<std::string> out;
    if (j.IsArray())
        for (size_t i = 0; i < j.Size(); ++i) out.push_back(j[i].AsString(""));
    return out;
}
inline Json StringListJson(const std::vector<std::string>& v) {
    Json a = Json::Array();
    for (const std::string& s : v) a.Push(Json(s));
    return a;
}
inline std::string LiteralToExprText(const Json& j) {
    if (j.IsString()) {
        std::string s = "\"";
        for (char c : j.AsString()) {
            if (c == '"' || c == '\\') s += '\\';
            s += c;
        }
        return s + "\"";
    }
    if (j.IsBool()) return j.AsBool() ? "true" : "false";
    if (j.IsNumber()) return DumpJson(j, 0, true);
    return std::string();
}

}  // namespace imforge::builder::props
